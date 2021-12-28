#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <PubSubClient.h>
#include "settings.h"

#define baudrate 115200
WiFiClient wifiClient;

#ifdef MQTT
PubSubClient client(wifiClient);
#endif

struct {
  bool read;
  bool valid;
  int ppm;
} zh18_result = { .read = true };

char hostString[20] = {0};

// All timestamps/periods in milliseconds
unsigned long uptime = 0;
unsigned long last_send = 0;
const unsigned long sending_interval = 15 * 60 * 1000;


void debugf(char *fmt, ... ) {
  char buf[128]; // resulting string limited to 128 chars
  va_list args;
  va_start (args, fmt );
  vsnprintf(buf, 128, fmt, args);
  va_end (args);
  Serial.print(buf);
}

void debugf_float(char *fmt, float val) {
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

#define INIT_CHAR 0x55
#define INIT_BAUD 2400
#define INIT_SIZE 522 // 504 steht in Doku?

void readWmz()
{
  Serial.println("read Wmz");
  Serial.flush();
  Serial.swap();
  Serial.begin(INIT_BAUD, SERIAL_8N1);

  // wakeup sequence
  byte wakeup[8];
  memset(wakeup, INIT_CHAR, sizeof(wakeup));

  // write sequence
  for (int i = 0; i < INIT_SIZE; ) {
    i += Serial.write(wakeup, sizeof(wakeup));
  }

  // sleep
  delay(110);

  Serial.begin(INIT_BAUD, SERIAL_8E1);

  // Sende Anfrage Daten Klasse 2
  char cmd[] = { 0x10, 0x5B, 0x00, 0x5B, 0x16 };
  Serial.write(cmd, sizeof(cmd));

  // Antwort empfangen
  char response[128];
  int read = Serial.readBytes(response, sizeof(response));

  // back to default
  Serial.swap();
  Serial.begin(baudrate);

  if (!read) {
    Serial.println("no bytes received");
  } else {
    Serial.print("received ");
    Serial.print(read);
    Serial.println(" bytes");
  }
  for (int i = 0; i < read; i++) {
    Serial.print(response[i], HEX);
  }

  if (response[0] != 0x68)
  {
    Serial.println("[zh18] Wrong starting byte received");
    return;
  }
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


void connectWifi() {
  Serial.printf("Connecting to %s ", wifi_ssid);

  WiFi.begin(wifi_ssid, wifi_pass);

  while (true) {
    Serial.print(".");
    int status;
    int new_status = WiFi.status();

    if (new_status != status) {
      status = new_status;

      Serial.printf("\nWifi status: %s\n", Statuses[status]);
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

void sendData() {
  String data, url;

  // Build data
  data = "";
  data += F("sensors,host=");
  data += hostString;
  data += F(",location=");
  data += location;
  data += " ";

  if (zh18_result.valid) {
    data += F("co2=");
    data += Float2String(zh18_result.ppm);
    data += F(",");
  }

  if (!data.endsWith(",")) {
    debugf("No data available\n");
    return;
  }

  data += F("uptime=");
  data += String(uptime / 1000);

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
  http.begin(wifiClient, url);
  http.setAuthorization(influx_user, influx_pass);
  http.addHeader("Content-Type", "text/plain");
  int status = http.POST(data);
  if (status != 204) {
    Serial.printf("[http] unexpected status code: %d\n", status);
    http.writeToStream(&Serial);
  }
  http.end();

}


void setup() {
  Serial.begin(baudrate);

  sprintf(hostString, "esp8266-%06X", ESP.getChipId());
  Serial.printf("\nHostname: %s\n", hostString);

  // Serial.setDebugOutput(true);
  initWifi();

#ifdef MQTT
  client.setServer(mqtt_server, 1883);
#endif

  delay(1000);
}


void loop() {
  uptime = millis();

  // uptime restarted at zero? (overflow after 50 days)
  if (last_send > uptime)
    last_send = 0;

  // Sending now?
  if (last_send == 0 || last_send + sending_interval < uptime) {
    debugf("\nreading sensors ...\n");
    debugf("Uptime:        %04d\n", uptime / 1000);

    readWmz();

    if (WiFi.status() != WL_CONNECTED)
      initWifi();

    sendData();

    last_send = uptime;
    debugf("done\n");
  }
}
