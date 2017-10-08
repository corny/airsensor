#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <PubSubClient.h>
#include <Adafruit_BME280.h>
#include <Wire.h>
#include <SSD1306.h>
#include "settings.h"

const int baudrate = 115200;
#define SDA D3
#define SDC D4

#ifdef MQTT
WiFiClient espClient;
PubSubClient client(espClient);
#endif

SSD1306 display(0x3c, SDC, SDA);
bool has_display;

// BME280, Luftdruck-Sensor
Adafruit_BME280 bme280;

struct {
  bool read;
  bool valid;
  int ppm;
} zh18_result = { .read = true };

struct {
  bool read;
  bool valid;
  float t; // Temperature
  float h; // Humidity
  float p; // Pressure
} bme280_result = { .read = true };


char hostString[20] = {0};

// All timestamps/periods in milliseconds
unsigned long uptime = 0;
unsigned long last_send = 0;
const unsigned long sending_interval = 10*1000;


void debugf(char *fmt, ... ){
  char buf[128]; // resulting string limited to 128 chars
  va_list args;
  va_start (args, fmt );
  vsnprintf(buf, 128, fmt, args);
  va_end (args);
  Serial.print(buf);
}

void debugf_float(char *fmt, float val){
  char buf[9];
  dtostrf(val, 6, 2, buf);
  debugf(fmt, buf);
}

String Float2String(const float value) {
  // Convert a float to String with two decimals.
  char temp[12];
  String s;

  dtostrf(value, 8, 2, temp);
  s = String(temp);
  s.trim();
  return s;
}

bool initBME280(char addr) {
  debugf("[bme280] Trying on 0x%02X ... ", addr);

  if (bme280.begin(addr)) {
    debugf("found\n");
    return true;
  } else {
    debugf("not found\n");
    return false;
  }
}

void readBME280() {
  bme280_result.t = bme280.readTemperature();
  bme280_result.h = bme280.readHumidity();
  bme280_result.p = bme280.readPressure();
  bme280_result.valid = !isnan(bme280_result.t) && !isnan(bme280_result.h) && !isnan(bme280_result.p);

  if (bme280_result.valid) {
    debugf_float("Temperature: %s C\n",   bme280_result.t);
    debugf_float("Humidity:    %s %%\n",  bme280_result.h);
    debugf_float("Pressure:    %s hPa\n", bme280_result.p/100);
  } else {
    debugf("[bme280] reading failed\n");
  }
}

void readZH18()
{
  zh18_result.valid = false;

  // command to ask for data
  const byte cmd[9] = {0xFF, 0x01, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x79};
  char response[9]; // for answer

  Serial.flush();
  delay(100);
  Serial.begin(9600);
  Serial.swap();

  Serial.write(cmd, sizeof(cmd));
  int read = Serial.readBytes(response, sizeof(response));

  Serial.swap();
  Serial.begin(baudrate);

  if (read != sizeof(response)) {
    if (!read){
      Serial.println("[zh18] no bytes received");
    } else {
      Serial.print("[zh18] received ");
      Serial.print(read);
      Serial.println(" bytes");
    }
    return;
  }

  if (response[0] != 0xFF)
  {
    Serial.println("[zh18] Wrong starting byte received");
    return;
  }

  if (response[1] != 0x86)
  {
    Serial.println("[zh18] Wrong command received");
    return;
  }

  char checksum = 0;
  for (char i = 1; i < 8; i++)
  {
    checksum += response[i];
  }
  checksum = 0xff - checksum + 1;

  if (checksum != response[8])
  {
    Serial.printf("[zh18] Checksum invalid: expected=%02x is=%02x\n", checksum, response[8]);
    return;
  }

  bool preheating = uptime < 1000*60*3; // up to three minutes preheat time

  zh18_result.ppm   = response[3] | response[2] << 8;
  zh18_result.valid = !preheating || (preheating && zh18_result.ppm != 400);
  debugf("CO2:           %04d ppm\n", zh18_result.ppm);
}

char* Statuses[] = {
  "Idle",
  "SSID not available",
  "Scan completed",
  "Connected",
  "Connect failed",
  "Connection lost",
  "Disconnected"
};

void displayWifiStatus(String status) {
  if (!has_display)
    return;

  display.clear();
  display.drawString(0,  0, hostString);
  display.drawString(0, 10, String("SSID: ") + wifi_ssid);
  if (status != "")
    display.drawString(0, 20, status);
  display.display();
}

void connectWifi() {
  Serial.printf("Connecting to %s ", wifi_ssid);
  displayWifiStatus("");

  WiFi.begin(wifi_ssid, wifi_pass);

  while (true) {
    Serial.print(".");
    int status;
    int new_status = WiFi.status();

    if (new_status != status) {
      status = new_status;

      Serial.printf("\nWifi status: %s\n", Statuses[status]);
      displayWifiStatus(Statuses[status]);
    }

    if (status == WL_CONNECTED)
      break;

    if (status == WL_NO_SSID_AVAIL) {
      delay(3000);
      ESP.restart();
    }

    delay(100);
  }
  Serial.println(" done");
}

void initWifi() {
  int status = -1;

  WiFi.hostname(hostString);
  WiFi.softAPdisconnect(true);
  ESP.wdtEnable(1000);

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Wifi already connected");
  } else {
    connectWifi();
  }
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void displayData() {
  display.clear();

  if (bme280_result.valid) {
    display.drawString(0, 0, String("Temperature: ") + Float2String(bme280_result.t) + String(" °C"));
    display.drawString(0, 10, String("Humidity: ") + Float2String(bme280_result.h) + String(" %"));
    display.drawString(0, 20, String("Pressure: ") + Float2String(bme280_result.p/100) + String(" hPa"));
  }

  if (zh18_result.valid) {
    display.drawString(0, 40, String("CO2: ") + zh18_result.ppm + String(" ppm"));
  }else{
    display.drawString(0, 40, String("CO2: -"));
  }

  display.display();
}

void sendData() {
  String data, url;

  // Build data
  data = "";
  data += F("sensors,host=");
  data += hostString;
  data += F(",location=");
  data += location;
  data += " ";

  if (bme280_result.valid) {
    data += F("temperature=");
    data += Float2String(bme280_result.t);
    data += F(",humidity=");
    data += Float2String(bme280_result.h);
    data += F(",pressure=");
    data += Float2String(bme280_result.p);
    data += F(",");
  }

  if (zh18_result.valid) {
    data += F("co2=");
    data += Float2String(zh18_result.ppm);
    data += F(",");
  }

  if (!data.endsWith(",")){
    debugf("No data available\n");
    return;
  }

  data += F("uptime=");
  data += String(uptime/1000);

  // Build URL
  url = String(influx_url);
  url += F("write?precision=s&db=");
  url += influx_database;

#ifdef MQTT
  // Send data to MQTT
  client.publish(mqtt_topic, String(data).c_str(), true);
#endif

  // Send data to InfluxDB
  HTTPClient http;
  http.begin(url);
  http.setAuthorization(influx_user, influx_pass);
  http.addHeader("Content-Type", "text/plain");
  int status = http.POST(data);
  if (status != 204) {
    Serial.printf("[http] unexpected status code: %d\n", status);
    http.writeToStream(&Serial);
  }
  http.end();

}

void initDisplay(){
  Wire.beginTransmission (0x3C);

  if (Wire.endTransmission () != 0) {
    has_display = false;
    Serial.printf("[display] not found on: 0x3c\n");
  }

  Serial.printf("[display] found on: 0x3c\n");
  display.init();
  display.flipScreenVertically();
  display.setFont(ArialMT_Plain_10);
  has_display = true;
}

void setup() {
  Serial.begin(baudrate);

  Wire.pins(SDC, SDA);
  Wire.begin(SDC, SDA);

  initDisplay();

  sprintf(hostString, "esp8266-%06X", ESP.getChipId());
  Serial.printf("\nHostname: %s\n", hostString);

  if (has_display){
    display.clear();
    display.drawString(0, 0, "Booting ...");
    display.drawString(0, 10, hostString);
    display.display();
  }

  // Serial.setDebugOutput(true);
  initWifi();

#ifdef MQTT
  client.setServer(mqtt_server, 1883);
#endif

  // BME280 initialisieren
  if (bme280_result.read) {
    if (!initBME280(0x76) && !initBME280(0x77)) {
      debugf("Check BME280 wiring\n");
      bme280_result.read = false;
    }
  }

  delay(2500);
}


void loop() {
  uptime = millis();

  // uptime restarted at zero? (overflow after 50 days)
  if (last_send > uptime)
    last_send = 0;

  // Sending now?
  if (last_send == 0 || last_send + sending_interval < uptime){
    debugf("\nreading sensors ...\n");
    debugf("Uptime:        %04d\n", uptime/1000);

    if (bme280_result.read) readBME280();
    if (zh18_result.read)   readZH18();
    if (has_display)        displayData();

    if (WiFi.status() != WL_CONNECTED)
      initWifi();

    sendData();

    last_send = uptime;
    debugf("done\n");
  }
}
