# WT32 M-Bus → MQTT Gateway

Firmware for the **WT32-ETH01** board (ESP32 + Ethernet) that acts as an **M-Bus TCP/IP to MQTT gateway**.

It reads data from M-Bus utility meters (heat, water, gas, electricity) via a TCP-connected M-Bus master (e.g. **PW50 from Relay GmbH**, iMega) and publishes decoded telemetry to an MQTT broker over TLS.

> **⚠️ Important:** This firmware has been tested only with the following hardware:
> - **M-Bus master:** PW50 (Relay GmbH) — TCP/IP M-Bus gateway
> - **MQTT broker:** EMQX (including TLS connection)
> - **Maddalena Water (MAD)** — water meters
> - **Landis Heat (LUG)** — heat meters
>
> **Slave device count:** the maximum number of M-Bus slave devices that can be polled simultaneously has not been determined yet, as testing has been performed with no more than 3 meters on the bus.
>
> Compatibility with other manufacturers is not guaranteed, but since the M-Bus protocol is an open standard, most meters should work.

---

## 🔑 Important: Serial Number (SN) — Key Identifier

The device serial number is used in three places:

| Purpose | Value |
|---------|-------|
| **MQTT Client ID** | = SN |
| **MQTT Username** | = SN |
| **Web interface password** | = SN (default) |

By default, the web interface password equals the serial number stored in NVS at first boot:

- If you have not changed the SN — password = `DEVICE_SN` from the firmware (`DeviceConfig.h`)
- If you changed it via `PROVISIONSN` — password = the new SN

You can change the password on the **Web Access** page.

---

## ⚡ Recovery Modes (important!)

The firmware has **two independent recovery modes** in case you forget the web password or need to fully reset the settings:

### 1. UART mode — Serial recovery (dev profile)

On boot with **dev profile**, the board waits **5 seconds** for a Serial command:

| Command | Action |
|---------|--------|
| `RESETWEB` | Reset web credentials to `admin` / current serial number |
| `PROVISIONSN` | Overwrite NVS serial number with `DEVICE_SN` from the build |

### 2. GPIO mode — hardware NVS reset (works in both profiles)

Works in **dev** and **prod** profiles. Use when UART mode is unavailable:
- Short **GPIO12 to GND** at power-on
- NVS will be **fully erased** (all settings reset)
- **Remove the jumper** before the next reboot

---

## Features

- **M-Bus protocol** — primary address scanning (1–250), secondary address discovery with range filtering, live telegram reads with full VIF/VIFE data record parsing
- **MQTT over TLS** — connect to any MQTT 3.1.1 broker with optional custom CA certificate (uploaded via web UI)
- **OTA updates** — the firmware supports over-the-air updates via the web interface (Firmware Update page)
- **Web Admin UI** — full-featured browser interface with Basic Auth:
  - Device status (Ethernet, MQTT, time sync, heap)
  - MQTT broker configuration (host, port, credentials, TLS, topics)
  - Network settings (DHCP / static IP, DNS)
  - M-Bus gateway settings (host, port)
  - M-Bus device management: scan, save, delete
  - Live telegram viewer with decoded data fields
  - Time sync (SNTP) and timezone settings
  - OTA firmware update
- **Auto-refresh snapshots** — periodically polls all saved M-Bus devices, caches values, and publishes JSON telemetry + logs to MQTT
- **Health Watchdog** — monitors Ethernet, MQTT, and internet connectivity; auto-restarts on persistent failures

---

## Hardware

| Component | Details |
|-----------|---------|
| Board | **WT32-ETH01** (ESP32 Ethernet MAC + LAN8720A PHY) |
| Ethernet | 10/100 Mbps RJ45 |
| Flash | 4 MB |
| PSRAM | — (отсутствует) |
| RAM | 520 KB SRAM (встроенная в ESP32) |
| Framework | Arduino (ESP32 Arduino Core) |

---

## Quick Start

### Prerequisites

- [PlatformIO](https://platformio.org/) (VS Code extension or CLI)
- M-Bus TCP gateway (e.g. PW50, iMega) on your network
- MQTT 3.1.1 broker (e.g. [EMQX](https://www.emqx.io/), [Mosquitto](https://mosquitto.org/), [HiveMQ Cloud](https://www.hivemq.com/mqtt-cloud-broker/))

### First-time setup

Before the first flash, set the minimum parameters in configuration files so the board can connect to your network. You can also change them later via the web interface.

1. **Clone the repository** and open in VS Code with PlatformIO
2. **Set the device serial number** in `include/DeviceConfig.h`:
   ```c
   #define DEVICE_SN "YOUR_UNIQUE_SN"
   ```
   This SN is used as MQTT Client ID, default web password, and MQTT topic identifier.  
   *Can be changed via Serial using the `PROVISIONSN` command (see recovery section).*

3. **Set your MQTT broker** in `include/MqttConfig.h`:
   ```c
   #define DEFAULT_MQTT_BROKER_HOST "your-broker.example.com"
   #define DEFAULT_MQTT_BROKER_PORT 8883
   #define DEFAULT_MQTT_PASSWORD "your_password"
   ```
   *Can be changed later on the **MQTT Settings** page.*

4. **Set your M-Bus gateway** in `include/DeviceConfig.h`:
   ```c
   #define DEFAULT_MBUS_GATEWAY_HOST "192.168.1.100"
   #define DEFAULT_MBUS_GATEWAY_PORT 1001
   ```
   *Can be changed later on the **M-Bus Gateway** page.*

5. Build and upload:
   ```bash
   pio run -e wt32-eth01-dev -t upload
   ```

---

## ⚠️ Important: Serial Number and First Boot

### How the serial number works

The serial number (SN) is stored in the board's **NVS (non-volatile storage)**. On first boot after flashing:

1. The firmware checks if an SN exists in NVS
2. If NVS is **empty** — the SN is automatically written from `DEVICE_SN` (the value in `DeviceConfig.h`)
3. If NVS **already has an SN** — the stored value is used, and `DEVICE_SN` from the build is ignored

**This means that on subsequent reflashes (with the same or different `DEVICE_SN`), the NVS serial number will NOT change!** NVS is not erased by regular USB flashing.

> **🔑 Web interface password** — by default equals the serial number stored in NVS at first boot. If you have not changed the SN — password = `DEVICE_SN` from the firmware. If changed via `PROVISIONSN` — password = the new SN.

### How to change the serial number on an already running board

There are **two methods**:

#### Method 1: Via Serial (recommended, dev profile)

On each boot in dev mode, the board prints:

```
Type RESETWEB in Serial within 5 seconds to reset web credentials.
Saved device serial in NVS: SN1234
Type PROVISIONSN in Serial within 5 seconds to overwrite NVS serial with build DEVICE_SN: SN1234
```

You have **5 seconds** after this message to send one of the commands:

| Command | Action |
|---------|--------|
| `RESETWEB` | Reset web credentials to `admin` / current serial number |
| `PROVISIONSN` | **Overwrite** NVS serial with the `DEVICE_SN` used in the build |

**Example of changing SN:**
1. Change `#define DEVICE_SN "MY_NEW_SN"` in `DeviceConfig.h`
2. Reflash the board
3. At startup, send `PROVISIONSN` via Serial within 5 seconds
4. The board confirms: `Device serial stored in NVS: MY_NEW_SN`

#### Method 2: NVS reset (GPIO mode)

If you are using the **prod profile** (or missed the Serial window):
1. Short **GPIO12 to GND**
2. Power on the board
3. NVS will be fully erased
4. **Remove the jumper** and reboot
5. On next boot, the SN will be written from `DEVICE_SN` again

> **Warning:** GPIO reset erases **all** settings (MQTT, network, M-Bus devices, web password), not just the SN.

---

## Web Admin Interface

Open `http://<board-ip>/` in your browser.

### Default credentials

| Field | Value |
|-------|-------|
| **Username** | `admin` |
| **Password** | Device serial number (`DEVICE_SN` or NVS-stored value) |

Password can be changed on the **Web Access** page.

### Pages

| Route | Description |
|-------|-------------|
| `/` | Main status dashboard |
| `/mqtt` | MQTT broker settings |
| `/network` | Ethernet configuration (DHCP / static IP) |
| `/mbus` | M-Bus device management, scan, snapshots |
| `/mbus-gateway` | M-Bus TCP gateway settings |
| `/mbus-device` | Live telegram read for a specific device |
| `/time` | SNTP and timezone settings |
| `/web-access` | Web UI credentials |
| `/firmware` | OTA firmware update |

---

## Factory Reset (GPIO mode)

**Short GPIO12 to GND** at power-on to wipe NVS completely.

After reset:
- All settings return to defaults (from `DeviceConfig.h` and `MqttConfig.h`)
- Serial number is re-written from `DEVICE_SN`
- **Remove the jumper** before the next boot, otherwise NVS will be wiped again

---

## Build Profiles

| Profile | Build Flag | Recovery Mode | Purpose |
|---------|------------|---------------|---------|
| `wt32-eth01-dev` | `RECOVERY_MODE_DEV_UART=1` | Serial window (5 s) | Development / debugging |
| `wt32-eth01-prod` | `RECOVERY_MODE_PROD_GPIO=1` | GPIO12 to GND | Production |

```bash
# Development build
pio run -e wt32-eth01-dev

# Production build
pio run -e wt32-eth01-prod
```

---

## Automatic Telemetry Publishing (Auto Refresh)

> **Important:** MQTT publishing happens **only** when auto-refresh is enabled. Pressing **Refresh Now** in the web UI updates the on-board cache but does **not** publish to MQTT.

If the auto-refresh interval is set (not `0`), the firmware independently polls saved M-Bus devices and publishes telemetry to MQTT.

### How to configure

1. Add M-Bus devices via the web interface (**M-Bus Devices** page)
2. For each device, open the telegram viewer (`/mbus-device`) and **check the fields** you want to include in telemetry — only selected fields will be published to MQTT
3. On the main page (**Main**), set the auto-refresh interval in minutes in the **Auto (min)** field
4. Click **Save**

> If the interval is `0` — auto-refresh is disabled, no MQTT publishing will occur.

### When auto-refresh runs

- If time is **synchronized (SNTP)** — the poll runs at the start of each time slot aligned to the configured interval (e.g., at 60 min — at :00 of each hour, at 15 min — at :00, :15, :30, :45). This ensures all devices are polled at the same moment
- If time is **not yet synchronized** — the first poll runs after the configured interval from boot, then on a timer
- After a successful auto-poll, the firmware publishes JSON telemetry to MQTT
- Error logs are also published only during auto-refresh

> **Limit:** Maximum **50 devices** per auto-refresh cycle.

---

## MQTT Topics

In all topics, `%s` is replaced with the device serial number.

| Topic | Direction | Description |
|-------|-----------|-------------|
| `telemetry/%s` | Publish | JSON telemetry with selected field readings (auto-refresh only) |
| `telemetry/%s/log` | Publish | Log messages (warnings, errors) from the device (auto-refresh only) |
| `config/%s` | Subscribe | Configuration commands |

Topics can be overridden via the web interface (**MQTT Settings** page).

---

## JSON Telemetry Format

```json
{
  "deviceSerial": "SN1234",
  "timestamp": 1700000000,
  "timestampReadable": "2024-11-14 12:00:00",
  "devices": [
    {
      "primaryAddress": 1,
      "secondaryAddress": "1234567890ABCDEF",
      "manufacturer": "KAM",
      "type": "Heat",
      "description": "Heat meter #1",
      "id": "12345678",
      "readings": [
        { "type": "energy", "valueText": "12345.678", "numericValue": 12345.678, "numeric": true, "unit": "kWh" },
        { "type": "volume", "valueText": "123.456", "numericValue": 123.456, "numeric": true, "unit": "m³" },
        { "type": "power", "valueText": "12.345", "numericValue": 12.345, "numeric": true, "unit": "kW" },
        { "type": "flow", "valueText": "1.234", "numericValue": 1.234, "numeric": true, "unit": "m³/h" },
        { "type": "temperature_in", "valueText": "65.4", "numericValue": 65.4, "numeric": true, "unit": "°C" },
        { "type": "temperature_out", "valueText": "45.3", "numericValue": 45.3, "numeric": true, "unit": "°C" }
      ]
    }
  ]
}
```

---

## Project Structure

```
├── include/
│   ├── AppSettings.h       — NVS settings storage
│   ├── Config.h            — Aggregates DeviceConfig + MqttConfig
│   ├── Debug.h             — Logging utility (Serial + server forwarding)
│   ├── DeviceConfig.h      — Hardware defaults (SN, gateway, GPIO)
│   ├── EthernetManager.h   — Ethernet init (DHCP / static)
│   ├── HealthWatchdog.h    — Connectivity monitoring and auto-reboot
│   ├── MBusGateway.h       — M-Bus TCP protocol (scan, read, parse)
│   ├── MbusService.h       — Background M-Bus polling and telemetry
│   ├── MqttConfig.h        — MQTT broker defaults and topic templates
│   ├── MQTTClient.h        — MQTT over TLS client
│   ├── Recovery.h          — UART and GPIO recovery modes
│   ├── TimeSync.h          — SNTP time synchronization
│   └── WebAdmin.h          — Web UI server
├── src/
│   └── main.cpp            — Entry point, setup, main loop
├── partitions.csv           — Partition table (2×OTA + SPIFFS)
├── platformio.ini           — PlatformIO project configuration
├── LICENSE                  — GNU General Public License v3.0
└── README.md                — Russian documentation
└── docs/
    └── README.ru.md         — Russian documentation
```

---

## Dependencies

| Library | Version | Purpose |
|---------|---------|---------|
| [PubSubClient](https://github.com/knolleary/pubsubclient) | ^2.8 | MQTT client |
| [ArduinoJson](https://arduinojson.org/) | ^7.0.4 | JSON serialization |
| ESP32 Arduino Core | (built-in) | ESP32 HAL + WiFiClientSecure + ETH + WebServer |

All dependencies are resolved automatically by PlatformIO.

---

## License

This project is licensed under the **GNU General Public License v3.0**.  
See [LICENSE](LICENSE) for details.

---

## Disclaimer

**This firmware is distributed WITHOUT ANY WARRANTY.**  
Use at your own risk. Always verify meter readings against their physical displays.

The firmware has been tested only with Maddalena Water (MAD) and Landis Heat (LUG) devices. Compatibility with other devices is not guaranteed, but the M-Bus protocol is an open standard.
