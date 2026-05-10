#pragma once

#include <Arduino.h>
#include <NetworkClient.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <vector>
#include <stdlib.h>
#include "AppSettings.h"
#include "Debug.h"

namespace MBusGateway {
  static const unsigned long CONNECT_TIMEOUT_MS = 1500;
  static const unsigned long RESPONSE_TIMEOUT_MS = 2500;
  static const unsigned long RESPONSE_IDLE_MS = 250;
  static const unsigned long AUTO_REFRESH_RESPONSE_TIMEOUT_MS = 1800;
  static const unsigned long AUTO_REFRESH_RESPONSE_IDLE_MS = 180;
  // Primary scan must tolerate slower replies from some meters such as LUG.
  static const unsigned long PRIMARY_SCAN_RESPONSE_TIMEOUT_MS = RESPONSE_TIMEOUT_MS;
  static const unsigned long PRIMARY_SCAN_RESPONSE_IDLE_MS = RESPONSE_IDLE_MS;
  static const unsigned long SECONDARY_SELECTION_TIMEOUT_MS = 500;
  static const unsigned long SECONDARY_SELECTION_IDLE_MS = 120;
  static const unsigned long SECONDARY_DESELECT_TIMEOUT_MS = 300;
  static const unsigned long SECONDARY_DESELECT_IDLE_MS = 50;
  static const unsigned long PRIMARY_SCAN_ADDRESS_DELAY_MS = 150;
  static const int PRIMARY_MIN_ADDRESS = 1;
  static const int PRIMARY_MAX_ADDRESS = 250;
  static const int MAX_SECONDARY_DISCOVERY = 32;

  struct DeviceInfo {
    int primaryAddress;
    String secondaryAddress;
    String id;
    String manufacturer;
    String type;
    uint8_t version;
  };

  struct TelegramRecord {
    int index;
    int storage;
    String key;
    String function;
    String quantity;
    String unit;
    String value;
    String vif;
    String raw;
    bool recognized;
  };

  struct TelegramResult {
    bool success;
    String error;
    DeviceInfo device;
    String rawFrame;
    std::vector<TelegramRecord> records;
  };

  static String describeDevice(const DeviceInfo& info);

  static String buildHeaderRecordKey(const char* suffix) {
    return String("hdr:") + suffix;
  }

  static String byteToHex(uint8_t value) {
    char buffer[3];
    snprintf(buffer, sizeof(buffer), "%02X", value);
    return String(buffer);
  }

  static bool isAck(const std::vector<uint8_t>& response) {
    for (uint8_t value : response) {
      if (value == 0xE5) {
        return true;
      }
    }
    return false;
  }

  static bool hasLongFrameHeader(const std::vector<uint8_t>& response, size_t minSize = 6) {
    if (response.size() < minSize || response[0] != 0x68 || response[3] != 0x68) {
      return false;
    }

    size_t expectedLength = static_cast<size_t>(response[1]) + 6;
    return expectedLength <= response.size();
  }

  static bool isLongFrame(const std::vector<uint8_t>& response) {
    if (!hasLongFrameHeader(response)) {
      return false;
    }

    size_t expectedLength = static_cast<size_t>(response[1]) + 6;
    if (response.size() < expectedLength || response[expectedLength - 1] != 0x16) {
      return false;
    }

    uint8_t checksum = 0;
    for (size_t index = 4; index < 4 + response[1]; ++index) {
      checksum = static_cast<uint8_t>(checksum + response[index]);
    }
    return checksum == response[4 + response[1]];
  }

  static String extractSecondaryAddress(const std::vector<uint8_t>& response) {
    if (!hasLongFrameHeader(response, 15)) {
      return "";
    }

    String address;
    for (size_t index = 7; index <= 14; ++index) {
      address += byteToHex(response[index]);
    }
    return address;
  }

  static String decodeIdentification(const std::vector<uint8_t>& response) {
    if (!hasLongFrameHeader(response, 11)) {
      return "";
    }

    String value;
    for (int index = 10; index >= 7; --index) {
      uint8_t byte = response[index];
      value += char('0' + ((byte >> 4) & 0x0F));
      value += char('0' + (byte & 0x0F));
    }
    value.trim();
    return value;
  }

  static String decodeManufacturer(const std::vector<uint8_t>& response) {
    if (!hasLongFrameHeader(response, 13)) {
      return "";
    }

    uint16_t value = static_cast<uint16_t>(response[11]) | (static_cast<uint16_t>(response[12]) << 8);
    char manufacturer[4];
    manufacturer[0] = static_cast<char>(((value >> 10) & 0x1F) + 64);
    manufacturer[1] = static_cast<char>(((value >> 5) & 0x1F) + 64);
    manufacturer[2] = static_cast<char>((value & 0x1F) + 64);
    manufacturer[3] = '\0';
    return String(manufacturer);
  }

  static String mediumLabel(uint8_t medium) {
    switch (medium) {
      case 0x00: return "Other";
      case 0x01: return "Oil";
      case 0x02: return "Electricity";
      case 0x03: return "Gas";
      case 0x04: return "Heat";
      case 0x06: return "Warm Water";
      case 0x07: return "Water";
      case 0x08: return "Heat Cost";
      case 0x0A: return "Cooling";
      case 0x0B: return "Cooling Inlet";
      case 0x0C: return "Cooling Outlet";
      default: return String("0x") + byteToHex(medium);
    }
  }

  static DeviceInfo parseDeviceInfo(const std::vector<uint8_t>& response) {
    DeviceInfo info;
    info.primaryAddress = -1;
    if (hasLongFrameHeader(response) && response.size() > 5) {
      uint8_t primaryAddress = response[5];
      if (primaryAddress >= PRIMARY_MIN_ADDRESS && primaryAddress <= PRIMARY_MAX_ADDRESS) {
        info.primaryAddress = primaryAddress;
      }
    }
    info.secondaryAddress = extractSecondaryAddress(response);
    info.id = decodeIdentification(response);
    info.manufacturer = decodeManufacturer(response);
    info.version = response.size() > 13 ? response[13] : 0;
    info.type = response.size() > 14 ? mediumLabel(response[14]) : "";
    return info;
  }

  static uint64_t parseSecondaryNumber(const String& secondaryAddress) {
    if (secondaryAddress.length() == 0) {
      return 0;
    }

    char buffer[17];
    size_t copyLength = secondaryAddress.length();
    if (copyLength > 16) {
      copyLength = 16;
    }
    memcpy(buffer, secondaryAddress.c_str(), copyLength);
    buffer[copyLength] = '\0';
    return strtoull(buffer, nullptr, 16);
  }

  static bool isSecondaryHexText(const String& secondaryAddress) {
    for (size_t index = 0; index < secondaryAddress.length(); ++index) {
      char ch = secondaryAddress[index];
      bool isHex = (ch >= '0' && ch <= '9') ||
                   (ch >= 'A' && ch <= 'F') ||
                   (ch >= 'a' && ch <= 'f');
      if (!isHex) {
        return false;
      }
    }
    return true;
  }

  static bool isSecondaryRangeValid(const String& startAddress, const String& endAddress) {
    if (startAddress.length() == 0 && endAddress.length() == 0) {
      return true;
    }
    if (startAddress.length() != 16 || endAddress.length() != 16) {
      return false;
    }
    if (!isSecondaryHexText(startAddress) || !isSecondaryHexText(endAddress)) {
      return false;
    }
    return parseSecondaryNumber(startAddress) <= parseSecondaryNumber(endAddress);
  }

  static bool secondaryInRange(const String& address, const String& startAddress, const String& endAddress) {
    if (address.length() == 0) {
      return false;
    }
    if (startAddress.length() == 0 && endAddress.length() == 0) {
      return true;
    }

    uint64_t value = parseSecondaryNumber(address);
    uint64_t startValue = parseSecondaryNumber(startAddress);
    uint64_t endValue = parseSecondaryNumber(endAddress);
    return value >= startValue && value <= endValue;
  }

  static String bytesToHex(const std::vector<uint8_t>& bytes, size_t start = 0, size_t endExclusive = SIZE_MAX, const char* separator = " ") {
    String text;
    if (endExclusive > bytes.size()) {
      endExclusive = bytes.size();
    }
    for (size_t index = start; index < endExclusive; ++index) {
      if (index > start) {
        text += separator;
      }
      text += byteToHex(bytes[index]);
    }
    return text;
  }

  static String formatDoubleValue(double value, int decimals = 3) {
    if (decimals < 0) {
      decimals = 0;
    }
    if (decimals > 6) {
      decimals = 6;
    }
    char buffer[48];
    dtostrf(value, 0, decimals, buffer);
    String text(buffer);
    text.trim();
    int dot = text.indexOf('.');
    if (dot >= 0) {
      while (text.endsWith("0")) {
        text.remove(text.length() - 1);
      }
      if (text.endsWith(".")) {
        text.remove(text.length() - 1);
      }
    }
    return text;
  }

  static double applyScale10(double value, int scale10) {
    while (scale10 > 0) {
      value *= 10.0;
      --scale10;
    }
    while (scale10 < 0) {
      value /= 10.0;
      ++scale10;
    }
    return value;
  }

  static int64_t decodeSignedLE(const std::vector<uint8_t>& bytes, size_t start, size_t length) {
    uint64_t raw = 0;
    for (size_t index = 0; index < length && index < 8; ++index) {
      raw |= static_cast<uint64_t>(bytes[start + index]) << (index * 8);
    }
    if (length > 0 && length < 8) {
      uint64_t signBit = static_cast<uint64_t>(1) << (length * 8 - 1);
      if ((raw & signBit) != 0) {
        uint64_t mask = ~((static_cast<uint64_t>(1) << (length * 8)) - 1);
        raw |= mask;
      }
    }
    return static_cast<int64_t>(raw);
  }

  static String decodeBcd(const std::vector<uint8_t>& bytes, size_t start, size_t length) {
    String text;
    for (size_t offset = 0; offset < length; ++offset) {
      uint8_t value = bytes[start + length - 1 - offset];
      uint8_t high = (value >> 4) & 0x0F;
      uint8_t low = value & 0x0F;
      if (high <= 9) {
        text += char('0' + high);
      }
      if (low <= 9) {
        text += char('0' + low);
      }
    }
    while (text.length() > 1 && text[0] == '0') {
      text.remove(0, 1);
    }
    return text.length() > 0 ? text : String("0");
  }

  static String decodeDate2(const std::vector<uint8_t>& bytes, size_t start) {
    uint8_t lo = bytes[start];
    uint8_t hi = bytes[start + 1];
    int day = lo & 0x1F;
    int month = hi & 0x0F;
    int year = 2000 + (((hi >> 4) & 0x0F) << 3) + ((lo >> 5) & 0x07);
    if (day <= 0 || month <= 0 || month > 12) {
      return bytesToHex(bytes, start, start + 2, " ");
    }

    int maxDay = 31;
    if (month == 4 || month == 6 || month == 9 || month == 11) {
      maxDay = 30;
    } else if (month == 2) {
      bool leap = ((year % 4) == 0 && (year % 100) != 0) || ((year % 400) == 0);
      maxDay = leap ? 29 : 28;
    }
    if (day > maxDay) {
      return bytesToHex(bytes, start, start + 2, " ");
    }

    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d", year, month, day);
    return String(buffer);
  }

  static String decodeDateTime4(const std::vector<uint8_t>& bytes, size_t start) {
    uint8_t minuteByte = bytes[start];
    uint8_t hourByte = bytes[start + 1];
    uint8_t dayByte = bytes[start + 2];
    uint8_t monthYearByte = bytes[start + 3];
    int minute = minuteByte & 0x3F;
    int hour = hourByte & 0x1F;
    int day = dayByte & 0x1F;
    int month = monthYearByte & 0x0F;
    int year = 2000 + (((monthYearByte >> 4) & 0x0F) << 3) + ((dayByte >> 5) & 0x07);
    if (day <= 0 || month <= 0 || month > 12 || hour > 23 || minute > 59) {
      return bytesToHex(bytes, start, start + 4, " ");
    }
    int maxDay = 31;
    if (month == 4 || month == 6 || month == 9 || month == 11) {
      maxDay = 30;
    } else if (month == 2) {
      bool leap = ((year % 4) == 0 && (year % 100) != 0) || ((year % 400) == 0);
      maxDay = leap ? 29 : 28;
    }
    if (day > maxDay) {
      return bytesToHex(bytes, start, start + 4, " ");
    }
    char buffer[24];
    snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d", year, month, day, hour, minute);
    return String(buffer);
  }

  struct VifInfo {
    uint8_t code;
    String quantity;
    String unit;
    int scale10;
    bool recognized;
    bool isDate;
    bool isDateTime;
  };

  enum class FunctionLabelKind {
    Dif,
    FixedHeader,
  };

  static String functionLabel(uint8_t dif, FunctionLabelKind kind = FunctionLabelKind::Dif) {
    if (kind == FunctionLabelKind::FixedHeader) {
      return "Telegram";
    }

    switch ((dif >> 4) & 0x03) {
      case 0: return "Instantaneous";
      case 1: return "Maximum";
      case 2: return "Minimum";
      case 3: return "Error";
      default: return "Unknown";
    }
  }

  static bool isMadWaterDevice(const std::vector<uint8_t>& response) {
    return decodeManufacturer(response) == "MAD" && response.size() > 14 && response[14] == 0x07;
  }

  static VifInfo decodeVif(uint8_t vif) {
    VifInfo info;
    info.code = vif & 0x7F;
    info.quantity = "Unknown";
    info.unit = "";
    info.scale10 = 0;
    info.recognized = false;
    info.isDate = false;
    info.isDateTime = false;

    uint8_t code = info.code;
    if (code <= 0x07) {
      info.quantity = "Energy";
      info.unit = "Wh";
      info.scale10 = static_cast<int>(code) - 3;
      info.recognized = true;
    } else if (code >= 0x08 && code <= 0x0F) {
      info.quantity = "Energy";
      info.unit = "J";
      info.scale10 = static_cast<int>(code - 0x08) + 0;
      info.recognized = true;
    } else if (code >= 0x10 && code <= 0x17) {
      info.quantity = "Volume";
      info.unit = "m3";
      info.scale10 = static_cast<int>(code & 0x07) - 6;
      info.recognized = true;
    } else if (code >= 0x20 && code <= 0x23) {
      info.quantity = "On time";
      info.unit = code == 0x20 ? "s" : (code == 0x21 ? "min" : (code == 0x22 ? "h" : "days"));
      info.recognized = true;
    } else if (code >= 0x24 && code <= 0x27) {
      info.quantity = "Operating time";
      info.unit = code == 0x24 ? "s" : (code == 0x25 ? "min" : (code == 0x26 ? "h" : "days"));
      info.recognized = true;
    } else if (code >= 0x30 && code <= 0x37) {
      info.quantity = "Power";
      info.unit = "W";
      info.scale10 = static_cast<int>(code & 0x07) - 3;
      info.recognized = true;
    } else if (code >= 0x38 && code <= 0x3F) {
      info.quantity = "Power";
      info.unit = "J/h";
      info.scale10 = static_cast<int>(code & 0x07);
      info.recognized = true;
    } else if (code >= 0x40 && code <= 0x47) {
      info.quantity = "Volume flow";
      info.unit = "m3/h";
      info.scale10 = static_cast<int>(code & 0x07) - 6;
      info.recognized = true;
    } else if (code >= 0x48 && code <= 0x4F) {
      info.quantity = "Volume flow";
      info.unit = "m3/min";
      info.scale10 = static_cast<int>(code & 0x07) - 7;
      info.recognized = true;
    } else if (code >= 0x50 && code <= 0x57) {
      info.quantity = "Volume flow";
      info.unit = "m3/s";
      info.scale10 = static_cast<int>(code & 0x07) - 9;
      info.recognized = true;
    } else if (code >= 0x58 && code <= 0x5B) {
      info.quantity = "Mass flow";
      info.unit = "kg/h";
      info.scale10 = static_cast<int>(code & 0x03) - 3;
      info.recognized = true;
    } else if (code >= 0x5C && code <= 0x5F) {
      info.quantity = "Flow temperature";
      info.unit = "C";
      info.scale10 = static_cast<int>(code & 0x03) - 3;
      info.recognized = true;
    } else if (code >= 0x60 && code <= 0x63) {
      info.quantity = "Return temperature";
      info.unit = "C";
      info.scale10 = static_cast<int>(code & 0x03) - 3;
      info.recognized = true;
    } else if (code >= 0x64 && code <= 0x67) {
      info.quantity = "Temperature difference";
      info.unit = "K";
      info.scale10 = static_cast<int>(code & 0x03) - 3;
      info.recognized = true;
    } else if (code >= 0x68 && code <= 0x6B) {
      info.quantity = "External temperature";
      info.unit = "C";
      info.scale10 = static_cast<int>(code & 0x03) - 3;
      info.recognized = true;
    } else if (code == 0x78) {
      info.quantity = "Serial number";
      info.unit = "";
      info.recognized = true;
    } else if (code == 0x6C) {
      info.quantity = "Date / DateTime";
      info.recognized = true;
    } else if (code == 0x6D) {
      info.quantity = "Date / DateTime";
      info.recognized = true;
    }
    return info;
  }

  static String buildRecordKey(int index, int storage, uint8_t dif, const String& vifText) {
    String compactVif = vifText;
    compactVif.replace(" ", "");
    compactVif.toUpperCase();
    return String("r") + String(index) + "_s" + String(storage) + "_d" + byteToHex(dif) + "_v" + compactVif;
  }

  static void appendFixedHeaderRecords(const std::vector<uint8_t>& response,
                                       std::vector<TelegramRecord>& records,
                                       int& recordIndex) {
    if (!isLongFrame(response) || response.size() < 19) {
      return;
    }

    if (response[6] != 0x72 && response[6] != 0x76) {
      return;
    }

    TelegramRecord record;
    record.storage = 0;
    record.function = functionLabel(0, FunctionLabelKind::FixedHeader);
    record.unit = "";
    record.vif = "";
    record.recognized = true;

    record.index = recordIndex++;
    record.key = buildHeaderRecordKey("id");
    record.quantity = "ID";
    record.raw = bytesToHex(response, 7, 11, " ");
    record.value = decodeIdentification(response);
    records.push_back(record);

    record.index = recordIndex++;
    record.key = buildHeaderRecordKey("manufacturer");
    record.quantity = "Manufacturer";
    record.raw = bytesToHex(response, 11, 13, " ");
    record.value = decodeManufacturer(response);
    records.push_back(record);

    record.index = recordIndex++;
    record.key = buildHeaderRecordKey("version");
    record.quantity = "Version";
    record.raw = bytesToHex(response, 13, 14, " ");
    record.value = response.size() > 13 ? String(response[13]) : String();
    records.push_back(record);

    record.index = recordIndex++;
    record.key = buildHeaderRecordKey("medium");
    record.quantity = "Medium";
    record.raw = bytesToHex(response, 14, 15, " ");
    record.value = response.size() > 14 ? mediumLabel(response[14]) : String();
    records.push_back(record);
  }

  static size_t dataLengthFromDif(uint8_t dif, bool& variableLength, bool& special) {
    variableLength = false;
    special = false;
    switch (dif & 0x0F) {
      case 0x00: return 0;
      case 0x01: return 1;
      case 0x02: return 2;
      case 0x03: return 3;
      case 0x04: return 4;
      case 0x05: return 4;
      case 0x06: return 6;
      case 0x07: return 8;
      case 0x08: return 0;
      case 0x09: return 1;
      case 0x0A: return 2;
      case 0x0B: return 3;
      case 0x0C: return 4;
      case 0x0D: variableLength = true; return 0;
      case 0x0E: return 6;
      case 0x0F: special = true; return 0;
      default: special = true; return 0;
    }
  }

  static String decodeRecordValue(const std::vector<uint8_t>& response,
                                  size_t start,
                                  size_t length,
                                  uint8_t dif,
                                  const VifInfo& vifInfo) {
    uint8_t dataCode = dif & 0x0F;
    if (length == 0) {
      return "";
    }
    if ((vifInfo.code == 0x6C || vifInfo.code == 0x6D) && length == 2) {
      return decodeDate2(response, start);
    }
    if ((vifInfo.code == 0x6C || vifInfo.code == 0x6D) && length == 4) {
      return decodeDateTime4(response, start);
    }
    if (dataCode == 0x05 && length == 4) {
      union {
        uint32_t integer;
        float real;
      } value;
      value.integer = static_cast<uint32_t>(response[start]) |
                      (static_cast<uint32_t>(response[start + 1]) << 8) |
                      (static_cast<uint32_t>(response[start + 2]) << 16) |
                      (static_cast<uint32_t>(response[start + 3]) << 24);
      return formatDoubleValue(value.real, 3);
    }
    if (dataCode >= 0x09 && dataCode <= 0x0E) {
      String bcd = decodeBcd(response, start, length);
      if (vifInfo.quantity == "Serial number") {
        return bcd;
      }
      double numeric = bcd.toDouble();
      if (vifInfo.recognized) {
        double scaled = applyScale10(numeric, vifInfo.scale10);
        return formatDoubleValue(scaled, vifInfo.scale10 < 0 ? -vifInfo.scale10 : 0);
      }
      return bcd;
    }

    int64_t raw = decodeSignedLE(response, start, length);
    if (vifInfo.recognized) {
      double scaled = applyScale10(static_cast<double>(raw), vifInfo.scale10);
      return formatDoubleValue(scaled, vifInfo.scale10 < 0 ? -vifInfo.scale10 : 0);
    }
    return String(raw);
  }

  static std::vector<TelegramRecord> parseTelegramRecords(const std::vector<uint8_t>& response) {
    std::vector<TelegramRecord> records;
    if (!isLongFrame(response) || response.size() < 8) {
      return records;
    }

    size_t start = 7;
    if ((response[6] == 0x72 || response[6] == 0x76) && response.size() > 19) {
      start = 19;
    }
    size_t end = response.size() >= 2 ? response.size() - 2 : response.size();
    int recordIndex = 1;
    bool madWaterDevice = isMadWaterDevice(response);

    appendFixedHeaderRecords(response, records, recordIndex);

    for (size_t position = start; position < end;) {
      if (response[position] == 0x2F) {
        ++position;
        continue;
      }

      uint8_t dif = response[position++];
      uint8_t baseDif = dif;
      if (dif == 0x0F || dif == 0x1F) {
        break;
      }

      int storage = (dif >> 6) & 0x01;
      int storageShift = 1;
      while ((dif & 0x80) != 0 && position < end) {
        uint8_t dife = response[position++];
        storage |= (dife & 0x0F) << storageShift;
        storageShift += 4;
        dif = dife;
      }
      if (position >= end) {
        break;
      }

      uint8_t vif = response[position++];
      String vifText = byteToHex(vif);
      while ((vif & 0x80) != 0 && position < end) {
        vif = response[position++];
        vifText += String(" ") + byteToHex(vif);
      }

      bool variableLength = false;
      bool special = false;
      size_t length = dataLengthFromDif(baseDif, variableLength, special);
      if (special) {
        break;
      }
      if (variableLength) {
        if (position >= end) {
          break;
        }
        length = response[position++];
      }
      if (position + length > end) {
        break;
      }

      VifInfo vifInfo = decodeVif(vif);
      if (madWaterDevice && vifText == "93 3C") {
        vifInfo.quantity = "Volume tariff 1";
        vifInfo.unit = "L";
        vifInfo.scale10 = 0;
        vifInfo.recognized = true;
      }
      TelegramRecord record;
      record.index = recordIndex++;
      record.storage = storage;
      record.key = buildRecordKey(record.index, storage, baseDif, vifText);
      record.function = functionLabel(baseDif);
      if (madWaterDevice && storage > 0) {
        record.function = "Historical";
      }
      record.quantity = vifInfo.quantity;
      if ((vifInfo.code == 0x6C || vifInfo.code == 0x6D) && length == 2) {
        record.quantity = "Date";
      } else if ((vifInfo.code == 0x6C || vifInfo.code == 0x6D) && length == 4) {
        record.quantity = "DateTime";
      }
      record.unit = vifInfo.unit;
      record.vif = vifText;
      record.raw = bytesToHex(response, position, position + length, " ");
      record.recognized = vifInfo.recognized;
      record.value = decodeRecordValue(response, position, length, baseDif, vifInfo);
      records.push_back(record);
      position += length;
    }
    return records;
  }

  class Client {
   public:
    bool connect(const String& host, uint16_t port) {
      disconnect();
      tcp.setTimeout(RESPONSE_TIMEOUT_MS);
      return tcp.connect(host.c_str(), port, CONNECT_TIMEOUT_MS);
    }

    void disconnect() {
      if (tcp.connected()) {
        tcp.stop();
      }
    }

    std::vector<uint8_t> sendFrame(const std::vector<uint8_t>& frame,
                                   unsigned long timeoutMs = RESPONSE_TIMEOUT_MS,
                                   unsigned long idleMs = RESPONSE_IDLE_MS) {
      std::vector<uint8_t> response;
      if (!tcp.connected()) {
        return response;
      }

      while (tcp.available() > 0) {
        tcp.read();
      }

      tcp.write(frame.data(), frame.size());
      return readResponse(timeoutMs, idleMs);
    }

    std::vector<uint8_t> reqUD2(uint8_t address,
                                unsigned long timeoutMs = RESPONSE_TIMEOUT_MS,
                                unsigned long idleMs = RESPONSE_IDLE_MS) {
      std::vector<uint8_t> frame;
      frame.push_back(0x10);
      frame.push_back(0x5B);
      frame.push_back(address);
      frame.push_back(static_cast<uint8_t>((0x5B + address) & 0xFF));
      frame.push_back(0x16);
      return sendFrame(frame, timeoutMs, idleMs);
    }

   private:
    NetworkClient tcp;

    size_t expectedResponseLength(const std::vector<uint8_t>& response) {
      if (response.empty()) {
        return 0;
      }
      if (response[0] == 0xE5) {
        return 1;
      }
      if (response[0] == 0x10) {
        return 5;
      }
      if (response[0] == 0x68 && response.size() >= 2) {
        return static_cast<size_t>(response[1]) + 6;
      }
      return 0;
    }

    std::vector<uint8_t> readResponse(unsigned long timeoutMs, unsigned long idleMs) {
      std::vector<uint8_t> response;
      unsigned long start = millis();
      unsigned long lastDataAt = start;

      while (millis() - start < timeoutMs) {
        while (tcp.available() > 0) {
          int value = tcp.read();
          if (value >= 0) {
            response.push_back(static_cast<uint8_t>(value));
            lastDataAt = millis();
          }
        }

        size_t expectedLength = expectedResponseLength(response);
        if (expectedLength > 0 && response.size() >= expectedLength) {
          break;
        }

        if (!response.empty() && expectedLength == 0 && millis() - lastDataAt >= idleMs) {
          break;
        }
        delay(10);
      }
      return response;
    }
  };

  static int resolvePrimaryAddressOnBus(Client& client,
                                        const String& secondaryAddress,
                                        int startAddress = PRIMARY_MIN_ADDRESS,
                                        int endAddress = PRIMARY_MAX_ADDRESS) {
    if (secondaryAddress.length() != 16) {
      return -1;
    }

    for (int address = startAddress; address <= endAddress; ++address) {
      std::vector<uint8_t> response = client.reqUD2(static_cast<uint8_t>(address));
      if (!isLongFrame(response) || response.size() < 15) {
        delay(20);
        continue;
      }

      String foundSecondary = extractSecondaryAddress(response);
      if (foundSecondary == secondaryAddress) {
        return address;
      }
      delay(20);
    }
    return -1;
  }

  static std::vector<uint8_t> buildLongFrame(uint8_t control, uint8_t address, uint8_t ci, const uint8_t* payload, size_t payloadSize) {
    std::vector<uint8_t> frame;
    uint8_t length = static_cast<uint8_t>(payloadSize + 3);
    frame.push_back(0x68);
    frame.push_back(length);
    frame.push_back(length);
    frame.push_back(0x68);
    frame.push_back(control);
    frame.push_back(address);
    frame.push_back(ci);

    uint8_t checksum = static_cast<uint8_t>(control + address + ci);
    for (size_t index = 0; index < payloadSize; ++index) {
      frame.push_back(payload[index]);
      checksum = static_cast<uint8_t>(checksum + payload[index]);
    }
    frame.push_back(checksum);
    frame.push_back(0x16);
    return frame;
  }

  static int hexNibbleValue(char ch) {
    if (ch >= '0' && ch <= '9') {
      return ch - '0';
    }
    if (ch >= 'A' && ch <= 'F') {
      return ch - 'A' + 10;
    }
    if (ch >= 'a' && ch <= 'f') {
      return ch - 'a' + 10;
    }
    return -1;
  }

  static String buildSecondaryPattern(const String& prefix) {
    String pattern = prefix;
    pattern.trim();
    pattern.toUpperCase();
    if (pattern.length() > 16) {
      pattern = pattern.substring(0, 16);
    }
    while (pattern.length() < 16) {
      pattern += 'F';
    }
    return pattern;
  }

  static std::vector<uint8_t> buildSecondaryAddressBytes(const String& secondaryAddress) {
    std::vector<uint8_t> bytes;
    bytes.reserve(8);
    for (size_t index = 0; index < 8; ++index) {
      String pair = secondaryAddress.substring(index * 2, index * 2 + 2);
      bytes.push_back(static_cast<uint8_t>(strtoul(pair.c_str(), nullptr, 16)));
    }
    return bytes;
  }

  static std::vector<uint8_t> buildSecondaryPatternBytes(const String& secondaryPattern) {
    std::vector<uint8_t> bytes;
    bytes.reserve(8);
    for (size_t index = 0; index < 8; ++index) {
      int high = hexNibbleValue(secondaryPattern[index * 2]);
      int low = hexNibbleValue(secondaryPattern[index * 2 + 1]);
      if (high < 0) {
        high = 0x0F;
      }
      if (low < 0) {
        low = 0x0F;
      }
      bytes.push_back(static_cast<uint8_t>((high << 4) | low));
    }
    return bytes;
  }

  static bool secondaryPrefixOverlapsRange(const String& prefix, const String& startAddress, const String& endAddress) {
    if (startAddress.length() == 0 && endAddress.length() == 0) {
      return true;
    }

    String minText = prefix;
    String maxText = prefix;
    minText.toUpperCase();
    maxText.toUpperCase();
    while (minText.length() < 16) {
      minText += '0';
    }
    while (maxText.length() < 16) {
      maxText += 'F';
    }

    uint64_t minValue = parseSecondaryNumber(minText);
    uint64_t maxValue = parseSecondaryNumber(maxText);
    uint64_t startValue = parseSecondaryNumber(startAddress);
    uint64_t endValue = parseSecondaryNumber(endAddress);
    return maxValue >= startValue && minValue <= endValue;
  }

  static std::vector<uint8_t> selectBySecondaryPattern(Client& client, const String& secondaryPattern) {
    std::vector<uint8_t> payload = buildSecondaryPatternBytes(secondaryPattern);
    return client.sendFrame(buildLongFrame(0x53, 0xFD, 0x52, payload.data(), payload.size()),
                            SECONDARY_SELECTION_TIMEOUT_MS,
                            SECONDARY_SELECTION_IDLE_MS);
  }

  static std::vector<uint8_t> selectBySecondaryStandard(Client& client, const String& secondaryAddress) {
    std::vector<uint8_t> payload = buildSecondaryAddressBytes(secondaryAddress);
    return client.sendFrame(buildLongFrame(0x53, 0xFD, 0x52, payload.data(), payload.size()));
  }

  static std::vector<uint8_t> selectBySecondaryAltFe43(Client& client, const String& secondaryAddress) {
    std::vector<uint8_t> payload = buildSecondaryAddressBytes(secondaryAddress);
    return client.sendFrame(buildLongFrame(0x53, 0xFE, 0x43, payload.data(), payload.size()));
  }

  // Select device by secondary address. Returns true when ACK received.
  static bool selectBySecondary(Client& client, const String& secondaryAddress) {
    const int maxAttempts = 3;
    for (int attempt = 0; attempt < maxAttempts; ++attempt) {
      std::vector<uint8_t> resp = selectBySecondaryStandard(client, secondaryAddress);
      if (isAck(resp)) {
        DBG_DEBUG(String("M-Bus select secondary: standard FD/52 ACK, resp=") + bytesToHex(resp));
        return true;
      }

      std::vector<uint8_t> altResp = selectBySecondaryAltFe43(client, secondaryAddress);
      if (isAck(altResp)) {
        DBG_DEBUG(String("M-Bus select secondary: alt FE/43 ACK, resp=") + bytesToHex(altResp));
        return true;
      }

      DBG_WARN(String("M-Bus select secondary: no ACK, standard=") + bytesToHex(resp) + String(", alt=") + bytesToHex(altResp));
      delay(120 + attempt * 120);
    }
    return false;
  }

  static bool deselectAllBySelection(Client& client) {
    uint8_t payload[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    std::vector<uint8_t> resp = client.sendFrame(buildLongFrame(0x53, 0xFD, 0x52, payload, sizeof(payload)),
                                                 SECONDARY_DESELECT_TIMEOUT_MS,
                                                 SECONDARY_DESELECT_IDLE_MS);
    if (isAck(resp)) {
      DBG_DEBUG(String("M-Bus deselect by selection: ACK, resp=") + bytesToHex(resp));
      return true;
    }

    DBG_DEBUG(String("M-Bus deselect by selection: no ACK, resp=") + bytesToHex(resp));
    return false;
  }

  static TelegramResult readDeviceTelegram(const String& host,
                                           uint16_t port,
                                           int primaryAddress,
                                           const String& secondaryAddress,
                                           unsigned long responseTimeoutMs = RESPONSE_TIMEOUT_MS,
                                           unsigned long responseIdleMs = RESPONSE_IDLE_MS) {
    unsigned long startedAtMs = millis();
    TelegramResult result;
    result.success = false;

    String secondary = secondaryAddress;
    secondary.trim();
    secondary.toUpperCase();

    if (secondary.length() > 0 && (secondary.length() != 16 || !isSecondaryHexText(secondary))) {
      result.error = "Secondary address must be a 16-character hexadecimal value.";
      return result;
    }

    if (primaryAddress < PRIMARY_MIN_ADDRESS && secondary.length() == 0) {
      result.error = "A primary or 16-character secondary address is required.";
      return result;
    }

    Client client;
    if (!client.connect(host, port)) {
      result.error = "Failed to connect to the M-Bus TCP gateway.";
      return result;
    }

    std::vector<uint8_t> response;
    if (secondary.length() == 16) {
      bool selected = selectBySecondary(client, secondary);
      if (selected) {
        delay(180);
        response = client.reqUD2(0xFD, responseTimeoutMs, responseIdleMs);
        delay(120);
        deselectAllBySelection(client);
      }
    }

    if ((!isLongFrame(response) || response.size() < 15) && primaryAddress >= PRIMARY_MIN_ADDRESS && primaryAddress <= PRIMARY_MAX_ADDRESS) {
      response = client.reqUD2(static_cast<uint8_t>(primaryAddress), responseTimeoutMs, responseIdleMs);
    }

    client.disconnect();

    if (!isLongFrame(response) || response.size() < 15) {
      result.error = "No valid M-Bus long frame received from the device.";
      return result;
    }

    result.device = parseDeviceInfo(response);
    if (secondary.length() == 16 && result.device.secondaryAddress != secondary) {
      result.error = result.device.secondaryAddress.length() > 0
        ? String("M-Bus telegram secondary address mismatch: expected ") + secondary + String(", got ") + result.device.secondaryAddress + String(".")
        : String("M-Bus telegram does not contain the requested secondary address.");
      return result;
    }
    if (primaryAddress >= PRIMARY_MIN_ADDRESS && primaryAddress <= PRIMARY_MAX_ADDRESS) {
      result.device.primaryAddress = primaryAddress;
    }
    if (result.device.secondaryAddress.length() == 0) {
      result.device.secondaryAddress = secondary;
    }
    result.rawFrame = bytesToHex(response, 0, response.size(), " ");
    result.records = parseTelegramRecords(response);
    result.success = true;
    DBG_DEBUG(String("M-Bus read: OK, ") + describeDevice(result.device) +
             String(", records=") + String(result.records.size()) +
             String(", durationMs=") + String(millis() - startedAtMs));
    return result;
  }

  static String describeDevice(const DeviceInfo& info) {
    String text = "primary=";
    text += info.primaryAddress >= 0 ? String(info.primaryAddress) : String("-");
    text += ", secondary=";
    text += info.secondaryAddress;
    if (info.manufacturer.length() > 0) {
      text += ", manufacturer=";
      text += info.manufacturer;
    }
    if (info.type.length() > 0) {
      text += ", type=";
      text += info.type;
    }
    return text;
  }

  static void appendUnique(std::vector<DeviceInfo>& devices, const DeviceInfo& info) {
    for (size_t index = 0; index < devices.size(); ++index) {
      if (devices[index].primaryAddress == info.primaryAddress &&
          devices[index].secondaryAddress == info.secondaryAddress) {
        return;
      }
    }
    devices.push_back(info);
  }

}
