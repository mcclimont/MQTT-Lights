#ifndef CONFIG_EXAMPLE_H
#define CONFIG_EXAMPLE_H

/**
 * PRIVATE CONFIGURATION - DO NOT COMMIT config.h TO GIT
 * 
 * Copy this file to config.h and update with your private values:
 * WiFi credentials, MQTT broker details, API keys, etc.
 * 
 * File: include/config.h (not tracked by git)
 */

// WiFi Configuration
#define WIFI_SSID "Your_WiFi_SSID"
#define WIFI_PASSWORD "Your_WiFi_Password"

// MQTT Configuration (add as needed)
#define MQTT_BROKER "mqtt.example.com"
#define MQTT_PORT 1883
#define MQTT_USER "mqtt_user"
#define MQTT_PASSWORD "mqtt_password"

// Device Configuration
#define DEVICE_NAME "mqtt-screen-device"

#endif // CONFIG_EXAMPLE_H
