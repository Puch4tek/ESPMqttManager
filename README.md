# EspMqttManager

A lightweight manager built on top of [PubSubClient](https://github.com/knolleary/pubsubclient) for ESP32 / ESP8266 projects. It takes care of the boilerplate that almost every MQTT-connected device needs — connecting, automatically reconnecting, and re-subscribing to topics — so you can focus on your application logic instead of connection plumbing.

## Features

- **Simple connection configuration** — host, port, client ID, credentials, and Last Will & Testament (LWT) in a single `Config` struct.
- **Automatic, non-blocking reconnection** — retries on a configurable interval without you having to write any reconnect state machine.
- **Subscription registry with per-topic callbacks** — register a topic once, and it is automatically re-subscribed after every reconnect.
- **MQTT wildcard support** (`+` and `#`) for callback dispatch.
- **Convenience helpers** — `isConnected()`, `disconnect()`, `onConnect()` / `onDisconnect()` hooks.
- **Small footprint** — thin wrapper around PubSubClient, no extra heavy dependencies.
- **Escape hatch** — `client()` gives you direct access to the underlying `PubSubClient` for advanced use cases not covered by the API.

## Table of Contents

- [Installation](#installation)
- [Quick Start](#quick-start)
- [API Reference](#api-reference)
  - [Config](#config)
  - [Constructor](#constructor)
  - [begin](#beginconst-config-config)
  - [loop](#loop)
  - [subscribe](#subscribe)
  - [unsubscribe](#unsubscribeconst-string-topic)
  - [publish](#publish)
  - [isConnected](#isconnected)
  - [disconnect](#disconnect)
  - [onConnect / onDisconnect](#onconnect--ondisconnect)
  - [client](#client)
- [Behavior Notes](#behavior-notes)
- [Limitations](#limitations)
- [License](#license)

## Installation

### PlatformIO

Add the library to your `platformio.ini`:

```ini
lib_deps =
    https://github.com/Puch4tek/ESPMqttManager.git
```

Or, once published to the PlatformIO registry:

```ini
lib_deps =
    EspMqttManager
```

`PubSubClient` (`knolleary/PubSubClient@^2.8`) is declared as a dependency and will be installed automatically.

### Arduino IDE

1. Download or clone this repository into your `Arduino/libraries/` folder (or install via Library Manager once published).
2. Install [PubSubClient](https://github.com/knolleary/pubsubclient) from the Library Manager as well — it is a required dependency.
3. Restart the Arduino IDE.

## Quick Start

```cpp
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include "MqttManager.h"

WiFiClient networkClient;
MqttManager mqttManager(networkClient);

void onCommand(const String &topic, const String &payload) {
    Serial.printf("Received on %s: %s\n", topic.c_str(), payload.c_str());
}

void setup() {
    Serial.begin(115200);

    WiFi.begin("your-ssid", "your-password");
    while (WiFi.status() != WL_CONNECTED) {
        delay(250);
    }

    MqttManager::Config config;
    config.host = "broker.example.com";
    config.port = 1883;
    config.clientId = "my-esp-device";
    config.username = "mqtt-user";
    config.password = "mqtt-pass";
    config.willTopic = "my-esp-device/status";
    config.willMessage = "offline";
    config.willRetain = true;

    mqttManager.onConnect([]() {
        Serial.println("MQTT connected");
        mqttManager.publish("my-esp-device/status", "online", true);
    });

    mqttManager.onDisconnect([]() {
        Serial.println("MQTT disconnected, will retry automatically");
    });

    // Can be called before or after begin() — subscriptions are replayed
    // automatically on every (re)connect.
    mqttManager.subscribe("my-esp-device/command/#", onCommand);

    mqttManager.begin(config);
}

void loop() {
    mqttManager.loop(); // must be called on every loop iteration
}
```

A complete, runnable version of this example lives in [`examples/basic/src/main.cpp`](examples/basic/src/main.cpp).

## API Reference

### `Config`

Passed to `begin()` to configure the connection.

| Field                  | Type       | Default | Description                                                                 |
|------------------------|------------|---------|-------------------------------------------------------------------------------|
| `host`                 | `String`   | `""`    | MQTT broker hostname or IP address.                                          |
| `port`                 | `uint16_t` | `1883`  | MQTT broker port.                                                             |
| `clientId`             | `String`   | `""`    | Client identifier sent to the broker. Must be unique per broker.             |
| `username`             | `String`   | `""`    | Optional broker username. Leave empty for anonymous connections.             |
| `password`             | `String`   | `""`    | Optional broker password.                                                     |
| `willTopic`            | `String`   | `""`    | Optional LWT topic, published by the broker if the device disconnects uncleanly. |
| `willMessage`          | `String`   | `""`    | Optional LWT payload.                                                         |
| `willQos`              | `uint8_t`  | `0`     | QoS level for the LWT message.                                               |
| `willRetain`           | `bool`     | `false` | Whether the LWT message is retained.                                          |
| `keepAliveSeconds`     | `uint16_t` | `15`    | MQTT keep-alive interval.                                                     |
| `socketTimeoutSeconds` | `uint16_t` | `4`     | Socket timeout for connect/read/write operations (see [Behavior Notes](#behavior-notes)). |
| `reconnectIntervalMs`  | `uint32_t` | `5000`  | Minimum time between reconnect attempts while disconnected.                  |

### Constructor

```cpp
explicit MqttManager(Client &networkClient);
```

Takes a reference to a `Client` implementation (e.g. `WiFiClient`, `WiFiClientSecure`, `EthernetClient`). **The client must outlive the `MqttManager` instance** — typically declare both as global/static objects.

### `begin(const Config &config)`

Applies the given configuration to the underlying `PubSubClient` and (re)arms the connection state machine so that the next `loop()` call attempts to connect immediately. Safe to call again later (e.g. to change broker settings at runtime); existing subscriptions are preserved and will be replayed against the new connection.

### `loop()`

Must be called on every iteration of your sketch's `loop()`. It:

- Drives `PubSubClient::loop()` while connected (handles keep-alive pings and incoming message dispatch).
- Attempts to reconnect on a timer (`reconnectIntervalMs`) while disconnected.
- Fires `onDisconnect` the moment a previously-established connection is detected as lost.
- Fires `onConnect` (after replaying all subscriptions) whenever a (re)connection succeeds.

### `subscribe`

```cpp
bool subscribe(const String &topic, uint8_t qos, MessageCallback callback);
bool subscribe(const String &topic, MessageCallback callback); // qos = 0
```

Registers a topic and its callback (`void(const String &topic, const String &payload)`).

- If already connected, subscribes immediately at the broker.
- The subscription is stored internally and **automatically re-subscribed** after every future reconnect — you never need to re-subscribe manually.
- Calling `subscribe()` again with the same topic replaces the existing callback/QoS.
- Supports MQTT wildcards (`+` for a single level, `#` for multiple trailing levels) — matching is performed locally against all registered subscriptions when a message arrives.
- Returns `true` if the subscription was registered/sent successfully (or queued for when the connection is established), `false` if the broker rejected the immediate subscribe attempt.

### `unsubscribe(const String &topic)`

Removes a previously registered subscription by topic string (must match exactly, including wildcards, as originally registered). Also unsubscribes at the broker if currently connected. Returns `false` if the topic wasn't registered.

### `publish`

```cpp
bool publish(const String &topic, const String &payload, bool retain = false);
bool publish(const String &topic, const uint8_t *payload, unsigned int length, bool retain = false);
```

Publishes a message. Returns `false` immediately (without attempting to send) if not currently connected — check `isConnected()` or rely on the return value.

### `isConnected()`

```cpp
[[nodiscard]] bool isConnected();
```

Returns whether the underlying `PubSubClient` currently reports a live connection.

### `disconnect()`

Gracefully disconnects from the broker. `loop()` will attempt to reconnect again after `reconnectIntervalMs`, following the same automatic reconnect/resubscribe flow as any other disconnect.

### `onConnect` / `onDisconnect`

```cpp
void onConnect(ConnectCallback callback);       // void()
void onDisconnect(DisconnectCallback callback); // void()
```

Register callbacks fired on every successful (re)connection and every detected disconnection, respectively. Useful for publishing an "online"/availability message on connect, or logging/state tracking on disconnect. `onConnect` fires **after** all subscriptions have already been replayed, so it's safe to publish immediately inside it.

### `client()`

```cpp
PubSubClient &client();
```

Returns a reference to the underlying `PubSubClient` instance, for advanced use cases not covered by this API (e.g. custom buffer sizes, direct access to less common PubSubClient options).

## Behavior Notes

- **Blocking calls**: `PubSubClient::connect()`, `loop()`, and `publish()` are synchronous. If the broker is unreachable or slow, these calls can block your sketch's main loop for up to `socketTimeoutSeconds`. Keep this in mind on time-sensitive applications; consider using a shorter `socketTimeoutSeconds` if you need faster failure detection, or run MQTT in a dedicated task if your application cannot tolerate any stalling.
- **Callbacks run synchronously and inline**: message, connect, and disconnect callbacks are invoked directly from within `loop()`. Keep them fast and non-blocking — avoid `delay()` or long-running work inside them, as it will stall MQTT keep-alives and your main loop.
- **Fixed reconnect interval**: reconnect attempts happen at a constant `reconnectIntervalMs` cadence with no backoff. If your broker is down for an extended period, the device will keep retrying at that fixed rate indefinitely.
- **Network connectivity is your responsibility**: this library does not manage Wi-Fi/Ethernet — ensure your network connection is established (and stays established) independently; `MqttManager` only manages the MQTT session on top of the `Client` you provide.

## Limitations

- **Single active instance**: `MqttManager` uses a single static callback trampoline internally to bridge PubSubClient's C-style callback into instance methods. If you create more than one `MqttManager` instance, only the most recently `begin()`-initialized instance will correctly route incoming messages. If you need multiple brokers/clients, track this limitation and avoid running more than one instance simultaneously.
- **No QoS 1/2 delivery guarantees beyond what PubSubClient itself offers**: this library does not add message persistence or retry/acknowledgement tracking on top of PubSubClient.
- **No TLS-specific helpers**: pass a `WiFiClientSecure` (or equivalent) as the `Client` if you need TLS; certificate/cipher configuration must be done on that client directly, outside this library.

## License

MIT — see [LICENSE](LICENSE) for details.
