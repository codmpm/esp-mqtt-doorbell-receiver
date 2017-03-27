/*
  mqtt-doorbell-receiver (c) 2017 patrik.mayer@codm.de

  This is a simple sketch to publish to mqtt when the doorbell rings
  Based on https://github.com/sui77/rc-switch/

  Decimal 3954437, Protocol 1

*/

#include <RCSwitch.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>

//---- config

// WiFi
const char* ssid = "<your-wifi-ssid>";
const char* password = "<your-wifi-key>";

const char* mqttServer = "<mqtt-broker-ip-or-host>";
const char* mqttUser = "<mqtt-user>";
const char* mqttPass = "<mqtt-password>";
const char* mqttClientName = "<mqtt-client-id>"; //will also be used for hostname and OTA name
const char* mqttTopicPrefix = "<mqtt-topic-prefix>";

//doorbell - use rc-switch receive example to get the code
long wantedCodeDec = 3954437;
uint8_t wantedProt = 1;
long ringDelay = 5000;

//----


WiFiClient espClient;
PubSubClient client(espClient);
RCSwitch mySwitch = RCSwitch();

char mqttTopicStatus[64];
char mqttTopicIp[64];
char mqttTopic[64];

long lastReconnectAttempt = 0; //For the non blocking mqtt reconnect (in millis)
long lastRing = 0;

void setup() {
  Serial.begin(115200);

  setup_wifi();
  client.setServer(mqttServer, 1883);

  //put in mqtt prefix
  sprintf(mqttTopicStatus, "%sstatus", mqttTopicPrefix);
  sprintf(mqttTopicIp, "%sip", mqttTopicPrefix);
  sprintf(mqttTopic, "%sring", mqttTopicPrefix);

  //rc receiver
  mySwitch.enableReceive(2);  // Using GPIO2 on ESP-01

  //----------- OTA
  ArduinoOTA.setHostname(mqttClientName);

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_SPIFFS
      type = "filesystem";
    }
    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    Serial.println("Start updating " + type);
  });

  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
    delay(1000);
    ESP.restart();
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

  Serial.println("ready...");

}

void loop() {

  //handle mqtt connection, non-blocking
  if (!client.connected()) {
    long now = millis();
    if (now - lastReconnectAttempt > 5000) {
      lastReconnectAttempt = now;
      // Attempt to reconnect
      if (MqttReconnect()) {
        lastReconnectAttempt = 0;
      }
    }
  }
  client.loop();
  

  if (mySwitch.available()) {
    if (lastRing == 0) {
      int value = mySwitch.getReceivedValue();
      if (value > 0) {

        if (
          mySwitch.getReceivedValue() == wantedCodeDec &&
          mySwitch.getReceivedProtocol() == wantedProt
        ) {
          Serial.print("Riiing");
          Serial.print(" : ");
          Serial.print("Received ");
          Serial.print( mySwitch.getReceivedValue() );
          Serial.print(" / ");
          Serial.print( mySwitch.getReceivedBitlength() );
          Serial.print("bit ");
          Serial.print("Protocol: ");
          Serial.println( mySwitch.getReceivedProtocol() );

          lastRing = millis();
          client.publish(mqttTopic, "1");

        }
      }
    }
    mySwitch.resetAvailable();
  }

  //reset lastRing time if the ringDelay is reached
  //also reset ringCtr as there could be too few packets from last receive
  if ((lastRing > 0) && ((millis() - lastRing) > ringDelay)) {
    lastRing = 0;
  }

  //handle OTA
  ArduinoOTA.handle();

}


void setup_wifi() {

  delay(10);

  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA); //disable AP mode, only station
  WiFi.hostname(mqttClientName);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}


bool MqttReconnect() {

  if (!client.connected()) {

    Serial.print("Attempting MQTT connection...");

    // Attempt to connect with last will retained
    if (client.connect(mqttClientName, mqttUser, mqttPass, mqttTopicStatus, 1, true, "offline")) {

      Serial.println("connected");
      // Once connected, publish an announcement...
      char curIp[16];
      sprintf(curIp, "%d.%d.%d.%d", WiFi.localIP()[0], WiFi.localIP()[1], WiFi.localIP()[2], WiFi.localIP()[3]);

      client.publish(mqttTopicStatus, "online", true);
      client.publish(mqttTopicIp, curIp, true);

    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
    }
  }
  return client.connected();
}


