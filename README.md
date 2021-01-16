# Koda Light IR - ESP MQTT Controller

Use an [ESP8266](https://www.wemos.cc/en/latest/d1/d1_mini.html) to control a [Koda Shop Light](https://www.costco.ca/koda-116-cm-(45.6-in.)-led-linkable-shop-light-with-motion-sensor-and-remote.product.100537804.html) with [MQTT](https://mqtt.org/).

I got this Koda Shop Light from Costco. It's a decent light and comes with an IR remote, but I wanted to hook it into my smart home system. This project emulates the IR remote and allows the light to be controlled via MQTT over WiFi.

This probably works with other Koda IR-controlled lights, but I don't have any to test.

## Software Setup

The code runs using the [ESP8266 Arduino Core](https://github.com/esp8266/Arduino)

You'll need the following libraries:
- [EspMQTTClient](https://github.com/plapointe6/EspMQTTClient) For handling the MQTT protocol
- [IRremoteESP8266](https://github.com/crankyoldgit/IRremoteESP8266) For sending IR commands
- [ArduinoJson](https://arduinojson.org/?utm_source=meta&utm_medium=library.properties) For processing MQTT commands (could be skipped if you're not using HomeAssistant json-based commands)

Configure the project to connect to your WiFi and MQTT networks. Rename `Config.h.example` to `Config.h`, and fill in the fields.

## Hardware Setup

Get an ESP8266 board. I used a [WEMOS D1 Mini.](https://www.wemos.cc/en/latest/d1/d1_mini.html)

Connect it to an IR LED. The digital out pins probably aren't enough to drive the LED brightly, so you could use a transistor to control is, [as shown here.](https://github.com/crankyoldgit/IRremoteESP8266/wiki#ir-sending)

## Brightness Control

The Koda light doesn't have IR commands for setting absolute brightness, only relative. This means we have to send several increase/decrease brightness commands to reach the desired level.

Each IR command needs a short delay (500ms seems safe) before the next command, or the light will ignore them. Therefore, it can take a few seconds to fully transition between brightness levels.

We also have to assume the light's state. The light seems to maintain its brightness state across power on/off, even when unplugged, so as long as the IR transmitter has a reliable line-of-sight to the light, the assumed state should be accurate.

There _are_ absolute commands for setting minimum and maximum brightness, so the ESP will send those whenever possible.

When the ESP first starts up, it sets the light to minimum brightness, then turns if off. This ensures we have a known light state on boot.

## Command Processing

Commands that change brightness can take a few seconds to execute. If a new MQTT command is received, it will cancel the in-progress command and start processing the new one ASAP.

## Home Assistant Integration

I use Home Assistant to control light over MQTT. This project has a handy function for automatically integrating itself, `setupHomeAssistantAutoDiscovery`. If you don't use Home Assistant or want to manually configure the connection, just remove that function.

## Gathering Codes

This projects includes all codes needed to control and adjust brightness of the Koda Shop Light. If you want to control other functions or Koda devices, you may want to gather additional codes. I used the [IRrecvDumpV3 example project](https://github.com/crankyoldgit/IRremoteESP8266/tree/master/examples/IRrecvDumpV3) for these ones.

## Future Ideas

- Make this a more general-purpose project for IR device control
- Use IR-repeat commands to change brightness faster