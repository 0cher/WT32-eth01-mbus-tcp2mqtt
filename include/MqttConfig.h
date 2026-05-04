#ifndef MQTT_CONFIG_H
#define MQTT_CONFIG_H

// ⚠️ Replace these with your actual MQTT broker details before first flash.
// You can also change all settings later via the web admin interface.
#define DEFAULT_MQTT_BROKER_HOST "mqtt.example.com"
#define DEFAULT_MQTT_BROKER_PORT 8883
#define DEFAULT_MQTT_KEEPALIVE_SECONDS 60
#define DEFAULT_MQTT_RECONNECT_INTERVAL_MS 5000

#define DEFAULT_MQTT_PASSWORD "changeme"

// Default topic templates — change to suit your MQTT namespace.
// You can override all three via the web interface (MQTT Settings page).
#define TOPIC_TELEMETRY_FORMAT "telemetry/%s"
#define TOPIC_TELEMETRY_LOG_FORMAT "telemetry/%s/log"
#define TOPIC_CONFIG_FORMAT "config/%s"
#define MQTT_PUBLISH_BUFFER_SIZE 4096

// If using a custom CA, upload it via the web interface (MQTT Settings page).
// Example format (replace with your actual PEM certificate):
// static const char DEFAULT_MQTT_CA_CERT[] = "-----BEGIN CERTIFICATE-----\n"
// "MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw\n"
// "...\n"
// "-----END CERTIFICATE-----\n";
static const char DEFAULT_MQTT_CA_CERT[] = "";

#endif // MQTT_CONFIG_H