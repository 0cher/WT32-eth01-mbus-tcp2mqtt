#ifndef TIME_SYNC_H
#define TIME_SYNC_H

#include <Arduino.h>
#include <time.h>
#include "AppSettings.h"
#include "Debug.h"
#include "EthernetManager.h"

namespace TimeSync {
  static const char* NTP_SERVER_PRIMARY = "pool.ntp.org";
  static const char* NTP_SERVER_SECONDARY = "time.nist.gov";
  static const char* NTP_SERVER_TERTIARY = "time.google.com";
  static const unsigned long SYNC_ATTEMPT_TIMEOUT_MS = 30000UL;
  static const unsigned long FIRST_RETRY_DELAY_MS = 5UL * 60UL * 1000UL;
  static const unsigned long RETRY_DELAY_MS = 60UL * 60UL * 1000UL;
  static const unsigned long RESYNC_INTERVAL_MS = 12UL * 60UL * 60UL * 1000UL;  // 12 hours
  static const time_t MIN_VALID_UNIX_TIME = 1700000000;

  static bool networkWasReady = false;
  static bool syncInProgress = false;
  static bool synchronized = false;
  static bool everSynchronized = false;
  static unsigned long syncAttemptStartedAtMs = 0;
  static unsigned long nextSyncAttemptAtMs = 0;
  static unsigned long lastSuccessfulSyncAtMs = 0;
  static unsigned int failedAttempts = 0;

  static bool hasReached(unsigned long now, unsigned long target) {
    return static_cast<long>(now - target) >= 0;
  }

  static bool isTimeValid(time_t epoch) {
    return epoch >= MIN_VALID_UNIX_TIME;
  }

  static time_t currentTimestamp() {
    time_t epoch = time(nullptr);
    return isTimeValid(epoch) ? epoch : 0;
  }

  static String currentTimestampReadable() {
    time_t epoch = currentTimestamp();
    if (epoch == 0) {
      return "";
    }

    struct tm timeInfo;
    gmtime_r(&epoch, &timeInfo);
    char buffer[24];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeInfo);
    return String(buffer);
  }

  static String formatTimezoneOffset(int16_t offsetMinutes) {
    char sign = offsetMinutes < 0 ? '-' : '+';
    int total = offsetMinutes < 0 ? -offsetMinutes : offsetMinutes;
    char buffer[12];
    snprintf(buffer, sizeof(buffer), "UTC%c%02d:%02d", sign, total / 60, total % 60);
    return String(buffer);
  }

  static String currentTimestampReadableWithOffset(int16_t offsetMinutes) {
    time_t epoch = currentTimestamp();
    if (epoch == 0) {
      return "";
    }

    epoch += static_cast<time_t>(offsetMinutes) * 60;
    struct tm timeInfo;
    gmtime_r(&epoch, &timeInfo);
    char buffer[24];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeInfo);
    return String(buffer);
  }

  static String currentLocalTimestampReadable() {
    return currentTimestampReadableWithOffset(AppSettings::get().timezoneOffsetMinutes);
  }

  static String telemetryTimestampReadable() {
    AppSettings::Settings& settings = AppSettings::get();
    return settings.telemetryUseLocalTime
      ? currentTimestampReadableWithOffset(settings.timezoneOffsetMinutes)
      : currentTimestampReadable();
  }

  static String telemetryTimezoneLabel() {
    AppSettings::Settings& settings = AppSettings::get();
    return settings.telemetryUseLocalTime ? formatTimezoneOffset(settings.timezoneOffsetMinutes) : String("UTC");
  }

  static bool isSynchronized() {
    return synchronized && currentTimestamp() != 0;
  }

  static bool hasEverSynchronized() {
    return everSynchronized || currentTimestamp() != 0;
  }

  static void startSyncAttempt(const char* reason) {
    configTime(0, 0, NTP_SERVER_PRIMARY, NTP_SERVER_SECONDARY, NTP_SERVER_TERTIARY);
    syncInProgress = true;
    syncAttemptStartedAtMs = millis();
    DBG_INFO(String("Time sync: started (") + reason + ")");
  }

  static void handleSyncSuccess(time_t epoch) {
    syncInProgress = false;
    synchronized = true;
    everSynchronized = true;
    failedAttempts = 0;
    lastSuccessfulSyncAtMs = millis();
    nextSyncAttemptAtMs = lastSuccessfulSyncAtMs + RESYNC_INTERVAL_MS;
    DBG_INFO(String("Time sync: success, unix=") + String(static_cast<unsigned long>(epoch)) +
             String(", readable=") + currentTimestampReadable() + String(" UTC"));
  }

  static void handleSyncFailure() {
    syncInProgress = false;
    synchronized = false;
    ++failedAttempts;
    unsigned long delayMs = failedAttempts == 1 ? FIRST_RETRY_DELAY_MS : RETRY_DELAY_MS;
    nextSyncAttemptAtMs = millis() + delayMs;
    DBG_WARN(String("Time sync: failed, next retry in ") + String(delayMs / 60000UL) + String(" min"));
  }

  static void begin() {
    networkWasReady = EthernetManager::isReady();
    synchronized = currentTimestamp() != 0;
    everSynchronized = synchronized;
    nextSyncAttemptAtMs = 0;
    if (synchronized) {
      lastSuccessfulSyncAtMs = millis();
    }
  }

  static void loop() {
    unsigned long now = millis();
    bool networkReady = EthernetManager::isReady();

    if (networkReady != networkWasReady) {
      networkWasReady = networkReady;
      if (networkReady) {
        DBG_INFO("Time sync: network ready");
        if (!isSynchronized()) {
          nextSyncAttemptAtMs = now;
        }
      } else {
        DBG_WARN("Time sync: network unavailable");
        syncInProgress = false;
      }
    }

    if (!networkReady) {
      return;
    }

    time_t epoch = time(nullptr);
    if (syncInProgress) {
      if (isTimeValid(epoch)) {
        handleSyncSuccess(epoch);
      } else if (hasReached(now, syncAttemptStartedAtMs + SYNC_ATTEMPT_TIMEOUT_MS)) {
        handleSyncFailure();
      }
      return;
    }

    if (!synchronized && isTimeValid(epoch)) {
      handleSyncSuccess(epoch);
      return;
    }

    if (synchronized) {
      if (hasReached(now, lastSuccessfulSyncAtMs + RESYNC_INTERVAL_MS)) {
        startSyncAttempt("scheduled daily refresh");
      }
      return;
    }

    if (hasReached(now, nextSyncAttemptAtMs)) {
      startSyncAttempt(failedAttempts == 0 ? "initial network sync" : "retry after previous failure");
    }
  }
}

#endif // TIME_SYNC_H
