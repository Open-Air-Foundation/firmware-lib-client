/**
 * AirGradient
 * https://airgradient.com
 *
 * CC BY-SA 4.0 Attribution-ShareAlike 4.0 International License
 */

#ifndef ESP8266

#ifndef AG_COMMON_H
#define AG_COMMON_H

#include <string>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

#define DELAY_MS(ms) vTaskDelay(pdMS_TO_TICKS(ms))
#define MILLIS() ((uint32_t)(esp_timer_get_time() / 1000))

class Common {
public:
  static void splitByDelimiter(const std::string &data, int *v1, int *v2, char delimiter = ',') {
    size_t pos = data.find(delimiter);
    if (pos != std::string::npos) {
      *v1 = std::stoi(data.substr(0, pos));
      *v2 = std::stoi(data.substr(pos + 1, data.length()));
    }
  }
  static void splitByDelimiter(const std::string &data, std::string &v1, std::string &v2,
                               char delimiter = ',') {
    size_t pos = data.find(delimiter);
    if (pos != std::string::npos) {
      v1 = data.substr(0, pos);
      v2 = data.substr(pos + 1, data.length());
    }
  }

  static bool isValidMqttCredential(const std::string &value) {
    return !value.empty() &&
           value.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
                                   "0123456789-._~!$&'()*+,;=") == std::string::npos;
  }

  static bool isValidMqttHost(const std::string &host) {
    // Leave DNS/IPv4 validation to the network stack.
    return !host.empty() &&
           host.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
                                  "0123456789.-") == std::string::npos;
  }

  // MQTT URI only: no encoding, paths, queries, fragments or IPv6.
  // Only use the parsed fields when this returns true.
  static bool parseUri(const std::string &uri, std::string &protocol, std::string &username,
                       std::string &password, std::string &host, int &port) {
    protocol.clear();
    username.clear();
    password.clear();
    host.clear();
    port = -1;

    size_t pos = uri.find("://");
    if (pos != std::string::npos) {
      if (uri.compare(0, pos, "mqtt") != 0) {
        return false;
      }
      protocol = "mqtt";
      pos += 3; // skip "://"
    } else {
      pos = 0;
    }
    if (uri.size() - pos > 165) {
      return false;
    }

    // Only one @ is permitted, separating credentials from the host.
    size_t atPos = uri.find('@', pos);
    if (atPos != std::string::npos) {
      if (uri.find('@', atPos + 1) != std::string::npos) {
        return false;
      }
      std::string userinfo = uri.substr(pos, atPos - pos);
      pos = atPos + 1;

      size_t colonPos = userinfo.find(':');
      username = userinfo.substr(0, colonPos);
      if (colonPos != std::string::npos) {
        password = userinfo.substr(colonPos + 1);
        // The whitelist also rejects any additional colon.
        if (!isValidMqttCredential(password)) {
          return false;
        }
      }
      if (!isValidMqttCredential(username)) {
        return false;
      }
    }

    // Host and optional port
    size_t colonPos = uri.find(':', pos);
    host = uri.substr(pos, colonPos == std::string::npos ? std::string::npos : colonPos - pos);
    if (!isValidMqttHost(host)) {
      return false;
    }
    if (colonPos != std::string::npos) {
      if (colonPos + 1 == uri.size()) {
        return false;
      }
      port = 0;
      for (size_t i = colonPos + 1; i < uri.size(); ++i) {
        char c = uri[i];
        if (c < '0' || c > '9') {
          return false;
        }
        port = port * 10 + (c - '0');
        if (port > 65535) {
          return false;
        }
      }
      if (port == 0) {
        return false;
      }
    }

    return true;
  }

  // Validate and add the scheme before handing a URI to ESP-MQTT.
  // Invalid input is left unchanged.
  static bool normalizeMqttUri(std::string &uri) {
    std::string protocol, username, password, host;
    int port;
    if (!parseUri(uri, protocol, username, password, host, port)) {
      return false;
    }
    if (protocol.empty()) {
      uri.insert(0, "mqtt://");
    }
    return true;
  }
};

#endif // AG_COMMON_H
#endif // ESP8266
