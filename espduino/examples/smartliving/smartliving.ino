/**
   \file
         ESP8266 SmartLiving example

   \author
         Alex D <ad@allthingstalk.com>

   \brief
         A bare-metal integration example with SmartLiving IoT platform.
         Demonstrates pushing/receiving data over MQTT and creating Assets using REST.

   \detail
         ### Prerequisites:
          - An account on https://maker.smartliving.io
          - A device created on SmartLiving platform to receive the data.

         This example is designed to run on Arduino Due which has second
         hardware serial port available. Fell free to change it to SoftwareSerial
         in case your platform doesn't support second HardwareSerial.

         To create device on SmartLiving, after logging in, go to "Playground" and
         add an Arduino device afterwards. Click on the new device to receive configuration settings.

         The example demonstrates:
          - sending a numeric value to the platform (simple counter incrementing each second)
          - receiving value from the platform (toggling a LED)
*/

#include <espduino.h>
#include <mqtt.h>
#include <rest.h>
#include <Wire.h>
#include <SPI.h>
#include <ArduinoJson.h>

// Set these values according to your SmartLiving device
#define DEVICE_ID "YOUR-DEVICE-ID-HERE"
#define CLIENT_ID "YOUR-CLIENT-ID-HERE"
#define CLIENT_KEY "YOUR-CLIENT-KEY-HERE"

// Your WiFi credentials
#define SSID "YOUR-WIFI-SSID"
#define PASS "YOUR-WIFI-PASSWORD"

// SmartLiving endpoints
#define MQTT_BROKER "broker.smartliving.io"
#define API_HOST "api.smartliving.io"

// LED attached to pin 39
#define LED 39

// Uses Due's Serial1 as data port. Change according to your platform
HardwareSerial *data  = &Serial1;
HardwareSerial *debug = &Serial;

// ESP stuff
ESP esp(data, debug, 4);
MQTT mqtt(&esp);
REST rest(&esp);
uint16_t _counter = 0;

boolean wifiConnected = false;

/*
  Adds single asset, by it's name.
*/
void addAsset(String name, String type, String dataType)
{
  /*
    Set asset payload is in format:
    {
      "is": "sensor" // or "actuator"
      "title": "title of the asset",
      "profile": {} // a JSON schema describing a type
    }
  */
  StaticJsonBuffer<128> jsonBuffer;
  JsonObject &profile = jsonBuffer.createObject();
  JsonObject &root = jsonBuffer.createObject();

  root["title"] = name;
  root["is"] = type;
  profile["type"] = dataType;
  root["profile"] = profile;

  char url[64];
  char header[128];
  char body[128];

  root.printTo(body, sizeof(body));
  int contentLength = strlen(body);

  // We're authorizing using Auth-ClientId and Auth-ClientKey headers on /device/:id/asset/:name URI
  sprintf(header, "Auth-ClientId: %s\r\nAuth-ClientKey: %s\r\nContent-Length: %d\r\n", CLIENT_ID, CLIENT_KEY, contentLength);
  sprintf(url, "/device/%s/asset/%s", DEVICE_ID, name.c_str());

  char resp[1024];
  rest.setHeader(header);
  rest.put(url, body);

  // dump response for debugging purposes, you can turn this off
  rest.getResponse(resp, sizeof(resp));
  debug->println(resp);
}

/*
  Creates Counter and LED assets on the platform
*/
void setupAssets() {
  rest.begin(API_HOST);
  rest.setContentType("application/json");

  addAsset("Counter", "sensor", "integer");
  delay(100);

  addAsset("LED", "actuator", "boolean");
}

void wifiCb(void* response)
{
  uint32_t status;
  RESPONSE res(response);

  if (res.getArgc() == 1) {
    res.popArgs((uint8_t*)&status, 4);
    if (status == STATION_GOT_IP) {
      debug->println("WIFI CONNECTED");

      // setup assets on platform
      setupAssets();
      debug->println("SL: Done setting assets");

      // connect to MQTT broker
      mqtt.connect(MQTT_BROKER, 1883);
      wifiConnected = true;
    } else {
      wifiConnected = false;
      mqtt.disconnect();
    }

  }
}

void mqttConnected(void* response)
{
  char topic[128];
  sprintf(topic, "client.%s.in.device.%s.asset.*.command ", CLIENT_ID, DEVICE_ID);
  debug->println("Connected");
  mqtt.subscribe(topic);
}

void mqttDisconnected(void* response)
{
  debug->println("Disconnected");
}

void mqttData(void* response)
{
  RESPONSE res(response);

  debug->print("Received: topic=");
  String topic = res.popString();
  debug->println(topic);

  debug->print("data=");
  String data = res.popString();
  debug->println(data);

  // we should check agains topic what asset is sending the data
  // but we have just one so we're skipping that
  bool led = data == "true";
  digitalWrite(LED, led);
}

void mqttPublished(void* response)
{
  debug->println("mqtt published");
}

void setup() {
  pinMode(LED, OUTPUT);

  data->begin(19200);
  debug->begin(19200);

  esp.enable();
  delay(500);
  esp.reset();
  delay(500);
  while (!esp.ready());

  char user[32];
  sprintf(user, "%s:%s", CLIENT_ID, CLIENT_ID);
  debug->println("ARDUINO: setup mqtt client");
  if (!mqtt.begin("espduino example", user, CLIENT_KEY, 120, 1)) {
    debug->println("ARDUINO: fail to setup mqtt");
    while (1);
  }

  /*setup mqtt events */
  mqtt.connectedCb.attach(&mqttConnected);
  mqtt.disconnectedCb.attach(&mqttDisconnected);
  mqtt.publishedCb.attach(&mqttPublished);
  mqtt.dataCb.attach(&mqttData);

  /*setup wifi*/
  debug->println("ARDUINO: setup wifi");
  esp.wifiCb.attach(&wifiCb);

  esp.wifiConnect(SSID, PASS);
  debug->println("ARDUINO: system started");
}

void count() {
  char data[32];
  char topic[128];

  // payload format is timestamp|data. We can use 0 here, in that case
  // state update will be marked with server's time
  sprintf(data, "0|%d", _counter++);

  // publish directly to Counter topic structure
  sprintf(topic, "client.%s.out.device.%s.asset.%s.state", CLIENT_ID, DEVICE_ID, "Counter");

  mqtt.publish(topic, data);
}

void loop() {
  static uint32_t refresh = 0;
  esp.process();
  if (wifiConnected) {
    delay(50);
    if (refresh++ > 100) {
      count();
      refresh = 0;
    }
  }
}
