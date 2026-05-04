#ifndef MQTTCLIENT_H
#define MQTTCLIENT_H

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include "Config.h"
#include "AppSettings.h"
#include "Debug.h"
#include "EthernetManager.h"

namespace MQTTClient {
  static WiFiClientSecure netClient;
  static PubSubClient client(netClient);
  static char telemetryTopic[64] = {0};
  static char telemetryLogTopic[80] = {0};
  static char configTopic[64] = {0};
  static unsigned long lastReconnectAttemptMs = 0;
  static bool wasConnected = false;
  static bool connectInProgress = false;
  static bool reconnectPausedForNetwork = false;
  static bool transportConfigured = false;

  static void (*onConnected)() = nullptr;
  static void (*onConfigMessage)(const char*, const uint8_t*, unsigned int) = nullptr;

  static void logInfoLocal(const String& message) {
    if (Debug.shouldLog(LOG_INFO)) {
      Debug.println(message);
    }
  }

  static void logWarnLocal(const String& message) {
    if (Debug.shouldLog(LOG_WARN)) {
      Debug.println(message);
    }
  }

  static void ensureTopics() {
    if (telemetryTopic[0] == '\0') {
      snprintf(telemetryTopic, sizeof(telemetryTopic),
        AppSettings::mqttTopicTelemetry().c_str(), AppSettings::deviceSerialCStr());
    }
    if (telemetryLogTopic[0] == '\0') {
      snprintf(telemetryLogTopic, sizeof(telemetryLogTopic),
        AppSettings::mqttTopicTelemetryLog().c_str(), AppSettings::deviceSerialCStr());
    }
    if (configTopic[0] == '\0') {
      snprintf(configTopic, sizeof(configTopic),
        AppSettings::mqttTopicConfig().c_str(), AppSettings::deviceSerialCStr());
    }
  }

  static const char* brokerHost() {
    return AppSettings::get().mqttHost.c_str();
  }

  static uint16_t brokerPort() {
    return AppSettings::get().mqttPort;
  }

  static const char* brokerPassword() {
    return AppSettings::get().mqttPassword.c_str();
  }

  static const char* brokerUsername() {
    const String& user = AppSettings::get().mqttUsername;
    return user.length() > 0 ? user.c_str() : AppSettings::deviceSerialCStr();
  }

  static uint16_t keepAliveSeconds() {
    return AppSettings::get().mqttKeepAliveSeconds;
  }

  static uint32_t reconnectIntervalMs() {
    return AppSettings::get().mqttReconnectIntervalMs;
  }

  static void handleMessage(char* topic, uint8_t* payload, unsigned int length) {
    if (onConfigMessage != nullptr) {
      onConfigMessage(topic, payload, length);
    }
  }

  static void configureTransport() {
    if (transportConfigured) {
      return;
    }

    if (AppSettings::usesCustomCa()) {
      netClient.setCACert(AppSettings::customMqttCaCert());
    } else if (DEFAULT_MQTT_CA_CERT[0] != '\0') {
      netClient.setCACert(DEFAULT_MQTT_CA_CERT);
    }
    logInfoLocal(String("MQTT TLS CA source: ") + AppSettings::mqttCaSourceLabel());

    client.setServer(brokerHost(), brokerPort());
    client.setBufferSize(MQTT_PUBLISH_BUFFER_SIZE);
    client.setKeepAlive(keepAliveSeconds());
    client.setCallback(handleMessage);
    transportConfigured = true;
  }

  static bool connect() {
    ensureTopics();
    configureTransport();
    if (client.connected()) {
      return true;
    }
    if (!EthernetManager::isReady()) {
      if (!reconnectPausedForNetwork) {
        logInfoLocal("MQTT reconnect paused: Ethernet link or IP is down");
        reconnectPausedForNetwork = true;
      }
      return false;
    }
    if (connectInProgress) {
      return false;
    }

    reconnectPausedForNetwork = false;
    connectInProgress = true;
    logInfoLocal(String("MQTT connecting: ") + brokerHost() + ":" + String(brokerPort()));
    if (!client.connect(AppSettings::deviceSerialCStr(), brokerUsername(), brokerPassword())) {
      logWarnLocal(String("MQTT connect failed, state=") + String(client.state()));
      wasConnected = false;
      connectInProgress = false;
      return false;
    }

    client.subscribe(configTopic);
    logInfoLocal(String("MQTT connected, telemetry topic: ") + telemetryTopic);
    logInfoLocal(String("MQTT subscribed: ") + configTopic);
    wasConnected = true;
    connectInProgress = false;
    if (onConnected) onConnected();
    return true;
  }

  static bool publish(const char* topic, const char* payload) {
    if (connectInProgress) {
      return false;
    }
    if (!client.connected() && !connect()) {
      return false;
    }
    return client.publish(topic, payload);
  }

  static bool publishTelemetry(const char* payload) {
    ensureTopics();
    return publish(telemetryTopic, payload);
  }

  static bool publishTelemetryLog(const char* payload) {
    ensureTopics();
    if (connectInProgress) {
      return false;
    }
    if (!client.connected() && !connect()) {
      return false;
    }
    return client.publish(telemetryLogTopic, payload, true);
  }

  static const char* getTelemetryTopic() {
    ensureTopics();
    return telemetryTopic;
  }

  static const char* getTelemetryLogTopic() {
    ensureTopics();
    return telemetryLogTopic;
  }

  static const char* getConfigTopic() {
    ensureTopics();
    return configTopic;
  }

  static void setOnConnected(void (*callback)()) { onConnected = callback; }
  static void setOnConfigMessage(void (*callback)(const char*, const uint8_t*, unsigned int)) {
    onConfigMessage = callback;
  }

  static bool isConnected() { return client.connected(); }

  static void reloadSettings() {
    netClient.stop();
    client.disconnect();
    lastReconnectAttemptMs = 0;
    wasConnected = false;
    connectInProgress = false;
    reconnectPausedForNetwork = false;
    transportConfigured = false;
    telemetryTopic[0] = '\0';
    telemetryLogTopic[0] = '\0';
    configTopic[0] = '\0';
  }

  static void loop() {
    if (client.connected()) {
      client.loop();
      return;
    }

    if (wasConnected) {
      logWarnLocal("MQTT disconnected");
      wasConnected = false;
    }

    if (!EthernetManager::isReady()) {
      connect();
      return;
    }

    unsigned long now = millis();
    if (now - lastReconnectAttemptMs >= reconnectIntervalMs()) {
      lastReconnectAttemptMs = now;
      connect();
    }
  }
}

#endif // MQTTCLIENT_H
