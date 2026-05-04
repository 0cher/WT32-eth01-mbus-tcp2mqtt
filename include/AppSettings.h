#ifndef APP_SETTINGS_H
#define APP_SETTINGS_H

#include <Arduino.h>
#include <Preferences.h>
#include <vector>
#include "Config.h"

namespace AppSettings {

  struct SavedMbusDevice {
    int primaryAddress;
    String secondaryAddress;
    String description;
    String manufacturer;
    String type;
  };

  struct Settings {
    String deviceSerial;
    bool networkUseDhcp;
    String networkIp;
    String networkGateway;
    String networkSubnet;
    String networkDns1;
    String networkDns2;
    String mqttHost;
    uint16_t mqttPort;
    String mqttPassword;
    String mqttUsername;
    String mqttCaCert;
    bool mqttUseCustomCa;
    uint16_t mqttKeepAliveSeconds;
    uint32_t mqttReconnectIntervalMs;
    String webUsername;
    String webPassword;
    String mbusGatewayHost;
    uint16_t mbusGatewayPort;
    uint16_t mbusSnapshotRefreshMinutes;
    uint32_t mbusSnapshotLastRefreshEpoch;
    int16_t timezoneOffsetMinutes;
    bool telemetryUseLocalTime;
    String mbusSavedDevices;
    String mbusSelectedFields;
    String mqttTopicTelemetry;
    String mqttTopicTelemetryLog;
    String mqttTopicConfig;
  };

  struct SavedMbusFieldSelection {
    int primaryAddress;
    String secondaryAddress;
    String fieldKey;
  };

  static Preferences preferences;
  static Settings settings;
  static bool initialized = false;

  static constexpr const char* KEY_DEVICE_SERIAL = "dev_sn";
  static constexpr const char* KEY_NETWORK_DHCP = "net_dhcp";
  static constexpr const char* KEY_NETWORK_IP = "net_ip";
  static constexpr const char* KEY_NETWORK_GATEWAY = "net_gw";
  static constexpr const char* KEY_NETWORK_SUBNET = "net_mask";
  static constexpr const char* KEY_NETWORK_DNS1 = "net_dns1";
  static constexpr const char* KEY_NETWORK_DNS2 = "net_dns2";
  static constexpr const char* KEY_MQTT_HOST = "mq_host";
  static constexpr const char* KEY_MQTT_PORT = "mq_port";
  static constexpr const char* KEY_MQTT_PASSWORD = "mq_pass";
  static constexpr const char* KEY_MQTT_USERNAME = "mq_user";
  static constexpr const char* KEY_MQTT_CA = "mq_ca";
  static constexpr const char* KEY_MQTT_USE_CUSTOM_CA = "mq_custca";
  static constexpr const char* KEY_MQTT_KEEPALIVE = "mq_keep";
  static constexpr const char* KEY_MQTT_RECONNECT = "mq_recon";
  static constexpr const char* KEY_WEB_USERNAME = "web_user";
  static constexpr const char* KEY_WEB_PASSWORD = "web_pass";
  static constexpr const char* KEY_MBUS_GATEWAY_HOST = "mb_host";
  static constexpr const char* KEY_MBUS_GATEWAY_PORT = "mb_port";
  static constexpr const char* KEY_MBUS_REFRESH_MINUTES = "mb_refmin";
  static constexpr const char* KEY_MBUS_REFRESH_EPOCH = "mb_refep";
  static constexpr const char* KEY_TIMEZONE_OFFSET = "tz_offset";
  static constexpr const char* KEY_TELEMETRY_LOCAL_TIME = "tel_local";
  static constexpr const char* KEY_MBUS_DEVICES = "mb_devs";
  static constexpr const char* KEY_MBUS_SELECTED_FIELDS = "mb_sel";
  static constexpr const char* KEY_MQTT_TOPIC_TELEMETRY = "mq_ttele";
  static constexpr const char* KEY_MQTT_TOPIC_TELEMETRY_LOG = "mq_ttellog";
  static constexpr const char* KEY_MQTT_TOPIC_CONFIG = "mq_tconf";

  static void begin();

  static String normalizePem(const String& value);
  static bool looksLikePemCertificate(const String& value);

  static String getStoredString(const char* key, const String& fallback) {
    if (!preferences.isKey(key)) {
      return fallback;
    }
    return preferences.getString(key, fallback);
  }

  static void persistNetworkSettings() {
    preferences.putBool(KEY_NETWORK_DHCP, settings.networkUseDhcp);
    preferences.putString(KEY_NETWORK_IP, settings.networkIp);
    preferences.putString(KEY_NETWORK_GATEWAY, settings.networkGateway);
    preferences.putString(KEY_NETWORK_SUBNET, settings.networkSubnet);
    preferences.putString(KEY_NETWORK_DNS1, settings.networkDns1);
    preferences.putString(KEY_NETWORK_DNS2, settings.networkDns2);
  }

  static void persistMqttSettings() {
    preferences.putString(KEY_MQTT_HOST, settings.mqttHost);
    preferences.putUShort(KEY_MQTT_PORT, settings.mqttPort);
    preferences.putString(KEY_MQTT_PASSWORD, settings.mqttPassword);
    preferences.putString(KEY_MQTT_USERNAME, settings.mqttUsername);
    preferences.putBool(KEY_MQTT_USE_CUSTOM_CA, settings.mqttUseCustomCa);
    if (settings.mqttUseCustomCa) {
      preferences.putString(KEY_MQTT_CA, settings.mqttCaCert);
    } else if (preferences.isKey(KEY_MQTT_CA)) {
      preferences.remove(KEY_MQTT_CA);
    }
    preferences.putUShort(KEY_MQTT_KEEPALIVE, settings.mqttKeepAliveSeconds);
    preferences.putUInt(KEY_MQTT_RECONNECT, settings.mqttReconnectIntervalMs);
  }

  static void persistMqttTopicSettings() {
    preferences.putString(KEY_MQTT_TOPIC_TELEMETRY, settings.mqttTopicTelemetry);
    preferences.putString(KEY_MQTT_TOPIC_TELEMETRY_LOG, settings.mqttTopicTelemetryLog);
    preferences.putString(KEY_MQTT_TOPIC_CONFIG, settings.mqttTopicConfig);
  }

  static void persistWebAuthSettings() {
    preferences.putString(KEY_WEB_USERNAME, settings.webUsername);
    preferences.putString(KEY_WEB_PASSWORD, settings.webPassword);
  }

  static void persistMbusGatewaySettings() {
    preferences.putString(KEY_MBUS_GATEWAY_HOST, settings.mbusGatewayHost);
    preferences.putUShort(KEY_MBUS_GATEWAY_PORT, settings.mbusGatewayPort);
  }

  static void persistTimeSettings() {
    preferences.putShort(KEY_TIMEZONE_OFFSET, settings.timezoneOffsetMinutes);
    preferences.putBool(KEY_TELEMETRY_LOCAL_TIME, settings.telemetryUseLocalTime);
  }

  static void persistMbusSelectedFields() {
    preferences.putString(KEY_MBUS_SELECTED_FIELDS, settings.mbusSelectedFields);
  }

  static void loadStoredDeviceSerial() {
    bool hasStoredDeviceSerial = preferences.isKey(KEY_DEVICE_SERIAL);
    settings.deviceSerial = getStoredString(KEY_DEVICE_SERIAL, settings.deviceSerial);
    if (!hasStoredDeviceSerial) {
      preferences.putString(KEY_DEVICE_SERIAL, settings.deviceSerial);
    }
  }

  static void loadStoredNetworkSettings() {
    settings.networkUseDhcp = preferences.getBool(KEY_NETWORK_DHCP, settings.networkUseDhcp);
    settings.networkIp = getStoredString(KEY_NETWORK_IP, settings.networkIp);
    settings.networkGateway = getStoredString(KEY_NETWORK_GATEWAY, settings.networkGateway);
    settings.networkSubnet = getStoredString(KEY_NETWORK_SUBNET, settings.networkSubnet);
    settings.networkDns1 = getStoredString(KEY_NETWORK_DNS1, settings.networkDns1);
    settings.networkDns2 = getStoredString(KEY_NETWORK_DNS2, settings.networkDns2);
  }

  static void loadStoredMqttSettings() {
    settings.mqttHost = getStoredString(KEY_MQTT_HOST, settings.mqttHost);
    settings.mqttPort = preferences.getUShort(KEY_MQTT_PORT, settings.mqttPort);
    settings.mqttPassword = getStoredString(KEY_MQTT_PASSWORD, settings.mqttPassword);
    settings.mqttCaCert = normalizePem(getStoredString(KEY_MQTT_CA, settings.mqttCaCert));
    settings.mqttUseCustomCa = preferences.getBool(KEY_MQTT_USE_CUSTOM_CA, false);
    settings.mqttKeepAliveSeconds = preferences.getUShort(KEY_MQTT_KEEPALIVE, settings.mqttKeepAliveSeconds);
    settings.mqttReconnectIntervalMs = preferences.getUInt(KEY_MQTT_RECONNECT, settings.mqttReconnectIntervalMs);
    settings.mqttUsername = getStoredString(KEY_MQTT_USERNAME, settings.mqttUsername);
  }

  static void loadStoredWebSettings() {
    settings.webUsername = getStoredString(KEY_WEB_USERNAME, settings.webUsername);
    settings.webPassword = getStoredString(KEY_WEB_PASSWORD, settings.deviceSerial);
  }

  static void loadStoredMbusSettings() {
    settings.mbusGatewayHost = getStoredString(KEY_MBUS_GATEWAY_HOST, settings.mbusGatewayHost);
    settings.mbusGatewayPort = preferences.getUShort(KEY_MBUS_GATEWAY_PORT, settings.mbusGatewayPort);
    settings.mbusSnapshotRefreshMinutes = preferences.getUShort(KEY_MBUS_REFRESH_MINUTES, settings.mbusSnapshotRefreshMinutes);
    settings.mbusSnapshotLastRefreshEpoch = preferences.getUInt(KEY_MBUS_REFRESH_EPOCH, settings.mbusSnapshotLastRefreshEpoch);
    settings.mbusSavedDevices = getStoredString(KEY_MBUS_DEVICES, settings.mbusSavedDevices);
    settings.mbusSelectedFields = getStoredString(KEY_MBUS_SELECTED_FIELDS, settings.mbusSelectedFields);
  }

  static void loadStoredTimeSettings() {
    settings.timezoneOffsetMinutes = preferences.getShort(KEY_TIMEZONE_OFFSET, settings.timezoneOffsetMinutes);
    settings.telemetryUseLocalTime = preferences.getBool(KEY_TELEMETRY_LOCAL_TIME, settings.telemetryUseLocalTime);
  }

  static void loadStoredTopicSettings() {
    settings.mqttTopicTelemetry = getStoredString(KEY_MQTT_TOPIC_TELEMETRY, settings.mqttTopicTelemetry);
    settings.mqttTopicTelemetryLog = getStoredString(KEY_MQTT_TOPIC_TELEMETRY_LOG, settings.mqttTopicTelemetryLog);
    settings.mqttTopicConfig = getStoredString(KEY_MQTT_TOPIC_CONFIG, settings.mqttTopicConfig);
  }

  static void validateStoredMqttCaSettings() {
    if (settings.mqttUseCustomCa && !looksLikePemCertificate(settings.mqttCaCert)) {
      settings.mqttUseCustomCa = false;
      settings.mqttCaCert = "";
      preferences.putBool(KEY_MQTT_USE_CUSTOM_CA, false);
      if (preferences.isKey(KEY_MQTT_CA)) {
        preferences.remove(KEY_MQTT_CA);
      }
    }
  }

  static String normalizePem(const String& value) {
    String pem = value;
    pem.replace("\r\n", "\n");
    pem.replace("\r", "\n");
    pem.trim();

    if (!pem.startsWith("-----BEGIN CERTIFICATE-----")) {
      return pem;
    }

    String normalized;
    normalized.reserve(pem.length() + 4);
    int start = 0;
    while (start < pem.length()) {
      int end = pem.indexOf('\n', start);
      String line = end == -1 ? pem.substring(start) : pem.substring(start, end);
      line.trim();
      if (line.length() > 0 && !line.startsWith("-----BEGIN") && !line.startsWith("-----END")) {
        line.replace(" ", "+");
      }
      normalized += line;
      normalized += "\n";
      if (end == -1) {
        break;
      }
      start = end + 1;
    }
    return normalized;
  }

  static String trimmedOrDefault(const String& value, const char* fallback) {
    String candidate = value;
    candidate.trim();
    return candidate.isEmpty() ? String(fallback) : candidate;
  }

  static bool looksLikePemCertificate(const String& value) {
    return value.indexOf("-----BEGIN CERTIFICATE-----") >= 0 &&
           value.indexOf("-----END CERTIFICATE-----") >= 0;
  }

  static String sanitizeStoredField(const String& value) {
    String sanitized = value;
    sanitized.replace("\r", " ");
    sanitized.replace("\n", " ");
    sanitized.replace("|", "/");
    sanitized.trim();
    return sanitized;
  }

  static String normalizeMbusSecondaryKey(const String& value) {
    String secondary = sanitizeStoredField(value);
    secondary.toUpperCase();
    return secondary;
  }

  static bool mbusIdentityMatches(int savedPrimaryAddress,
                                  const String& savedSecondaryAddress,
                                  int primaryAddress,
                                  const String& secondaryAddress) {
    String savedSecondary = normalizeMbusSecondaryKey(savedSecondaryAddress);
    String secondary = normalizeMbusSecondaryKey(secondaryAddress);
    if (secondary.length() > 0) {
      return savedSecondary == secondary;
    }
    return savedSecondary.length() == 0 && savedPrimaryAddress == primaryAddress;
  }

  static std::vector<SavedMbusDevice> parseSavedMbusDevices(const String& encoded) {
    std::vector<SavedMbusDevice> devices;
    int start = 0;
    while (start < encoded.length()) {
      int end = encoded.indexOf('\n', start);
      String line = end == -1 ? encoded.substring(start) : encoded.substring(start, end);
      line.trim();
      if (line.length() > 0) {
        int firstSep = line.indexOf('|');
        int secondSep = firstSep == -1 ? -1 : line.indexOf('|', firstSep + 1);
        if (firstSep > -1 && secondSep > -1) {
          int thirdSep = line.indexOf('|', secondSep + 1);
          int fourthSep = thirdSep == -1 ? -1 : line.indexOf('|', thirdSep + 1);
          SavedMbusDevice device;
          device.primaryAddress = line.substring(0, firstSep).toInt();
          device.secondaryAddress = line.substring(firstSep + 1, secondSep);
          if (thirdSep == -1) {
            device.description = line.substring(secondSep + 1);
            device.manufacturer = "";
            device.type = "";
          } else {
            device.description = line.substring(secondSep + 1, thirdSep);
            if (fourthSep == -1) {
              device.manufacturer = line.substring(thirdSep + 1);
              device.type = "";
            } else {
              device.manufacturer = line.substring(thirdSep + 1, fourthSep);
              device.type = line.substring(fourthSep + 1);
            }
          }
          device.secondaryAddress.trim();
          device.description.trim();
          device.manufacturer.trim();
          device.type.trim();
          devices.push_back(device);
        }
      }

      if (end == -1) {
        break;
      }
      start = end + 1;
    }
    return devices;
  }

  static String encodeSavedMbusDevices(const std::vector<SavedMbusDevice>& devices) {
    String encoded;
    for (size_t index = 0; index < devices.size(); ++index) {
      const SavedMbusDevice& device = devices[index];
      if (index > 0) {
        encoded += "\n";
      }
      encoded += String(device.primaryAddress);
      encoded += "|";
      encoded += sanitizeStoredField(device.secondaryAddress);
      encoded += "|";
      encoded += sanitizeStoredField(device.description);
      encoded += "|";
      encoded += sanitizeStoredField(device.manufacturer);
      encoded += "|";
      encoded += sanitizeStoredField(device.type);
    }
    return encoded;
  }

  static std::vector<SavedMbusFieldSelection> parseSavedMbusFieldSelections(const String& encoded) {
    std::vector<SavedMbusFieldSelection> items;
    int start = 0;
    while (start < encoded.length()) {
      int end = encoded.indexOf('\n', start);
      String line = end == -1 ? encoded.substring(start) : encoded.substring(start, end);
      line.trim();
      if (line.length() > 0) {
        int firstSep = line.indexOf('|');
        int secondSep = firstSep == -1 ? -1 : line.indexOf('|', firstSep + 1);
        if (firstSep > -1 && secondSep > -1) {
          SavedMbusFieldSelection item;
          item.primaryAddress = line.substring(0, firstSep).toInt();
          item.secondaryAddress = line.substring(firstSep + 1, secondSep);
          item.fieldKey = line.substring(secondSep + 1);
          item.secondaryAddress.trim();
          item.fieldKey.trim();
          items.push_back(item);
        }
      }

      if (end == -1) {
        break;
      }
      start = end + 1;
    }
    return items;
  }

  static String encodeSavedMbusFieldSelections(const std::vector<SavedMbusFieldSelection>& items) {
    String encoded;
    for (size_t index = 0; index < items.size(); ++index) {
      if (index > 0) {
        encoded += "\n";
      }
      encoded += String(items[index].primaryAddress);
      encoded += "|";
      encoded += sanitizeStoredField(items[index].secondaryAddress);
      encoded += "|";
      encoded += sanitizeStoredField(items[index].fieldKey);
    }
    return encoded;
  }

  static const String& deviceSerial() {
    begin();
    return settings.deviceSerial;
  }

  static const char* deviceSerialCStr() {
    begin();
    return settings.deviceSerial.c_str();
  }

  static bool setDeviceSerial(const String& serial) {
    begin();
    String candidate = serial;
    candidate.trim();
    if (candidate.isEmpty()) {
      return false;
    }
    settings.deviceSerial = candidate;
    preferences.putString(KEY_DEVICE_SERIAL, settings.deviceSerial);
    return true;
  }

  static bool usesCustomCa() {
    begin();
    return settings.mqttUseCustomCa && looksLikePemCertificate(settings.mqttCaCert);
  }

  static const char* customMqttCaCert() {
    begin();
    return settings.mqttCaCert.c_str();
  }

  static const char* mqttCaSourceLabel() {
    if (usesCustomCa()) return "custom";
    if (DEFAULT_MQTT_CA_CERT[0] != '\0') return "built-in";
    return "none";
  }

  static void applyDefaults() {
    settings.deviceSerial = DEVICE_SN;
    settings.networkUseDhcp = true;
    settings.networkIp = "";
    settings.networkGateway = "";
    settings.networkSubnet = "";
    settings.networkDns1 = "";
    settings.networkDns2 = "";
    settings.mqttHost = DEFAULT_MQTT_BROKER_HOST;
    settings.mqttPort = DEFAULT_MQTT_BROKER_PORT;
    settings.mqttPassword = DEFAULT_MQTT_PASSWORD;
    settings.mqttUsername = DEVICE_SN;
    settings.mqttCaCert = "";
    settings.mqttUseCustomCa = true;
    settings.mqttKeepAliveSeconds = DEFAULT_MQTT_KEEPALIVE_SECONDS;
    settings.mqttReconnectIntervalMs = DEFAULT_MQTT_RECONNECT_INTERVAL_MS;
    settings.webUsername = DEFAULT_WEB_USERNAME;
    settings.webPassword = DEFAULT_WEB_PASSWORD;
    settings.mbusGatewayHost = DEFAULT_MBUS_GATEWAY_HOST;
    settings.mbusGatewayPort = DEFAULT_MBUS_GATEWAY_PORT;
    settings.mbusSnapshotRefreshMinutes = 0;
    settings.mbusSnapshotLastRefreshEpoch = 0;
    settings.timezoneOffsetMinutes = 0;
    settings.telemetryUseLocalTime = false;
    settings.mbusSavedDevices = "";
    settings.mbusSelectedFields = "";
    settings.mqttTopicTelemetry = TOPIC_TELEMETRY_FORMAT;
    settings.mqttTopicTelemetryLog = TOPIC_TELEMETRY_LOG_FORMAT;
    settings.mqttTopicConfig = TOPIC_CONFIG_FORMAT;
  }

  static void begin() {
    if (initialized) {
      return;
    }

    applyDefaults();
    preferences.begin("wt32cfg", false);
    loadStoredDeviceSerial();
    loadStoredNetworkSettings();
    loadStoredMqttSettings();
    loadStoredWebSettings();
    loadStoredMbusSettings();
    loadStoredTimeSettings();
    loadStoredTopicSettings();
    validateStoredMqttCaSettings();
    initialized = true;
  }

  static Settings& get() {
    begin();
    return settings;
  }

  static void saveMqtt(const String& host,
                       uint16_t port,
                       const String& password,
                       const String& username,
                       const String& caCert,
                       bool useCustomCa,
                       uint16_t keepAliveSeconds,
                       uint32_t reconnectIntervalMs) {
    begin();
    settings.mqttHost = trimmedOrDefault(host, DEFAULT_MQTT_BROKER_HOST);
    settings.mqttPort = port == 0 ? DEFAULT_MQTT_BROKER_PORT : port;
    settings.mqttPassword = password;
    if (username.length() > 0) {
      settings.mqttUsername = username;
    }
    settings.mqttCaCert = normalizePem(caCert);
    settings.mqttUseCustomCa = useCustomCa && looksLikePemCertificate(settings.mqttCaCert);
    settings.mqttKeepAliveSeconds = keepAliveSeconds == 0 ? DEFAULT_MQTT_KEEPALIVE_SECONDS : keepAliveSeconds;
    settings.mqttReconnectIntervalMs = reconnectIntervalMs == 0 ? DEFAULT_MQTT_RECONNECT_INTERVAL_MS : reconnectIntervalMs;

    persistMqttSettings();
  }

  static void saveNetwork(bool useDhcp,
                          const String& ip,
                          const String& gateway,
                          const String& subnet,
                          const String& dns1,
                          const String& dns2) {
    begin();
    settings.networkUseDhcp = useDhcp;
    settings.networkIp = ip;
    settings.networkGateway = gateway;
    settings.networkSubnet = subnet;
    settings.networkDns1 = dns1;
    settings.networkDns2 = dns2;

    persistNetworkSettings();
  }

  static void saveMqttTopics(const String& telemetryTopic,
                              const String& telemetryLogTopic,
                              const String& configTopic) {
    begin();
    if (telemetryTopic.length() > 0) {
      settings.mqttTopicTelemetry = telemetryTopic;
    }
    if (telemetryLogTopic.length() > 0) {
      settings.mqttTopicTelemetryLog = telemetryLogTopic;
    }
    if (configTopic.length() > 0) {
      settings.mqttTopicConfig = configTopic;
    }
    persistMqttTopicSettings();
  }

  static const String& mqttTopicTelemetry() {
    begin();
    return settings.mqttTopicTelemetry;
  }

  static const String& mqttTopicTelemetryLog() {
    begin();
    return settings.mqttTopicTelemetryLog;
  }

  static const String& mqttTopicConfig() {
    begin();
    return settings.mqttTopicConfig;
  }

  static void saveWebAuth(const String& username, const String& password) {
    begin();
    settings.webUsername = trimmedOrDefault(username, DEFAULT_WEB_USERNAME);
    settings.webPassword = trimmedOrDefault(password, settings.deviceSerial.c_str());
    persistWebAuthSettings();
  }

  static void saveMbusGateway(const String& host, uint16_t port) {
    begin();
    settings.mbusGatewayHost = trimmedOrDefault(host, DEFAULT_MBUS_GATEWAY_HOST);
    settings.mbusGatewayPort = port == 0 ? DEFAULT_MBUS_GATEWAY_PORT : port;
    persistMbusGatewaySettings();
  }

  static void saveMbusSnapshotRefreshMinutes(uint16_t minutes) {
    begin();
    settings.mbusSnapshotRefreshMinutes = minutes;
    preferences.putUShort(KEY_MBUS_REFRESH_MINUTES, settings.mbusSnapshotRefreshMinutes);
  }

  static void saveMbusSnapshotLastRefreshEpoch(uint32_t epoch) {
    begin();
    settings.mbusSnapshotLastRefreshEpoch = epoch;
    preferences.putUInt(KEY_MBUS_REFRESH_EPOCH, settings.mbusSnapshotLastRefreshEpoch);
  }

  static void saveTimeSettings(int16_t timezoneOffsetMinutes, bool telemetryUseLocalTime) {
    begin();
    if (timezoneOffsetMinutes < -720) {
      timezoneOffsetMinutes = -720;
    }
    if (timezoneOffsetMinutes > 840) {
      timezoneOffsetMinutes = 840;
    }
    settings.timezoneOffsetMinutes = timezoneOffsetMinutes;
    settings.telemetryUseLocalTime = telemetryUseLocalTime;
    persistTimeSettings();
  }

  static std::vector<SavedMbusDevice> mbusDevices() {
    begin();
    return parseSavedMbusDevices(settings.mbusSavedDevices);
  }

  static void saveMbusDevices(const std::vector<SavedMbusDevice>& devices) {
    begin();
    settings.mbusSavedDevices = encodeSavedMbusDevices(devices);
    preferences.putString(KEY_MBUS_DEVICES, settings.mbusSavedDevices);
  }

  static std::vector<String> mbusSelectedFieldKeys(int primaryAddress, const String& secondaryAddress) {
    begin();
    std::vector<String> result;
    String secondary = normalizeMbusSecondaryKey(secondaryAddress);
    std::vector<SavedMbusFieldSelection> items = parseSavedMbusFieldSelections(settings.mbusSelectedFields);
    for (size_t index = 0; index < items.size(); ++index) {
      if (mbusIdentityMatches(items[index].primaryAddress, items[index].secondaryAddress, primaryAddress, secondary)) {
        result.push_back(items[index].fieldKey);
      }
    }
    return result;
  }

  static void saveMbusSelectedFieldKeys(int primaryAddress,
                                        const String& secondaryAddress,
                                        const std::vector<String>& fieldKeys) {
    begin();
    String secondary = normalizeMbusSecondaryKey(secondaryAddress);
    std::vector<SavedMbusFieldSelection> items = parseSavedMbusFieldSelections(settings.mbusSelectedFields);

    for (int index = static_cast<int>(items.size()) - 1; index >= 0; --index) {
      if (mbusIdentityMatches(items[index].primaryAddress, items[index].secondaryAddress, primaryAddress, secondary)) {
        items.erase(items.begin() + index);
      }
    }

    for (size_t index = 0; index < fieldKeys.size(); ++index) {
      String fieldKey = sanitizeStoredField(fieldKeys[index]);
      if (fieldKey.length() == 0) {
        continue;
      }
      SavedMbusFieldSelection item;
      item.primaryAddress = primaryAddress;
      item.secondaryAddress = secondary;
      item.fieldKey = fieldKey;
      items.push_back(item);
    }

    settings.mbusSelectedFields = encodeSavedMbusFieldSelections(items);
    persistMbusSelectedFields();
  }

  static void removeMbusSelectedFieldKeys(int primaryAddress, const String& secondaryAddress) {
    begin();
    String secondary = normalizeMbusSecondaryKey(secondaryAddress);
    std::vector<SavedMbusFieldSelection> items = parseSavedMbusFieldSelections(settings.mbusSelectedFields);

    for (int index = static_cast<int>(items.size()) - 1; index >= 0; --index) {
      if (mbusIdentityMatches(items[index].primaryAddress, items[index].secondaryAddress, primaryAddress, secondary)) {
        items.erase(items.begin() + index);
      }
    }

    settings.mbusSelectedFields = encodeSavedMbusFieldSelections(items);
    persistMbusSelectedFields();
  }

  static void addMbusDevice(int primaryAddress,
                            const String& secondaryAddress,
                            const String& description,
                            const String& manufacturer = String(),
                            const String& type = String()) {
    begin();
    std::vector<SavedMbusDevice> devices = parseSavedMbusDevices(settings.mbusSavedDevices);
    String secondary = normalizeMbusSecondaryKey(secondaryAddress);
    String note = sanitizeStoredField(description);
    String maker = sanitizeStoredField(manufacturer);
    String deviceType = sanitizeStoredField(type);

    for (size_t index = 0; index < devices.size(); ++index) {
      if (mbusIdentityMatches(devices[index].primaryAddress, devices[index].secondaryAddress, primaryAddress, secondary)) {
        devices[index].primaryAddress = primaryAddress;
        devices[index].secondaryAddress = secondary;
        devices[index].description = note;
        if (maker.length() > 0) {
          devices[index].manufacturer = maker;
        }
        if (deviceType.length() > 0) {
          devices[index].type = deviceType;
        }
        saveMbusDevices(devices);
        return;
      }
    }

    SavedMbusDevice device;
    device.primaryAddress = primaryAddress;
    device.secondaryAddress = secondary;
    device.description = note;
    device.manufacturer = maker;
    device.type = deviceType;
    devices.push_back(device);
    saveMbusDevices(devices);
  }

  static void removeMbusDevice(int primaryAddress, const String& secondaryAddress) {
    begin();
    std::vector<SavedMbusDevice> devices = parseSavedMbusDevices(settings.mbusSavedDevices);
    String secondary = normalizeMbusSecondaryKey(secondaryAddress);

    for (size_t index = 0; index < devices.size(); ++index) {
      if (mbusIdentityMatches(devices[index].primaryAddress, devices[index].secondaryAddress, primaryAddress, secondary)) {
        devices.erase(devices.begin() + index);
        break;
      }
    }
    saveMbusDevices(devices);
    removeMbusSelectedFieldKeys(primaryAddress, secondary);
  }

  static void resetWebAuthToDefaults() {
    begin();
    settings.webUsername = DEFAULT_WEB_USERNAME;
    settings.webPassword = settings.deviceSerial;
    persistWebAuthSettings();
  }

  // Wipe all NVS settings and re-apply code defaults. Next boot behaves like first flash.
  static void factoryReset() {
    bool wasInitialized = initialized;
    if (!wasInitialized) {
      begin();
    }
    preferences.end();
    preferences.begin("wt32cfg", false);
    preferences.clear();
    preferences.end();
    initialized = false;
  }
}

#endif // APP_SETTINGS_H
