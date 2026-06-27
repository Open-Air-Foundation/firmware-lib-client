/**
 * AirGradient
 * https://airgradient.com
 *
 * CC BY-SA 4.0 Attribution-ShareAlike 4.0 International License
 */

#ifndef AIRGRADIENT_WIFI_CLIENT_H
#define AIRGRADIENT_WIFI_CLIENT_H

#ifndef ESP8266
#define ARDUINOJSON_ENABLE_PROGMEM 0

#include <string>
#include <ArduinoJson.h>

#include "airgradientClient.h"
#ifdef ARDUINO
#include "endpointSelector.h"
#endif

#ifndef ARDUINO
#define MAX_RESPONSE_BUFFER 2048
#endif

class AirgradientWifiClient : public AirgradientClient {
private:
  const char *const TAG = "AgWifiClient";
  uint16_t timeoutMs = 15000; // Default set to 15s
#ifndef ARDUINO
  char responseBuffer[2048];
#endif
#ifdef ARDUINO
  EndpointSelector selector_;
  bool selectorReady_ = false;
  std::string selectorHost_;
#endif
public:
  AirgradientWifiClient() {};
  ~AirgradientWifiClient() {};

  bool begin(std::string sn, PayloadType pt);
  std::string httpFetchConfig();
  bool httpPostMeasures(const std::string &payload);
  bool httpPostMeasures(const AirgradientPayload &payload);

private:
  bool _httpGet(const std::string &url, int &responseCode, std::string &responseBody);
  bool _httpPost(const std::string &url, const std::string &payload, int &responseCode);
  void _serialize(JsonDocument &doc, const PayloadBuffer &payload);

#ifdef ARDUINO
  // Unified HTTPS GET/POST helpers used by both the IP-failover loop and
  // the hostname fallback. If `ip` is 0.0.0.0, the WiFiClientSecure will
  // resolve `host` via lwIP itself; otherwise TCP connects to `ip` while
  // SNI + cert validation still use `host`. Return false on any
  // transport/TLS failure or negative HTTP response code; true on any
  // valid HTTP response (status code stored in responseCode).
  bool _httpGetSecure(const IPAddress &ip, const char *host, const std::string &path,
                      int &responseCode, std::string &responseBody);
  bool _httpPostSecure(const IPAddress &ip, const char *host, const std::string &path,
                       const std::string &payload, int &responseCode);

  // Ensure selector is initialized and tracks the current httpDomain.
  // Performs lazy first-time init and periodic refresh.
  void _ensureSelectorReady();

  // Extract path portion from a full URL ("https://host/path" -> "/path").
  // Returns empty string if the URL does not match the expected format.
  std::string _extractPath(const std::string &url) const;
#endif
};

#endif // ESP8266
#endif // !AIRGRADIENT_WIFI_CLIENT_H
