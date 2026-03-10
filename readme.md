# ESP32-C6 Zigbee Gateway

ESP32-C6 based Zigbee Coordinator and MQTT Gateway.

## Features

- Zigbee coordinator (ESP Zigbee stack)
- MQTT bridge
- Zigbee device database
- Cluster parser
- Home Assistant auto discovery
- JSON state publish
- Web dashboard
- OTA firmware update
- Watchdog reconnect

## Build

Requires:

- PlatformIO
- ESP-IDF
- ESP32-C6 board

```bash
pio run