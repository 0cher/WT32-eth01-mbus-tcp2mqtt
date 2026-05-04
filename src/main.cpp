#include <Arduino.h>
#include "AppSettings.h"
#include "Config.h"
#include "EthernetManager.h"
#include "HealthWatchdog.h"
#include "MbusService.h"
#include "MQTTClient.h"
#include "Recovery.h"
#include "TimeSync.h"
#include "WebAdmin.h"
#include "Debug.h"

// define global Debug instance
Debugger Debug;

const unsigned long INTERVAL_MS = 1000;
const unsigned long MEMINFO_INTERVAL_MS = 30000;
unsigned long lastMillis = 0;
unsigned long lastMemInfoMs = 0;
bool prevLinkUp = false;
unsigned long lastEthCheck = 0;
const unsigned long ETH_CHECK_MS = 500;

static void printEthStatus() {
  DBG_INFO(String("ETH link: ") + (ETH.linkUp() ? "UP" : "DOWN"));

  uint8_t mac[6];
  ETH.macAddress(mac);
  String macs;
  for (int i = 0; i < 6; ++i) {
    char buf[3];
    sprintf(buf, "%02X", mac[i]);
    macs += buf;
    if (i < 5) macs += ":";
  }
  DBG_INFO(String("MAC: ") + macs);

  DBG_INFO(String("IP: ") + ETH.localIP().toString());
  DBG_INFO(String("Gateway: ") + ETH.gatewayIP().toString());
  DBG_INFO(String("Netmask: ") + ETH.subnetMask().toString());
}

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println();
  // initialize Debug utility (verbose in development, quieter in production)
#if RECOVERY_MODE_DEV_UART
  Debug.begin(&Serial, true, LOG_DEBUG);
#else
  Debug.begin(&Serial, true, LOG_INFO);
#endif
  // set server forwarding for WARN and ERROR
  Debug.setServerLevel(LOG_WARN);
  AppSettings::begin();
  Recovery::begin();

  // register MQTT sender (defined below)
  void mqttSendWrapper(const char* msg); // forward declaration
  Debug.setServerSender(mqttSendWrapper);

  DBG_INFO("=== WT32_mbus Ethernet Test ===");
  DBG_INFO(String("Build profile: ") + Recovery::profileName());
  DBG_INFO(String("Build time: ") + String(__DATE__ " " __TIME__));
  DBG_INFO(String("Serial baud: ") + String(115200));
  DBG_INFO(String("Device serial: ") + AppSettings::deviceSerial());
  DBG_MEMINFO();
  DBG_INFO("Starting...");

  DBG_INFO(String("Starting Ethernet (") + EthernetManager::modeLabel() + ")...");
  bool ok = EthernetManager::begin();
  if (ok) {
    DBG_INFO(String("IP address: ") + EthernetManager::localIP().toString());
    printEthStatus();
  } else {
    DBG_WARN("Network init failed or no IP");
    printEthStatus();
  }

  WebAdmin::begin();
  TimeSync::begin();

  MQTTClient::setOnConfigMessage(MbusService::handleConfigMessage);

  // connect MQTT (will subscribe to config topic)
  MQTTClient::connect();
  HealthWatchdog::begin();
}

void loop() {
  unsigned long now = millis();

  // Periodically check Ethernet link status and report changes
  if (now - lastEthCheck >= ETH_CHECK_MS) {
    lastEthCheck = now;
    bool link = ETH.linkUp();
    if (link != prevLinkUp) {
      prevLinkUp = link;
      if (link) {
          DBG_INFO(String(millis()) + " ms - Ethernet link UP");
      } else {
          DBG_WARN(String(millis()) + " ms - Ethernet link DOWN");
      }
      printEthStatus();
    }
  }

  // MQTT loop
  MQTTClient::loop();
  TimeSync::loop();
  MbusService::loop();
  WebAdmin::loop();
  HealthWatchdog::loop();

  if (now - lastMillis >= INTERVAL_MS) {
    lastMillis = now;
    DBG_DEBUG(String("WT32_mbus: OK - uptime (ms): ") + String(now));
  }

  if (now - lastMemInfoMs >= MEMINFO_INTERVAL_MS) {
    lastMemInfoMs = now;
    DBG_MEMINFO();
  }

  delay(10);
}

// MQTT send wrapper for Debug -> publishes to telemetry log topic
void mqttSendWrapper(const char* msg) {
  MQTTClient::publishTelemetryLog(msg);
}
