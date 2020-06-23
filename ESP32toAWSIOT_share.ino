#include "WiFi.h"
#include "greenwall_sensor_01_certs.h"
#include <WiFiClientSecure.h>
#include <MQTTClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include "DHT.h"

// The name of the device. This MUST match up with the name defined in the AWS console
#define DEVICE_NAME "greenwall_sensor_01"

// The MQTTT endpoint for the device (unique for each AWS account but shared amongst devices within the account)
#define AWS_IOT_ENDPOINT "*.iot.ap-southeast-1.amazonaws.com"

// The MQTT topic that this device should publish to
#define AWS_IOT_SHADOW "$aws/things/" DEVICE_NAME "/shadow/update"
#define AWS_IOT_TOPIC_SOIL "greenwall/readings"
#define AWS_IOT_TOPIC_ENV "environment/readings"

// How many times we should attempt to connect to AWS
#define AWS_MAX_RECONNECT_TRIES 50

WiFiClientSecure net = WiFiClientSecure();
MQTTClient client = MQTTClient();

// Wifi credentials
const char *WIFI_SSID = "";
const char *WIFI_PASSWORD = "";

#define DHTPIN 0 
#define DHTTYPE DHT22  

DHT dht(DHTPIN, DHTTYPE);

const int numReadings = 10;
int readings[numReadings];      // the readings from the analog input         
int average = 0;                // the average

int readingInterval = 600000;

int NUM_POTS = 4;

char* POTS[] = {
    "x01y01",
    "x02y01",
    "x03y01",
    "x04y01"
};

const int PINS[] = {
  34,
  35,
  32,
  33
};

int moisture = 0;

//Timestamp from network
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 28800;
const int   daylightOffset_sec = 0;

char dayStamp[100];
char dateStamp[100];
char timeStamp[100];


void connectToWiFi()
{
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  // Only try 15 times to connect to the WiFi
  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 15){
    delay(500);
    Serial.print(".");
    retries++;
  }

  // If we still couldn't connect to the WiFi, go to deep sleep for a minute and try again.
  if(WiFi.status() != WL_CONNECTED){
    esp_sleep_enable_timer_wakeup(1 * 60L * 1000000L);
    esp_deep_sleep_start();
  }
}

void connectToAWS()
{
    // Configure WiFiClientSecure to use the AWS certificates we generated
    net.setCACert(AWS_CERT_CA);
    net.setCertificate(AWS_CERT_CRT);
    net.setPrivateKey(AWS_CERT_PRIVATE);

    // Connect to the MQTT broker on the AWS endpoint we defined earlier
    client.begin(AWS_IOT_ENDPOINT, 8883, net);

    // Try to connect to AWS and count how many times we retried.
    int retries = 0;
    Serial.print("Connecting to AWS IOT");

    while (!client.connect(DEVICE_NAME) && retries < AWS_MAX_RECONNECT_TRIES) {
        Serial.print(".");
        delay(100);
        retries++;
    }

    // Make sure that we did indeed successfully connect to the MQTT broker
    // If not we just end the function and wait for the next loop.
    if(!client.connected()){
        Serial.println(" Timeout!");
        return;
    }

    // If we land here, we have successfully connected to AWS!
    // And we can subscribe to topics and send messages.
    Serial.println("Connected!");
}
//String date, String current_time
void sendSoilToAWS(char* pot, int moisture, String timeStamp)
{
  StaticJsonDocument<128> stateDoc;
  StaticJsonDocument<128> reportDocSoil;
  
  JsonObject stateObj = stateDoc.createNestedObject("state");
  JsonObject reportObj = stateObj.createNestedObject("reported");
  
  reportObj["state"] = "on";
  reportObj["deviceID"] = DEVICE_NAME;
  //reportObj["valve"] = "off";
  
  JsonObject reportedObj = reportDocSoil.createNestedObject("reported");
  
  // Write the temperature & humidity. Here you can use any C++ type (and you can refer to variables)
  reportedObj["location"] = pot;
  //reportedObj["date"] = dateStamp;
  //reportedObj["day"] = dayStamp;
  reportedObj["timestamp"] = timeStamp;
  reportedObj["moisture"] = moisture;
  //reportedObj["temp"] = temp;
  //reportedObj["wifi"] = WiFi.RSSI();

  Serial.println("Publishing message to AWS...");
  
  char stateBuffer[512];
  char reportBufferSoil[1024];
  
  serializeJson(stateDoc, stateBuffer);
  serializeJson(reportDocSoil, reportBufferSoil);
  serializeJson(reportDocSoil, Serial);

  client.publish(AWS_IOT_SHADOW, stateBuffer);
  client.publish(AWS_IOT_TOPIC_SOIL, reportBufferSoil);

}

void sendEnvToAWS(float temp, float humidity, String timeStamp)
{
  StaticJsonDocument<128> stateDoc;
  StaticJsonDocument<128> reportDocEnv;
  
  JsonObject stateObj = stateDoc.createNestedObject("state");
  JsonObject reportObj = stateObj.createNestedObject("reported");
  
  reportObj["state"] = "on";
  reportObj["deviceID"] = DEVICE_NAME;
  //reportObj["valve"] = "off";
 
  JsonObject reportedObj = reportDocEnv.createNestedObject("reported");

  reportedObj["location"] = "greenwall";
  reportedObj["timestamp"] = timeStamp;
  reportedObj["temp"] = temp;
  reportedObj["humidity"] = humidity;
  
  Serial.println("Publishing message to AWS...");
  
  char stateBuffer[512];
  char reportBufferEnv[1024];
  
  serializeJson(stateDoc, stateBuffer);
  serializeJson(reportDocEnv, reportBufferEnv);
  serializeJson(reportDocEnv, Serial);

  client.publish(AWS_IOT_SHADOW, stateBuffer);
  client.publish(AWS_IOT_TOPIC_ENV, reportBufferEnv);
}

int pinread(int pin)  {

  for (int k = 0; k < numReadings; k++) {
    readings[k] = analogRead(pin);
    delay(1000);
  }

  int total = 0;
  
  for ( int j = 0; j < numReadings; j++ ) {
      total += readings[j];
  }

  average = total / numReadings;
  return(average);
  
}


void setup() {
  // put your setup code here, to run once:
  Serial.begin(9600);
  connectToWiFi();
  connectToAWS();

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  for (int thisReading = 0; thisReading < numReadings; thisReading++) {
    readings[thisReading] = 0;
  }

  dht.begin();
}

void loop() {
  // put your main code here, to run repeatedly:

  connectToAWS();
  
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
    return;
  }
  Serial.println(&timeinfo, "%A %B %d %Y %H:%M:%S");

  //sprintf(dateStamp, "%d,%d,%d,%d" , timeinfo.tm_year+1900, timeinfo.tm_mon+1, timeinfo.tm_mday, timeinfo.tm_wday);
  //Serial.print(dateStamp);

  //sprintf(dayStamp, "%d" , timeinfo.tm_wday);
  
  sprintf(timeStamp, "%d-%02d-%02dT%d:%02d:%02d" , timeinfo.tm_year+1900, timeinfo.tm_mon+1, timeinfo.tm_mday , timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  Serial.println(timeStamp);

  if(0 < timeinfo.tm_hour < 1 and 0 < timeinfo.tm_min < 30){
     configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  }

  float h = dht.readHumidity();
  float t = dht.readTemperature();
  sendEnvToAWS(t, h, timeStamp);
  

  for (int i = 0; i < NUM_POTS; i++) {
    moisture = pinread(PINS[i]);
    Serial.print(POTS[i]);
    Serial.print(":");
    Serial.println(moisture);
    sendSoilToAWS(POTS[i], moisture, timeStamp);
  }

  client.loop();
  delay(readingInterval);
}
