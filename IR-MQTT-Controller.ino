#include <Arduino.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>

#include "EspMQTTClient.h"

#include <ArduinoJson.h>

#include "Secrets.h"

const uint16_t kIrLed = 4;

const long IR_COMMAND_ON = 0x7E7EA05FUL; // Returns to previous state, even if light was unplugged
const long IR_COMMAND_OFF = 0x7E7E20DFUL; // Fades off the light
const long IR_COMMAND_MIN_BRIGHTNESS = 0x7E7EB847UL; // If light is on, sets to max brightness (9)
const long IR_COMMAND_MAX_BRIGHTNESS = 0x7E7E38C7UL; // If light is on, sets to min brightness (0)
const long IR_COMMAND_DECREASE_BRIGHTNESS = 0x7E7ED827UL; // If light is on, reduces the brightness (-1)
const long IR_COMMAND_INCREASE_BRIGHTNESS = 0x7E7E58A7UL; // If light is on, increases the brightness (+1)
const long IR_COMMAND_MOTION_DISABLE_A = 0x7E7EB04FUL; // Disable motion detection. Related commands must also be called.
const long IR_COMMAND_MOTION_DISABLE_B = 0x7E7E8877UL; // Disable motion detection. Related commands must also be called.
const long IR_COMMAND_MOTION_DISABLE_C = 0x7E7EA857UL; // Disable motion detection. Related commands must also be called.
const long MIN_IR_COMMAND_INTERVAL = 500; // Minnimum time between IR commands for them to be registered correctly.

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
int brightness = 0;

void setup() {
  irsend.begin();
  Serial.begin(115200, SERIAL_8N1, SERIAL_TX_ONLY);
  delay(2000);

  pinMode(LED, OUTPUT);

  // Enable debugging messages sent to serial output
  client.enableDebuggingMessages();

  // Disable motion detection
  irsend.sendNEC(IR_COMMAND_MOTION_DISABLE_A);
  delay(MIN_IR_COMMAND_INTERVAL);
  irsend.sendNEC(IR_COMMAND_MOTION_DISABLE_B);
  delay(MIN_IR_COMMAND_INTERVAL);
  irsend.sendNEC(IR_COMMAND_MOTION_DISABLE_C);
  delay(MIN_IR_COMMAND_INTERVAL);

  // Set light to known state - Off, Minimum brightness
  irsend.sendNEC(IR_COMMAND_MIN_BRIGHTNESS);
  delay(MIN_IR_COMMAND_INTERVAL);
  irsend.sendNEC(IR_COMMAND_OFF);
  delay(MIN_IR_COMMAND_INTERVAL);

  brightness = 0;
  state = false;
}

void loop()
{
  client.loop();
}

// This function is called once everything is connected (Wifi and MQTT)
void onConnectionEstablished()
{
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

  publishState();
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

    if (state == false)
    {
      // Enable light if its currently off
      irsend.sendNEC(IR_COMMAND_ON);
      state = true;
      delay(MIN_IR_COMMAND_INTERVAL);
    }

    // State wont have brightness if its just a simple on/off command, so check for it
    if (doc.containsKey("brightness"))
    {
      int requestedBrightness = doc["brightness"];
      int scaledBrightness = (int)round(requestedBrightness / 28.333);

      Serial.println("Current brightness: " + String(brightness) + ", target " + String(requestedBrightness) + " = " + String(scaledBrightness));

      if (scaledBrightness != brightness)
      {
        // Change brightness if its different enough from the current value
        Serial.println("set to " + String(scaledBrightness));
        setBrightness(scaledBrightness);
      }
    }
  }
  
  else if (newState == "OFF")
  {
    digitalWrite(LED, HIGH);
    irsend.sendNEC(IR_COMMAND_OFF);
    state = false;
    delay(MIN_IR_COMMAND_INTERVAL);
  }

  publishState();
}

void setBrightness(int newBrightness)
{
  if (newBrightness == 0)
  {
    // There are IR commands to immediately set brightness to min, so just use that
    irsend.sendNEC(IR_COMMAND_MIN_BRIGHTNESS);
  } else if (newBrightness == 9)
  {
    // There are IR commands to immediately set brightness to max, so just use that
    irsend.sendNEC(IR_COMMAND_MAX_BRIGHTNESS);
  } else {
    // Since this isn't a simple min/max set, we need to send several delta updates to reach the target
    int delta;
    long command;

    if (newBrightness > brightness)
    {
      delta = 1;
      command = IR_COMMAND_INCREASE_BRIGHTNESS;
    } else {
      delta = -1;
      command = IR_COMMAND_DECREASE_BRIGHTNESS;
    }

    Serial.println("Delta changing brightness! Delta " + String(delta) + ", command " + String(command));
    for (int i = brightness; i != newBrightness; i += delta)
    {
      Serial.println("i " + String(i));
      irsend.sendNEC(command);
      delay(MIN_IR_COMMAND_INTERVAL);
    }
  }

  brightness = newBrightness;
  delay(MIN_IR_COMMAND_INTERVAL);
}

void publishState()
{
  String scaledBrightness = String(round(brightness * 28.333));
  
  if (state == true)
  {
    client.publish("homeassistant/light/office-shop-light/state", "{\"state\": \"ON\", \"brightness\": " + scaledBrightness + "}");
  } else {
    client.publish("homeassistant/light/office-shop-light/state", "{\"state\": \"OFF\", \"brightness\": " + scaledBrightness + "}");
  }
}
