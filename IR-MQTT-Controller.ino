#include <Arduino.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <ESP8266WiFi.h>
#include "Adafruit_MQTT.h"
#include "Adafruit_MQTT_Client.h"

#include "Secrets.h"

const uint16_t kIrLed = 4;

IRsend irsend(kIrLed);


#define LED 2

WiFiClient client;

Adafruit_MQTT_Client mqtt(&client, MQTT_SERVER, MQTT_SERVERPORT, MQTT_USERNAME, MQTT_USERNAME, MQTT_KEY);

Adafruit_MQTT_Subscribe onoffbutton = Adafruit_MQTT_Subscribe(&mqtt, "/feeds/onoff");

Adafruit_MQTT_Publish stateTopic = Adafruit_MQTT_Publish(&mqtt, "homeassistant/light/office-shop-light/state");

bool configured = false;
bool state = false;

void setup() {
  irsend.begin();
  Serial.begin(115200, SERIAL_8N1, SERIAL_TX_ONLY);
  delay(2000);

  pinMode(LED, OUTPUT);

  Serial.println("MQTT Go!");

  // Connect to WiFi
  {
    Serial.println(); Serial.println();
    Serial.print("Connecting to ");
    Serial.println(WLAN_SSID);

    WiFi.begin(WLAN_SSID, WLAN_PASS);
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }
    Serial.println();

    Serial.println("WiFi connected");
    Serial.println("IP address: "); Serial.println(WiFi.localIP());
  }

  // Setup MQTT subscription
  mqtt.subscribe(&onoffbutton);
}


uint32_t x = 0;

void loop() {

  if (mqtt.connected() && !configured)
  {
    MQTTAutoDiscovery();
  }
  
  // Ensure the connection to the MQTT server is alive (this will make the first
  // connection and automatically reconnect when disconnected).  See the MQTT_connect
  // function definition further below.
  MQTTConnect();

  // this is our 'wait for incoming subscription packets' busy subloop
  // try to spend your time here
  Adafruit_MQTT_Subscribe *subscription;
  while ((subscription = mqtt.readSubscription(5000))) {
    // Check if its the onoff button feed
    if (subscription == &onoffbutton) {
      setState((char *)onoffbutton.lastread);
    }
  }

  // ping the server to keep the mqtt connection alive
  if (! mqtt.ping()) {
    configured = false;
    mqtt.disconnect();
  }

}

// Function to connect and reconnect as necessary to the MQTT server.
// Should be called in the loop function and it will take care if connecting.
void MQTTConnect() {
  int8_t ret;

  // Stop if already connected.
  if (mqtt.connected()) {
    return;
  }

  Serial.print("Connecting to MQTT... ");

  uint8_t retries = 3;
  while ((ret = mqtt.connect()) != 0) { // connect will return 0 for connected
    Serial.println(mqtt.connectErrorString(ret));
    Serial.println("Retrying MQTT connection in 5 seconds...");
    mqtt.disconnect();
    delay(5000);  // wait 5 seconds
    retries--;
    if (retries == 0) {
      // basically die and wait for WDT to reset me
      while (1);
    }
  }
  Serial.println("MQTT Connected!");
}

void MQTTAutoDiscovery()
{
  Adafruit_MQTT_Publish configTopic = Adafruit_MQTT_Publish(&mqtt, "homeassistant/light/office-shop-light/config");
  Serial.print("Topic created");
  configTopic.publish(
    "{"
      "'~': 'homeassistant/light/ir-mqtt-controller',"
      "'name': 'Office Shop Light',"
      "'unique_id': 'office_shop_light',"
      "'command_topic': '~/set',"
      "'state_topic': '~/state',"
      "'schema': 'json',"
      "'brightness': true"
    "}"
  );

  Serial.print("Config published");
  //publishState();

  configured = true;

  
  Serial.print("Done!");
}

void setState(String newState)
{
  
  Serial.print("On-Off button: ");
  Serial.println(newState);
  
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
    stateTopic.publish("{'state': 'ON'}");
  } else {
    stateTopic.publish("{'state': 'OFF'}");
  }
}
