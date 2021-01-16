#include <Arduino.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include "EspMQTTClient.h"
#include <ArduinoJson.h>

#include "Secrets.h"

const unsigned int IR_SEND_PIN = 4; // Pin to send IR commands on
const unsigned int DEBUG_LED_PIN = 2; // Pin to an LED that indicates the current on/off state

const unsigned long MIN_IR_COMMAND_INTERVAL = 500; // Minnimum time between IR commands for them to be registered correctly.

const unsigned long IR_COMMAND_ON = 0x7E7EA05FUL; // Returns to previous state, even if light was unplugged
const unsigned long IR_COMMAND_OFF = 0x7E7E20DFUL; // Fades off the light
const unsigned long IR_COMMAND_MIN_BRIGHTNESS = 0x7E7EB847UL; // If light is on, sets to max brightness (9)
const unsigned long IR_COMMAND_MAX_BRIGHTNESS = 0x7E7E38C7UL; // If light is on, sets to min brightness (0)
const unsigned long IR_COMMAND_DECREASE_BRIGHTNESS = 0x7E7ED827UL; // If light is on, reduces the brightness (-1)
const unsigned long IR_COMMAND_INCREASE_BRIGHTNESS = 0x7E7E58A7UL; // If light is on, increases the brightness (+1)
const unsigned long IR_COMMAND_MOTION_DISABLE_A = 0x7E7EB04FUL; // Disable motion detection. Related commands must also be called.
const unsigned long IR_COMMAND_MOTION_DISABLE_B = 0x7E7E8877UL; // Disable motion detection. Related commands must also be called.
const unsigned long IR_COMMAND_MOTION_DISABLE_C = 0x7E7EA857UL; // Disable motion detection. Related commands must also be called.

// Contains parsed json data in a fixed-size buffer
StaticJsonDocument<256> parsedJson;

IRsend irsend(IR_SEND_PIN);

EspMQTTClient client(
  WLAN_SSID,
  WLAN_PASS,
  MQTT_SERVER,
  MQTT_USERNAME,
  MQTT_KEY,
  "office-shop-lights",
  1883
);

// on/off state of the light
bool state = false;

// 0-9 brightness level of the light
int brightness = 0;

// Increments on every new MQTT command, so we can cancel out early from in-progress command processing
int mqttCommandSequence = 0;

// Buffers the next incomming command from MQTT. It will be processed in the next loop().
// We buffer instead of processing immediately, because processing from the MQTT callback would cause too much recursion.
bool mqttNextCommandReady;
String mqttNextCommandPayload;

void setup() {
  irsend.begin();
  Serial.begin(115200, SERIAL_8N1, SERIAL_TX_ONLY);
  delay(2000);

  pinMode(DEBUG_LED_PIN, OUTPUT);

  // Enable MQTT debugging messages sent to serial output
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

  if (mqttNextCommandReady)
  {
    mqttNextCommandReady = false;
    processMqttCommand(mqttNextCommandPayload);
  }
}

// This function is called once everything is connected (Wifi and MQTT)
void onConnectionEstablished()
{
  setupHomeAssistantAutoDiscovery();

  client.subscribe("homeassistant/light/office-shop-light/set", [](const String & payload) {
    
    // Increment command sequence so in-progress commands know to cancel themselves
    mqttCommandSequence++;

    // Buffer command so it can be processed from a fresh stack, instead of deep recursion
    mqttNextCommandReady = true;
    mqttNextCommandPayload = payload;
  });
  
  Serial.println("All good to go!");
}

// Sends MQTT commands to configure ourselves as a new light on Home Assistant
void setupHomeAssistantAutoDiscovery()
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

void processMqttCommand(String json)
{
  Serial.println("Processing command " + String(mqttCommandSequence) + " json: " + json);
  
  DeserializationError error = deserializeJson(parsedJson, json);
  if (error) {
    Serial.print("deserializeJson() failed: ");
    Serial.println(error.f_str());
    return;
  }

  String newState = parsedJson["state"];
  if (newState == "ON") {

    // Turn on the LED state indicator
    digitalWrite(DEBUG_LED_PIN, LOW);

    if (state == false)
    {
      // Enable light if its currently off
      irsend.sendNEC(IR_COMMAND_ON);
      state = true;
      if (interuptableIrDelay())
      {
        return;
      }
    }

    // State wont have brightness if its just a simple on/off command, so check for it
    if (parsedJson.containsKey("brightness"))
    {
      int requestedBrightness = parsedJson["brightness"];
      int scaledBrightness = (int)round(requestedBrightness / 28.333);

      if (scaledBrightness != brightness)
      {
        // Change brightness if its different enough from the current value
        if (setBrightness(scaledBrightness))
        {
          // Command interupted, so cancel early
          return;
        }
      }
    }
  }
  
  else if (newState == "OFF")
  {
    
    // Turn off the LED state indicator
    digitalWrite(DEBUG_LED_PIN, HIGH);
    
    irsend.sendNEC(IR_COMMAND_OFF);
    state = false;
    if (interuptableIrDelay())
    {
      return;
    }
  }

  Serial.println("Finished setting brightness to " + String(json));
  publishState();
}

bool setBrightness(int newBrightness)
{
  if (newBrightness == 0)
  {
    // There are IR commands to immediately set brightness to min, so just use that
    irsend.sendNEC(IR_COMMAND_MIN_BRIGHTNESS);
    brightness = 0;
    if (interuptableIrDelay())
    {
      return true;
    }
    
  } else if (newBrightness == 9)
  {
    // There are IR commands to immediately set brightness to max, so just use that
    irsend.sendNEC(IR_COMMAND_MAX_BRIGHTNESS);
    brightness = 9;
    if (interuptableIrDelay())
    {
      return true;
    }
    
  } else {
    // Since this isn't a simple min/max set, we need to send several delta updates to reach the target
    int delta;
    long command;

    // Select the command and brightness offset required to increase/decrease the brightness
    if (newBrightness > brightness)
    {
      delta = 1;
      command = IR_COMMAND_INCREASE_BRIGHTNESS;
    } else {
      delta = -1;
      command = IR_COMMAND_DECREASE_BRIGHTNESS;
    }

    // Iterate from the current brightness to the desired brightness
    for (int i = brightness; i != newBrightness; i += delta)
    {
      irsend.sendNEC(command);
      brightness += delta;
      if (interuptableIrDelay())
      {
        return true;
      }
    }
  }

  return false;
}

/*
 * This should be called after every IR command, if there's a possibility of sending more IR commands afterwards.
 * 
 * It takes care of the following:
 * 
 * 1. Delays for MIN_IR_COMMAND_INTERVAL (the minimum time between IR commands for the light to process them correctly)
 * 2. Yields for WiFi and MQTT messages, to see if any new MQTT commands have been sent to us while waiting
 * 3. If a new command has arrived, returns true. The calling method should cancel back up to loop() asap, so the new command can be processed.
 */
bool interuptableIrDelay()
{
  
  int currentSeq = mqttCommandSequence;
  
  delay(MIN_IR_COMMAND_INTERVAL);

  // Update WiFi networking to allow new commands to arrive
  yield;

  // Update MQTT processing to check for new commands
  client.loop();

  if (currentSeq != mqttCommandSequence)
  {
    Serial.println("Command " + String(currentSeq) + " interupted, cancelling");
    return true;
  }

  return false;
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
