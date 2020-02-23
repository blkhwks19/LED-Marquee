#include <SPI.h>
#include <WiFiNINA.h>

char ssid[] = "NHL Network";
char pass[] = "@Haley2010";
char host[] = "jeffschaefer.net";
int port = 80;

String str = "";

int status = WL_IDLE_STATUS;

// Initialize the client library
WiFiClient client;

void setup() {
  Serial.begin(9600);
  Serial.println("Attempting to connect to WPA network...");
  Serial.print("SSID: ");
  Serial.println(ssid);

  status = WiFi.begin(ssid, pass);
  if ( status != WL_CONNECTED) {
    Serial.println("Couldn't get a wifi connection");
    // don't do anything else:
    while(true);
  }
  else {
    Serial.println("Connected to wifi");
    Serial.println("\nStarting connection...");
    // if you get a connection, report back via serial:
    if (client.connect(host, 80)) {
      Serial.println("connected");
      // Make a HTTP request:
      // We now create a URI for the request
      String url = "/marquee/data.txt";
      
      // This will send the request to the server
      client.print(String("GET ") + url + " HTTP/1.1\r\n" +
                   "Host: " + host + "\r\n" + 
                   "Connection: close\r\n\r\n");

      // Read the reply from server, store message data for later
      while(client.available()){
        // For now put a Z at top of data file, and use readBytesUntil(). Next phase, change it to a '{' since that will be what is used for JSON
        String block = client.readStringUntil('~');
        if (block.substring(0,4) != "HTTP") {
          str = block;
        }
      }

      Serial.println(str);
    }
  }
}

void loop() {

}
