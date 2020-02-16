#define MQTT_KEEPALIVE 5
#define MQTT_SOCKET_TIMEOUT 10
#include "sps30.h"
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>


const char* ssid = "xxxx"; // Enter your WiFi name
const char* password =  "xxxx"; // Enter WiFi password
const char* mqtt_server = "xxxx";
static const char *fingerprint PROGMEM = "8C FE 78 00 5F BF DE 87 0F 3F D7 27 E1 EA C1 EC BA F0 DD FC";
const char* mqtt_username = "xxxx";
const char* mqtt_password = "xxxx";

/////////////////////////////////////////////////////////////
#define SP30_COMMS I2C_COMMS

/////////////////////////////////////////////////////////////
/* define RX and TX pin for softserial and Serial1 on ESP32
 * can be set to zero if not applicable / needed           */
/////////////////////////////////////////////////////////////
//#define TX_PIN 26
//#define RX_PIN 25

///////////////////////////////////////////////////////////////
/* define new AUTO Clean interval
 * Will be remembered after power off
 *
 * default is 604800 seconds
 * 0 = disable Auto Clean
 * -1 = do not change current setting */
//////////////////////////////////////////////////////////////
#define AUTOCLEANINTERVAL -1

///////////////////////////////////////////////////////////////
/* Perform a clean NOW ?
 *  1 = yes
 *  0 = NO */
//////////////////////////////////////////////////////////////
#define PERFORMCLEANNOW 0

/////////////////////////////////////////////////////////////
/* define driver debug
 * 0 : no messages
 * 1 : request sending and receiving
 * 2 : request sending and receiving + show protocol errors */
 //////////////////////////////////////////////////////////////
#define DEBUG 0

///////////////////////////////////////////////////////////////
/////////// NO CHANGES BEYOND THIS POINT NEEDED ///////////////
///////////////////////////////////////////////////////////////

// function prototypes (sometimes the pre-processor does not create prototypes themself on ESPxx)
void ErrtoMess(char *mess, uint8_t r);
void GetDeviceInfo();
bool read_all();
void SetAutoClean();

// create constructor
WiFiClientSecure sslclient;
SPS30 sps30;
PubSubClient mqttclient(mqtt_server, 8883, sslclient);

void setup() {

  // set serial
  Serial.begin(115200);

  // wifi setup
  wifi_connect();
  WiFi.softAPdisconnect (true); //dont want an AP to be visible
  WiFi.mode(WIFI_STA); // ditto
  sslclient.setFingerprint(fingerprint);
  
  //ssl
  //WiFiClientSecure sslclient;
    
  Serial.println("Connected to the WiFi network!");

  //mqttconnect
  mqttclient.connect("SPS30", mqtt_username, mqtt_password);
  if (mqttclient.connected()) {
    Serial.println("Connected to MQTT broker!");
  }
  else {
    Serial.println("Unable to connect to MQTTbroker"); 
  }
  
  //sps30
  Serial.println(F("Trying to connect to SPS30"));

  // set driver debug level
  sps30.EnableDebugging(DEBUG);

  // set pins to use for softserial and Serial1 on ESP32
  //if (TX_PIN != 0 && RX_PIN != 0) sps30.SetSerialPin(RX_PIN,TX_PIN);

  // Begin communication channel;
  if (sps30.begin(SP30_COMMS) == false) {
    Serial.println(F("Could not initialize communication channel to SPS30"));
  }

  // check for SPS30 connection
  if (sps30.probe() == false) {
    Serial.println(F("Could not probe /connect with SPS30"));
  }
  else
    Serial.println(F("Detected SPS30."));

  // reset SPS30 connection
  if (sps30.reset() == false) {
    Serial.println(F("Could not reset SPS30"));
  }

  // do Auto Clean interval
  SetAutoClean();

  // start measurement
  if (sps30.start() == true) {
    Serial.println(F("Measurement started"));
  }
  else
    Serial.println(F("Could NOT start measurement"));

  // clean now requested
  if (PERFORMCLEANNOW) {
    // clean now
    if (sps30.clean() == true)
      Serial.println(F("fan-cleaning manually started"));
    else
      Serial.println(F("Could NOT manually start fan-cleaning"));
  }

  if (SP30_COMMS == I2C_COMMS) {
    if (sps30.I2C_expect() == 4)
      Serial.println(F("Due to I2C buffersize only the SPS30 MASS concentration is available"));
  }
}

uint8_t error_cnt = 0;

// LOOP
void loop() {

  uint8_t ret;
  struct sps_values val;

  if (WiFi.status() != WL_CONNECTED) {
    wifi_connect();
  }

  // loop to get data
  do {
    ret = sps30.GetValues(&val);
    // data might not have been ready
    if (ret == ERR_DATALENGTH){
        if (error_cnt++ > 3) {
          ErrtoMess("Error during reading values: ",ret);
          //return(false);
        }
        delay(1000);
    }
    // if other error
    else if(ret != ERR_OK) {
      ErrtoMess("Error during reading values: ",ret);
      //return(false);
    }

  } while (ret != ERR_OK);

  // only print header first time
  //if (header) {
  Serial.println(F("-------------Mass -----------    ------------- Number --------------   -Average-"));
  Serial.println(F("     Concentration [μg/m3]             Concentration [#/cm3]             [μm]"));
  Serial.println(F("P1.0\tP2.5\tP4.0\tP10\tP0.5\tP1.0\tP2.5\tP4.0\tP10\tPartSize\n"));
  Serial.print(val.MassPM1);
  Serial.print(F("\t"));
  Serial.print(val.MassPM2);
  Serial.print(F("\t"));
  Serial.print(val.MassPM4);
  Serial.print(F("\t"));
  Serial.print(val.MassPM10);
  Serial.print(F("\t"));
  Serial.print(val.NumPM0);
  Serial.print(F("\t"));
  Serial.print(val.NumPM1);
  Serial.print(F("\t"));
  Serial.print(val.NumPM2);
  Serial.print(F("\t"));
  Serial.print(val.NumPM4);
  Serial.print(F("\t"));
  Serial.print(val.NumPM10);
  Serial.print(F("\t"));
  Serial.print(val.PartSize);
  Serial.print(F("\n"));
  error_cnt = 0;

  //convert all values to char for mqttclient (bit of a pain in the butt)
  char pm1mass[7];
  char pm25mass[7];
  char pm4mass[7];
  char pm10mass[7];
  dtostrf(val.MassPM1, 5, 2, pm1mass);
  dtostrf(val.MassPM2, 5, 2, pm25mass);
  dtostrf(val.MassPM4, 5, 2, pm4mass);
  dtostrf(val.MassPM10, 5, 2, pm10mass);

  //then the counters
  char pm0num[7];
  char pm1num[7];
  char pm2num[7];
  char pm4num[7];
  char pm10num[7];
  char partsize[7];
  dtostrf(val.NumPM0, 5, 2, pm0num);
  dtostrf(val.NumPM1, 5, 2, pm1num);
  dtostrf(val.NumPM2, 5, 2, pm2num);
  dtostrf(val.NumPM4, 5, 2, pm4num);
  dtostrf(val.NumPM10, 5, 2, pm10num);
  dtostrf(val.PartSize, 5, 2, partsize);

  if (mqttclient.connected()) {
    mqttclient.publish("test/airquality/xxx/inside-pmmass1", pm1mass);
    mqttclient.publish("test/airquality/xxx/inside-pmmass25", pm25mass);
    mqttclient.publish("test/airquality/xxx/inside-pmmass4", pm4mass);
    mqttclient.publish("test/airquality/xxx/inside-pmmass10", pm10mass);
    mqttclient.publish("test/airquality/xxx/inside-pmnum0", pm0num);
    mqttclient.publish("test/airquality/xxx/inside-pmnum1", pm1num);
    mqttclient.publish("test/airquality/xxx/inside-pmnum2", pm2num);
    mqttclient.publish("test/airquality/xxx/inside-pmnum4", pm4num);
    mqttclient.publish("test/airquality/xxx/inside-pmnum10", pm10num);
    mqttclient.publish("test/airquality/xxx/inside-partsize", partsize);
  }
  else {
    Serial.print("MQTT disconnected, reason:"); Serial.println(mqttclient.state()); Serial.println();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("Reconnecting to Wifi network!");
      wifi_connect();
    }
    Serial.println("Reconnecting to MQTT broker!");
    mqttclient.connect("SPS30", mqtt_username, mqtt_password);
  }
  mqttclient.loop();
  
  delay(10000);
}


/**
 * @brief: Get & Set new Auto Clean Interval
 *
 */
void SetAutoClean()
{
  uint32_t interval;
  uint8_t ret;

  // try to get interval
  ret = sps30.GetAutoCleanInt(&interval);
  if (ret == ERR_OK) {
    Serial.print(F("Current Auto Clean interval: "));
    Serial.print(interval);
    Serial.println(F(" seconds"));
  }
  else
    ErrtoMess("could not get clean interval.", ret);

  // only if requested
  if (AUTOCLEANINTERVAL == -1) {
    Serial.println(F("No Auto Clean interval change requested."));
    return;
  }

  // try to set interval
  interval = AUTOCLEANINTERVAL;
  ret = sps30.SetAutoCleanInt(interval);
  if (ret == ERR_OK) {
    Serial.print(F("Auto Clean interval now set : "));
    Serial.print(interval);
    Serial.println(F(" seconds"));
  }
  else
      ErrtoMess("could not set clean interval.", ret);

  // try to get interval
  ret = sps30.GetAutoCleanInt(&interval);
  if (ret == ERR_OK) {
    Serial.print(F("Current Auto Clean interval: "));
    Serial.print(interval);
    Serial.println(F(" seconds"));
  }
  else
    ErrtoMess("could not get clean interval.", ret);
}

/**
 *  @brief : display error message
 *  @param mess : message to display
 *  @param r : error code
 *
 */
void ErrtoMess(char *mess, uint8_t r)
{
  char buf[80];

  Serial.print(mess);

  sps30.GetErrDescription(r, buf, 80);
  Serial.println(buf);
}

void wifi_connect()
{
  if (WiFi.status() != WL_CONNECTED) {
    // WIFI
      Serial.println();
      Serial.print("===> WIFI ---> Connecting to ");
      Serial.println(ssid);

      delay(10);
      //WiFi.mode(WIFI_STA);
      WiFi.begin(ssid, password);
      //WiFi.config(IPAddress(ip_static), IPAddress(ip_gateway), IPAddress(ip_subnet), IPAddress(ip_dns));

      int Attempt = 0;
      while (WiFi.status() != WL_CONNECTED) {
        Serial.print(". ");
        Serial.print(Attempt);

        delay(100);
        Attempt++;
      if (Attempt == 150)
      {
        Serial.println();
        Serial.println("-----> Could not connect to WIFI");

        ESP.restart();
        delay(200);
      }

    }
      Serial.println();
      Serial.print("===> WiFi connected");
      Serial.print(" ------> IP address: ");
      Serial.println(WiFi.localIP());
    }
}
