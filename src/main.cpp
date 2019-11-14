#include <Arduino.h>
#include <Wire.h>
#include <Ticker.h>
#include <ArduinoOTA.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>


#define VERSION "001"
#define SDA  D6
#define SCL  D1
#define ADDRESS 0x4D // MCP3221-A5 address in 7bit format (There are other addresses available)
#define PIN_SW  D0 //pin na prepinani topeni-mereni
#define TIME_TOP 60  //doba v sekundach pro topeni
#define TIME_MER 90  //doba v sekundach pro mereni
#define TIME_ODSTUP 20 //doba od konce topeni do zacatku mereni
#define LED_G D2
#define LED_R D3
#define BUZZ D4
#define DELKA 100 //delka pole s vysledky
#define SKRIPTV "sonda/cosensor.php"




typedef struct {
  uint32_t cas;
  uint32_t count;
  float    adc;
  float    ratio;
  float   ppm;
  float   temp;
  float   humi;
  float   pres;
} ZAZNAM;


uint8_t mereni=0;
uint32_t last_millis=0;
uint32_t step_time=0;
float vysledek;
uint16_t counter;
uint8_t odstup_flag=0;
//promenne pro pripojeni k wifi
#include "password.h"
//const char* ssid     = "xxxxx";
//const char* password = "yyyyyy";

//pole pro hodnoty
ZAZNAM hodnoty[DELKA];
uint8_t vysl_index=0;
uint32_t vysl_count=0;
//hodnota odporu cidla pro VZDUCH
float rs_gas=0;
//kalibracni konstany
float UCC=4.59;
float ADC=4096;
float R2=6861;
float VZDUCH=2114;
float UCC3V3=3.243;
float R1X=2191;
float R2X=4670;
//promenne pro odesilanionst char* host = "zapadlo.name";
IPAddress host_IP_fallback(91,139,59,185);
IPAddress host_IP;
const int httpPort = 80;
const char* host = "zapadlo.name";



String INDEX_HTML;
String INDEX_HTML_1 =
"<!DOCTYPE HTML>"
"<html>"
"<head>"
"<meta charset=\"UTF-8\">"
"<meta name = \"viewport\" content = \"width = device-width, initial-scale = 1.0, maximum-scale = 1.0, user-scalable=0\">"
"<title>ESP8266 Web Form Settings</title>"
"<style>"
"\"body { background-color: #808080; font-family: Arial, Helvetica, Sans-Serif; Color: #000000; }\""
"</style>"
"</head>"
"<body>";
//"<h1>Nastavení svícení</h1>"
//"</body>"
//"</html>";

//definice funkci
uint16_t ret_adc();
void adc_mereni();
void odstup_func();
void handleRoot();
void handleNotFound();
void index();
void  create_row(uint8_t pozice);
void odesli (uint8_t pozice);

Ticker adc;
Ticker odstup_ticker;
ESP8266WebServer server(80);
Adafruit_BME280 bme; // I2C

void setup() {
  // put your setup code here, to run once:
  Wire.begin(SDA,SCL);
  Serial.begin(115200);
  pinMode(PIN_SW, OUTPUT);
  digitalWrite(PIN_SW,0);
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.println("");
  Serial.print("Start");
  Serial.print("Verze");
  Serial.println(VERSION);
  //inicializace ADC prevodniku
  Wire.beginTransmission(ADDRESS);
  if ( Wire.endTransmission()==0){
    Serial.println("I2C device found at address 0x4D");
  } else {
      Serial.println("I2C device NOT found at address 0x4D");
  }
  //inicializace cidla teplota/tlak/vlhkost
  if (!bme.begin(0x76)) {
    if (!bme.begin(0x77)){
      Serial.println("Could not find a valid BME280 sensor, check wiring!");
    } else {
      Serial.println("Found in i2c adress 0x77");
    }
  } else {
    Serial.println("Found in i2c adress 0x76");
  }
  Serial.println();
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);

  while (WiFi.status() != WL_CONNECTED) {
    digitalWrite(LED_BUILTIN,0);
    delay(50);
    Serial.print(".");
    digitalWrite(LED_BUILTIN,1);
    delay(350);
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.println("Starting http server");
  //start web server
  if (MDNS.begin("cosensor")) {
     Serial.println("MDNS responder started");
  }
  server.on("/", handleRoot);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("HTTP server started");
  adc.attach_ms(600, adc_mereni);

   //OTA

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

   ArduinoOTA.setHostname("cosensor");
   ArduinoOTA.begin();
//vypocet odporu cidla na vzduchu
float sensor_volt=(VZDUCH/ADC)*UCC3V3*R2/R2X;
rs_gas=((UCC*R2)/sensor_volt)-R2;
Serial.print("Hodnota sensor_volt: ");
Serial.println(sensor_volt);
Serial.print("Hodnota rs_gas: ");
Serial.println(rs_gas);


}

void loop() {
  //obsluha OTA
  ArduinoOTA.handle();
  //obsluha web clientu
  server.handleClient();



  if (millis() > last_millis){
    if (mereni==1){
      mereni=0;
      //prvni pruchod na konci mereni
      Serial.print("Vysledek: ");
      Serial.println(vysledek/counter);
      Serial.print("Pocet mereni: ");
      Serial.println(counter);
      hodnoty[vysl_index].adc=vysledek/counter;
      hodnoty[vysl_index].count=vysl_count;
      hodnoty[vysl_index].temp=bme.readTemperature();
      hodnoty[vysl_index].pres=bme.readPressure();
      hodnoty[vysl_index].humi=bme.readHumidity();
      //vypocet hodnota
      float napeti=(hodnoty[vysl_index].adc*UCC3V3/ADC)*(R1X+R2X)/R2X;
      float rs_co=(UCC-napeti)*R2/napeti;
      float ratio=(rs_co/rs_gas);
      float ppm=0.96*pow(ratio,-1.67);
      hodnoty[vysl_index].ppm=ppm;
      hodnoty[vysl_index].ratio=ratio;
      odesli(vysl_index);
      vysl_count++;
      vysl_index++;
      if (vysl_index==DELKA){
        vysl_index=0;
      }
      vysledek=0;
      counter=0;
      odstup_flag=0;

      //posledni hodnota pro kalibraci
      Serial.print("Posledni hodnota: ");
      Serial.println(ret_adc());

      // zapiname topeni
      last_millis=millis()+TIME_TOP*1000;
      digitalWrite(PIN_SW, 1);
      Serial.println("Topeni");
    } else {
      //vypiname topeni
      last_millis=millis()+TIME_MER*1000;
      digitalWrite(PIN_SW, 0);
      mereni=1;
      odstup_ticker.attach_ms(TIME_ODSTUP*1000,odstup_func);
      Serial.println("Mereni");
    }
  }



}


//------------------------------------------------

uint16_t ret_adc(){
  Wire.requestFrom(ADDRESS, 2); //requests 2 bytes
  while(Wire.available() < 2);
  //while two bytes to receive
  uint8_t adc_LSB, adc_MSB;
  adc_MSB = Wire.read();
  adc_LSB= Wire.read();
  return ((adc_MSB * 256) + adc_LSB);

}

void adc_mereni(){
  if ((odstup_flag==1)&&(mereni==1)){
    counter++;
    vysledek += ret_adc();
  }
}

void odstup_func(){
  odstup_flag=1;
  odstup_ticker.detach();
}

void handleRoot() {
  digitalWrite(LED_BUILTIN, 0);
// if (server.hasArg("JASDEN")) {
//    handleSubmit();
//  } else {
  index();
  server.send(200, "text/html", INDEX_HTML);
//  }
  digitalWrite(LED_BUILTIN, 1);
}

void handleNotFound(){
  digitalWrite(LED_BUILTIN, 0);
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET)?"GET":"POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i=0; i<server.args(); i++){
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
  digitalWrite(LED_BUILTIN, 1);
}

void index() {
  INDEX_HTML = INDEX_HTML_1;
  INDEX_HTML += "<table>";
  INDEX_HTML +="<tr><td>Pozice v poli</td><td>Čas</td><td>Číslo měření</td><td>Hodnota ADC</td><td>Ratio</td><td>PPM</td><td>Teplota</td>";
  INDEX_HTML +="<td>Tlak</td><td>Vlhkost</td></tr>\n";
  for (uint8_t i=vysl_index; i > 0; i--){
    create_row(i-1);
  }
  for (uint8_t i=DELKA-1; i>vysl_index; i--){
    create_row(i);
  }
  INDEX_HTML +="</table></body></html>";
}

void  create_row(uint8_t pozice){
  INDEX_HTML +="<tr><td>";
  INDEX_HTML += pozice;
  INDEX_HTML +="</td><td>";
  INDEX_HTML += hodnoty[pozice].cas;
  INDEX_HTML +="</td><td>";
  INDEX_HTML += hodnoty[pozice].count;
  INDEX_HTML +="</td><td>";
  INDEX_HTML += hodnoty[pozice].adc;
  INDEX_HTML +="</td><td>";
  INDEX_HTML += hodnoty[pozice].ratio;
  INDEX_HTML +="</td><td>";
  INDEX_HTML += hodnoty[pozice].ppm;
  INDEX_HTML +="</td><td>";
  INDEX_HTML += hodnoty[pozice].temp;
  INDEX_HTML +="</td><td>";
  INDEX_HTML += hodnoty[pozice].pres;
  INDEX_HTML +="</td><td>";
  INDEX_HTML += hodnoty[pozice].humi;
  INDEX_HTML +="</td></tr>\n";
}


//odeslani dat na PI
void odesli (uint8_t pozice){
      uint8_t err;
      long rssi = WiFi.RSSI();
      WiFiClient client;
      err=client.connect(host, httpPort);
      if (err != 1) {
        Serial.print("connection over DNS name failed error:");
        Serial.println(err);
        err=client.connect(host_IP, httpPort);
        if (err != 1) {
            Serial.print("connection over IP resolved failed error:");
            Serial.println(err);
            err=client.connect(host_IP_fallback, httpPort);
            if (err != 1) {
                Serial.print("connection over IP fallback failed error:");
                Serial.println(err);
                return;
              }
        }

      }
      String url = "http://";
      url += host;
      url += "/";
      url += SKRIPTV;
      url += "?rssi=";
      url += rssi;
      url +="&mereni=";
      url += hodnoty[pozice].count;
      url +="&adc=";
      url += hodnoty[pozice].adc;
      url +="&ratio=";
      url += hodnoty[pozice].ratio;
      url +="&ppm=";
      url += hodnoty[pozice].ppm;
      url +="&temp=";
      url += hodnoty[pozice].temp;
      url +="&pres=";
      url += hodnoty[pozice].pres;
      url +="&humi=";
      url += hodnoty[pozice].humi;

      Serial.println (url);
      client.print(String("GET ") + url + " HTTP/1.1\r\n" +
                   "Host: " + host + "\r\n" +
                   "Connection: close\r\n\r\n");
}
