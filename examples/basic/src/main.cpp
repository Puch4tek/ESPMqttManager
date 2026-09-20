#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include "MqttManager.h"

WiFiClient networkClient;
MqttManager mqttManager(networkClient);

const char *WIFI_SSID = "your-ssid";
const char *WIFI_PASSWORD = "your-password";

void onSensorCommand(const String &topic, const String &payload) {
    Serial.printf("Received on %s: %s\n", topic.c_str(), payload.c_str());
}

void setup() {
    Serial.begin(115200);

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
        delay(250);
        Serial.print(".");
    }
    Serial.println("\nWiFi connected");

    MqttManager::Config config;
    config.host = "broker.example.com";
    config.port = 1883;
    config.clientId = "esp-mqtt-manager-example";
    config.username = "mqtt-user";
    config.password = "mqtt-pass";
    config.willTopic = "esp-mqtt-manager-example/status";
    config.willMessage = "offline";
    config.willRetain = true;
    config.reconnectIntervalMs = 5000;

    mqttManager.onConnect([]() {
        Serial.println("MQTT connected");
        mqttManager.publish("esp-mqtt-manager-example/status", "online", true);
    });

    mqttManager.onDisconnect([]() {
        Serial.println("MQTT disconnected, will retry automatically");
    });

    // Subscriptions registered before or after begin() are all replayed
    // automatically on every (re)connect.
    mqttManager.subscribe("esp-mqtt-manager-example/command/#", onSensorCommand);

    mqttManager.begin(config);
}

void loop() {
    mqttManager.loop();

    if (mqttManager.isConnected()) {
        // ... application logic ...
    }
}
