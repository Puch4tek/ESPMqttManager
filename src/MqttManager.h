#pragma once

#include <Arduino.h>
#include <Client.h>
#include <PubSubClient.h>
#include <functional>
#include <vector>

// MqttManager wraps PubSubClient to provide:
//  - simple connection configuration (host/port/credentials/last-will)
//  - non-blocking automatic reconnection with a configurable retry interval
//  - a subscription registry with per-topic callbacks that is automatically
//    re-subscribed after every reconnect
//  - convenience helpers such as isConnected()
//
// This library is intentionally free of any application specific logic
// (e.g. Home Assistant discovery) so it can be reused across projects.
class MqttManager {
public:
    using MessageCallback = std::function<void(const String &topic, const String &payload)>;
    using ConnectCallback = std::function<void()>;
    using DisconnectCallback = std::function<void()>;

    struct Config {
        String host;
        uint16_t port = 1883;
        String clientId;
        String username;
        String password;
        String willTopic;
        String willMessage;
        uint8_t willQos = 0;
        bool willRetain = false;
        uint16_t keepAliveSeconds = 15;
        uint16_t socketTimeoutSeconds = 4;
        uint32_t reconnectIntervalMs = 5000;
    };

    explicit MqttManager(Client &networkClient);

    // Configures the underlying PubSubClient and (re)starts the connection
    // state machine. Safe to call again to change configuration.
    void begin(const Config &config);

    // Must be called frequently (e.g. every loop() iteration). Drives the
    // PubSubClient loop when connected, or attempts a reconnect on a timer.
    void loop();

    // Registers a topic + callback. If already connected, subscribes right
    // away; the subscription is also replayed automatically after every
    // future reconnect. Supports MQTT wildcards (+ and #).
    bool subscribe(const String &topic, uint8_t qos, MessageCallback callback);
    bool subscribe(const String &topic, MessageCallback callback);

    // Removes a previously registered subscription (and unsubscribes from
    // the broker if currently connected).
    bool unsubscribe(const String &topic);

    bool publish(const String &topic, const String &payload, bool retain = false);
    bool publish(const String &topic, const uint8_t *payload, unsigned int length, bool retain = false);

    [[nodiscard]] bool isConnected();
    void disconnect();

    // Fired every time a connection attempt succeeds (after subscriptions
    // have been replayed) / every time the connection is lost.
    void onConnect(ConnectCallback callback);
    void onDisconnect(DisconnectCallback callback);

    // Escape hatch for advanced use cases not covered by this API.
    PubSubClient &client();

private:
    struct Subscription {
        String topic;
        uint8_t qos;
        MessageCallback callback;
    };

    bool attemptConnect();
    void resubscribeAll();
    void handleMessage(char *topic, uint8_t *payload, unsigned int length);
    static bool topicMatches(const String &subscribedTopic, const String &incomingTopic);
    static void staticCallback(char *topic, uint8_t *payload, unsigned int length);

    PubSubClient mqttClient;
    Config config;
    std::vector<Subscription> subscriptions;
    ConnectCallback connectCallback;
    DisconnectCallback disconnectCallback;
    unsigned long lastReconnectAttemptMs = 0;
    bool wasConnected = false;

    static MqttManager *activeInstance;
};

