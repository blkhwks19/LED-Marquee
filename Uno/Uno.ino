// Used to receive data on a virtual RX pin instead of the usual pin 0
#include <SoftwareSerial.h>
SoftwareSerial virtualSerial(10, 11); // RX, TX

#define STRINGBUFFER_LEN 200      // The length of the buffer used to read a new string from the serial port
char str[STRINGBUFFER_LEN+1];     // Leave room for a safety null terminator at the end.

// Change this to be at least as long as your pixel string (too long will work fine, just be a little slower)
#define PIXELS 96*4  // Number of pixels in the string. I am using 4 meters of 96LED/M

// These values depend on which pins your 8 strings are connected to and what board you are using 
// More info on how to find these at http://www.arduino.cc/en/Reference/PortManipulation

// PORTD controls Digital Pins 0-7 on the Uno

// You'll need to look up the port/bit combination for other boards. 

// Note that you could also include the DigitalWriteFast header file to not need to to this lookup.

#define PIXEL_PORT  PORTD  // Port of the pin the pixels are connected to
#define PIXEL_DDR   DDRD   // Port of the pin the pixels are connected to


static const uint8_t onBits=0b11111111;   // Bit pattern to write to port to turn on all pins connected to LED strips. 
                                          // If you do not want to use all 8 pins, you can mask off the ones you don't want
                                          // Note that these will still get 0 written to them when we send pixels
                                          // TODO: If we have time, we could even add a variable that will and/or into the bits before writing to the port to support any combination of bits/values                                  

// These are the timing constraints taken mostly from 
// imperically measuring the output from the Adafruit library strandtest program

// Note that some of these defined values are for refernce only - the actual timing is determinted by the hard code.

#define T1H  814    // Width of a 1 bit in ns - 13 cycles
#define T1L  438    // Width of a 1 bit in ns -  7 cycles

#define T0H  312    // Width of a 0 bit in ns -  5 cycles
#define T0L  936    // Width of a 0 bit in ns - 15 cycles 

// Phase #1 - Always 1  - 5 cycles
// Phase #2 - Data part - 8 cycles
// Phase #3 - Always 0  - 7 cycles

#define RES 50000   // Width of the low gap between bits to cause a frame to latch

// Here are some convience defines for using nanoseconds specs to generate actual CPU delays

#define NS_PER_SEC (1000000000L)          // Note that this has to be SIGNED since we want to be able to check for negative values of derivatives

#define CYCLES_PER_SEC (F_CPU)

#define NS_PER_CYCLE ( NS_PER_SEC / CYCLES_PER_SEC )

#define NS_TO_CYCLES(n) ( (n) / NS_PER_CYCLE )


// Sends a full 8 bits down all the pins, represening a single color of 1 pixel
// We walk though the 8 bits in colorbyte one at a time. If the bit is 1 then we send the 8 bits of row out. Otherwise we send 0. 
// We send onBits at the first phase of the signal generation. We could just send 0xff, but that mught enable pull-ups on pins that we are not using. 

/// Unforntunately we have to drop to ASM for this so we can interleave the computaions durring the delays, otherwise things get too slow.

// OnBits is the mask of which bits are connected to strips. We pass it on so that we
// do not turn on unused pins becuase this would enable the pullup. Also, hopefully passing this
// will cause the compiler to allocate a Register for it and avoid a reload every pass.

static inline void sendBitx8(  const uint8_t row , const uint8_t colorbyte , const uint8_t onBits ) {  
              
    asm volatile (


      "L_%=: \n\r"  
      
      "out %[port], %[onBits] \n\t"                 // (1 cycles) - send either T0H or the first part of T1H. Onbits is a mask of which bits have strings attached.

      // Next determine if we are going to be sending 1s or 0s based on the current bit in the color....
      
      "mov r0, %[bitwalker] \n\t"                   // (1 cycles) 
      "and r0, %[colorbyte] \n\t"                   // (1 cycles)  - is the current bit in the color byte set?
      "breq OFF_%= \n\t"                            // (1 cycles) - bit in color is 0, then send full zero row (takes 2 cycles if branch taken, count the extra 1 on the target line)

      // If we get here, then we want to send a 1 for every row that has an ON dot...
      "nop \n\t  "                                  // (1 cycles) 
      "out %[port], %[row]   \n\t"                  // (1 cycles) - set the output bits to [row] This is phase for T0H-T1H.
                                                    // ==========
                                                    // (5 cycles) - T0H (Phase #1)


      "nop \n\t nop \n\t "                          // (2 cycles) 
      "nop \n\t nop \n\t "                          // (2 cycles) 
      "nop \n\t nop \n\t "                          // (2 cycles) 
      "nop \n\t "                                   // (1 cycles) 

      "out %[port], __zero_reg__ \n\t"              // (1 cycles) - set the output bits to 0x00 based on the bit in colorbyte. This is phase for T0H-T1H
                                                    // ==========
                                                    // (8 cycles) - Phase #2
                                                    
      "ror %[bitwalker] \n\t"                      // (1 cycles) - get ready for next pass. On last pass, the bit will end up in C flag
                  
      "brcs DONE_%= \n\t"                          // (1 cycles) Exit if carry bit is set as a result of us walking all 8 bits. We assume that the process around us will tak long enough to cover the phase 3 delay

      "nop \n\t \n\t "                             // (1 cycles) - When added to the 5 cycles in S:, we gte the 7 cycles of T1L
            
      "jmp L_%= \n\t"                              // (3 cycles) 
                                                   // (1 cycles) - The OUT on the next pass of the loop
                                                   // ==========
                                                   // (7 cycles) - T1L
                                                   
                                                          
      "OFF_%=: \n\r"                                // (1 cycles)    Note that we land here becuase of breq, which takes takes 2 cycles

      "out %[port], __zero_reg__ \n\t"              // (1 cycles) - set the output bits to 0x00 based on the bit in colorbyte. This is phase for T0H-T1H
                                                    // ==========
                                                    // (5 cycles) - T0H

      "ror %[bitwalker] \n\t"                      // (1 cycles) - get ready for next pass. On last pass, the bit will end up in C flag
                  
      "brcs DONE_%= \n\t"                          // (1 cycles) Exit if carry bit is set as a result of us walking all 8 bits. We assume that the process around us will tak long enough to cover the phase 3 delay

      "nop \n\t nop \n\t "                          // (2 cycles) 
      "nop \n\t nop \n\t "                          // (2 cycles) 
      "nop \n\t nop \n\t "                          // (2 cycles)             
      "nop \n\t nop \n\t "                          // (2 cycles)             
      "nop \n\t "                                   // (1 cycles)             
            
      "jmp L_%= \n\t"                               // (3 cycles) 
                                                    // (1 cycles) - The OUT on the next pass of the loop      
                                                    // ==========
                                                    //(15 cycles) - T0L 
      
            
      "DONE_%=: \n\t"

      // Don't need an explicit delay here since the overhead that follows will always be long enough
    
      ::
      [port]    "I" (_SFR_IO_ADDR(PIXEL_PORT)),
      [row]   "d" (row),
      [onBits]   "d" (onBits),
      [colorbyte]   "d" (colorbyte ),     // Phase 2 of the signal where the actual data bits show up.                
      [bitwalker] "r" (0x80)                      // Alocate a register to hold a bit that we will walk down though the color byte

    );
                                  
    // Note that the inter-bit gap can be as long as you want as long as it doesn't exceed the reset timeout (which is A long time)
    
} 


// Just wait long enough without sending any bots to cause the pixels to latch and display the last sent frame

void show() {
  delayMicroseconds( (RES / 1000UL) + 1);       // Round up since the delay must be _at_least_ this long (too short might not work, too long not a problem)
}


// Send 3 bytes of color data (R,G,B) for a signle pixel down all the connected stringsat the same time
// A 1 bit in "row" means send the color, a 0 bit means send black. 

static inline void sendRowRGB( uint8_t row ,  uint8_t r,  uint8_t g,  uint8_t b ) {

  sendBitx8( row , g , onBits);    // WS2812 takes colors in GRB order
  sendBitx8( row , r , onBits);    // WS2812 takes colors in GRB order
  sendBitx8( row , b , onBits);    // WS2812 takes colors in GRB order
  
}

// Turn off all pixels

static inline void clear() {

  cli();
  for( unsigned int i=0; i< PIXELS; i++ ) {

    sendRowRGB( 0 , 0 , 0 , 0 );
  }
  sei();
  show(); 
}

// This nice 5x7 font from here...
// http://sunge.awardspace.com/glcd-sd/node4.html

// Font details:
// 1) Each char is fixed 5x7 pixels. 
// 2) Each byte is one column.
// 3) Columns are left to right order, leftmost byte is leftmost column of pixels.
// 4) Each column is 8 bits high.
// 5) Bit #7 is top line of char, Bit #1 is bottom.
// 6) Bit #0 is always 0, becuase this pin is used as serial input and setting to 1 would enable the pull-up.

// defines ascii characters 0x20-0x7F (32-127)
// PROGMEM after variable name as per https://www.arduino.cc/en/Reference/PROGMEM

#define FONT_WIDTH 5      
#define INTERCHAR_SPACE 1
#define ASCII_OFFSET 0x20    // ASSCI code of 1st char in font array

const uint8_t Font5x8[] PROGMEM = {
0x00,0x00,0x00,0x00,0x00,//  
0x00,0x00,0xfa,0x00,0x00,// !
0x00,0xe0,0x00,0xe0,0x00,// "
0x28,0xfe,0x28,0xfe,0x28,// #
0x24,0x54,0xfe,0x54,0x48,// $
0xc4,0xc8,0x10,0x26,0x46,// %
0x6c,0x92,0xaa,0x44,0x0a,// &
0x00,0xa0,0xc0,0x00,0x00,// '
0x00,0x38,0x44,0x82,0x00,// (
0x00,0x82,0x44,0x38,0x00,// )
0x10,0x54,0x38,0x54,0x10,// *
0x10,0x10,0x7c,0x10,0x10,// +
0x00,0x0a,0x0c,0x00,0x00,// ,
0x10,0x10,0x10,0x10,0x10,// -
0x00,0x06,0x06,0x00,0x00,// .
0x04,0x08,0x10,0x20,0x40,// /
0x7c,0x8a,0x92,0xa2,0x7c,// 0
0x00,0x42,0xfe,0x02,0x00,// 1
0x42,0x86,0x8a,0x92,0x62,// 2
0x84,0x82,0xa2,0xd2,0x8c,// 3
0x18,0x28,0x48,0xfe,0x08,// 4
0xe4,0xa2,0xa2,0xa2,0x9c,// 5
0x3c,0x52,0x92,0x92,0x0c,// 6
0x80,0x8e,0x90,0xa0,0xc0,// 7
0x6c,0x92,0x92,0x92,0x6c,// 8
0x60,0x92,0x92,0x94,0x78,// 9
0x00,0x6c,0x6c,0x00,0x00,// :
0x00,0x6a,0x6c,0x00,0x00,// ;
0x00,0x10,0x28,0x44,0x82,// <
0x28,0x28,0x28,0x28,0x28,// =
0x82,0x44,0x28,0x10,0x00,// >
0x40,0x80,0x8a,0x90,0x60,// ?
0x4c,0x92,0x9e,0x82,0x7c,// @
0x7e,0x88,0x88,0x88,0x7e,// A
0xfe,0x92,0x92,0x92,0x6c,// B
0x7c,0x82,0x82,0x82,0x44,// C
0xfe,0x82,0x82,0x44,0x38,// D
0xfe,0x92,0x92,0x92,0x82,// E
0xfe,0x90,0x90,0x80,0x80,// F
0x7c,0x82,0x82,0x8a,0x4c,// G
0xfe,0x10,0x10,0x10,0xfe,// H
0x00,0x82,0xfe,0x82,0x00,// I
0x04,0x02,0x82,0xfc,0x80,// J
0xfe,0x10,0x28,0x44,0x82,// K
0xfe,0x02,0x02,0x02,0x02,// L
0xfe,0x40,0x20,0x40,0xfe,// M
0xfe,0x20,0x10,0x08,0xfe,// N
0x7c,0x82,0x82,0x82,0x7c,// O
0xfe,0x90,0x90,0x90,0x60,// P
0x7c,0x82,0x8a,0x84,0x7a,// Q
0xfe,0x90,0x98,0x94,0x62,// R
0x62,0x92,0x92,0x92,0x8c,// S
0x80,0x80,0xfe,0x80,0x80,// T
0xfc,0x02,0x02,0x02,0xfc,// U
0xf8,0x04,0x02,0x04,0xf8,// V
0xfe,0x04,0x18,0x04,0xfe,// W
0xc6,0x28,0x10,0x28,0xc6,// X
0xc0,0x20,0x1e,0x20,0xc0,// Y
0x86,0x8a,0x92,0xa2,0xc2,// Z
0x00,0x00,0xfe,0x82,0x82,// [
0x40,0x20,0x10,0x08,0x04,// (backslash)
0x82,0x82,0xfe,0x00,0x00,// ]
0x20,0x40,0x80,0x40,0x20,// ^
0x02,0x02,0x02,0x02,0x02,// _
0x00,0x80,0x40,0x20,0x00,// `
0x04,0x2a,0x2a,0x2a,0x1e,// a
0xfe,0x12,0x22,0x22,0x1c,// b
0x1c,0x22,0x22,0x22,0x04,// c
0x1c,0x22,0x22,0x12,0xfe,// d
0x1c,0x2a,0x2a,0x2a,0x18,// e
0x10,0x7e,0x90,0x80,0x40,// f
0x18,0x25,0x25,0x25,0x3e,// g
0xfe,0x10,0x20,0x20,0x1e,// h
0x00,0x22,0xbe,0x02,0x00,// i
0x04,0x02,0x22,0xbc,0x00,// j
0x00,0xfe,0x08,0x14,0x22,// k
0x00,0x82,0xfe,0x02,0x00,// l
0x3e,0x20,0x18,0x20,0x1e,// m
0x3e,0x10,0x20,0x20,0x1e,// n
0x1c,0x22,0x22,0x22,0x1c,// o
0x3f,0x24,0x24,0x24,0x18,// p
0x18,0x24,0x24,0x24,0x3f,// q
0x3e,0x10,0x20,0x20,0x10,// r
0x12,0x2a,0x2a,0x2a,0x04,// s
0x20,0xfc,0x22,0x02,0x04,// t
0x3c,0x02,0x02,0x04,0x3e,// u
0x38,0x04,0x02,0x04,0x38,// v
0x3c,0x02,0x0c,0x02,0x3c,// w
0x22,0x14,0x08,0x14,0x22,// x
0x38,0x05,0x05,0x05,0x3e,// y
0x22,0x26,0x2a,0x32,0x22,// z
0x00,0x10,0x6c,0x82,0x00,// {
0x00,0x00,0xfe,0x00,0x00,// |
0x00,0x82,0x6c,0x10,0x00,// }
0x10,0x10,0x54,0x38,0x10,// ~
0x10,0x38,0x54,0x10,0x10,// 
};

/* tilde draws -> and i think the last one (delete) will draw <-. 
   here is an actual tilde: 0x10,0x20,0x10,0x08,0x10,// ~ */

// Send the pixels to form the specified char, not including interchar space
// skip is the number of pixels to skip at the begining to enable sub-char smooth scrolling

// TODO: Subtract the offset from the char before starting the send sequence to save time if nessisary
// TODO: Also could pad the begining of the font table to aovid the offset subtraction at the cost of 20*8 bytes of progmem
// TODO: Could pad all chars out to 8 bytes wide to turn the the multiply by FONT_WIDTH into a shift 

static inline void sendChar( uint8_t c ,  uint8_t skip , uint8_t r,  uint8_t g,  uint8_t b ) {

  const uint8_t *charbase = Font5x8 + (( c -' ')* FONT_WIDTH ) ; 

  uint8_t col=FONT_WIDTH; 

  while (skip--) {
      charbase++;
      col--;    
  }
  
  while (col--) {
      sendRowRGB( pgm_read_byte_near( charbase++ ) , r , g , b );
  }    

  // TODO: FLexible interchar spacing

  sendRowRGB( 0 , r , g , b );    // Interchar space
  
}


// Show the passed string. The last letter of the string will be in the rightmost pixels of the display.
// Skip is how many cols of the 1st char to skip for smooth scrolling

static inline void sendString( const char *s , uint8_t skip ,  const uint8_t r,  const uint8_t g,  const uint8_t b ) {

  unsigned int l=PIXELS/(FONT_WIDTH+INTERCHAR_SPACE); 

  sendChar( *s , skip ,  r , g , b );   // First char is special case becuase it can be stepped for smooth scrolling
  
  while ( *(++s) && l--) {

    sendChar( *s , 0,  r , g , b );

  }
}




// Set the specified pins up as digital out

void ledsetup() {

  PIXEL_DDR |= onBits;   // Set all used pins to output
    
}



void setup() {
    
  ledsetup();  

  //Serial.begin(115200); // Don't think we need this unless troubleshooting; if you enable this the last 2 rows don't light up for some reason
  virtualSerial.begin(115200);
  //Serial.println("BEGINNING");
  
}


// https://learn.adafruit.com/led-tricks-gamma-correction/the-quick-fix

const uint8_t PROGMEM gamma[] = {
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  1,  1,  1,
    1,  1,  1,  1,  1,  1,  1,  1,  1,  2,  2,  2,  2,  2,  2,  2,
    2,  3,  3,  3,  3,  3,  3,  3,  4,  4,  4,  4,  4,  5,  5,  5,
    5,  6,  6,  6,  6,  7,  7,  7,  7,  8,  8,  8,  9,  9,  9, 10,
   10, 10, 11, 11, 11, 12, 12, 13, 13, 13, 14, 14, 15, 15, 16, 16,
   17, 17, 18, 18, 19, 19, 20, 20, 21, 21, 22, 22, 23, 24, 24, 25,
   25, 26, 27, 27, 28, 29, 29, 30, 31, 32, 32, 33, 34, 35, 35, 36,
   37, 38, 39, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 50,
   51, 52, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 66, 67, 68,
   69, 70, 72, 73, 74, 75, 77, 78, 79, 81, 82, 83, 85, 86, 87, 89,
   90, 92, 93, 95, 96, 98, 99,101,102,104,105,107,109,110,112,114,
  115,117,119,120,122,124,126,127,129,131,133,135,137,138,140,142,
  144,146,148,150,152,154,156,158,160,162,164,167,169,171,173,175,
  177,180,182,184,186,189,191,193,196,198,200,203,205,208,210,213,
  215,218,220,223,225,228,231,233,236,239,241,244,247,249,252,255 };

// Map 0-255 visual brightness to 0-255 LED brightness 
#define GAMMA(x) (pgm_read_byte(&gamma[x]))


static inline void sendIcon( const uint8_t *fontbase , uint8_t which, int8_t shift , uint8_t width , uint8_t r , uint8_t g , uint8_t b ) {

  const uint8_t *charbase = fontbase + (which*width);

  if (shift <0) {

        uint8_t shiftabs = -1 * shift;
  
        while (width--) {

          uint8_t row = pgm_read_byte_near( charbase++ );
    
          sendRowRGB(  row << shiftabs , r , g , b );
     
    }

  } else {


    
    while (width--) {
  
        sendRowRGB(  (pgm_read_byte_near( charbase++ ) >> shift) & onBits , r , g , b );
     
    }

  }

}




void scroll() {

  const char *m = "                                        CHI 2 COL 7 2ND ";
  
  while (*m) {      

      uint8_t r = 0x01;
      uint8_t g = 0x00;
      uint8_t b = 0x00;

      for( uint8_t step=0; step<FONT_WIDTH+INTERCHAR_SPACE  ; step++ ) {   // step though each column of the 1st char for smooth scrolling

       cli();

       sendString( m , step , r, g, b );
      
       sei();

       delay(25); // speed. lower = faster, default value is 25

       PORTB|=0x01;      
       delay(1);
       PORTB&=~0x01;

      }

    m++;

  }

}


// COLORS
// 0x01, 0x00, 0x00 // red
// 0x00, 0x01, 0x00 // green
// 0x00, 0x00, 0x01 // blue
// 0x01, 0x01, 0x00 // yellow
// 0x01, 0x00, 0x01 // magenta
// 0x00, 0x01, 0x01 // cyan/teal
// 0x02, 0x01, 0x00 // orange
// 0x02, 0x00, 0x01 // pink
// 0x02, 0x01, 0x01 // light pink
// 0x01, 0x02, 0x00 // light green
// 0x00, 0x02, 0x01 // turquoise
// 0x01, 0x02, 0x01 // sea green
// 0x01, 0x00, 0x02 // purple
// 0x00, 0x01, 0x02 // light blue
// 0x01, 0x01, 0x02 // lavender
// 0x01, 0x01, 0x01 // white



// ICONS
const uint8_t heart[] PROGMEM = { 0x70,0x88,0x84,0x42,0x84,0x88,0x70,0x00 };
const uint8_t heart_filled[] PROGMEM = { 0x70,0xf8,0xfc,0x7e,0xfc,0xf8,0x70,0x00 };
const uint8_t heart_half[] PROGMEM = { 0x70,0xf8,0xfc,0x7e,0x84,0x88,0x70,0x00 };
const uint8_t lock[] PROGMEM = { 0x00,0x1f,0x71,0x91,0x91,0x71,0x1f,0x00, };
const uint8_t lock_filled[] PROGMEM = { 0x00,0x1f,0x7f,0x9f,0x9f,0x7f,0x1f,0x00, };
const uint8_t unlock[] PROGMEM = { 0x60,0x80,0x9f,0x71,0x11,0x11,0x11,0x1f, };
const uint8_t unlock_filled[] PROGMEM = { 0x60,0x80,0x9f,0x7f,0x1f,0x1f,0x1f,0x1f, };
const uint8_t mail[] PROGMEM = { 0x7e,0x62,0x52,0x4a,0x4a,0x52,0x62,0x7e, };
const uint8_t mail_10[] PROGMEM = { 0xfe,0xc2,0xa2,0x92,0x8a,0x8a,0x92,0xa2,0xc2,0xfe, };
const uint8_t clock_icon[] PROGMEM = { 0x3c,0x42,0x81,0xb9,0x89,0x89,0x42,0x3c, };
const uint8_t facebook[] PROGMEM = { 0x00,0x00,0x00,0x08,0x3f,0x7f,0x48,0x00, };
const uint8_t facebook_filled[] PROGMEM = { 0xff,0xff,0xff,0xf7,0xc0,0x80,0xb7,0xff, };
const uint8_t twitter[] PROGMEM = { 0x00,0x24,0x76,0x3e,0x3e,0x7c,0x78,0x20, };
const uint8_t twitter_filled[] PROGMEM = { 0xff,0xdb,0x89,0xc1,0xc1,0x83,0x87,0xdf, };

void stationary() {

  clear();
  cli();

  // first 3 characters are wrapped around board, so send 3 spaces as padding
  sendString("   ", 0, 0x00, 0x00, 0x00);

  // after that, sign can display 35 characters (at the current font width of 5)

  // send padding to center message
//  sendString("  ", 0, 0x00, 0x00, 0x00);
//
//  sendString("NHL: ", 0, 0x01, 0x01, 0x01);
//  sendString("Anaheim", 0, 0x03, 0x01, 0x00);
//  sendString(" @ ", 0, 0x01, 0x01, 0x01);
//  sendString("Chicago", 0, 0x01, 0x00, 0x00);
//  sendString(" 7:30pm CT", 0, 0x01, 0x01, 0x01);
  
//  sendString("ARI", 0, 0x02, 0x00, 0x01);
//  sendString(" 4 ", 0, 0x01, 0x01, 0x01);
//  sendString("CHI", 0, 0x01, 0x00, 0x00);
//  sendString(" 3 ", 0, 0x01, 0x01, 0x01);

//  sendString("F/SO", 0, 0x01, 0x01, 0x01);

//  sendIcon(heart_filled, 0, 0, 8, 0x01, 0x00, 0x00);
//  sendIcon(heart_half, 0, 0, 8, 0x01, 0x00, 0x00);
//  sendIcon(heart, 0, 0, 8, 0x01, 0x00, 0x00);
//  sendIcon(lock_filled, 0, 0, 8, 0x00, 0x01, 0x00);
//  sendIcon(lock, 0, 0, 8, 0x00, 0x01, 0x00);
//  sendIcon(unlock_filled, 0, 0, 8, 0x00, 0x01, 0x00);
//  sendIcon(unlock, 0, 0, 8, 0x00, 0x01, 0x00);
//  sendIcon(mail, 0, 0, 8, 0x01, 0x01, 0x01);
//  sendIcon(mail_10, 0, 0, 10, 0x01, 0x01, 0x01);
//  sendIcon(clock_icon, 0, 0, 8, 0x01, 0x01, 0x00);
//  sendIcon(facebook, 0, 0, 8, 0x00, 0x00, 0x01);
//  sendIcon(facebook_filled, 0, 0, 8, 0x00, 0x00, 0x01);
//  sendIcon(twitter, 0, 0, 8, 0x00, 0x01, 0x01);
//  sendIcon(twitter_filled, 0, 0, 8, 0x00, 0x01, 0x01);
    
//sendIcon(heart_filled, 0, 0, 8, 0x01, 0x00, 0x00); // red

}


// Notifies the ESP Wifi module that we're ready to retrieve custom message data
void getCustomData() {
  // send special symbol so ESP knows to respond with custom message data
//  Serial.println("Sending special character");
  virtualSerial.print("~");
  // loop until its available
  while (!virtualSerial.available()){  
//    Serial.print(".");
    delay(500);
  }
//  Serial.println(".");
//  Serial.println("Getting data...");
  int len  = virtualSerial.readBytesUntil('\r', str, STRINGBUFFER_LEN);
  str[len] = 0x00;        // Null terminate
//  Serial.println(len);
//  Serial.println(str);
}

void scrollCustom() {

  clear();
  getCustomData();

  const char *m = str;
    
  while (*m) {      
      uint8_t r = 0x01;
      uint8_t g = 0x00;
      uint8_t b = 0x00;

      for( uint8_t step=0; step<FONT_WIDTH+INTERCHAR_SPACE; step++ ) {   // step though each column of the 1st char for smooth scrolling

       cli();

       sendString( m , step , r, g, b );
      
       sei();

       delay(25); // speed. lower = faster, default value is 25

       PORTB|=0x01;      
       delay(1);
       PORTB&=~0x01;

      }

    m++;
  }

}







void stationaryNHL() {

  clear();
  cli();

  // first 3 characters are wrapped around board, so send 3 spaces as padding
  sendString("   ", 0, 0x00, 0x00, 0x00);

  // after that, sign can display 35 characters (at the current font width of 5)

  // send padding to center message
//  sendString("  ", 0, 0x00, 0x00, 0x00);

  sendString("NHL: ", 0, 0x01, 0x01, 0x01);
  sendString("Ducks", 0, 0x02, 0x01, 0x00);
  sendString(" @ ", 0, 0x01, 0x01, 0x01);
  sendString("Blackhawks", 0, 0x01, 0x00, 0x00);
  sendString(" 7:30pm CT", 0, 0x01, 0x01, 0x01);
  
//  sendString("ANA", 0, 0x02, 0x00, 0x01);
//  sendString(" 0 ", 0, 0x01, 0x01, 0x01);
//  sendString("CHI", 0, 0x01, 0x00, 0x00);
//  sendString(" 0 ", 0, 0x01, 0x01, 0x01);

//  sendString("1P", 0, 0x01, 0x01, 0x01);
}

void stationaryNFL1() {

  clear();
  cli();

  // first 3 characters are wrapped around board, so send 3 spaces as padding
  sendString("   ", 0, 0x00, 0x00, 0x00);

  // after that, sign can display 35 characters (at the current font width of 5)

  // send padding to center message
//  sendString("   ", 0, 0x00, 0x00, 0x00);

  sendString("NFL: ", 0, 0x01, 0x01, 0x01);
  sendString("Vikings", 0, 0x01, 0x00, 0x02);
  sendString(" @ ", 0, 0x01, 0x01, 0x01);
  sendString("49ers", 0, 0x01, 0x00, 0x00);
  sendString(" 3:35pm CT", 0, 0x01, 0x01, 0x01);
  
//  sendString("MIN", 0, 0x01, 0x00, 0x02);
//  sendString(" 0 ", 0, 0x01, 0x01, 0x01);
//  sendString("SF", 0, 0x01, 0x00, 0x00);
//  sendString(" 0 ", 0, 0x01, 0x01, 0x01);

//  sendString("Q1", 0, 0x01, 0x01, 0x01);
}

void stationaryNFL2() {

  clear();
  cli();

  // first 3 characters are wrapped around board, so send 3 spaces as padding
  sendString("   ", 0, 0x00, 0x00, 0x00);

  // after that, sign can display 35 characters (at the current font width of 5)

  // send padding to center message
//  sendString("   ", 0, 0x00, 0x00, 0x00);

  sendString("NFL: ", 0, 0x01, 0x01, 0x01);
  sendString("Titans", 0, 0x00, 0x01, 0x02);
  sendString(" @ ", 0, 0x01, 0x01, 0x01);
  sendString("Ravens", 0, 0x01, 0x00, 0x02);
  sendString(" 7:15pm CT", 0, 0x01, 0x01, 0x01);
  
//  sendString("TEN", 0, 0x00, 0x01, 0x02);
//  sendString(" 0 ", 0, 0x01, 0x01, 0x01);
//  sendString("BAL", 0, 0x01, 0x00, 0x02);
//  sendString(" 0 ", 0, 0x01, 0x01, 0x01);

//  sendString("Q1", 0, 0x01, 0x01, 0x01);
}

void stationaryMovie() {

  clear();
  cli();

  // first 3 characters are wrapped around board, so send 3 spaces as padding
  sendString("   ", 0, 0x00, 0x00, 0x00);

  // after that, sign can display 35 characters (at the current font width of 5)

  // send padding to center message
//  sendString("   ", 0, 0x00, 0x00, 0x00);

  sendString("NOW SHOWING: ", 0, 0x01, 0x00, 0x00);
  sendString("WAR GAMES", 0, 0x02, 0x01, 0x00);
  sendString("      12:10PM", 0, 0x02, 0x01, 0x00);
  
}

void loop() {

//  stationaryMovie();
  
  stationaryNHL();
  delay(2000000);
  stationaryNFL1();
  delay(2000000);
  stationaryNFL2();
  delay(2000000);
  
  
}
