// Simple debug/log utility for Serial + optional server forwarding
#ifndef DEBUG_H
#define DEBUG_H

#include <Arduino.h>
#include <stdarg.h>
#include <esp_heap_caps.h>

enum LogLevel {
  LOG_NONE = 0,
  LOG_ERROR = 1,
  LOG_WARN = 2,
  LOG_INFO = 3,
  LOG_DEBUG = 4,
};

class Debugger {
public:
  HardwareSerial* serial = nullptr;
  bool serialEnabled = true;
  int level = LOG_INFO;
  void (*serverSender)(const char*) = nullptr;

  void begin(HardwareSerial* s = &Serial, bool enable = true, int lvl = LOG_INFO) {
    serial = s;
    serialEnabled = enable;
    level = lvl;
  }

  void setLevel(int lvl) { level = lvl; }
  void enableSerial(bool en) { serialEnabled = en; }
  void setServerSender(void (*fn)(const char*)) { serverSender = fn; }
  int serverLevel = LOG_ERROR;
  void setServerLevel(int lvl) { serverLevel = lvl; }

  void sendToServerIfNeeded(int msgLevel, const char* s) {
    if (serverSender && msgLevel <= serverLevel) serverSender(s);
  }
  void sendToServerIfNeeded(int msgLevel, const String& s) { sendToServerIfNeeded(msgLevel, s.c_str()); }

  bool shouldLog(int msgLevel) { return serialEnabled && msgLevel <= level; }

  void print(const char* s) {
    if (serial && serialEnabled) serial->print(s);
  }
  void println(const char* s) {
    if (serial && serialEnabled) serial->println(s);
  }
  void println(const String& s) { if (serial && serialEnabled) serial->println(s); }
  void printf(const char* fmt, ...) {
    if (!(serial && serialEnabled)) return;
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    serial->print(buf);
  }

  // critical: always forward to server if sender set; optionally print to Serial
  void critical(const char* s) {
    if (serial && serialEnabled) serial->println(s);
    if (serverSender) serverSender(s);
  }
  void critical(const String& s) { critical(s.c_str()); }
};

extern Debugger Debug;

// Convenience macros
#define DBG_DEBUG(x) do { if (Debug.shouldLog(LOG_DEBUG)) Debug.println(x); } while(0)
#define DBG_INFO(x)  do { if (Debug.shouldLog(LOG_INFO))  Debug.println(x); } while(0)
#define DBG_WARN(x)  do { if (Debug.shouldLog(LOG_WARN))  Debug.println(x); Debug.sendToServerIfNeeded(LOG_WARN, (x)); } while(0)
#define DBG_ERROR(x) do { if (Debug.shouldLog(LOG_ERROR)) Debug.println(x); Debug.sendToServerIfNeeded(LOG_ERROR, (x)); } while(0)
#define DBG_CRITICAL(x) do { Debug.critical(x); } while(0)

// Report current heap usage as a single formatted line
#define DBG_MEMINFO() do { \
  if (Debug.shouldLog(LOG_DEBUG)) { \
    char buf[196]; \
    uint32_t free8    = heap_caps_get_free_size(MALLOC_CAP_8BIT); \
    uint32_t largest8 = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT); \
    uint32_t minFree8 = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT); \
    uint32_t free32   = heap_caps_get_free_size(MALLOC_CAP_32BIT); \
    uint32_t total    = heap_caps_get_total_size(MALLOC_CAP_8BIT); \
    unsigned long uptimeSec = millis() / 1000UL; \
    snprintf(buf, sizeof(buf), \
      "MEM: total=%u free=%u (min=%u) largest=%u free32=%u uptime=%lus", \
      total, free8, minFree8, largest8, free32, uptimeSec); \
    Debug.println(buf); \
  } \
} while(0)

#endif // DEBUG_H
