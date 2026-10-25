#include <Arduino.h>
#include <cstring>
#include <cstdlib>

#include "time.h"
#include "IPAddress.h"

#include <WiFi.h>
#include <PicoMQTT.h>

#include "config.h" // Private WiFi and MQTT configuration

void testHelloWorld();

// Defines
// ==========

// Sketch Name
#define SKETCH_NAME "MQTT-LightController"

// Prototype
// ==========

// Constants
// ==========

// Hardware Details
#if defined(ARDUINO_XIAO_ESP32C3)
const int led = D10; // Assigned pin for attaching an external LED to indicate status on the Seeed XIAO ESP32C3
#else
const int led = 15; // Built-in LED pin (GPIO 15 on ESP32 S2)
#endif

// Relay pin definitions
#if defined(ARDUINO_XIAO_ESP32C3)
const int relayPins[4] = {3, 4, 5, 6}; // Replace with the GPIO pins you're using
#else
const int relayPins[4] = {1, 2, 4, 6}; // Replace with the GPIO pins you're using
#endif
const int relayOnLevels[4] = {HIGH, LOW, LOW, LOW};

int getRelayOffLevel(int relayIndex)
{
  return relayOnLevels[relayIndex] == LOW ? HIGH : LOW;
}

// MQTT topics for each relay
const char *relaySetTopics[4] = {
    "home/relay1/set",
    "home/relay2/set",
    "home/relay3/set",
    "home/relay4/set"};

const char *relayStateTopics[4] = {
    "home/relay1/state",
    "home/relay2/state",
    "home/relay3/state",
    "home/relay4/state"};

const char *relayTimerSetTopics[4] = {
    "home/relay1/timer/set",
    "home/relay2/timer/set",
    "home/relay3/timer/set",
    "home/relay4/timer/set"};

const char *relayTimerStateTopics[4] = {
    "home/relay1/timer/state",
    "home/relay2/timer/state",
    "home/relay3/timer/state",
    "home/relay4/timer/state"};

struct RelayTimer
{
  bool active = false;
  uint32_t durationSeconds = 0;
  uint64_t elapsedMilliseconds = 0;
  uint32_t lastUpdatedAt = 0;
};

RelayTimer relayTimers[4];
static bool wifiDisconnected = false;
static uint32_t lastWifiReconnectAttempt = 0;
static const uint32_t WIFI_RECONNECT_INTERVAL_MS = 5000;

// NTP Server Details
const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 38000;
const int daylightOffset_sec = 3600;

// Variables
// ==========

// Network Details
IPAddress myIP(0, 0, 0, 0);

// Functions
// ==========

String getRelayStateString(int relayIndex);
void publishRelayState(int relayIndex);
void publishRelayTimerState(int relayIndex);
void updateRelayTimers();

class RelayMqttServer : public PicoMQTT::ServerLocalSubscribe
{
protected:
  void on_subscribe(const char *client_id, const char *topic) override;
};

// MQTT server and client instances
RelayMqttServer mqtt; // Acts as an MQTT broker with local subscription support

void printLocalTime()
{
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo))
  {
    Serial.println("Failed to obtain time");
    return;
  }
  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");

  // time_t now;
  // time(&now);
  // Serial.println(now);
}

time_t getTime()
{
  time_t now;
  time(&now);
  return now;
}

String getTimePrintable()
{
  time_t now;
  time(&now);

  String time_string = String(ctime(&now));
  return time_string;
}

IPAddress getlocalIP()
{
  return WiFi.localIP();
}

String getRelayStateString(int relayIndex)
{
  return digitalRead(relayPins[relayIndex]) == relayOnLevels[relayIndex] ? "ON" : "OFF";
}

void publishRelayState(int relayIndex)
{
  String state = getRelayStateString(relayIndex);
  const int pinLevel = digitalRead(relayPins[relayIndex]);
  mqtt.publish(relayStateTopics[relayIndex], state.c_str());
  Serial.printf("Published %s: %s (GPIO %d=%s, ON level=%s)\n",
                relayStateTopics[relayIndex], state.c_str(), relayPins[relayIndex],
                pinLevel == HIGH ? "HIGH" : "LOW",
                relayOnLevels[relayIndex] == HIGH ? "HIGH" : "LOW");
}

uint32_t getRelayTimerRemainingSeconds(int relayIndex)
{
  const RelayTimer &timer = relayTimers[relayIndex];
  if (!timer.active)
  {
    return 0;
  }

  const uint64_t durationMilliseconds = static_cast<uint64_t>(timer.durationSeconds) * 1000ULL;
  if (timer.elapsedMilliseconds >= durationMilliseconds)
  {
    return 0;
  }

  const uint64_t remainingMilliseconds = durationMilliseconds - timer.elapsedMilliseconds;
  return static_cast<uint32_t>((remainingMilliseconds + 999ULL) / 1000ULL);
}

void publishRelayTimerState(int relayIndex)
{
  char remainingSeconds[11];
  snprintf(remainingSeconds, sizeof(remainingSeconds), "%lu",
           static_cast<unsigned long>(getRelayTimerRemainingSeconds(relayIndex)));
  mqtt.publish(relayTimerStateTopics[relayIndex], remainingSeconds);
  Serial.printf("Published %s: %s\n", relayTimerStateTopics[relayIndex], remainingSeconds);
}

void cancelRelayTimer(int relayIndex)
{
  relayTimers[relayIndex] = RelayTimer{};
  publishRelayTimerState(relayIndex);
}

bool parseTimerDurationSeconds(const String &payload, uint32_t &durationSeconds)
{
  if (payload.isEmpty())
  {
    return false;
  }

  uint32_t parsedDuration = 0;
  for (size_t charIndex = 0; charIndex < payload.length(); ++charIndex)
  {
    const char digit = payload[charIndex];
    if (digit < '0' || digit > '9')
    {
      return false;
    }

    const uint32_t digitValue = static_cast<uint32_t>(digit - '0');
    if (parsedDuration > (UINT32_MAX - digitValue) / 10U)
    {
      return false;
    }
    parsedDuration = parsedDuration * 10U + digitValue;
  }

  if (parsedDuration == 0)
  {
    return false;
  }

  durationSeconds = parsedDuration;
  return true;
}

void updateRelayTimers()
{
  const uint32_t now = millis();
  for (int relayIndex = 0; relayIndex < 4; ++relayIndex)
  {
    RelayTimer &timer = relayTimers[relayIndex];
    if (!timer.active)
    {
      continue;
    }

    timer.elapsedMilliseconds += static_cast<uint32_t>(now - timer.lastUpdatedAt);
    timer.lastUpdatedAt = now;
    const uint64_t durationMilliseconds = static_cast<uint64_t>(timer.durationSeconds) * 1000ULL;
    if (timer.elapsedMilliseconds < durationMilliseconds)
    {
      continue;
    }

    timer = RelayTimer{};
    digitalWrite(relayPins[relayIndex], getRelayOffLevel(relayIndex));
    Serial.printf("Relay %d timer expired; relay OFF\n", relayIndex + 1);
    publishRelayState(relayIndex);
    publishRelayTimerState(relayIndex);
  }
}

void configureRelayTimerSubscriptions(int relayIndex)
{
  mqtt.subscribe(relayTimerSetTopics[relayIndex], [relayIndex](const char *payload)
                 {
    if (payload == nullptr)
    {
      return;
    }

    String message = payload;
    message.trim();
    if (message.equalsIgnoreCase("CANCEL"))
    {
      cancelRelayTimer(relayIndex);
      Serial.printf("Relay %d timer canceled\n", relayIndex + 1);
      return;
    }

    uint32_t durationSeconds = 0;
    if (!parseTimerDurationSeconds(message, durationSeconds))
    {
      Serial.printf("Invalid timer duration for Relay %d: %s\n", relayIndex + 1, message.c_str());
      return;
    }

    RelayTimer &timer = relayTimers[relayIndex];
    timer.active = true;
    timer.durationSeconds = durationSeconds;
    timer.elapsedMilliseconds = 0;
    timer.lastUpdatedAt = millis();
    digitalWrite(relayPins[relayIndex], relayOnLevels[relayIndex]);
    Serial.printf("Relay %d ON; timer started for %lu seconds\n",
                  relayIndex + 1, static_cast<unsigned long>(durationSeconds));
    publishRelayState(relayIndex);
    publishRelayTimerState(relayIndex); });

  mqtt.subscribe(relayTimerStateTopics[relayIndex], [relayIndex](const char *payload)
                 {
    if (payload == nullptr)
    {
      return;
    }

    String request = payload;
    request.trim();
    if (request.isEmpty() || request.equalsIgnoreCase("GET") ||
        request.equalsIgnoreCase("QUERY") || request.equalsIgnoreCase("STATE"))
    {
      updateRelayTimers();
      publishRelayTimerState(relayIndex);
    } });

  Serial.printf("Subscribed to %s\n", relayTimerSetTopics[relayIndex]);
  Serial.printf("Listening for timer state requests on %s\n", relayTimerStateTopics[relayIndex]);
}

void RelayMqttServer::on_subscribe(const char *client_id, const char *topic)
{
  (void)client_id;

  for (int relayIndex = 0; relayIndex < 4; relayIndex++)
  {
    if (strcmp(topic, relayStateTopics[relayIndex]) == 0)
    {
      publishRelayState(relayIndex);
      return;
    }

    if (strcmp(topic, relayTimerStateTopics[relayIndex]) == 0)
    {
      updateRelayTimers();
      publishRelayTimerState(relayIndex);
      return;
    }
  }
}

void configureRelaySubscriptions(int relayIndex)
{
  // Subscribe this relay's set topic and interpret commands.
  // Example payloads: ON, OFF, TOGGLE.
  mqtt.subscribe(relaySetTopics[relayIndex], [relayIndex](const char *payload)
                 {
    String message = payload; // Convert payload to string
    message.trim();
    if (message.equalsIgnoreCase("ON")) {
      digitalWrite(relayPins[relayIndex], relayOnLevels[relayIndex]); // Turn on relay
      Serial.printf("Relay %d ON\n", relayIndex + 1);
    } else if (message.equalsIgnoreCase("OFF")) {
      digitalWrite(relayPins[relayIndex], getRelayOffLevel(relayIndex));  // Turn off relay
      cancelRelayTimer(relayIndex);
      Serial.printf("Relay %d OFF\n", relayIndex + 1);
    } else if (message.equalsIgnoreCase("TOGGLE")) {
      int currentState = digitalRead(relayPins[relayIndex]);
      digitalWrite(relayPins[relayIndex], !currentState); // Toggle relay state
      Serial.printf("Relay %d TOGGLED\n", relayIndex + 1);
    } else {
      Serial.printf("Unknown command for Relay %d: %s\n", relayIndex + 1, message.c_str());
      return;
    }

    publishRelayState(relayIndex); });

  // Listen on the relay state topic so the device can respond to state queries.
  // Empty or GET/QUERY/STATE payloads cause the latest relay state to be published.
  mqtt.subscribe(relayStateTopics[relayIndex], [relayIndex](const char *payload)
                 {
    if (payload == nullptr)
      return;

    String request = String(payload);
    request.trim();
    Serial.printf("State request received for relay %d: '%s'\n",
                  relayIndex + 1, request.c_str());
    if (request.isEmpty() || request.equalsIgnoreCase("GET") || request.equalsIgnoreCase("QUERY") || request.equalsIgnoreCase("STATE"))
    {
      publishRelayState(relayIndex);
    } });

  Serial.printf("Subscribed to %s\n", relaySetTopics[relayIndex]);
  Serial.printf("Listening for state requests on %s\n", relayStateTopics[relayIndex]);
  configureRelayTimerSubscriptions(relayIndex);
}

// MQTT Functions
// ==========

// Bootstrap
// ==========

void setup()
{
  // Initialize serial communication
  delay(1000);
  Serial.begin(115200);

  // Sketch tagging
  Serial.println("Sketch: [" + String(SKETCH_NAME) + "]");

  // PIN Configuration
  // --------------------

  // initialize digital pin LED_BUILTIN as an output.
  pinMode(led, OUTPUT);
  digitalWrite(led, HIGH); // turn the LED on during setup

  // Set relay pins as output
  Serial.println("Setting relay pins...");
  for (int i = 0; i < 4; i++)
  {
    pinMode(relayPins[i], OUTPUT);
    digitalWrite(relayPins[i], HIGH);
    Serial.printf("Relay %d GPIO %d initialized HIGH; readback=%s\n",
                  i + 1, relayPins[i], digitalRead(relayPins[i]) == HIGH ? "HIGH" : "LOW");
  }

  // WiFi
  // --------------------
  Serial.printf("Connecting to WiFi: %s\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
#if defined(ARDUINO_LOLIN_S2_MINI)
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
#endif
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(1000);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");

  // PicoMQTT Broker
  // --------------------
  // Start the MQTT broker (local subscribe)
  mqtt.begin();
  Serial.println("MQTT broker started");

  // Register each relay's command and state-response handlers.
  // This lets the board react to commands on /set and answer state requests on /state.
  for (int i = 0; i < 4; i++)
  {
    configureRelaySubscriptions(i);
    publishRelayState(i);
    publishRelayTimerState(i);
  }

  // NTP Client
  // --------------------
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  digitalWrite(led, LOW); // turn the LED off
}

void loop()
{
  // Provides a time slice for system tasks
  yield();

  updateRelayTimers();

  // Check WiFi connection status
  if (WiFi.status() != WL_CONNECTED)
  {
    const uint32_t now = millis();
    if (!wifiDisconnected)
    {
      Serial.println("WiFi disconnected! Reconnecting in the background...");
      digitalWrite(led, HIGH);
      wifiDisconnected = true;
      lastWifiReconnectAttempt = now - WIFI_RECONNECT_INTERVAL_MS;
    }

    if (static_cast<uint32_t>(now - lastWifiReconnectAttempt) >= WIFI_RECONNECT_INTERVAL_MS)
    {
      WiFi.reconnect();
      lastWifiReconnectAttempt = now;
    }
  }
  else if (wifiDisconnected)
  {
    Serial.println("Reconnected to WiFi!");
    digitalWrite(led, LOW);
    wifiDisconnected = false;
  }

  // Check if the local IP has changed
  if (myIP != getlocalIP())
  {
    myIP = getlocalIP();
    Serial.println("Network IP: [" + myIP.toString() + "]");

    // Update the local time from the NTP
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    printLocalTime();
  }

  // Run the MQTT server loop to handle incoming client connections and messages.
  delay(1);
  mqtt.loop();
}
