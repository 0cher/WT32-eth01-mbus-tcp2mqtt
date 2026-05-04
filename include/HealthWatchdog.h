#ifndef HEALTH_WATCHDOG_H
#define HEALTH_WATCHDOG_H

#include <Arduino.h>
#include <NetworkClient.h>
#include "Debug.h"
#include "EthernetManager.h"
#include "MQTTClient.h"
#include "TimeSync.h"

namespace HealthWatchdog {
  static const unsigned long CHECK_INTERVAL_MS = 60UL * 1000UL;
  static const unsigned long INTERNET_PROBE_INTERVAL_MS = 5UL * 60UL * 1000UL;
  static const unsigned long MQTT_MAX_DOWN_MS = 6UL * 60UL * 60UL * 1000UL;
  static const unsigned long INTERNET_MAX_DOWN_MS = 6UL * 60UL * 60UL * 1000UL;
  static const unsigned long TIME_NEVER_SYNCED_MAX_MS = 24UL * 60UL * 60UL * 1000UL;
  static const unsigned long INTERNET_PROBE_TIMEOUT_MS = 3000UL;

  static unsigned long bootMs = 0;
  static unsigned long lastCheckMs = 0;
  static unsigned long lastInternetProbeMs = 0;
  static unsigned long lastMqttOkMs = 0;
  static unsigned long lastInternetOkMs = 0;
  static bool initialized = false;
  // Non-blocking probe state
  static NetworkClient* internetProbeClient = nullptr;
  static int internetProbePhase = 0; // 0=idle, 1=connecting, 2=reading
  static unsigned long internetProbeStartedAtMs = 0;
  static const IPAddress* internetProbeTargets[2];
  static int internetProbeTargetCount = 0;

  static bool hasReached(unsigned long now, unsigned long target) {
    return static_cast<long>(now - target) >= 0;
  }

  static void restartWithReason(const String& reason) {
    if (Debug.shouldLog(LOG_WARN)) {
      Debug.println(String("Health watchdog: restarting, reason=") + reason);
    }
    delay(250);
    ESP.restart();
  }

  static bool probeInternetTcp() {
    // Non-blocking probe using async connect
    internetProbePhase = 1;
    internetProbeStartedAtMs = millis();
    internetProbeClient = new NetworkClient();
    internetProbeTargetCount = 0;

    IPAddress cloudflare(1, 1, 1, 1);
    internetProbeTargets[internetProbeTargetCount++] = &cloudflare;

    IPAddress google(8, 8, 8, 8);
    internetProbeTargets[internetProbeTargetCount++] = &google;

    // Try first target
    internetProbeClient->connect(*internetProbeTargets[0], 53);
    return false; // result will be reported later via probeInternetPoll()
  }

  // Called from loop() every CHECK_INTERVAL_MS (60s) to check non-blocking probe progress
  static bool probeInternetPoll() {
    if (internetProbePhase == 0) {
      return false; // no probe pending
    }

    unsigned long now = millis();
    int probeIndex = internetProbePhase - 1; // 0-based target index for current attempt

    if (!internetProbeClient) {
      internetProbePhase = 0;
      return false;
    }

    // Check if connected
    if (internetProbeClient->connected()) {
      internetProbeClient->stop();
      delete internetProbeClient;
      internetProbeClient = nullptr;
      internetProbePhase = 0;
      lastInternetOkMs = now;
      return true;
    }

    // Check timeout for current target
    if (hasReached(now, internetProbeStartedAtMs + INTERNET_PROBE_TIMEOUT_MS)) {
      internetProbeClient->stop();
      probeIndex++;

      if (probeIndex < internetProbeTargetCount) {
        // Try next target
        internetProbePhase = probeIndex + 1;
        internetProbeStartedAtMs = now;
        internetProbeClient->connect(*internetProbeTargets[probeIndex], 53);
      } else {
        // All targets failed
        delete internetProbeClient;
        internetProbeClient = nullptr;
        internetProbePhase = 0;
      }
    }
    return false;
  }

  static void begin() {
    unsigned long now = millis();
    bootMs = now;
    lastCheckMs = now;
    lastInternetProbeMs = 0;
    lastMqttOkMs = MQTTClient::isConnected() ? now : bootMs;
    lastInternetOkMs = (MQTTClient::isConnected() || TimeSync::isSynchronized()) ? now : bootMs;
    initialized = true;
    internetProbeClient = nullptr;
    internetProbePhase = 0;
    DBG_INFO(String("Health watchdog started: mqttMaxDown=") + String(MQTT_MAX_DOWN_MS / 3600000UL) +
             String("h, internetMaxDown=") + String(INTERNET_MAX_DOWN_MS / 3600000UL) +
             String("h, timeNeverSyncedMax=") + String(TIME_NEVER_SYNCED_MAX_MS / 3600000UL) + String("h"));
  }

  static void loop() {
    if (!initialized) {
      begin();
    }

    unsigned long now = millis();
    if (!hasReached(now, lastCheckMs + CHECK_INTERVAL_MS)) {
      return;
    }
    lastCheckMs = now;

    if (MQTTClient::isConnected()) {
      lastMqttOkMs = now;
      lastInternetOkMs = now;
    }

    if (TimeSync::isSynchronized()) {
      lastInternetOkMs = now;
    }

    if (hasReached(now, lastInternetProbeMs + INTERNET_PROBE_INTERVAL_MS)) {
      lastInternetProbeMs = now;
      if (EthernetManager::isReady()) {
        bool probeOk = probeInternetTcp();
        if (probeOk) {
          lastInternetOkMs = now;
          DBG_DEBUG("Health watchdog: internet probe OK");
        } else {
          // probe may finish later (non-blocking); poll elsewhere
        }
      }
    }

    // Poll non-blocking internet probe result
    if (internetProbePhase != 0) {
      if (probeInternetPoll()) {
        DBG_DEBUG("Health watchdog: internet probe OK");
      }
    }

    if (hasReached(now, lastMqttOkMs + MQTT_MAX_DOWN_MS)) {
      restartWithReason(String("MQTT disconnected for ") + String(MQTT_MAX_DOWN_MS / 3600000UL) + String("h"));
      return;
    }

    if (hasReached(now, lastInternetOkMs + INTERNET_MAX_DOWN_MS)) {
      restartWithReason(String("internet unavailable for ") + String(INTERNET_MAX_DOWN_MS / 3600000UL) + String("h"));
      return;
    }

    if (!TimeSync::hasEverSynchronized() && hasReached(now, bootMs + TIME_NEVER_SYNCED_MAX_MS)) {
      restartWithReason(String("time never synchronized for ") + String(TIME_NEVER_SYNCED_MAX_MS / 3600000UL) + String("h"));
      return;
    }
  }
}

#endif // HEALTH_WATCHDOG_H
