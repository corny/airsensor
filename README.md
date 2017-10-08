Airsensor
=========

This Arduino project implements an indoor air quality sensor based on the [ESP8266](https://en.wikipedia.org/wiki/ESP8266).


## Components

* NodeMCU 1.0 (ESP-12E)
* ZH-18 CO2 sensor
* Bosch BME280 sensor for temperature, humidity and pressure
* SSD1306 OLED display


## Data transmission

The sensor data can be recorded via:

* [MQTT](https://en.wikipedia.org/wiki/MQTT)
* [InfluxDB](https://en.wikipedia.org/wiki/InfluxDB)


## Pins

Connect the components to the following pins:

* ZH-18:
    - D7 (GPIO13) as RXD2
    - D8 (GPIO15) as TXD2
* BME280 and SSD1306:
    - D3 (GPIO0) as SDA for I²C
    - D4 (GPIO2) as SDC for I²C
