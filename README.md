# LED-Marquee


This project uses 2 Arduino boards to retrieve a custom message that is saved on my personal website server and display it on the LED marquee sign. Here's the steps that are taken to achieve that:

data.txt - This is a plain text file that contains whatever custom message I want the sign to display. In future phases, this may contain multiple messages formatted in JSON, but I'm not there yet. For now, sticking with strictly plain text. This local file i sjust a copy of the live file that lives on my server. That file actually lives here: jeffschaefer.net/marquee/data.txt

ESP - This is the sketch that is uploaded to the NodeMCU ESP8266 WIFI board. It is used strictly for communicating with my server via my wifi network to retrieve the custom message. It then holds onto that message in a variable until the Arduino UNO requests it.

Uno - This is the sketch that is uploaded to the Arduino UNO board. The UNO is connected to 8 LED strips and also to the ESP. When it wants to retrieve the data it sends a special character to the ESP to indicate that its ready to receive the custom message data. The ESP responds and sends it to the UNO, afterwhich I might need to eventually do some parsing/formatting before displaying. NOTE: The UNO is already utilizing pin 0 (RX) for one of the LED strips, so it is set up to use the SoftwareSerial library, which allows me to use pin 10 as RX and pin 11 as TX. These of course are virtual RX/TX pins, but they behave the save way the hardware RX/TX pins do. RX/TX on UNO is connected to TX/RX on ESP.

