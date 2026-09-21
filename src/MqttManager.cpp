#include "MqttManager.h"

MqttManager *MqttManager::activeInstance = nullptr;

MqttManager::MqttManager(Client &networkClient): mqttClient(networkClient) {}

void MqttManager::begin(const Config &newConfig) {
    config = newConfig;
    activeInstance = this;

    mqttClient.setServer(config.host.c_str(), config.port);
    mqttClient.setKeepAlive(config.keepAliveSeconds);
    mqttClient.setSocketTimeout(config.socketTimeoutSeconds);
    mqttClient.setCallback(&MqttManager::staticCallback);
    mqttClient.setBufferSize(2048);

    lastReconnectAttemptMs = 0;
    wasConnected = false;
}

void MqttManager::loop() {
    if (!mqttClient.connected()) {
        if (wasConnected) {
            wasConnected = false;
            if (disconnectCallback) {
                disconnectCallback();
            }
        }

        const unsigned long now = millis();
        if (now - lastReconnectAttemptMs >= config.reconnectIntervalMs) {
            lastReconnectAttemptMs = now;
            attemptConnect();
        }
        return;
    }

    mqttClient.loop();
}

bool MqttManager::attemptConnect() {
    const char *clientId = config.clientId.c_str();
    const char *username = config.username.length() ? config.username.c_str() : nullptr;
    const char *password = config.password.length() ? config.password.c_str() : nullptr;
    const char *willTopic = config.willTopic.length() ? config.willTopic.c_str() : nullptr;
    const char *willMessage = config.willMessage.length() ? config.willMessage.c_str() : nullptr;

    const bool connected = mqttClient.connect(
        clientId,
        username,
        password,
        willTopic,
        config.willQos,
        config.willRetain,
        willMessage
    );

    if (connected) {
        wasConnected = true;
        resubscribeAll();
        if (connectCallback) {
            connectCallback();
        }
    }

    return connected;
}

void MqttManager::resubscribeAll() {
    for (const auto &subscription: subscriptions) {
        mqttClient.subscribe(subscription.topic.c_str(), subscription.qos);
    }
}

bool MqttManager::subscribe(const String &topic, uint8_t qos, MessageCallback callback) {
    for (auto &subscription: subscriptions) {
        if (subscription.topic == topic) {
            subscription.qos = qos;
            subscription.callback = std::move(callback);
            return isConnected() ? mqttClient.subscribe(topic.c_str(), qos) : true;
        }
    }

    subscriptions.push_back(Subscription{topic, qos, std::move(callback)});

    if (isConnected()) {
        return mqttClient.subscribe(topic.c_str(), qos);
    }
    return true;
}

bool MqttManager::subscribe(const String &topic, MessageCallback callback) {
    return subscribe(topic, 0, std::move(callback));
}

bool MqttManager::unsubscribe(const String &topic) {
    for (auto it = subscriptions.begin(); it != subscriptions.end(); ++it) {
        if (it->topic == topic) {
            subscriptions.erase(it);
            if (isConnected()) {
                return mqttClient.unsubscribe(topic.c_str());
            }
            return true;
        }
    }
    return false;
}

bool MqttManager::publish(const String &topic, const String &payload, bool retain) {
    if (!isConnected()) {
        return false;
    }
    return mqttClient.publish(topic.c_str(), payload.c_str(), retain);
}

bool MqttManager::publish(const String &topic, const uint8_t *payload, unsigned int length, bool retain) {
    if (!isConnected()) {
        return false;
    }
    return mqttClient.publish(topic.c_str(), payload, length, retain);
}

bool MqttManager::isConnected() {
    return mqttClient.connected();
}

void MqttManager::disconnect() {
    mqttClient.disconnect();
}

void MqttManager::onConnect(ConnectCallback callback) {
    connectCallback = std::move(callback);
}

void MqttManager::onDisconnect(DisconnectCallback callback) {
    disconnectCallback = std::move(callback);
}

PubSubClient &MqttManager::client() {
    return mqttClient;
}

void MqttManager::handleMessage(char *topic, uint8_t *payload, unsigned int length) {
    const String topicStr(topic);
    String payloadStr;
    payloadStr.reserve(length);
    for (unsigned int i = 0; i < length; i++) {
        payloadStr += static_cast<char>(payload[i]);
    }

    for (const auto &subscription: subscriptions) {
        if (topicMatches(subscription.topic, topicStr) && subscription.callback) {
            subscription.callback(topicStr, payloadStr);
        }
    }
}

bool MqttManager::topicMatches(const String &subscribedTopic, const String &incomingTopic) {
    if (subscribedTopic == incomingTopic) {
        return true;
    }
    if (subscribedTopic.indexOf('+') == -1 && subscribedTopic.indexOf('#') == -1) {
        return false;
    }

    int subStart = 0;
    int inStart = 0;
    const int subLen = subscribedTopic.length();
    const int inLen = incomingTopic.length();

    while (subStart <= subLen) {
        int subEnd = subscribedTopic.indexOf('/', subStart);
        const String subLevel = subEnd == -1
            ? subscribedTopic.substring(subStart)
            : subscribedTopic.substring(subStart, subEnd);

        if (subLevel == "#") {
            return true; // '#' matches this level and all remaining levels
        }

        if (inStart > inLen) {
            return false; // ran out of incoming levels before matching subscription levels
        }

        int inEnd = incomingTopic.indexOf('/', inStart);
        const String inLevel = inEnd == -1
            ? incomingTopic.substring(inStart)
            : incomingTopic.substring(inStart, inEnd);

        if (subLevel != "+" && subLevel != inLevel) {
            return false;
        }

        const bool subDone = subEnd == -1;
        const bool inDone = inEnd == -1;
        if (subDone != inDone) {
            return false;
        }
        if (subDone) {
            return true;
        }

        subStart = subEnd + 1;
        inStart = inEnd + 1;
    }

    return false;
}

void MqttManager::staticCallback(char *topic, uint8_t *payload, unsigned int length) {
    if (activeInstance) {
        activeInstance->handleMessage(topic, payload, length);
    }
}
