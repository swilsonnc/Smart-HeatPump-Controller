/*
  Heat Pump Defrost Controller.
  This is based off of arduino-fujitsu-awhpcontroller created by ToniA found here: https://github.com/ToniA/arduino-fujitsu-awhpcontroller
  It has been simplified just a bit to fit my needs.  Thanks to everyone who has contributed to this project.
  You should use at least Arduino IDE 1.6.12.  I am using a NodeMCU v1.0 with ESP-12E powered with a 5V 2A usb power brick.
  Can update OTA using the Arduino IDE.
  
  Monitors temperature of Outdoor ambient air and Pipe/Coil using 2 different DS18B20 sensors.
  Factory HP pipe temp sensor is suppose to close it's circuit at 30-32F and lower and not open back up until around 70-80F.
  When outdoor temp is below 44, Pipe temp is below 32, and the difference is PipeTemp / defrostThreshold or greater
  then the relay completes the factory sensor circuit and allows the HP to start the timer towards running it's normal defrost. 
  We wait 15 minutes before we check again.  Could put the relay in the timer jumper instead and achieve the same results.  Either way you
  are stopping the timer from starting and therefore never defrosting until you want.
  Alternatively when the defrost conditions are met we could trigger the relay for the thermo switch as well as a relay for the speedup
  pins for about 10 seconds which would force the HP to go into defrost immediately. (Maybe later after testing above)

  On HP, when the factory thermo switch is open (not triggered) there is not voltage on the timer jumper (30, 60, 90 jumper).
  When thermo switch is closed there is 24-28VAC on the timer jumper.  Can put relay there to know when switch is closed.

  A jumper is installed on NodeMCU from pin GPIO14 and GPIO12 for possible future use.
  Connected to HP on evening of 03-23-2017 via the thermo switch wiring.
  Sketch uses 331,556 bytes (31%) of program storage space. Maximum is 1,044,464 bytes.
  Global variables use 37,528 bytes (45%) of dynamic memory, leaving 44,392 bytes for local variables. Maximum is 81,920 bytes.
  Compiled by Scott Wilson
*/

#include <Timer.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

extern "C" {
#include "user_interface.h"
}

// Thingspeak stuff
const char* serverts = "api.thingspeak.com";
String APIKey = "YOUR-API-KEY";              //enter your channel's Write API Key

// I/O assignments
// DS18B20 sensors
const int ONEWIRE       = 4;

// Relay board
const int DEFROST       = 5;

unsigned long startTime = millis();

// Structs with device name and device address

typedef struct onewireSensor onewireSensor;
struct onewireSensor
{
  char *name;
  byte onewireAddress[8];
};

typedef struct DS18B20Sensor DS18B20Sensor;
struct DS18B20Sensor
{
  onewireSensor device;
  float temperature;
};


//
// DS18B20 sensors on OneWire
//

OneWire ow1(ONEWIRE);
DS18B20Sensor DS18B20Sensors[] = {
  {{ "Outdoor",              { 0x28, 0xFF, 0x22, 0x2B, 0x51, 0x16, 0x04, 0x20 } }, DEVICE_DISCONNECTED_F }, // 0 - Black X's 'Outdoor'
  {{ "Pipe",        { 0x28, 0xFF, 0x24, 0x2B, 0x51, 0x16, 0x04, 0xBC } }, DEVICE_DISCONNECTED_F }  // 1 - Black #1's 'Pipe'
};

const int sensorOutdoor  = 0;
const int sensorPipetemp = 1;

// Stock defrost threshold

float defrostThreshold = 1.8; // Divider used to determine defrostThreshold

// Global variables

WiFiClient client;
DallasTemperature owSensors(&ow1);
Timer t, t2;
WiFiServer telnetServer(23);

int displayedSensor = 0;

boolean defrostCheating = false;
boolean defrostActive = false;

//
// Set up
//
// - set serial mode
// - initialize the defrost pin
// - enable watchdog
// - read the defrost threshold from EEPROM
// - list all DS18B20 sensors from the bus
// - take the first readings
// - initialize the timer routines

void setup(void) {

  // Serial initialization
  Serial.begin(115200);

  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;

  //reset saved settings
  //wifiManager.resetSettings();

  //set custom ip for portal
  //wifiManager.setAPStaticIPConfig(IPAddress(10,0,1,1), IPAddress(10,0,1,1), IPAddress(255,255,255,0));

  //fetches ssid and pass from eeprom and tries to connect
  //if it does not connect it starts an access point with the specified name
  //here  "AutoConnectAP"
  //and goes into a blocking loop awaiting configuration
  wifiManager.autoConnect("AutoConnectAP");
  //or use this for auto generated name ESP + ChipID
  //wifiManager.autoConnect();

  Serial.print("Connected to ");

  delay(1000);

  Serial.println(F("Starting..."));

  // OTA Update Start

  // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname("esp8266-heatpump");

  // No authentication by default
  // ArduinoOTA.setPassword((const char *)"123");

  ArduinoOTA.onStart([]() {
    Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();

  // Telnet stuff
  telnetServer.begin();
  telnetServer.setNoDelay(true);
  Serial.println("Please connect Telnet Client, exit with ^] and 'quit'");
  Serial.print("Free Heap[B]: ");
  Serial.println(ESP.getFreeHeap());

  // Relay board initialization
  pinMode(DEFROST, OUTPUT);
  digitalWrite(DEFROST, HIGH);

  // printInfo();

  displayedSensor = 0;
  
  // Take the initial readings from all OneWire devices

  owSensors.requestTemperatures();
  delay(1000);
  takeReading();

  // Timers - everything is based on timer events

  t2.after(20000, hpDefrostSignal);       // Defrost reschedules itself using different intervals
  t.every(60000, printInfo);
  t.every(30000, startReading);
  t.every(2000, feedWatchdog);

}


//
// The main loop
// - update the timer to call the timed events
//

void loop() {
  t.update();
  t2.update();
  ArduinoOTA.handle();
  telnet();
}


//
// Start reading the DS18B20 sensors, schedule a one-time event to read the
// temperatures 1000 milliseconds later
//

void startReading()
{
  owSensors.requestTemperatures();
  t.after(1000, takeReading);
}


//
// Read the DS18B20 sensors, as scheduled from startReading()
//

void takeReading()
{
  byte i;
  float temperature_c;

  for ( i = 0; i < sizeof(DS18B20Sensors) / sizeof(DS18B20Sensor); i++ ) {
    temperature_c = owSensors.getTempC(DS18B20Sensors[i].device.onewireAddress);
    float temperature = (temperature_c * 9.0) / 5.0 + 32.0;
    // Sometimes we get read errors -> only allow sensible values
    if (temperature > -40.0 && temperature < 140.0) {
      DS18B20Sensors[i].temperature = temperature;
    }
  }
  Serial.print("Outdoor Temp: ");
  Serial.print(DS18B20Sensors[sensorOutdoor].temperature);
  Serial.println("F");
  Serial.print("Coil Temp: ");
  Serial.print(DS18B20Sensors[sensorPipetemp].temperature);
  Serial.println("F");
}

void hpDefrostSignal()
{
  unsigned long nextDefrostCheck = 60000; // 1 min
  Serial.print(F("Defrost cheating: outdoor "));
  Serial.println(DS18B20Sensors[sensorOutdoor].temperature);
  Serial.print(F("Defrost cheating: pipe "));
  Serial.println(DS18B20Sensors[sensorPipetemp].temperature);

  if (DS18B20Sensors[sensorOutdoor].temperature < 44.0 ) {
    if (DS18B20Sensors[sensorPipetemp].temperature < 32.0) {
      if ((DS18B20Sensors[sensorOutdoor].temperature - DS18B20Sensors[sensorPipetemp].temperature) < (DS18B20Sensors[sensorPipetemp].temperature / defrostThreshold)) {
      // outdoor below 45F and pipetemp below 32F with difference less than defrost threshold and defrosting is not in progress -> keep cheating on and check every minute
      // Cheating opens the relay which breaks continuity of the factory Pipe/Coil temp sensor so that even if factory sensor gets triggered we cheat until necessary.
        digitalWrite(DEFROST, HIGH);
        defrostCheating = true;
        defrostActive = false;
        Serial.println(F("Cheating"));
      }
      else {
        // outdoor below 44F and pipetemp below 32F with difference greater than defrost threshold or defrosting is in progress -> defrost cheating off for next 15 minutes -> defrosts
        // This closes the relay which restores continuity to the factory sensor and allows normal factory controlled defrost to take place.
        // Defrost board set to 90 minutes but with this method I will set to 30 minutes
        digitalWrite(DEFROST, LOW);
        defrostCheating = false;
        defrostActive = true;
        Serial.println(F("Defrosting"));

        nextDefrostCheck = 900000L; // 15 min, give time to defrost
      }
    }
    else {
      digitalWrite(DEFROST, HIGH);
      defrostCheating = false;
      defrostActive = false;
      Serial.println(F("Below 44F but still not cheating or defrosting"));

      nextDefrostCheck = 300000L; // 5 min
    }
  }
  else {
    digitalWrite(DEFROST, HIGH);
    defrostCheating = false;
    defrostActive = false;
    Serial.println(F("Above 44F and not cheating or defrosting"));

    nextDefrostCheck = 300000L; // 5 min
  }

  t2.after(nextDefrostCheck, hpDefrostSignal);
}

void printInfo()
{
  int i;
  int j;
  byte onewireAddress[8];
 
  Serial.print(F("My IP address: "));
  Serial.print(WiFi.localIP());
  Serial.print(F("."));

  Serial.println();

  wdt_enable(WDTO_8S);

  Serial.print(F("Defrost threshold is: "));
  Serial.println(DS18B20Sensors[sensorPipetemp].temperature / defrostThreshold);

  // List OneWire devices

  owSensors.begin();
  owSensors.setWaitForConversion(false);
  j = owSensors.getDeviceCount();

  Serial.print(F("1-wire has "));
  Serial.print(j);
  Serial.println(F(" devices:"));

  for ( displayedSensor = 0; displayedSensor < j; displayedSensor++) {
    owSensors.getAddress(onewireAddress, displayedSensor);

    Serial.print(F("ADDR="));
    for ( i = 0; i < sizeof(onewireAddress); i++) {
      if (onewireAddress[i] < 0x10) {
        Serial.print(F("0"));
      }
      Serial.print(onewireAddress[i], HEX);
      if ( i == 0 ) {
        Serial.print(F("."));
      }
    }
    Serial.println(F(""));
  }

  if (client.connect(serverts, 80)) { //   "184.106.153.149" or api.thingspeak.com
    String postStr = APIKey;
    postStr += "&field1=";
    postStr += String(DS18B20Sensors[sensorOutdoor].temperature);
    postStr += "&field2=";
    postStr += String(DS18B20Sensors[sensorPipetemp].temperature);
    if (defrostActive == 1) {
      postStr += "&field3=";
      postStr += String(DS18B20Sensors[sensorOutdoor].temperature - DS18B20Sensors[sensorPipetemp].temperature);
    }
    if (defrostCheating == 1) {
      postStr += "&field4=";
      postStr += String(DS18B20Sensors[sensorOutdoor].temperature - DS18B20Sensors[sensorPipetemp].temperature);
    }
    postStr += "&field5=";
    postStr += String(DS18B20Sensors[sensorPipetemp].temperature / defrostThreshold);
    postStr += "\r\n\r\n";

    client.print("POST /update HTTP/1.1\n");
    client.print("Host: api.thingspeak.com\n");
    client.print("Connection: close\n");
    client.print("X-THINGSPEAKAPIKEY: " + APIKey + "\n");
    client.print("Content-Type: application/x-www-form-urlencoded\n");
    client.print("Content-Length: ");
    client.print(postStr.length());
    client.print("\n\n");
    client.print(postStr);

    Serial.println("Send to Thingspeak.");
  }
  client.stop();
}


void telnet()
{
  // look for Client connect trial
  if (telnetServer.hasClient()) {
    if (!client || !client.connected()) {
      if (client) {
        client.stop();
        Serial.println("Telnet Client Stop");
      }
      client = telnetServer.available();
      Serial.println("New Telnet client");
      client.flush();  // clear input buffer, else you get strange characters
    }
  }

  while (client.available()) { // get data from Client
    Serial.write(client.read());
  }

  if (millis() - startTime > 2000) { // run every 2000 ms
    startTime = millis();

    if (client && client.connected()) {  // send data to Client
      client.print("Telnet Test, millis: ");
      client.println(millis());
      client.print("Free Heap RAM: ");
      client.println(ESP.getFreeHeap());
      client.print(F("Outdoor Temp: "));
      client.print(DS18B20Sensors[sensorOutdoor].temperature);
      client.println(F("F"));
      client.print(F("Coil Temp: "));
      client.print(DS18B20Sensors[sensorPipetemp].temperature);
      client.println(F("F"));
      client.print(DS18B20Sensors[sensorOutdoor].temperature - DS18B20Sensors[sensorPipetemp].temperature);
      client.println(F("F"));
      client.println(DS18B20Sensors[sensorPipetemp].temperature / defrostThreshold);
      if (defrostActive == 1) {
        client.println(F("Defrosting"));
      }
      if (defrostCheating == 1) {
        client.println(F("Cheating"));
      }
    }
  }
  delay(10); // to avoid strange characters left in buffer
}

//
// Convert float to string
// See http://forum.arduino.cc/index.php/topic,44262.0.html
//

char *ftoa(char *a, double f, int precision)
{
  long p[] = {0, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000};

  char *ret = a;
  long heiltal = (long)f;
  itoa(heiltal, a, 10);
  while (*a != '\0') a++;
  *a++ = '.';
  long desimal = abs((long)((f - heiltal) * p[precision]));
  itoa(desimal, a, 10);
  return ret;
}


//
// Free RAM - for figuring out the reason to upgrade from IDE 1.0.1 to 1.0.5
// Returns the free RAM in bytes - you'd be surprised to see how little that is :)
// http://playground.arduino.cc/Code/AvailableMemory
//

int freeRam () {
  extern int __heap_start, *__brkval;
  int v;
  return (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval);
}

//
// The most important thing of all, feed the watchdog
//

void feedWatchdog()
{
  wdt_reset();
}
