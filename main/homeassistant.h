#pragma once

#include <Arduino.h>
#include <WebServer.h>

enum class VehicleFilterMode {
  All,
  Tram,
  Bus,
};

class HomeAssistantControl {
 public:
  explicit HomeAssistantControl(uint16_t port = 80)
      : server(port) {}

  void begin(const char* token, uint16_t defaultWakeMinutes) {
    authToken = token ? String(token) : String("");
    wakeDefaultMinutes = defaultWakeMinutes;

    server.on("/wake", HTTP_GET, [this]() {
      if (!isAuthorized()) {
        return;
      }
      uint16_t mins = getMinutesArg(wakeDefaultMinutes);
      mode = VehicleFilterMode::All;
      wakeForMinutes(mins);
      refreshRequested = true;
      server.send(200, "application/json", "{\"ok\":true,\"action\":\"wake\",\"mode\":\"all\"}");
    });

    server.on("/sleep", HTTP_GET, [this]() {
      if (!isAuthorized()) {
        return;
      }
      sleepNow();
      server.send(200, "application/json", "{\"ok\":true,\"action\":\"sleep\",\"mode\":\"all\"}");
    });

    server.on("/mode", HTTP_GET, [this]() {
      if (!isAuthorized()) {
        return;
      }

      if (!server.hasArg("value")) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"missing value\"}");
        return;
      }

      String value = server.arg("value");
      value.toLowerCase();
      if (value == "all") {
        mode = VehicleFilterMode::All;
      } else if (value == "tram") {
        mode = VehicleFilterMode::Tram;
      } else if (value == "bus") {
        mode = VehicleFilterMode::Bus;
      } else {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid mode\"}");
        return;
      }

      uint16_t mins = getMinutesArg(wakeDefaultMinutes);
      wakeForMinutes(mins);
      refreshRequested = true;

      String response = "{\"ok\":true,\"mode\":\"" + modeToString(mode) + "\"}";
      server.send(200, "application/json", response);
    });

    server.on("/state", HTTP_GET, [this]() {
      if (!isAuthorized()) {
        return;
      }

      String response = "{\"ok\":true,\"mode\":\"" + modeToString(mode) + "\",\"awake\":";
      response += isAwake() ? "true" : "false";
      response += "}";
      server.send(200, "application/json", response);
    });

    server.on("/scroll_up", HTTP_GET, [this]() {
      if (!isAuthorized()) {
        return;
      }
      scrollUpRequested = true;
      server.send(200, "application/json", "{\"ok\":true,\"action\":\"scroll_up\"}");
    });

    server.on("/scroll_down", HTTP_GET, [this]() {
      if (!isAuthorized()) {
        return;
      }
      scrollDownRequested = true;
      server.send(200, "application/json", "{\"ok\":true,\"action\":\"scroll_down\"}");
    });

    server.begin();
  }

  void handle() {
    server.handleClient();

    bool awakeNow = isAwake();
    if (!awakeNow && wasAwake) {
      // Always reset filter back to all once display goes off.
      mode = VehicleFilterMode::All;
    }
    wasAwake = awakeNow;
  }

  void wakeForMinutes(uint16_t minutes) {
    if (minutes == 0) {
      minutes = wakeDefaultMinutes;
    }
    wakeUntilMs = millis() + (unsigned long)minutes * 60000UL;
    wasAwake = true;
  }

  bool isAwake() const {
    return (long)(wakeUntilMs - millis()) > 0;
  }

  VehicleFilterMode getMode() const {
    return mode;
  }

  bool consumeRefreshRequest() {
    bool pending = refreshRequested;
    refreshRequested = false;
    return pending;
  }

  bool consumeScrollUpRequest() {
    bool pending = scrollUpRequested;
    scrollUpRequested = false;
    return pending;
  }

  bool consumeScrollDownRequest() {
    bool pending = scrollDownRequested;
    scrollDownRequested = false;
    return pending;
  }

  void sleepNow() {
    wakeUntilMs = 0;
    mode = VehicleFilterMode::All;
    refreshRequested = false;
  }

 private:
  WebServer server;
  String authToken;
  VehicleFilterMode mode = VehicleFilterMode::All;
  unsigned long wakeUntilMs = 0;
  uint16_t wakeDefaultMinutes = 5;
  bool wasAwake = false;
  bool refreshRequested = false;
  bool scrollUpRequested = false;
  bool scrollDownRequested = false;

  String modeToString(VehicleFilterMode m) const {
    switch (m) {
      case VehicleFilterMode::Tram:
        return "tram";
      case VehicleFilterMode::Bus:
        return "bus";
      default:
        return "all";
    }
  }

  uint16_t getMinutesArg(uint16_t fallback) {
    if (!server.hasArg("minutes")) {
      return fallback;
    }
    int parsed = server.arg("minutes").toInt();
    if (parsed <= 0) {
      return fallback;
    }
    if (parsed > 120) {
      parsed = 120;
    }
    return (uint16_t)parsed;
  }

  bool isAuthorized() {
    if (authToken.length() == 0) {
      return true;
    }

    if (!server.hasArg("token") || server.arg("token") != authToken) {
      server.send(401, "application/json", "{\"ok\":false,\"error\":\"unauthorized\"}");
      return false;
    }
    return true;
  }
};
