/*  OpenWindAir Smart CO2 sensor.
 *  Based on: ESP8266, MH-Z19, AM2302, Blynk and MQTT.
 *  Created in Arduino IDE.
 *  For more details please visit http://openwind.ru
 *  Contact us: hello@openwind.ru
*/

//#define BLYNK_PRINT Serial    // Comment this out to disable prints and save space
#include "version.h"
#include <FS.h>
#include <string.h>
//#include <SPI.h> FIXME, remove or?

//blynk
#include <ESP8266WiFi.h>
#include <BlynkSimpleEsp8266.h>

//RTC Time
#include <TimeLib.h>
#include <WidgetRTC.h>

// https://github.com/plerup/espsoftwareserial
#include <SoftwareSerial.h>

// https://github.com/adafruit/DHT-sensor-library
#include <DHT.h>

// OTA update
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>

//WiFiManager
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>

//LED ticker
#include <Ticker.h>

//MQTT library
#include <PubSubClient.h>

#define BLYNK_GREEN     "#23C48E"
#define BLYNK_BLUE      "#04C0F8"
#define BLYNK_YELLOW    "#ED9D00"
#define BLYNK_RED       "#D3435C"
#define BLYNK_DARK_BLUE "#5F7CD8"

#define DHTPIN 12
#define DHTTYPE DHT22   // DHT 22  (AM2302), AM2321

DHT dht(DHTPIN, DHTTYPE);
Ticker ticker;
WiFiClient pubsubClient;
PubSubClient mqttClient(pubsubClient);
SoftwareSerial co2Serial(2, 4, false, 256);
BlynkTimer timer;
WidgetRTC rtc;

bool connectBlynk(){
  _blynkWifiClient.stop();
  return _blynkWifiClient.connect(BLYNK_DEFAULT_DOMAIN, BLYNK_DEFAULT_PORT);
}

// PIN init
int ledRPin = 13;
int ledGPin = 14;
int ledYPin = 16;
int adcPin = A0;
int buttonS1Pin = 10;
int buttonS2Pin = 0;
int relayPin = 15;

// Variables
int buttonS1State = 1;
int buttonS2State = 1;

int ledRState = 1;
int ledGState = 1;
int ledYState = 1;

float old_h = 0;
float old_t = 0;
float old_f = 0;
float h = 0;
float t = 0;
float f = 0;

int average_ppm[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
int average_ppm_sum;
int average_ppm_index = 0;
int average_ppm_max = 1100;

int co2_limit = 2; //allowed value of CO2 limit 1, 2, 3, 5 (1k, 2k, 3k, 5k). 2k default
bool co2_limit_flag = false;

bool temp_correction = true; // default enabled for internal DHT sensor. +15%h -2C -1F

char msg_h[10];
char msg_t[10];
char msg_f[10];
char msg_ppm[10];

char blynk_token[34];
char mqtt_server[40];
char mqtt_port[6];
char mqtt_login[24];
char mqtt_key[24];

char mqtt_topic_pub[32];
char mqtt_topic_pub_status[32];
char mqtt_topic_pub_h[32];
char mqtt_topic_pub_t[32];
char mqtt_topic_pub_f[32];
char mqtt_topic_pub_ppm[32];

char Hostname[32] = "OpenWindAir";

String MAC;
char mqtt_MAC[14];

int ppm;
int uptime;

bool DHTreadOK = false; //false if not read

bool notify_flag = false; //send notify to user if true
bool notify_flag_beep = true; //beep works if true
int notify_timer_start; //not allow to send notification too often.
int notify_timer_max = 600; //interval of notify 10 min by default

bool wifilost_flag =  false;
int wifilost_timer_start;
int wifilost_timer_max = 60; // 60 sec timeout for reset if WiFi connection lost

bool shouldSaveConfig = false; //flag for saving data
int days, hours, minutes, seconds;

int adcvalue;

bool online = true;
bool ota_update = false;

String currentTime;
String currentDate;

// command to ask for data
byte askco2[9] = {0xFF, 0x01, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x79};
byte max1k[9] = {0xFF, 0x01, 0x99, 0x00, 0x00, 0x00, 0x03, 0xE8, 0x7B};
byte max2k[9] = {0xFF, 0x01, 0x99, 0x00, 0x00, 0x00, 0x07, 0xD0, 0x8F};
byte max3k[9] = {0xFF, 0x01, 0x99, 0x00, 0x00, 0x00, 0x0B, 0xB8, 0xA3};
byte max5k[9] = {0xFF, 0x01, 0x99, 0x00, 0x00, 0x00, 0x13, 0x88, 0xCB};

BLYNK_CONNECTED(){
  Blynk.syncVirtual(V101);
  Blynk.syncVirtual(V102);
  Blynk.syncVirtual(V103);
  Blynk.syncVirtual(V104);
  Blynk.syncVirtual(V105);
  Blynk.syncVirtual(V106);
  Blynk.syncVirtual(V107);
  Blynk.syncVirtual(V108);
  rtc.begin();
}

WidgetLED led1(V10);
WidgetLED led2(V11);
WidgetTerminal terminal(V100);

BLYNK_WRITE(V101){
  int v101 = param.asInt(); // assigning incoming value from pin V10x to a variable
  if (v101 == 1){
    terminal.print("\n\rRestart in 3..2..1..");
    terminal.flush();
    Serial.println("\n\rRestart in 3..2..1..");
    digitalWrite(ledRPin, HIGH);
    digitalWrite(ledGPin, HIGH);
    digitalWrite(ledYPin, HIGH);
    ESP.restart();
  }
}

BLYNK_WRITE(V102){
  int v102 = param.asInt();
  if (v102 == 1){
    terminal.print("\n\rReset WiFi settings in 3..2..1..");
    terminal.flush();
    Serial.println("\n\rReset WiFi settings in 3..2..1..");
    digitalWrite(ledRPin, HIGH);
    digitalWrite(ledGPin, HIGH);
    digitalWrite(ledYPin, HIGH);
    //wifiManager.resetSettings(); // FIXME
  }
}

BLYNK_WRITE(V103){
  int v103 = param.asInt();
  if (v103 == 1){
    terminal.print("\n\rFormat flash in 3..2..1..");
    terminal.flush();
    Serial.println("\n\rFormat flash in 3..2..1..");
    digitalWrite(ledRPin, HIGH);
    digitalWrite(ledGPin, HIGH);
    digitalWrite(ledYPin, HIGH);
    SPIFFS.format();
    ESP.restart();
  }
}

BLYNK_WRITE(V104){
  average_ppm_max = param.asInt();
  
  if (average_ppm_max <= 400){
    average_ppm_max = 400;
    Serial.print("\r\nNotify disabled");
    //terminal.print("\r\nNotify disabled");
    //terminal.flush();  
    notify_flag = false;    
  }
  
  if (average_ppm_max > 5000){
    average_ppm_max = 5000; 
  }
  
  if (average_ppm_max > 400 && average_ppm_max <= 5000){
    Serial.print("\r\nNotify level: ");    
    Serial.print(average_ppm_max);
    Serial.print(" ppm");
    //terminal.print("\r\nNotify level: ");
    //terminal.print(average_ppm_max);
    //terminal.print(" ppm");
    //terminal.flush();
  }
   
}

BLYNK_WRITE(V105){
  int v105 = param.asInt();

  if (v105 != 0){
    notify_flag_beep = true;   
  }
  else{
     notify_flag_beep = false;
  }
}

BLYNK_WRITE(V106){
  co2_limit = param.asInt();
    
  if (co2_limit != 1 && co2_limit != 2 && co2_limit != 3 && co2_limit != 5){
    co2_limit = 2;
    Serial.print("\r\nC02 limit: 2000 ppm (default value)");
    terminal.print("\r\nC02 limit: 2000 ppm (default value)");
    terminal.flush();
    co2_limit_flag = true;
  }
  else{
    Serial.print("\r\nC02 limit: ");
    Serial.print(co2_limit * 1000);
    Serial.print(" ppm");
    terminal.print("\r\nC02 limit: ");
    terminal.print(co2_limit * 1000);
    terminal.print(" ppm");
    terminal.flush();
    co2_limit_flag = true;
    
  }
     
}

BLYNK_WRITE(V107){
  int v107 = param.asInt();

  if (v107 != 0){
    temp_correction = true;    
  }
  else{
   temp_correction = false; 
  }

}

BLYNK_WRITE(V108){
  int v108 = param.asInt();

  if (v108 != 0){
    ota_update = true;    
  }
  else{
   ota_update = false; 
  }

}

void tick(){
  //toggle state
  int state = digitalRead(ledRPin);  // get the current state of Pin
  digitalWrite(ledRPin, !state);     // set Pin to the opposite state
}

// toggle LED state. for future use
void led_toggle_r(){
  int state = digitalRead(ledRPin);  // get the current state of Pin
  digitalWrite(ledRPin, !state);     // set Pin to the opposite state
}

void led_toggle_g(){
 int state = digitalRead(ledGPin);  // get the current state of Pin
 digitalWrite(ledGPin, !state);     // set Pin to the opposite state
}

void led_toggle_y(){
 int state = digitalRead(ledYPin);  // get the current state of Pin
 digitalWrite(ledYPin, !state);     // set Pin to the opposite state
}

// WiFiManager voids
void configModeCallback (WiFiManager *myWiFiManager){
  //gets called when WiFiManager enters configuration mode
  Serial.println("Entered config mode");
  Serial.println(WiFi.softAPIP());
  //if you used auto generated SSID, print it
  Serial.println(myWiFiManager->getConfigPortalSSID());
  //entered config mode, make led toggle faster
  ticker.attach(0.2, tick);
}

void saveConfigCallback(){  //callback notifying us of the need to save config
  Serial.println("Should save config");
  shouldSaveConfig = true;
  ticker.attach(0.2, tick);  // led toggle faster

}

// Main functions
int readCO2(){

  char response[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0}; // for answer

if (co2_limit_flag){
  switch(co2_limit){
    case '1' :
         co2Serial.write(max1k, 9);
         break;
   case '2' :
         co2Serial.write(max2k, 9);
         break;
   case '3' :
         co2Serial.write(max3k, 9);
         break;
   case '5' :
         co2Serial.write(max5k, 9);
         break;
   default :
         co2Serial.write(max2k, 9);
   }  
}

  co2_limit_flag = false;

  co2Serial.write(askco2, 9); //request PPM CO2
  delay(1);
  
  while (co2Serial.available() > 0 && (unsigned char)co2Serial.peek() != 0xFF){
    co2Serial.read();
  }

  memset(response, 0, 9);
  co2Serial.readBytes(response, 9);

  if (response[0] != 0xFF){
    Serial.print("\n\rWrong starting byte from co2 sensor!");
    return -1;
  }

  if (response[1] != 0x86){
    Serial.print("\n\rWrong command from co2 sensor!");
    return -1;
  }

  int responseHigh = (int) response[2];
  int responseLow = (int) response[3];
  int ppm = (256 * responseHigh) + responseLow;
  Serial.print(" ok");
  return ppm;  
}

void notify(){
  
   if (!notify_flag && average_ppm_max > 400 && average_ppm_sum >= average_ppm_max){
      notify_flag = !notify_flag;
      notify_timer_start = uptime;
    }

   if (notify_flag && average_ppm_max > average_ppm_sum ){
      notify_flag = !notify_flag;
    }
    
   if (notify_flag && ((uptime -  notify_timer_start) > notify_timer_max)){
      
      Blynk.notify(String("CO2 level > ") + average_ppm_sum + ". Please Open Window.");
      terminal.print("\n\rSending notify to phone. ");
      terminal.print("ppm > ");
      terminal.print(average_ppm_sum);
      terminal.flush();
      Serial.print("\n\rSending notify to phone. ");
      Serial.print("\n\rCO2 level > ");
      Serial.print(average_ppm_sum);
      
      tone(5, 1000, 50);
      delay(50);
      tone(5, 1000, 50);
      delay(50);
      tone(5, 1000, 50); 
      
      notify_flag = false;    
  }
    
   if (notify_flag){
      terminal.print("\n\rNotify in: ");
      terminal.print(notify_timer_max + notify_timer_start - uptime);
      terminal.print(" seconds");
      terminal.flush();
    
      Serial.print("\n\rNotify in: ");
      Serial.print(notify_timer_max + notify_timer_start - uptime);
      Serial.print(" seconds");     
    }

}

void readMHZ19(){

  int i = 0;
  ppm = -1;
  Serial.print("\n\rReading MHZ19 sensor:");
  while (i < 5 && ppm == -1){
    delay(i*50);
    ppm = readCO2();
    i++;
  }
 
  if (ppm == -1){
    led2.on();
    led2.setColor(BLYNK_YELLOW);
    Serial.print(" failed");
  }

  if (average_ppm_sum == 0){        
    digitalWrite(ledRPin, LOW);
    digitalWrite(ledGPin, HIGH);
    digitalWrite(ledYPin, LOW);    
  }
  if (average_ppm_sum > 0 && average_ppm_sum <= 900){
  digitalWrite(ledRPin, LOW);
  digitalWrite(ledGPin, HIGH);
  digitalWrite(ledYPin, LOW);

  led1.on();
  led1.setColor(BLYNK_GREEN);

  led2.on();
  led2.setColor(BLYNK_GREEN);  

  }

  if (average_ppm_sum > 900 && average_ppm_sum < 1400){
  digitalWrite(ledRPin, LOW);
  digitalWrite(ledGPin, LOW);
  digitalWrite(ledYPin, HIGH);
  
  led1.on();
  led1.setColor(BLYNK_YELLOW);
  
  led2.on();
  led2.setColor(BLYNK_GREEN);
  }
  
  if (average_ppm_sum >= 1400){
   
   digitalWrite(ledRPin, HIGH);
   digitalWrite(ledGPin, LOW);
   digitalWrite(ledYPin, LOW);

  led1.on();
  led1.setColor(BLYNK_RED); 
  
  led2.on();
  led2.setColor(BLYNK_GREEN);

  }

  ledRState = digitalRead(ledRPin);
  ledGState = digitalRead(ledGPin);
  ledYState = digitalRead(ledYPin);

}

void readDHT22(){
  
  DHTreadOK = false;
  int i = 0;
  Serial.print("\n\rReading DHT22 sensor:");
  while (i < 5 && !DHTreadOK){
    delay(i*75);
    h = dht.readHumidity();
    t = dht.readTemperature();
    f = dht.readTemperature(true); // Read temperature as Fahrenheit (isFahrenheit = true)
  
    if (isnan(h) || isnan(t) || isnan(f)){
      Serial.print(".");
      i++;
    }
    else{   
      DHTreadOK = true;
      if (temp_correction){
       if (!isnan(h)){
          h = h + 15;
          old_h = h;}
       if (!isnan(h)){
          t = t - 2;
          old_t = t;}
       if (!isnan(h)){
          f = f - 1;
          old_f = f;}   
      }
      else{
       if (!isnan(h)){
          old_h = h;}
       if (!isnan(h)){
          old_t = t;}
       if (!isnan(h)){
          old_f = f;}
        
      }
    }
  }
  if (DHTreadOK){ 
    led2.on();
    led2.setColor(BLYNK_GREEN);
    Serial.print(" ok");
  }
  else{
    led2.on();
    led2.setColor(BLYNK_RED);
    Serial.print(" failed");
  }
  
}

void readADC(){
  
  adcvalue = analogRead(adcPin);
  if (Blynk.connected()){
    Blynk.virtualWrite(V5, adcvalue);
  }
  
}

void SayHello(){
  Serial.print("\n\r======SYSTEM-STATUS================================");
  Serial.print("\n\rDevice name: ");
  Serial.print(Hostname);
  Serial.print("\r\nSoftware version: ");
  Serial.print(SW_VERSION);  
  Serial.print("\r\nFreeHeap: ");
  Serial.print(ESP.getFreeHeap());
  Serial.print("\r\nChipId: "); //ESP8266 chip IDE, int 32bit
  Serial.print(ESP.getChipId());
  Serial.print("\r\nFlashChipId: "); //flash chip ID, int 32bit
  Serial.print(ESP.getFlashChipId());
  Serial.print("\r\nFlashChipSize: ");
  Serial.print(ESP.getFlashChipSize());
  Serial.print("\r\nFlashChipSpeed: ");
  Serial.print(ESP.getFlashChipSpeed());
  Serial.print("\r\nCycleCount: "); //unsigned 32-bit
  Serial.print(ESP.getCycleCount());
  Serial.print("\r\nTime: ");
  Serial.print(currentTime);
  Serial.print(" ");
  Serial.print(currentDate);
  Serial.print("\r\nUpTime: ");
  Serial.print(uptime);
  Serial.print("\n\r======BLYNK-STATUS=================================");
  Serial.print("\n\rBlynk token: ");
  Serial.print(blynk_token);
  Serial.print("\n\rBlynk connected: ");
  Serial.print(Blynk.connected());
  if (average_ppm_max == 400){
    Serial.print("\r\nNotify level: disabled");
  }
  else{
    Serial.print("\r\nNotify level: ");
    Serial.print(average_ppm_max);
  }
  Serial.print("\r\nBeep: ");
  Serial.print(notify_flag_beep); 
  Serial.print("\r\nCO2 limit: ");
  Serial.print(co2_limit * 1000);  
  Serial.print("\r\nTemperature correction: ");
  Serial.print(temp_correction);
  Serial.print("\n\r======NETWORK-STATUS===============================");
  Serial.print("\n\rWiFi network: ");
  Serial.print(WiFi.SSID());
  Serial.print("\n\rWiFi status: ");
  Serial.print(WiFi.status());
  Serial.print("\r\nRSSI: ");
  Serial.print(WiFi.RSSI());  
  Serial.print("\n\rMAC: ");
  Serial.print(mqtt_MAC);
  Serial.print("\n\rIP: ");
  Serial.print(WiFi.localIP());
  Serial.print("\n\rOnline: ");
  Serial.print(online);
  Serial.print("\n\r======MQTT-STATUS==================================");
  Serial.print("\n\rMQTT server: ");
  Serial.print(mqtt_server);
  Serial.print("\n\rMQTT port: ");
  Serial.print(mqtt_port);
  Serial.print("\n\rMQTT login: ");
  Serial.print(mqtt_login);
  Serial.print("\n\rMQTT key: ");
  Serial.print(mqtt_key);
  Serial.println("\n\rMQTT topics:");
  Serial.println(mqtt_topic_pub_h);
  Serial.println(mqtt_topic_pub_t);
  Serial.println(mqtt_topic_pub_f);
  Serial.println(mqtt_topic_pub_ppm);
  Serial.println(mqtt_topic_pub_status);
  Serial.println("======END-of-STATUS================================");
  
}

void tone(uint8_t _pin, unsigned int frequency, unsigned long duration){
  if (notify_flag_beep){
    pinMode (_pin, OUTPUT);
    analogWriteFreq(frequency);
    analogWrite(_pin,500);
    delay(duration);
    analogWrite(_pin,0);
  }
}

// Setup
void setup(){

  Serial.begin(9600);
  delay(2000);
  co2Serial.begin(9600);
  delay(2000);
  dht.begin();
    
  pinMode(buttonS1Pin, INPUT);
  pinMode(buttonS2Pin, INPUT);
  pinMode(relayPin, OUTPUT);
  pinMode(adcPin, INPUT);  
  pinMode(ledRPin, OUTPUT);
  pinMode(ledGPin, OUTPUT);
  pinMode(ledYPin, OUTPUT);
  
  delay(2000);

  switch(co2_limit){
    case '1' :
         co2Serial.write(max1k, 9);
         break;
   case '2' :
         co2Serial.write(max2k, 9);
         break;
   case '3' :
         co2Serial.write(max3k, 9);
         break;
   case '5' :
         co2Serial.write(max5k, 9);
         break;
   default :
         co2Serial.write(max2k, 9);
   }
  
  delay(500);
  co2Serial.write(askco2, 9);
  tone(5,1000,100);


  // start ticker with 0.5 because we start in AP mode and try to connect
  ticker.attach(0.6, tick);

  // clean FS, for testing
  //SPIFFS.format();

  // Check flash size
  String realSize = String(ESP.getFlashChipRealSize());
  String ideSize = String(ESP.getFlashChipSize());
  bool flashCorrectlyConfigured = realSize.equals(ideSize);

  //todo smth

  if(flashCorrectlyConfigured){
    Serial.println("flash correctly configured, SPIFFS starts, IDE size: " + ideSize + ", match real size: " + realSize);
  }
  else{
    Serial.println("flash incorrectly configured, SPIFFS cannot start, IDE size: " + ideSize + ", real size: " + realSize);
  }

  //read configuration from FS json
  Serial.println("mounting FS...");

  if (SPIFFS.begin()){
    Serial.println("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
          Serial.println("\nparsed json");

          strcpy(blynk_token, json["blynk_token"]);
          strcpy(mqtt_server, json["mqtt_server"]);
          strcpy(mqtt_port, json["mqtt_port"]);
          strcpy(mqtt_login, json["mqtt_login"]);
          strcpy(mqtt_key, json["mqtt_key"]);
        } 
        else{
          Serial.println("Failed to load json config");
        }
      }
    }
  } 
  else{
    Serial.println("Failed to mount FS");
  }
  //end read



  // The extra parameters to be configured (can be either global or just in the setup)
  // After connecting, parameter.getValue() will get you the configured value
  // id/name placeholder/prompt default length

  WiFiManagerParameter custom_blynk_token("blynk", "blynk token", blynk_token, 33);   // was 32 length ???
  WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server, 40);
  WiFiManagerParameter custom_mqtt_port("port", "mqtt port", mqtt_port, 5);
  WiFiManagerParameter custom_mqtt_login("login", "mqtt login", mqtt_login, 23);
  WiFiManagerParameter custom_mqtt_key("key", "mqtt key", mqtt_key, 23);

  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;
  
  //reset settings - for testing
  //wifiManager.resetSettings();

  
  // WiFi credentials will be reseted if button S1 will be pressed during boot
  buttonS1State = digitalRead(buttonS1Pin);

  if (buttonS1State == 0){
    Serial.println("ResetWiFi settings");
    wifiManager.resetSettings();    
  }

  //set minimu quality of signal so it ignores AP's under that quality
  //defaults to 8%
  //wifiManager.setMinimumSignalQuality();

  //set callback that gets called when connecting to previous WiFi fails, and enters Access Point mode
  wifiManager.setAPCallback(configModeCallback);

  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  wifiManager.addParameter(&custom_blynk_token);   //add all your parameters here
  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.addParameter(&custom_mqtt_login);
  wifiManager.addParameter(&custom_mqtt_key);

  //sets timeout until configuration portal gets turned off
  //useful to make it all retry or go to sleep, in seconds
  wifiManager.setTimeout(300);   // 5 minutes to enter data and then ESP resets to try again.

  //fetches ssid and pass and tries to connect
  //if it does not connect it starts an access point with the specified name
 
  if (!wifiManager.autoConnect("OpenWind - tap to config")){ 
    
    if (mqtt_server[0] != '\0' || blynk_token[0] != '\0'){
  
      Serial.println("Failed to go online for Blynk and MQTT, restarting..");
      delay(2000);
      ESP.restart();      
    }
    else{
      Serial.println("Failed to go online, offline mode activated");
      online = false;
      tone(5,2000,50);    
    }

  }
    
  ticker.detach();
  
  if (online){
  
  tone(5,1500,30);
    
  strcpy(blynk_token, custom_blynk_token.getValue());    //read updated parameters
  strcpy(mqtt_server, custom_mqtt_server.getValue());
  strcpy(mqtt_port, custom_mqtt_port.getValue());
  strcpy(mqtt_login, custom_mqtt_login.getValue());
  strcpy(mqtt_key, custom_mqtt_key.getValue());

  if (shouldSaveConfig) {      //save the custom parameters to FS
    Serial.println("saving config");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["blynk_token"] = blynk_token;
    json["mqtt_server"] = mqtt_server;
    json["mqtt_port"] = mqtt_port;
    json["mqtt_login"] = mqtt_login;
    json["mqtt_key"] = mqtt_key;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("Failed to open config file for writing");
    }

    json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();
    //end save

    delay(1000);
    Serial.println("Restart ESP to apply new WiFi settings..");
    ESP.restart();

  }

  Serial.print("\n\rWiFi network: ");
  Serial.print(WiFi.SSID());

  Serial.print("\n\rWiFi status: ");
  Serial.print(WiFi.status());
  
  Serial.print("\n\rlocal ip: ");
  Serial.print(WiFi.localIP());

  if (blynk_token[0] != '\0'){
    connectBlynk();    
    Blynk.config(blynk_token);
    Blynk.connect();
    setSyncInterval(10*60); // interval of RTC sync
    Serial.print("\n\rblynk token: ");
    Serial.print(blynk_token);
  }
  else{
     Serial.print("\n\rblynk auth token not set"); 
  }
  
  Serial.print("\n\rOpenWindAir is ready!");

  uint16_t mqtt_portnum = strtoul(mqtt_port, NULL, 10);

  mqttClient.setServer(mqtt_server, mqtt_portnum);
  //mqttClient.setCallback(callback);

  strcat(mqtt_topic_pub, mqtt_login);
  strcat(mqtt_topic_pub, "/");
  strcat(mqtt_topic_pub, Hostname);

  strcat(mqtt_topic_pub_h, mqtt_topic_pub);
  strcat(mqtt_topic_pub_t, mqtt_topic_pub);
  strcat(mqtt_topic_pub_f, mqtt_topic_pub);
  strcat(mqtt_topic_pub_ppm, mqtt_topic_pub);
  strcat(mqtt_topic_pub_status, mqtt_topic_pub);
    
  strcat(mqtt_topic_pub_h, "/h");
  strcat(mqtt_topic_pub_t, "/t");
  strcat(mqtt_topic_pub_f, "/f");
  strcat(mqtt_topic_pub_ppm, "/ppm");
  strcat(mqtt_topic_pub_status, "/status");

  MAC = WiFi.macAddress();
  MAC.replace(":", "");
  MAC.toCharArray(mqtt_MAC, 13);
     
  }
  
  timer.setInterval(1000L, sendUptime);
  timer.setInterval(30000L, notify);  
  timer.setInterval(30000L, readMHZ19);
  timer.setInterval(30000L, readDHT22);
  timer.setInterval(60000L, readADC);
  timer.setInterval(30000L, sendResults);
  timer.setInterval(300000L, mqttsend);

  //Serial.setDebugOutput(true);

  ESP.wdtDisable();
}

// Main functions 2
void reconnect(){
  //Serial.print("\n\rReading D");
  if (mqtt_server[0] != '\0'){
    // Loop until we're reconnected
    while (!mqttClient.connected()) {
      Serial.print("\n\rAttempting MQTT connection...");
      // Attempt to connect
        if (mqttClient.connect(mqtt_MAC, mqtt_login, mqtt_key)) {
        Serial.print("connected");
        // Once connected, publish an announcement...
        mqttClient.publish(mqtt_topic_pub_status, "online");
        // ... and resubscribe
        //mqttClient.subscribe("inTopic");
      } 
      else{
        int i = 0;
        while (i < 3){
          Serial.print("failed, rc=");
          Serial.print(mqttClient.state());
          Serial.println(" try again in 1 seconds");
          delay(1000);
          i++;          
        }
        break;
      }
    }
  }
  else{
    Serial.print("\n\rMQTT server not set");
  }
}

void mqttsend(){

  if (online){
  
    if (!mqttClient.connected()){
      reconnect();
    }
  
    if (mqttClient.connected()){
       mqttClient.loop();
  
       char msg_h[10];
       char msg_t[10];
       char msg_f[10];
       char msg_ppm[10];
       
       dtostrf(h , 2, 2, msg_h);
       dtostrf(t , 2, 2, msg_t);
       dtostrf(f , 2, 2, msg_f);
       dtostrf(average_ppm_sum , 2, 2, msg_ppm);
    
       Serial.print("\n\rSending data to ");
       Serial.print(mqtt_server);     
       if (DHTreadOK){
         mqttClient.publish(mqtt_topic_pub_h, msg_h);
         mqttClient.publish(mqtt_topic_pub_t, msg_t);
         mqttClient.publish(mqtt_topic_pub_f, msg_f);
       }
        if (average_ppm_sum != 0){
          mqttClient.publish(mqtt_topic_pub_ppm, msg_ppm);
        }
       mqttClient.disconnect();       
    }
}
}

void sendUptime(){

  uptime = millis() / 1000;
  
  seconds = millis() / 1000;
  minutes = seconds / 60;
  hours = seconds / 3600;
  days = seconds / 86400;
  seconds = seconds - minutes * 60;
  minutes = minutes - hours * 60;
  hours = hours - days * 24;
  
  if (Blynk.connected()){
  Blynk.virtualWrite(V99, uptime);
  currentTime = String(hour()) + ":" + minute() + ":" + second();
  currentDate = String(day()) + "/" + month() + "/" + year();
  }
  
}

void sendResults(){
  
  Serial.println("\n\r===================================================");
  if (DHTreadOK){ 

      Blynk.virtualWrite(V1, h);
      Blynk.virtualWrite(V2, t);
      Blynk.virtualWrite(V3, f);

      terminal.print("\n\rh t f: ");
      terminal.print(h);
      terminal.print(" ");
      terminal.print(t);
      terminal.print(" ");
      terminal.print(f);
      terminal.flush();
  
      Serial.print("\n\rHumidity: ");
      Serial.print(h);
      Serial.print("%");
      Serial.print("\n\rTemperature: ");
      Serial.print(t);
      Serial.print("C \\ ");
      Serial.print(f);
      Serial.print("F");  

    }
  else{
      Blynk.virtualWrite(V1, old_h);
      Blynk.virtualWrite(V2, old_t);
      Blynk.virtualWrite(V3, old_f);
      
      terminal.print("\n\rh t f: ");
      terminal.print(old_h);
      terminal.print(" ");
      terminal.print(old_t);
      terminal.print(" ");
      terminal.print(old_f);
      terminal.print(" (old)");
      terminal.flush();

      Serial.print("\n\rHumidity: ");
      Serial.print(old_h);
      Serial.print("% (old)");
      Serial.print("\n\rTemperature: ");
      Serial.print(old_t);
      Serial.print("C (old) \\ ");
      Serial.print(old_f);
      Serial.print("F (old)");  
    }
    
  if (ppm != -1){

      average_ppm[average_ppm_index] = ppm;
      average_ppm_index++;
      if (average_ppm_index > 8){
        average_ppm_index = 0;       
      }
   
      int i;

      // FIXME: need another average formula
      // for(i = 0, average_ppm_sum = 0; average_ppm[i] != 0; i++){
      //   average_ppm_sum+=average_ppm[i];
      //  } 
      // if (i){
      //   average_ppm_sum = average_ppm_sum / i;
      //  }
      
      
      average_ppm_sum = 0;
      for (i = 0; i < 10; i++){
        average_ppm_sum = average_ppm_sum + average_ppm[i];
      }        
      average_ppm_sum = average_ppm_sum / 10;
      
      
      Blynk.virtualWrite(V4, average_ppm_sum);
  
      terminal.print("\n\rC02 (aver): ");
      terminal.print(ppm);
      terminal.print(" (");
      terminal.print(average_ppm_sum);
      terminal.print(") ppm");
      terminal.flush();

      Serial.print("\n\rC02: ");
      Serial.print(ppm);
      Serial.print(" ppm");
      Serial.print("\n\rC02 average: ");
      Serial.print(average_ppm_sum);
      Serial.print(" ppm");      
     
    }
  else{

    Serial.print("\n\rC02: ");
    Serial.print("\n\rC02 average: ");
    Serial.print(average_ppm_sum);
    Serial.print(" ppm"); 
    terminal.print("\n\rC02 (aver): ");
    terminal.print(" - ");
    terminal.print(" (");
    terminal.print(average_ppm_sum);
    terminal.print(") ppm");
    terminal.flush();

  }

  Serial.print("\r\nADC: ");
  Serial.print(adcvalue);
  
  Serial.print("\n\rUpTime: ");
  Serial.print(days);
  Serial.print(" days, ");
  Serial.print(hours);
  Serial.print(" hours, ");  
  Serial.print(minutes);
  Serial.print(" minutes, ");
  Serial.print(seconds);
  Serial.print(" seconds.");

  Serial.print("\r\nTime: ");
  Serial.print(currentTime);
  Serial.print(" ");
  Serial.print(currentDate);  

  terminal.print("\r\nADC: ");
  terminal.print(adcvalue);
  terminal.print("\n\rUpTime: ");
  terminal.print(days);
  terminal.print("d, "); 
  terminal.print(hours);
  terminal.print("h, ");
  terminal.print(minutes);
  terminal.print("m, ");
  terminal.print(seconds);
  terminal.print("s");
  
  terminal.print("\r\nTime: ");
  terminal.print(currentTime);
  terminal.print(" ");
  terminal.print(currentDate);
  terminal.print("\r\nSW: ");
  terminal.print(SW_VERSION);
  terminal.flush();
  Serial.println("\n\r===================================================");
  
}

// LOOP
void loop(){

  if (WiFi.status() == WL_CONNECTED){
    wifilost_flag = false;

    if (blynk_token[0] != '\0'){
      
      if (Blynk.connected() && _blynkWifiClient.connected()){
        Blynk.run();
      }
      else{
        Serial.print("\n\rReconnecting to blynk.. ");
        Serial.print(Blynk.connected());
       if (!_blynkWifiClient.connected()){
           connectBlynk();
           return;
        }
        
        //FIXME: add exit after n-unsuccesfull tries.
        Blynk.connect(4000);
        Serial.print(Blynk.connected());
      }
    }

 }

  if (WiFi.status() != WL_CONNECTED && online){  
    if (!wifilost_flag){
      wifilost_timer_start = uptime;
      wifilost_flag = true;
    }
    if (((uptime -  wifilost_timer_start) > wifilost_timer_max) && wifilost_flag){
      Serial.print("\n\rWiFi connection lost, restarting..");
      wifilost_flag = false;
      ESP.restart();
    }
  }

  timer.run();  
  ESP.wdtFeed();
  

  //digitalWrite(ledRPin, LOW);
  //digitalWrite(ledGPin, LOW);
  //digitalWrite(ledYPin, LOW);

  //digitalWrite(ledRPin, HIGH);
  //digitalWrite(ledGPin, HIGH);
  //digitalWrite(ledYPin, HIGH);

  buttonS1State = digitalRead(buttonS1Pin);
  buttonS2State = digitalRead(buttonS2Pin);
 
  if (buttonS1State == 0){    
   
    tone(5, 1000, 50);
    delay(50);
    tone(5, 1000, 50);
    delay(50);
    tone(5, 1000, 50);
    
    delay(50);
    tone(5, 1000, 150);
    delay(50);
    tone(5, 1000, 150);
    delay(50);
    tone(5, 1000, 150);
    
    delay(50);
    tone(5, 1000, 50);
    delay(50);
    tone(5, 1000, 50);
    delay(50);
    tone(5, 1000, 50);

    int relayState = digitalRead(relayPin);  // get the current state of relay IO15 pin
    digitalWrite(relayPin, !relayState);     // set pin to the opposite state
    
    Serial.println("\n\rRelay tick");
    delay(1000);

    relayState = digitalRead(relayPin);  // get the current state of relay IO15 pin
    digitalWrite(relayPin, !relayState);     // set pin to the opposite state
    
    Serial.println("\n\rRelay tick");
    delay(1000);

    relayState = digitalRead(relayPin);  // get the current state of relay IO15 pin
    digitalWrite(relayPin, !relayState);     // set pin to the opposite state
    
    Serial.println("\n\rRelay tick");
    delay(1000);

  }
   
  int updatein = 10;   
  while ((buttonS2State == 0 && WiFi.status() == WL_CONNECTED) || ota_update){    
    tone(5, 5000, 50);
    delay(100);
    led_toggle_r();
    tone(5, 5000, 50);
    delay(100);
    led_toggle_g();
    tone(5, 5000, 50);
    delay(100);
    led_toggle_y();

    if (updatein == 0){
    Serial.print("\r\nOTA update starting. Please don't power off");
    t_httpUpdate_return ret = ESPhttpUpdate.update("http://openwind.ru/OpenWindAir.ino.nodemcu.bin");
    Serial.print(ret);    

    switch(ret) {
        case HTTP_UPDATE_FAILED:
            Serial.printf("HTTP_UPDATE_FAILD Error (%d): %s", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
            break;

        case HTTP_UPDATE_NO_UPDATES:
            Serial.println("HTTP_UPDATE_NO_UPDATES");
            break;

        case HTTP_UPDATE_OK:
            Serial.println("HTTP_UPDATE_OK");
            break;
    }


    }
    updatein--;
    buttonS2State = digitalRead(buttonS2Pin);    
  }

  digitalWrite(ledRPin, ledRState);
  digitalWrite(ledGPin, ledGState);
  digitalWrite(ledYPin, ledYState);

  while (Serial.available() > 0){
    //Serial.println(Serial.read());  
  
    if (Serial.read() == '\r' || Serial.read() == '\n'){
      SayHello();
      tone(5, 1000, 100);

    }
  }

}


