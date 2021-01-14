#include <Arduino.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>

#include "EspMQTTClient.h"

#include <ArduinoJson.h>

#include "Secrets.h"

const uint16_t kIrLed = 4;

IRsend irsend(kIrLed);

StaticJsonDocument<256> doc;

#define LED 2

EspMQTTClient client(
  WLAN_SSID,
  WLAN_PASS,
  MQTT_SERVER,
  MQTT_USERNAME,
  MQTT_KEY,
  "office-shop-lights",
  1883
);

bool state = false;

void setup() {
  irsend.begin();
  Serial.begin(115200, SERIAL_8N1, SERIAL_TX_ONLY);
  delay(2000);

  pinMode(LED, OUTPUT);

  // Enable debugging messages sent to serial output
  client.enableDebuggingMessages();

  Serial.println("Initialized!");
}

void loop()
{
  client.loop();
}

// This function is called once everything is connected (Wifi and MQTT)
void onConnectionEstablished()
{
  Serial.println("Connection established");
  
  MQTTAutoDiscovery();
    
  // Subscribe to "mytopic/test" and display received message to Serial
  client.subscribe("homeassistant/light/office-shop-light/set", [](const String & payload) {
    setState(payload);
  });
  
  Serial.println("All good to go!");
}

void MQTTAutoDiscovery()
{
  client.publish("homeassistant/light/office-shop-light/config",
    "{"
      "\"~\": \"homeassistant/light/office-shop-light\","
      "\"name\": \"Office Shop Light\","
      "\"unique_id\": \"office_shop_light\","
      "\"command_topic\": \"~/set\","
      "\"state_topic\": \"~/state\","
      "\"schema\": \"json\","
      "\"brightness\": true"
    "}"
  );

  Serial.println("Config published");
  publishState();
  
  Serial.println("Done!");
}

void setState(String json)
{

  Serial.println(json);
  
  DeserializationError error = deserializeJson(doc, json);
  if (error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.f_str());
    return;
  }

  String newState = doc["state"];
  if (newState == "ON") {
    digitalWrite(LED, LOW);
    irsend.sendNEC(0x7E7EA05FUL);
    state = true;
  }
  if (newState == "OFF") {
    digitalWrite(LED, HIGH);
    irsend.sendNEC(0x7E7E20DFUL);
    state = false;
  }

  publishState();
}

void publishState()
{
  if (state == true)
  {
    client.publish("homeassistant/light/office-shop-light/state", "{\"state\": \"ON\"}");
  } else {
    client.publish("homeassistant/light/office-shop-light/state", "{\"state\": \"OFF\"}");
  }
}
