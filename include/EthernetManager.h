// Network initialization wrapper (Ethernet)
#ifndef ETHERNET_MANAGER_H
#define ETHERNET_MANAGER_H

#include <Arduino.h>
#include <ETH.h>
#include "AppSettings.h"

namespace EthernetManager {
  static bool started = false;

  static bool parseIpAddress(const String& value, IPAddress& address) {
    return address.fromString(value);
  }

  static bool hasValidStaticConfig() {
    AppSettings::Settings& settings = AppSettings::get();
    if (settings.networkUseDhcp) {
      return false;
    }

    IPAddress ip;
    IPAddress gateway;
    IPAddress subnet;
    return parseIpAddress(settings.networkIp, ip) &&
           parseIpAddress(settings.networkGateway, gateway) &&
           parseIpAddress(settings.networkSubnet, subnet);
  }

  static bool usingDhcp() {
    AppSettings::Settings& settings = AppSettings::get();
    return settings.networkUseDhcp || !hasValidStaticConfig();
  }

  static String modeLabel() {
    return usingDhcp() ? "DHCP" : "Static";
  }

  static bool applyRuntimeConfig() {
    if (usingDhcp()) {
      return ETH.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
    }

    AppSettings::Settings& settings = AppSettings::get();
    IPAddress ip;
    IPAddress gateway;
    IPAddress subnet;
    IPAddress dns1(0, 0, 0, 0);
    IPAddress dns2(0, 0, 0, 0);

    if (!parseIpAddress(settings.networkIp, ip) ||
        !parseIpAddress(settings.networkGateway, gateway) ||
        !parseIpAddress(settings.networkSubnet, subnet)) {
      return false;
    }
    parseIpAddress(settings.networkDns1, dns1);
    parseIpAddress(settings.networkDns2, dns2);
    return ETH.config(ip, gateway, subnet, dns1, dns2);
  }

  // Initialize Ethernet. Returns true if link up and IP assigned.
  static bool begin(unsigned long timeoutMs = 10000) {
    ETH.begin();
    started = true;
    applyRuntimeConfig();
    unsigned long start = millis();
    while (!ETH.linkUp() && millis() - start < timeoutMs) {
      delay(200);
    }
    if (!ETH.linkUp()) return false;

    start = millis();
    while (ETH.localIP().toString() == "0.0.0.0" && millis() - start < timeoutMs) {
      delay(200);
    }
    return ETH.localIP().toString() != "0.0.0.0";
  }

  static bool restart(unsigned long timeoutMs = 10000) {
    if (!started) {
      return begin(timeoutMs);
    }

    if (!applyRuntimeConfig()) {
      return false;
    }

    unsigned long start = millis();
    while (ETH.localIP().toString() == "0.0.0.0" && millis() - start < timeoutMs) {
      delay(200);
    }
    return ETH.localIP().toString() != "0.0.0.0";
  }

  static bool linkUp() { return ETH.linkUp(); }
  static bool hasIP() { return ETH.localIP() != IPAddress(0, 0, 0, 0); }
  static bool isReady() { return linkUp() && hasIP(); }

  static IPAddress localIP() { return ETH.localIP(); }
  static IPAddress gatewayIP() { return ETH.gatewayIP(); }
  static IPAddress subnetMask() { return ETH.subnetMask(); }
  static IPAddress dnsIP(uint8_t index = 0) { return ETH.dnsIP(index); }
}

#endif // ETHERNET_MANAGER_H