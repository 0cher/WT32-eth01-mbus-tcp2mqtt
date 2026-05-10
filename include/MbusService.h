#ifndef MBUS_SERVICE_H
#define MBUS_SERVICE_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ctype.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <vector>
#include "AppSettings.h"
#include "Debug.h"
#include "MBusGateway.h"
#include "MQTTClient.h"
#include "TimeSync.h"

namespace MbusService {
  static const size_t MAX_CACHED_MBUS_DEVICES = 64;
  static const size_t MAX_CACHED_MBUS_VALUES_PER_DEVICE = 8;
  static const size_t MAX_AUTO_REFRESH_MBUS_DEVICES = 50;
  static const size_t MAX_TELEMETRY_JSON_BYTES = 3072;

  struct TelemetryReading {
    String type;
    String valueText;
    double numericValue;
    bool numeric;
    String unit;
  };

  struct TelemetryDevicePayload {
    int primaryAddress;
    String secondaryAddress;
    String manufacturer;
    String type;
    String description;
    String id;
    String serialNumber;
    std::vector<TelemetryReading> readings;
  };

  struct TelemetryLogEntry {
    String level;
    int primaryAddress;
    String secondaryAddress;
    String description;
    String message;
  };

  struct CachedMbusValue {
    String field;
    String value;
    String unit;
  };

  struct CachedMbusSnapshot {
    int primaryAddress;
    String secondaryAddress;
    String description;
    String manufacturer;
    String type;
    String id;
    unsigned long updatedAtMs;
    std::vector<CachedMbusValue> values;
  };

  struct SnapshotRefreshStatus {
    bool active;
    bool lastSuccess;
    bool autoTriggered;
    int totalDevices;
    int processedDevices;
    int updatedDevices;
    unsigned long startedAtMs;
    unsigned long finishedAtMs;
    String error;
  };

  struct ScanStatus {
    bool active;
    bool stopRequested;
    bool finished;
    bool success;
    String error;
    String mode;
    String progressLabel;
    int currentValue;
    int rangeStart;
    int rangeEnd;
    int totalSteps;
    int completedSteps;
    std::vector<MBusGateway::DeviceInfo> devices;
  };

  static SemaphoreHandle_t snapshotMutex = nullptr;
  static std::vector<CachedMbusSnapshot> lastMbusSnapshots;
  static std::vector<MBusGateway::DeviceInfo> lastMbusScanResults;
  static bool autoRefreshFallbackInitialized = false;
  static unsigned long autoRefreshFallbackStartedAtMs = 0;

  static void ensureSnapshotMutex() {
    if (snapshotMutex == nullptr) {
      snapshotMutex = xSemaphoreCreateMutex();
    }
  }

  static void lockSnapshots() {
    ensureSnapshotMutex();
    if (snapshotMutex != nullptr) {
      xSemaphoreTake(snapshotMutex, portMAX_DELAY);
    }
  }

  static void unlockSnapshots() {
    if (snapshotMutex != nullptr) {
      xSemaphoreGive(snapshotMutex);
    }
  }

  static int findMbusSnapshotIndex(int primaryAddress, const String& secondaryAddress) {
    for (size_t index = 0; index < lastMbusSnapshots.size(); ++index) {
      if (AppSettings::mbusIdentityMatches(lastMbusSnapshots[index].primaryAddress,
                                           lastMbusSnapshots[index].secondaryAddress,
                                           primaryAddress,
                                           secondaryAddress)) {
        return static_cast<int>(index);
      }
    }
    return -1;
  }

  enum class ReadProfile {
    Interactive,
    AutoRefresh,
  };

  static MBusGateway::TelegramResult readDeviceTelegram(int primaryAddress,
                                                        const String& secondaryAddress,
                                                        ReadProfile profile = ReadProfile::Interactive) {
    AppSettings::Settings& settings = AppSettings::get();
    unsigned long timeoutMs = profile == ReadProfile::AutoRefresh
      ? MBusGateway::AUTO_REFRESH_RESPONSE_TIMEOUT_MS
      : MBusGateway::RESPONSE_TIMEOUT_MS;
    unsigned long idleMs = profile == ReadProfile::AutoRefresh
      ? MBusGateway::AUTO_REFRESH_RESPONSE_IDLE_MS
      : MBusGateway::RESPONSE_IDLE_MS;
    return MBusGateway::readDeviceTelegram(settings.mbusGatewayHost,
                                           settings.mbusGatewayPort,
                                           primaryAddress,
                                           secondaryAddress,
                                           timeoutMs,
                                           idleMs);
  }

  static std::vector<MBusGateway::TelegramRecord> selectableRecords(const AppSettings::SavedMbusDevice& savedDevice,
                                                                    const MBusGateway::TelegramResult& telegramResult) {
    std::vector<MBusGateway::TelegramRecord> records;

    for (size_t index = 0; index < telegramResult.records.size(); ++index) {
      MBusGateway::TelegramRecord record = telegramResult.records[index];
      if (record.key == MBusGateway::buildHeaderRecordKey("manufacturer") && savedDevice.manufacturer.length() > 0) {
        continue;
      }
      if (record.key == MBusGateway::buildHeaderRecordKey("medium") && savedDevice.type.length() > 0) {
        continue;
      }
      records.push_back(record);
    }

    return records;
  }

  static void addTelemetryLog(std::vector<TelemetryLogEntry>& logs,
                              const char* level,
                              int primaryAddress,
                              const String& secondaryAddress,
                              const String& description,
                              const String& message) {
    TelemetryLogEntry entry;
    entry.level = level;
    entry.primaryAddress = primaryAddress;
    entry.secondaryAddress = secondaryAddress;
    entry.description = description;
    entry.message = message;
    logs.push_back(entry);
  }

  static void initTelemetryEnvelope(JsonDocument& doc) {
    doc["gateway"] = AppSettings::deviceSerial();
    doc["timestamp"] = static_cast<unsigned long>(TimeSync::currentTimestamp());
    doc["timestampReadable"] = TimeSync::telemetryTimestampReadable();
    doc["timestampTimezone"] = TimeSync::telemetryTimezoneLabel();
  }

  static void serializeTelemetryDocument(const JsonDocument& doc, String& payload) {
    payload.reserve(measureJsonPretty(doc) + 1);
    serializeJsonPretty(doc, payload);
  }

  template <typename AddItemFn, typename HandleOversizeFn>
  static bool fillTelemetryChunk(JsonDocument& doc,
                                 JsonArray items,
                                 size_t totalCount,
                                 size_t startIndex,
                                 size_t& nextIndex,
                                 AddItemFn addItem,
                                 HandleOversizeFn handleOversizeItem) {
    nextIndex = startIndex;
    for (; nextIndex < totalCount; ++nextIndex) {
      addItem(items, nextIndex);
      if (measureJsonPretty(doc) > MAX_TELEMETRY_JSON_BYTES) {
        items.remove(items.size() - 1);
        if (items.size() == 0) {
          return handleOversizeItem(nextIndex, doc);
        }
        break;
      }
    }

    return items.size() > 0;
  }

  static bool tryParseNumericValue(const String& value, double& outValue) {
    String trimmed = value;
    trimmed.trim();
    if (trimmed.length() == 0) {
      return false;
    }

    char buffer[48];
    size_t length = trimmed.length();
    if (length >= sizeof(buffer)) {
      return false;
    }
    memcpy(buffer, trimmed.c_str(), length);
    buffer[length] = '\0';

    char* endPtr = nullptr;
    double parsed = strtod(buffer, &endPtr);
    if (endPtr == buffer || endPtr == nullptr) {
      return false;
    }
    while (*endPtr != '\0') {
      if (!isspace(static_cast<unsigned char>(*endPtr))) {
        return false;
      }
      ++endPtr;
    }
    outValue = parsed;
    return true;
  }

  static String normalizeReadingType(const String& field) {
    if (field == "DateTime") {
      return "date_time";
    }
    if (field == "Serial number") {
      return "serial_number";
    }
    if (field == "ID") {
      return "id";
    }

    String type;
    bool lastUnderscore = false;
    for (size_t index = 0; index < field.length(); ++index) {
      char ch = field[index];
      if (isalnum(static_cast<unsigned char>(ch))) {
        if (isupper(static_cast<unsigned char>(ch)) && index > 0 && islower(static_cast<unsigned char>(field[index - 1])) && !lastUnderscore) {
          type += '_';
        }
        type += static_cast<char>(tolower(static_cast<unsigned char>(ch)));
        lastUnderscore = false;
      } else if (!lastUnderscore) {
        type += '_';
        lastUnderscore = true;
      }
    }

    while (type.startsWith("_")) {
      type.remove(0, 1);
    }
    while (type.endsWith("_")) {
      type.remove(type.length() - 1);
    }
    return type;
  }

  static bool buildTelemetryDevicePayload(const AppSettings::SavedMbusDevice& savedDevice,
                                          const MBusGateway::TelegramResult& telegramResult,
                                          const std::vector<String>& selectedFieldKeys,
                                          TelemetryDevicePayload& payload) {
    payload.primaryAddress = savedDevice.primaryAddress;
    payload.secondaryAddress = savedDevice.secondaryAddress;
    payload.manufacturer = savedDevice.manufacturer.length() > 0 ? savedDevice.manufacturer : telegramResult.device.manufacturer;
    payload.type = savedDevice.type.length() > 0 ? savedDevice.type : telegramResult.device.type;
    payload.description = savedDevice.description;
    payload.id = "";
    payload.serialNumber = "";
    payload.readings.clear();

    std::vector<MBusGateway::TelegramRecord> records = selectableRecords(savedDevice, telegramResult);
    for (size_t index = 0; index < records.size(); ++index) {
      const MBusGateway::TelegramRecord& record = records[index];
      bool selected = false;
      for (size_t selectedIndex = 0; selectedIndex < selectedFieldKeys.size(); ++selectedIndex) {
        if (selectedFieldKeys[selectedIndex] == record.key) {
          selected = true;
          break;
        }
      }
      if (!selected) {
        continue;
      }

      if (record.quantity == "ID") {
        payload.id = record.value;
        continue;
      }
      if (record.quantity == "Serial number") {
        payload.serialNumber = record.value;
        continue;
      }

      TelemetryReading reading;
      reading.type = normalizeReadingType(record.quantity);
      reading.valueText = record.value;
      reading.numericValue = 0.0;
      reading.numeric = tryParseNumericValue(record.value, reading.numericValue);
      reading.unit = record.unit;
      payload.readings.push_back(reading);
    }

    return payload.id.length() > 0 || payload.serialNumber.length() > 0 || !payload.readings.empty();
  }

  static void addTelemetryDeviceToJson(JsonArray dataArray, const TelemetryDevicePayload& devicePayload) {
    JsonObject device = dataArray.add<JsonObject>();
    device["primary"] = devicePayload.primaryAddress;
    device["secondary"] = devicePayload.secondaryAddress;
    device["manufacturer"] = devicePayload.manufacturer;
    device["type"] = devicePayload.type;
    device["description"] = devicePayload.description;
    if (devicePayload.id.length() > 0) {
      device["id"] = devicePayload.id;
    }
    if (devicePayload.serialNumber.length() > 0) {
      device["serialNumber"] = devicePayload.serialNumber;
    }

    JsonArray readings = device["readings"].to<JsonArray>();
    for (size_t index = 0; index < devicePayload.readings.size(); ++index) {
      const TelemetryReading& reading = devicePayload.readings[index];
      JsonObject item = readings.add<JsonObject>();
      item["type"] = reading.type;
      if (reading.numeric) {
        item["value"] = reading.numericValue;
      } else {
        item["value"] = reading.valueText;
      }
      if (reading.unit.length() > 0) {
        item["unit"] = reading.unit;
      }
    }
  }

  static bool publishTelemetryChunk(const std::vector<TelemetryDevicePayload>& devices,
                                    size_t startIndex,
                                    size_t& nextIndex,
                                    std::vector<TelemetryLogEntry>& logs) {
    JsonDocument doc;
    initTelemetryEnvelope(doc);
    JsonArray data = doc["data"].to<JsonArray>();

    bool hasItems = fillTelemetryChunk(
      doc,
      data,
      devices.size(),
      startIndex,
      nextIndex,
      [&](JsonArray items, size_t index) {
        addTelemetryDeviceToJson(items, devices[index]);
      },
      [&](size_t& oversizedIndex, JsonDocument&) {
        addTelemetryLog(logs,
                        "error",
                        devices[oversizedIndex].primaryAddress,
                        devices[oversizedIndex].secondaryAddress,
                        devices[oversizedIndex].description,
                        "Telemetry payload for one device exceeds MQTT chunk limit.");
        ++oversizedIndex;
        return false;
      });

    if (!hasItems) {
      return false;
    }

    String payload;
    serializeTelemetryDocument(doc, payload);
    if (!MQTTClient::publishTelemetry(payload.c_str())) {
      addTelemetryLog(logs, "error", -1, "", "", "Failed to publish telemetry payload to MQTT.");
      return false;
    }
    return true;
  }

  static void publishTelemetryLogs(const std::vector<TelemetryLogEntry>& logs) {
    if (logs.empty()) {
      return;
    }

    size_t index = 0;
    while (index < logs.size()) {
      JsonDocument doc;
      initTelemetryEnvelope(doc);
      JsonArray items = doc["entries"].to<JsonArray>();

      size_t nextIndex = index;
      bool hasItems = fillTelemetryChunk(
        doc,
        items,
        logs.size(),
        index,
        nextIndex,
        [&](JsonArray entries, size_t entryIndex) {
          JsonObject item = entries.add<JsonObject>();
          item["level"] = logs[entryIndex].level;
          if (logs[entryIndex].primaryAddress >= 0) {
            item["primary"] = logs[entryIndex].primaryAddress;
          }
          if (logs[entryIndex].secondaryAddress.length() > 0) {
            item["secondary"] = logs[entryIndex].secondaryAddress;
          }
          if (logs[entryIndex].description.length() > 0) {
            item["description"] = logs[entryIndex].description;
          }
          item["message"] = logs[entryIndex].message;
        },
        [&](size_t& oversizedIndex, JsonDocument& oversizedDoc) {
          String payload;
          serializeTelemetryDocument(oversizedDoc, payload);
          MQTTClient::publishTelemetryLog(payload.c_str());
          ++oversizedIndex;
          return false;
        });

      if (hasItems) {
        String payload;
        serializeTelemetryDocument(doc, payload);
        MQTTClient::publishTelemetryLog(payload.c_str());
      }

      if (nextIndex <= index) {
        break;
      }
      index = nextIndex;
    }
  }

  static void publishAutoRefreshTelemetry(const std::vector<TelemetryDevicePayload>& devices,
                                          std::vector<TelemetryLogEntry>& logs) {
    if (!TimeSync::isSynchronized()) {
      addTelemetryLog(logs, "warning", -1, "", "", "UTC time is not synchronized; telemetry timestamp is zero or empty.");
    }

    size_t index = 0;
    while (index < devices.size()) {
      size_t nextIndex = index;
      publishTelemetryChunk(devices, index, nextIndex, logs);
      if (nextIndex <= index) {
        break;
      }
      index = nextIndex;
    }

    publishTelemetryLogs(logs);
  }

  static uint32_t parseDurationSeconds(const String& input) {
    String text = input;
    text.trim();
    text.toLowerCase();
    if (text.length() == 0) {
      return 0;
    }

    char suffix = text[text.length() - 1];
    uint32_t multiplier = 1;
    if (suffix == 's' || suffix == 'm' || suffix == 'h') {
      text.remove(text.length() - 1);
      if (suffix == 'm') {
        multiplier = 60;
      } else if (suffix == 'h') {
        multiplier = 3600;
      }
    }

    text.trim();
    if (text.length() == 0) {
      return 0;
    }
    return static_cast<uint32_t>(text.toInt()) * multiplier;
  }

  static bool parseEndpoint(const String& endpoint, String& host, uint16_t& port) {
    int separator = endpoint.lastIndexOf(':');
    if (separator <= 0 || separator >= endpoint.length() - 1) {
      return false;
    }

    host = endpoint.substring(0, separator);
    host.trim();
    String portText = endpoint.substring(separator + 1);
    portText.trim();
    long parsedPort = portText.toInt();
    if (host.length() == 0 || parsedPort <= 0 || parsedPort > 65535) {
      return false;
    }
    port = static_cast<uint16_t>(parsedPort);
    return true;
  }

  static void handleConfigMessage(const char* topic, const uint8_t* payload, unsigned int length) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload, length);
    if (error) {
      DBG_WARN(String("M-Bus config: invalid JSON on topic ") + (topic != nullptr ? topic : "") + ", error=" + error.c_str());
      return;
    }

    JsonVariantConst mbus = doc["mbus"];
    if (mbus.isNull() || !mbus.is<JsonObjectConst>()) {
      return;
    }

    bool changed = false;

    if (mbus["endpoint"].is<const char*>()) {
      String host;
      uint16_t port = 0;
      if (parseEndpoint(String(mbus["endpoint"].as<const char*>()), host, port)) {
        AppSettings::saveMbusGateway(host, port);
        DBG_INFO(String("M-Bus config: endpoint updated to ") + host + ":" + String(port));
        changed = true;
      } else {
        DBG_WARN("M-Bus config: invalid endpoint, expected host:port");
      }
    }

    JsonVariantConst pollInterval = mbus["pollInterval"];
    if (!pollInterval.isNull()) {
      uint32_t seconds = 0;
      if (pollInterval.is<const char*>()) {
        seconds = parseDurationSeconds(String(pollInterval.as<const char*>()));
      } else if (pollInterval.is<uint32_t>()) {
        seconds = pollInterval.as<uint32_t>();
      } else if (pollInterval.is<int>()) {
        int value = pollInterval.as<int>();
        seconds = value > 0 ? static_cast<uint32_t>(value) : 0;
      }

      uint16_t minutes = seconds == 0 ? 0 : static_cast<uint16_t>((seconds + 59UL) / 60UL);
      AppSettings::saveMbusSnapshotRefreshMinutes(minutes);
      DBG_INFO(String("M-Bus config: pollInterval updated to ") + String(seconds) + " s (~" + String(minutes) + " min)");
      changed = true;
    }

    if (!mbus["retryInterval"].isNull()) {
      DBG_INFO("M-Bus config: retryInterval received but not applied yet.");
    }

    if (!changed) {
      DBG_DEBUG("M-Bus config: message received, no supported M-Bus fields changed.");
    }
  }

  static void updateSnapshot(const AppSettings::SavedMbusDevice& savedDevice,
                             const MBusGateway::TelegramResult& telegramResult,
                             const std::vector<String>& selectedFieldKeys) {
    if (!telegramResult.success) {
      return;
    }

    CachedMbusSnapshot snapshot;
    snapshot.primaryAddress = savedDevice.primaryAddress;
    snapshot.secondaryAddress = savedDevice.secondaryAddress;
    snapshot.description = savedDevice.description;
    snapshot.manufacturer = savedDevice.manufacturer.length() > 0 ? savedDevice.manufacturer : telegramResult.device.manufacturer;
    snapshot.type = savedDevice.type.length() > 0 ? savedDevice.type : telegramResult.device.type;
    snapshot.id = telegramResult.device.id;
    snapshot.updatedAtMs = millis();

    std::vector<MBusGateway::TelegramRecord> records = selectableRecords(savedDevice, telegramResult);
    for (size_t index = 0; index < records.size(); ++index) {
      const MBusGateway::TelegramRecord& record = records[index];
      bool selected = false;
      for (size_t selectedIndex = 0; selectedIndex < selectedFieldKeys.size(); ++selectedIndex) {
        if (selectedFieldKeys[selectedIndex] == record.key) {
          selected = true;
          break;
        }
      }
      if (!selected) {
        continue;
      }

      CachedMbusValue value;
      value.field = record.quantity.length() > 0 ? record.quantity : String("Unknown");
      value.value = record.value;
      value.unit = record.unit;
      snapshot.values.push_back(value);
      if (snapshot.values.size() >= MAX_CACHED_MBUS_VALUES_PER_DEVICE) {
        break;
      }
    }

    lockSnapshots();
    int existingIndex = findMbusSnapshotIndex(savedDevice.primaryAddress, savedDevice.secondaryAddress);
    if (existingIndex >= 0) {
      lastMbusSnapshots[static_cast<size_t>(existingIndex)] = snapshot;
      unlockSnapshots();
      return;
    }

    if (lastMbusSnapshots.size() >= MAX_CACHED_MBUS_DEVICES) {
      lastMbusSnapshots.erase(lastMbusSnapshots.begin());
    }
    lastMbusSnapshots.push_back(snapshot);
    unlockSnapshots();
  }

  static bool copySnapshot(int primaryAddress, const String& secondaryAddress, CachedMbusSnapshot& outSnapshot) {
    lockSnapshots();
    int index = findMbusSnapshotIndex(primaryAddress, secondaryAddress);
    if (index >= 0) {
      outSnapshot = lastMbusSnapshots[static_cast<size_t>(index)];
      unlockSnapshots();
      return true;
    }
    unlockSnapshots();
    return false;
  }

  static std::vector<CachedMbusSnapshot> snapshots() {
    lockSnapshots();
    std::vector<CachedMbusSnapshot> copy = lastMbusSnapshots;
    unlockSnapshots();
    return copy;
  }

  class BackgroundSnapshotRefresher {
   public:
    BackgroundSnapshotRefresher() : mutex(nullptr), taskHandle(nullptr) {
      resetStatus();
    }

    bool start(bool autoRun, String& error) {
      ensureMutex();
      if (mutex == nullptr) {
        error = "Failed to initialize snapshot refresh mutex.";
        return false;
      }

      lock();
      if (status.active) {
        unlock();
        error = "Snapshot refresh is already running.";
        return false;
      }

      resetStatus();
      status.active = true;
      status.autoTriggered = autoRun;
      status.startedAtMs = millis();
      unlock();

      BaseType_t created = xTaskCreate(taskEntry, "mbus_snap", 14336, this, 1, &taskHandle);
      if (created != pdPASS) {
        lock();
        resetStatus();
        unlock();
        error = "Failed to start snapshot refresh task.";
        return false;
      }
      return true;
    }

    SnapshotRefreshStatus snapshot() {
      lock();
      SnapshotRefreshStatus copy = status;
      unlock();
      return copy;
    }

   private:
    SemaphoreHandle_t mutex;
    TaskHandle_t taskHandle;
    SnapshotRefreshStatus status;

    static void taskEntry(void* context) {
      BackgroundSnapshotRefresher* refresher = static_cast<BackgroundSnapshotRefresher*>(context);
      refresher->run();
      refresher->taskHandle = nullptr;
      vTaskDelete(nullptr);
    }

    void run() {
      std::vector<AppSettings::SavedMbusDevice> devices = AppSettings::mbusDevices();
      std::vector<AppSettings::SavedMbusDevice> selectedDevices;
      std::vector<TelemetryDevicePayload> telemetryDevices;
      std::vector<TelemetryLogEntry> telemetryLogs;
      for (size_t index = 0; index < devices.size(); ++index) {
        if (!AppSettings::mbusSelectedFieldKeys(devices[index].primaryAddress, devices[index].secondaryAddress).empty()) {
          selectedDevices.push_back(devices[index]);
        }
      }

      bool autoTriggeredRun = false;
      lock();
      autoTriggeredRun = status.autoTriggered;
      unlock();

      if (selectedDevices.size() > MAX_AUTO_REFRESH_MBUS_DEVICES) {
        addTelemetryLog(telemetryLogs,
                        "warning",
                        -1,
                        "",
                        "",
                        String("Saved device count exceeds limit; only first ") + String(MAX_AUTO_REFRESH_MBUS_DEVICES) + String(" devices will be refreshed and published."));
        selectedDevices.resize(MAX_AUTO_REFRESH_MBUS_DEVICES);
      }

      lock();
      status.totalDevices = static_cast<int>(selectedDevices.size());
      unlock();

      int updatedCount = 0;
      for (size_t index = 0; index < selectedDevices.size(); ++index) {
        const AppSettings::SavedMbusDevice& device = selectedDevices[index];
        std::vector<String> selectedFieldKeys = AppSettings::mbusSelectedFieldKeys(device.primaryAddress, device.secondaryAddress);
        MBusGateway::TelegramResult telegramResult = readDeviceTelegram(device.primaryAddress,
                                                                        device.secondaryAddress,
                                                                        ReadProfile::AutoRefresh);
        if (telegramResult.success) {
          AppSettings::SavedMbusDevice refreshedDevice = device;
          refreshedDevice.manufacturer = telegramResult.device.manufacturer;
          refreshedDevice.type = telegramResult.device.type;
          updateSnapshot(refreshedDevice, telegramResult, selectedFieldKeys);
          TelemetryDevicePayload telemetryDevice;
          if (buildTelemetryDevicePayload(refreshedDevice, telegramResult, selectedFieldKeys, telemetryDevice)) {
            telemetryDevices.push_back(telemetryDevice);
          }
          ++updatedCount;
        } else {
          addTelemetryLog(telemetryLogs,
                          "warning",
                          device.primaryAddress,
                          device.secondaryAddress,
                          device.description,
                          telegramResult.error.length() > 0 ? telegramResult.error : String("Failed to read device telegram."));
        }

        lock();
        status.processedDevices = static_cast<int>(index + 1);
        status.updatedDevices = updatedCount;
        unlock();
        vTaskDelay(pdMS_TO_TICKS(100));
      }

      lock();
      status.active = false;
      status.lastSuccess = !selectedDevices.empty() && updatedCount > 0;
      status.finishedAtMs = millis();
      status.updatedDevices = updatedCount;
      if (selectedDevices.empty()) {
        status.error = "No saved selected fields to refresh.";
      } else if (updatedCount == 0) {
        status.error = "No devices returned fresh snapshot data.";
      } else {
        status.error = "";
      }
      unlock();

      time_t refreshEpoch = TimeSync::currentTimestamp();
      if (refreshEpoch > 0) {
        AppSettings::saveMbusSnapshotLastRefreshEpoch(static_cast<uint32_t>(refreshEpoch));
      }

      if (autoTriggeredRun) {
        // Append memory stats and uptime as a log entry
        {
          char memBuf[196];
          uint32_t free8    = heap_caps_get_free_size(MALLOC_CAP_8BIT);
          uint32_t largest8 = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
          uint32_t minFree8 = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
          uint32_t total    = heap_caps_get_total_size(MALLOC_CAP_8BIT);
          unsigned long uptimeMs = millis();
          unsigned long uptimeSec = uptimeMs / 1000UL;
          unsigned long uptimeMin = uptimeSec / 60UL;
          unsigned long uptimeHr  = uptimeMin / 60UL;
          snprintf(memBuf, sizeof(memBuf),
            "MEM: total=%u free=%u (min=%u) largest=%u uptime=%luh%lum",
            total, free8, minFree8, largest8, uptimeHr, uptimeMin % 60UL);
          addTelemetryLog(telemetryLogs, "info", -1, "", "", String(memBuf));
        }
        publishAutoRefreshTelemetry(telemetryDevices, telemetryLogs);
      } else {
        publishTelemetryLogs(telemetryLogs);
      }
    }

    void resetStatus() {
      status.active = false;
      status.lastSuccess = false;
      status.autoTriggered = false;
      status.totalDevices = 0;
      status.processedDevices = 0;
      status.updatedDevices = 0;
      status.startedAtMs = 0;
      status.finishedAtMs = 0;
      status.error = "";
    }

    void ensureMutex() {
      if (mutex == nullptr) {
        mutex = xSemaphoreCreateMutex();
      }
    }

    void lock() {
      ensureMutex();
      if (mutex != nullptr) {
        xSemaphoreTake(mutex, portMAX_DELAY);
      }
    }

    void unlock() {
      if (mutex != nullptr) {
        xSemaphoreGive(mutex);
      }
    }
  };

  static BackgroundSnapshotRefresher snapshotRefresher;

  class BackgroundScanner {
   public:
    BackgroundScanner() : mutex(nullptr), taskHandle(nullptr), mode(Mode::None), port(0), primaryStart(0), primaryEnd(0) {
      resetStatus();
    }

    bool startPrimary(const String& gatewayHost, uint16_t gatewayPort, int startAddress, int endAddress, String& error) {
      if (startAddress < MBusGateway::PRIMARY_MIN_ADDRESS || endAddress > MBusGateway::PRIMARY_MAX_ADDRESS || startAddress > endAddress) {
        error = "Primary range must be between 1 and 250.";
        return false;
      }

      return start(Mode::Primary, gatewayHost, gatewayPort, startAddress, endAddress, "", "", error);
    }

    bool startSecondary(const String& gatewayHost,
                        uint16_t gatewayPort,
                        const String& startAddress,
                        const String& endAddress,
                        String& error) {
      if (!MBusGateway::isSecondaryRangeValid(startAddress, endAddress)) {
        error = "Secondary range must use two 16-character hexadecimal addresses.";
        return false;
      }

      return start(Mode::Secondary,
                   gatewayHost,
                   gatewayPort,
                   1,
                   MBusGateway::MAX_SECONDARY_DISCOVERY,
                   startAddress,
                   endAddress,
                   error);
    }

    void requestStop() {
      lock();
      if (status.active) {
        status.stopRequested = true;
      }
      unlock();
    }

    ScanStatus snapshot() {
      lock();
      ScanStatus copy = status;
      unlock();
      return copy;
    }

   private:
    enum class Mode {
      None,
      Primary,
      Secondary,
    };

    SemaphoreHandle_t mutex;
    TaskHandle_t taskHandle;
    Mode mode;
    String host;
    uint16_t port;
    int primaryStart;
    int primaryEnd;
    String secondaryStart;
    String secondaryEnd;
    ScanStatus status;

    bool start(Mode nextMode,
               const String& gatewayHost,
               uint16_t gatewayPort,
               int startAddress,
               int endAddress,
               const String& secondaryStartAddress,
               const String& secondaryEndAddress,
               String& error) {
      ensureMutex();
      if (mutex == nullptr) {
        error = "Failed to initialize scan mutex.";
        return false;
      }

      lock();
      if (status.active) {
        unlock();
        error = "Scan is already running.";
        return false;
      }

      mode = nextMode;
      host = gatewayHost;
      port = gatewayPort;
      primaryStart = startAddress;
      primaryEnd = endAddress;
      secondaryStart = secondaryStartAddress;
      secondaryEnd = secondaryEndAddress;
      resetStatus();
      status.active = true;
      status.mode = nextMode == Mode::Primary ? "Primary" : "Secondary";
      status.progressLabel = nextMode == Mode::Primary ? "Primary address" : "Secondary prefix probe";
      status.rangeStart = startAddress;
      status.rangeEnd = endAddress;
      status.totalSteps = nextMode == Mode::Primary ? (endAddress - startAddress + 1) : 0;
      unlock();

      BaseType_t created = xTaskCreate(taskEntry, "mbus_scan", 12288, this, 1, &taskHandle);
      if (created != pdPASS) {
        lock();
        resetStatus();
        unlock();
        error = "Failed to start background scan task.";
        return false;
      }
      return true;
    }

    static void taskEntry(void* context) {
      BackgroundScanner* scanner = static_cast<BackgroundScanner*>(context);
      scanner->run();
      scanner->taskHandle = nullptr;
      vTaskDelete(nullptr);
    }

    void run() {
      if (mode == Mode::Primary) {
        runPrimary();
      } else if (mode == Mode::Secondary) {
        runSecondary();
      } else {
        finish(false, "Unknown scan mode.");
      }
    }

    void runPrimary() {
      DBG_INFO(String("M-Bus scan: start primary range ") + String(primaryStart) + "-" + String(primaryEnd));
      MBusGateway::Client client;
      if (!client.connect(host, port)) {
        DBG_WARN("M-Bus scan: failed to connect to TCP gateway for primary scan");
        finish(false, "Failed to connect to the M-Bus TCP gateway.");
        return;
      }

      for (int address = primaryStart; address <= primaryEnd; ++address) {
        if (shouldStop()) {
          client.disconnect();
          DBG_WARN(String("M-Bus scan: primary scan stopped at address ") + String(address));
          finish(false, "Scan stopped by user.");
          return;
        }

        if (((address - primaryStart) % 10) == 0 || address == primaryStart) {
          DBG_INFO(String("M-Bus scan: primary polling address ") + String(address));
        }
        setProgress(address, address - primaryStart);
        std::vector<uint8_t> response = client.reqUD2(static_cast<uint8_t>(address),
                                                      MBusGateway::PRIMARY_SCAN_RESPONSE_TIMEOUT_MS,
                                                      MBusGateway::PRIMARY_SCAN_RESPONSE_IDLE_MS);
        if (MBusGateway::hasLongFrameHeader(response, 15)) {
          MBusGateway::DeviceInfo info = MBusGateway::parseDeviceInfo(response);
          info.primaryAddress = address;
          if (info.secondaryAddress.length() > 0) {
            addDevice(info);
            DBG_INFO(String("M-Bus scan: found device ") + MBusGateway::describeDevice(info));
          }
        }
        setProgress(address, address - primaryStart + 1);
        vTaskDelay(pdMS_TO_TICKS(MBusGateway::PRIMARY_SCAN_ADDRESS_DELAY_MS));
      }

      client.disconnect();
      DBG_INFO(String("M-Bus scan: primary range finished, found ") + String(status.devices.size()) + " device(s)");
      finish(true, "");
    }

    void runSecondary() {
      DBG_INFO(String("M-Bus scan: start secondary range ") + secondaryStart + "-" + secondaryEnd);
      MBusGateway::Client client;
      if (!client.connect(host, port)) {
        DBG_WARN("M-Bus scan: failed to connect to TCP gateway for secondary scan");
        finish(false, "Failed to connect to the M-Bus TCP gateway.");
        return;
      }

      int probeCount = 0;
      std::vector<String> discoveredSecondaries;
      std::vector<String> idPrefixes;
      idPrefixes.push_back("");

      while (!idPrefixes.empty() && static_cast<int>(discoveredSecondaries.size()) < MBusGateway::MAX_SECONDARY_DISCOVERY) {
        if (shouldStop()) {
          client.disconnect();
          DBG_WARN(String("M-Bus scan: secondary scan stopped after ") + String(probeCount) + " prefix probe(s)");
          finish(false, "Scan stopped by user.");
          return;
        }

        String prefix = idPrefixes.back();
        idPrefixes.pop_back();
        prefix.toUpperCase();

        if (!MBusGateway::secondaryPrefixOverlapsRange(prefix, secondaryStart, secondaryEnd)) {
          continue;
        }

        ++probeCount;
        setProgress(probeCount, probeCount);

        String pattern = MBusGateway::buildSecondaryPattern(prefix);
        std::vector<uint8_t> selectionResponse = MBusGateway::selectBySecondaryPattern(client, pattern);
        DBG_DEBUG(String("M-Bus scan: secondary ID probe prefix=") + (prefix.length() > 0 ? prefix : String("*")) +
                  String(", pattern=") + pattern +
                  String(", response=") + MBusGateway::bytesToHex(selectionResponse));
        if (!MBusGateway::isAck(selectionResponse) && prefix.length() == 0) {
          vTaskDelay(pdMS_TO_TICKS(200));
          selectionResponse = MBusGateway::selectBySecondaryPattern(client, pattern);
          DBG_DEBUG(String("M-Bus scan: secondary root retry response=") + MBusGateway::bytesToHex(selectionResponse));
        }
        if (!MBusGateway::isAck(selectionResponse)) {
          vTaskDelay(pdMS_TO_TICKS(20));
          continue;
        }

        if (prefix.length() < 8) {
          static const char* digits = "0123456789";
          for (int index = 9; index >= 0; --index) {
            String nextPrefix = prefix + digits[index];
            if (MBusGateway::secondaryPrefixOverlapsRange(nextPrefix, secondaryStart, secondaryEnd)) {
              idPrefixes.push_back(nextPrefix);
            }
          }
          vTaskDelay(pdMS_TO_TICKS(20));
          continue;
        }

        vTaskDelay(pdMS_TO_TICKS(180));
        std::vector<uint8_t> dataResponse = client.reqUD2(0xFD);
        DBG_DEBUG(String("M-Bus scan: reqUD2(FD) for secondary ID ") + prefix +
                  String(" response=") + MBusGateway::bytesToHex(dataResponse));
        if (!MBusGateway::hasLongFrameHeader(dataResponse, 15)) {
          DBG_WARN(String("M-Bus scan: selected secondary ID ") + prefix +
                   String(" did not return a long frame, response=") + MBusGateway::bytesToHex(dataResponse));
          vTaskDelay(pdMS_TO_TICKS(80));
          continue;
        }

        String discoveredSecondary = MBusGateway::extractSecondaryAddress(dataResponse);
        DBG_INFO(String("M-Bus scan: discovered secondary=") + discoveredSecondary);
        if (discoveredSecondary.length() != 16) {
          DBG_WARN(String("M-Bus scan: invalid discovered secondary, response=") + MBusGateway::bytesToHex(dataResponse));
          vTaskDelay(pdMS_TO_TICKS(80));
          continue;
        }

        bool alreadyDiscovered = false;
        for (size_t discoveredIndex = 0; discoveredIndex < discoveredSecondaries.size(); ++discoveredIndex) {
          if (discoveredSecondaries[discoveredIndex] == discoveredSecondary) {
            alreadyDiscovered = true;
            break;
          }
        }
        if (alreadyDiscovered) {
          continue;
        }

        MBusGateway::DeviceInfo info = MBusGateway::parseDeviceInfo(dataResponse);
        if (MBusGateway::secondaryInRange(info.secondaryAddress, secondaryStart, secondaryEnd)) {
          discoveredSecondaries.push_back(info.secondaryAddress);
          addDevice(info);
          DBG_INFO(String("M-Bus scan: found device ") + MBusGateway::describeDevice(info));
        }

        vTaskDelay(pdMS_TO_TICKS(80));
      }

      client.disconnect();
      if (static_cast<int>(discoveredSecondaries.size()) >= MBusGateway::MAX_SECONDARY_DISCOVERY) {
        DBG_WARN(String("M-Bus scan: secondary discovery stopped at device limit ") + String(MBusGateway::MAX_SECONDARY_DISCOVERY));
      }
      DBG_INFO(String("M-Bus scan: secondary prefix search finished after ") + String(probeCount) +
               String(" probe(s), found ") + String(status.devices.size()) + " device(s)");
      finish(true, "");
    }

    void resetStatus() {
      status.active = false;
      status.stopRequested = false;
      status.finished = false;
      status.success = false;
      status.error = "";
      status.mode = "";
      status.progressLabel = "";
      status.currentValue = 0;
      status.rangeStart = 0;
      status.rangeEnd = 0;
      status.totalSteps = 0;
      status.completedSteps = 0;
      status.devices.clear();
    }

    void ensureMutex() {
      if (mutex == nullptr) {
        mutex = xSemaphoreCreateMutex();
      }
    }

    void lock() {
      ensureMutex();
      if (mutex != nullptr) {
        xSemaphoreTake(mutex, portMAX_DELAY);
      }
    }

    void unlock() {
      if (mutex != nullptr) {
        xSemaphoreGive(mutex);
      }
    }

    bool shouldStop() {
      lock();
      bool requested = status.stopRequested;
      unlock();
      return requested;
    }

    void setProgress(int currentValue, int completedSteps) {
      lock();
      status.currentValue = currentValue;
      status.completedSteps = completedSteps;
      unlock();
    }

    void addDevice(const MBusGateway::DeviceInfo& info) {
      lock();
      MBusGateway::appendUnique(status.devices, info);
      unlock();
    }

    void finish(bool success, const String& errorMessage) {
      lock();
      status.active = false;
      status.finished = true;
      status.success = success;
      status.error = errorMessage;
      if (status.totalSteps > 0 && success && status.completedSteps < status.totalSteps) {
        status.completedSteps = status.totalSteps;
      }
      unlock();
    }
  };

  static BackgroundScanner backgroundScanner;

  static SnapshotRefreshStatus snapshotRefreshStatus() {
    return snapshotRefresher.snapshot();
  }

  static bool startSnapshotRefresh(bool autoRun, String& error) {
    return snapshotRefresher.start(autoRun, error);
  }

  static ScanStatus scanStatus() {
    return backgroundScanner.snapshot();
  }

  static std::vector<MBusGateway::DeviceInfo> scanDevices() {
    ScanStatus status = scanStatus();
    return status.active || status.finished ? status.devices : lastMbusScanResults;
  }

  static bool startPrimaryScan(int primaryStart, int primaryEnd, String& error) {
    AppSettings::Settings& settings = AppSettings::get();
    bool started = backgroundScanner.startPrimary(settings.mbusGatewayHost,
                                                  settings.mbusGatewayPort,
                                                  primaryStart,
                                                  primaryEnd,
                                                  error);
    if (started) {
      lastMbusScanResults.clear();
    }
    return started;
  }

  static bool startSecondaryScan(const String& secondaryStart, const String& secondaryEnd, String& error) {
    AppSettings::Settings& settings = AppSettings::get();
    bool started = backgroundScanner.startSecondary(settings.mbusGatewayHost,
                                                    settings.mbusGatewayPort,
                                                    secondaryStart,
                                                    secondaryEnd,
                                                    error);
    if (started) {
      lastMbusScanResults.clear();
    }
    return started;
  }

  static void requestStopScan() {
    backgroundScanner.requestStop();
  }

  static uint32_t autoRefreshIntervalSeconds(uint16_t intervalMinutes) {
    return static_cast<uint32_t>(intervalMinutes) * 60UL;
  }

  static uint32_t autoRefreshAlignmentWindowSeconds(uint32_t intervalSeconds) {
    uint32_t window = intervalSeconds / 4UL;
    if (window < 10UL) {
      return 10UL;
    }
    if (window > 75UL) {
      return 75UL;
    }
    return window;
  }

  static uint32_t alignedSlotStart(uint32_t epoch, uint32_t intervalSeconds) {
    if (intervalSeconds == 0) {
      return epoch;
    }
    return epoch - (epoch % intervalSeconds);
  }

  static bool shouldRunAlignedAutoRefresh(const AppSettings::Settings& settings, time_t nowEpoch) {
    uint32_t intervalSeconds = autoRefreshIntervalSeconds(settings.mbusSnapshotRefreshMinutes);
    if (intervalSeconds == 0 || nowEpoch <= 0) {
      return false;
    }

    uint32_t now = static_cast<uint32_t>(nowEpoch);
    uint32_t slotStart = alignedSlotStart(now, intervalSeconds);
    uint32_t secondsIntoSlot = now - slotStart;
    if (secondsIntoSlot > autoRefreshAlignmentWindowSeconds(intervalSeconds)) {
      return false;
    }

    return settings.mbusSnapshotLastRefreshEpoch == 0 ||
           settings.mbusSnapshotLastRefreshEpoch < slotStart;
  }

  static String formatUtcEpoch(uint32_t epoch) {
    time_t value = static_cast<time_t>(epoch);
    struct tm timeInfo;
    gmtime_r(&value, &timeInfo);
    char buffer[24];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", &timeInfo);
    return String(buffer) + " UTC";
  }

  static String nextAutoRefreshLabel() {
    AppSettings::Settings& settings = AppSettings::get();
    if (settings.mbusSnapshotRefreshMinutes == 0) {
      return "disabled";
    }

    uint32_t intervalSeconds = autoRefreshIntervalSeconds(settings.mbusSnapshotRefreshMinutes);
    time_t nowEpoch = TimeSync::currentTimestamp();
    if (nowEpoch > 0) {
      uint32_t now = static_cast<uint32_t>(nowEpoch);
      uint32_t currentSlot = alignedSlotStart(now, intervalSeconds);
      uint32_t nextSlot = currentSlot + intervalSeconds;
      if (settings.mbusSnapshotLastRefreshEpoch < currentSlot &&
          now - currentSlot <= autoRefreshAlignmentWindowSeconds(intervalSeconds)) {
        nextSlot = currentSlot;
      }
      return formatUtcEpoch(nextSlot);
    }

    if (!autoRefreshFallbackInitialized) {
      return "after boot interval";
    }

    unsigned long intervalMs = static_cast<unsigned long>(settings.mbusSnapshotRefreshMinutes) * 60000UL;
    unsigned long elapsedMs = millis() - autoRefreshFallbackStartedAtMs;
    if (elapsedMs >= intervalMs) {
      return "due when idle";
    }
    unsigned long remainingSeconds = (intervalMs - elapsedMs) / 1000UL;
    if (remainingSeconds < 60UL) {
      return String(remainingSeconds) + " s";
    }
    return String(remainingSeconds / 60UL) + " min";
  }

  static void loop() {
    ScanStatus scan = scanStatus();
    if (!scan.active && scan.finished) {
      lastMbusScanResults = scan.devices;
    }

    AppSettings::Settings& settings = AppSettings::get();
    if (settings.mbusSnapshotRefreshMinutes > 0) {
      SnapshotRefreshStatus refreshStatus = snapshotRefreshStatus();
      if (!refreshStatus.active) {
        unsigned long intervalMs = static_cast<unsigned long>(settings.mbusSnapshotRefreshMinutes) * 60000UL;
        time_t nowEpoch = TimeSync::currentTimestamp();
        bool shouldRun = false;

        if (nowEpoch > 0) {
          shouldRun = shouldRunAlignedAutoRefresh(settings, nowEpoch);
        } else {
          if (!autoRefreshFallbackInitialized) {
            autoRefreshFallbackInitialized = true;
            autoRefreshFallbackStartedAtMs = millis();
          }

          unsigned long referenceMs = refreshStatus.finishedAtMs > 0 ? refreshStatus.finishedAtMs : autoRefreshFallbackStartedAtMs;
          shouldRun = millis() - referenceMs >= intervalMs;
        }

        if (shouldRun) {
          String error;
          if (!startSnapshotRefresh(true, error) && error.length() > 0) {
            DBG_WARN(String("M-Bus snapshot auto refresh start failed: ") + error);
          }
        }
      }
    }
  }
}

#endif
