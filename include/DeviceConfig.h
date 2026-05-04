#ifndef DEVICE_CONFIG_H
#define DEVICE_CONFIG_H

// Change this to a unique name for your device (used as MQTT Client ID, web password, etc.)
#define DEVICE_SN "SN1234"

// Web admin defaults
#define DEFAULT_WEB_USERNAME "admin"
// Default web password equals device serial (change via web interface after first login)
#define DEFAULT_WEB_PASSWORD DEVICE_SN

// M-Bus TCP gateway defaults (change to match your M-Bus gateway, e.g. PW50 or iMega)
#define DEFAULT_MBUS_GATEWAY_HOST "192.168.1.100"
#define DEFAULT_MBUS_GATEWAY_PORT 1001

// Factory reset GPIO config (production): short this pin to GND at boot to wipe NVS
#define RECOVERY_GPIO 12

#endif // DEVICE_CONFIG_H