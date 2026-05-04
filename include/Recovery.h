#ifndef RECOVERY_H
#define RECOVERY_H

#include <Arduino.h>
#include "AppSettings.h"
#include "Debug.h"

#ifndef RECOVERY_MODE_DEV_UART
#define RECOVERY_MODE_DEV_UART 0
#endif

#ifndef RECOVERY_MODE_PROD_GPIO
#define RECOVERY_MODE_PROD_GPIO 0
#endif

#if RECOVERY_MODE_DEV_UART
#define RECOVERY_PROFILE_NAME "wt32-eth01-dev"
#elif RECOVERY_MODE_PROD_GPIO
#define RECOVERY_PROFILE_NAME "wt32-eth01-prod"
#else
#define RECOVERY_PROFILE_NAME "wt32-eth01-unknown"
#endif

namespace Recovery {
  static const unsigned long UART_RECOVERY_WINDOW_MS = 5000;

  static const char* profileName() {
    return RECOVERY_PROFILE_NAME;
  }

  static void runDevUartRecovery() {
    DBG_INFO("Type RESETWEB in Serial within 5 seconds to reset web credentials.");
    DBG_INFO(String("Saved device serial in NVS: ") + AppSettings::deviceSerial());
    DBG_INFO(String("Type PROVISIONSN in Serial within 5 seconds to overwrite NVS serial with build DEVICE_SN: ") + DEVICE_SN);
    Serial.setTimeout(UART_RECOVERY_WINDOW_MS);

    unsigned long start = millis();
    while (millis() - start < UART_RECOVERY_WINDOW_MS) {
      if (!Serial.available()) {
        delay(10);
        continue;
      }

      String command = Serial.readStringUntil('\n');
      command.trim();
      command.toUpperCase();

      if (command == "RESETWEB") {
        AppSettings::resetWebAuthToDefaults();
        DBG_WARN(String("Web credentials reset to defaults. Username: admin, password: ") + AppSettings::deviceSerial());
        return;
      }

      if (command == "PROVISIONSN") {
        String provisionedSerial = String(DEVICE_SN);
        provisionedSerial.trim();
        if (!AppSettings::setDeviceSerial(provisionedSerial)) {
          DBG_WARN("Serial provisioning failed: DEVICE_SN is empty");
          return;
        }
        DBG_WARN(String("Device serial stored in NVS: ") + AppSettings::deviceSerial());
        return;
      }

      DBG_WARN(String("Unknown recovery command: ") + command);
      return;
    }
  }

  static void runProdGpioRecovery() {
    // Check if recovery GPIO is pulled LOW (shorted to GND at boot)
    pinMode(RECOVERY_GPIO, INPUT_PULLUP);
    delay(10);
    bool shorted = digitalRead(RECOVERY_GPIO) == LOW;
    pinMode(RECOVERY_GPIO, INPUT);

    if (!shorted) {
      return;
    }

    DBG_WARN(String("Factory reset: GPIO") + String(RECOVERY_GPIO) +
             String(" shorted to GND at boot."));

    // Wipe NVS — settings will be at code defaults on next boot
    AppSettings::factoryReset();
    DBG_CRITICAL("NVS wiped. Remove the GND jumper and reboot to start fresh with factory defaults.");
  }

  static void begin() {
#if RECOVERY_MODE_DEV_UART
    runDevUartRecovery();
#endif

    // Always check GPIO recovery — works in both dev and prod
    runProdGpioRecovery();
  }
}

#endif // RECOVERY_H