/**
 * AirGradient
 * https://airgradient.com
 *
 * CC BY-SA 4.0 Attribution-ShareAlike 4.0 International License
 */

#include "airgradientClient.h"
#include "config.h"
#ifndef ESP8266
#define ARDUINOJSON_ENABLE_PROGMEM 0

#define JSON_PROP_PM_FIRMWARE "firmware"
#define JSON_PROP_PM01_AE "pm01"
#define JSON_PROP_PM25_AE "pm02"
#define JSON_PROP_PM10_AE "pm10"
#define JSON_PROP_PM01_SP "pm01Standard"
#define JSON_PROP_PM25_SP "pm02Standard"
#define JSON_PROP_PM10_SP "pm10Standard"
#define JSON_PROP_PM25_COMPENSATED "pm02Compensated"
#define JSON_PROP_PM03_COUNT "pm003Count"
#define JSON_PROP_PM05_COUNT "pm005Count"
#define JSON_PROP_PM1_COUNT "pm01Count"
#define JSON_PROP_PM25_COUNT "pm02Count"
#define JSON_PROP_PM5_COUNT "pm50Count"
#define JSON_PROP_PM10_COUNT "pm10Count"
#define JSON_PROP_TEMP "atmp"
#define JSON_PROP_TEMP_COMPENSATED "atmpCompensated"
#define JSON_PROP_RHUM "rhum"
#define JSON_PROP_RHUM_COMPENSATED "rhumCompensated"
#define JSON_PROP_TVOC "tvocIndex"
#define JSON_PROP_TVOC_RAW "tvocRaw"
#define JSON_PROP_NOX "noxIndex"
#define JSON_PROP_NOX_RAW "noxRaw"
#define JSON_PROP_CO2 "rco2"
#define JSON_PROP_VBATT "volt"
#define JSON_PROP_VPANEL "light"
#define JSON_PROP_O3_WE "measure0"
#define JSON_PROP_O3_AE "measure1"
#define JSON_PROP_NO2_WE "measure2"
#define JSON_PROP_NO2_AE "measure3"
#define JSON_PROP_AFE_TEMP "measure4"
#define JSON_PROP_SIGNAL "wifi"

#include "airgradientWifiClient.h"
#include "agLogger.h"
#include "ArduinoJson.h"

#ifdef ARDUINO
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "common.h"
#else
#include "esp_http_client.h"
#endif

bool AirgradientWifiClient::begin(std::string sn, PayloadType pt) {
  serialNumber = sn;
  payloadType = pt;
  return true;
}

std::string AirgradientWifiClient::httpFetchConfig() {
  std::string url = buildFetchConfigUrl(true);
  AG_LOGI(TAG, "Fetch configuration from %s", url.c_str());

  // Perform HTTP GET
  int responseCode;
  std::string responseBody;
  if (_httpGet(url, responseCode, responseBody) == false) {
    lastFetchConfigSucceed = false;
    return {};
  }

  // Define result by response code
  if (responseCode != 200) {
    // client.end();
    AG_LOGE(TAG, "Failed fetch configuration from server with return code %d", responseCode);
    // Return code 400 means device not registered on ag server
    if (responseCode == 400) {
      registeredOnAgServer = false;
    }
    lastFetchConfigSucceed = false;
    return {};
  }

  // Sanity check if response body is empty
  if (responseBody.empty()) {
    AG_LOGW(TAG, "Success fetch configuration from server but somehow body is empty");
    lastFetchConfigSucceed = false;
    return responseBody;
  }

  AG_LOGI(TAG, "Received configuration: (%d) %s", responseBody.length(), responseBody.c_str());

  // Set success state flag
  registeredOnAgServer = true;
  lastFetchConfigSucceed = true;
  AG_LOGI(TAG, "Success fetch configuration from server, still needs to be parsed and validated");

  return responseBody;
}
bool AirgradientWifiClient::httpPostMeasures(const std::string &payload) {
  std::string url = buildPostMeasuresUrl(true);
  AG_LOGI(TAG, "Post measures to %s", url.c_str());
  AG_LOGI(TAG, "Payload: %s", payload.c_str());

  // Perform HTTP POST
  int responseCode;
  if (_httpPost(url, payload, responseCode) == false) {
    lastPostMeasuresSucceed = false;
    return false;
  }

  if ((responseCode != 200) && (responseCode != 429)) {
    AG_LOGE(TAG, "Failed post measures to server with response code %d", responseCode);
    lastPostMeasuresSucceed = false;
    return false;
  }

  lastPostMeasuresSucceed = true;
  AG_LOGI(TAG, "Success post measures to server with response code %d", responseCode);

  return true;
}

bool AirgradientWifiClient::httpPostMeasures(const AirgradientPayload &payload) {
  if (payload.bufferCount > 1) {
    AG_LOGI(TAG, "WiFi payload cannot handle more than 1 buffer");
    return false;
  }

  JsonDocument jdoc;
  jdoc[JSON_PROP_SIGNAL] = payload.signal;
  _serialize(jdoc, payload.payloadBuffer[0]);

  // Serialize the JSON document to a string
  std::string toSend;
  size_t bytesWritten = serializeJson(jdoc, toSend);
  if (bytesWritten == 0) {
    AG_LOGE(TAG, "Serialize json failed");
    return false;
  }

  return httpPostMeasures(toSend);
}

bool AirgradientWifiClient::_httpGet(const std::string &url, int &responseCode,
                                     std::string &responseBody) {
#ifdef ARDUINO
  _ensureSelectorReady();
  std::string path = _extractPath(url);
  if (path.empty()) {
    AG_LOGE(TAG, "Could not extract path from URL: %s", url.c_str());
    return false;
  }
  const char *host = httpDomain.c_str();

  // Phase 1: try every resolved IP exactly once, in cursor order. The
  // cursor stays put on success (sticky) and advances on failure.
  bool ipsExhausted = false;
  if (selectorReady_ && selector_.count() > 0) {
    const uint8_t total = selector_.count();
    for (uint8_t i = 0; i < total; ++i) {
      IPAddress ip = selector_.current();
      if (_httpGetSecure(ip, host, path, responseCode, responseBody)) {
        return true;
      }
      selector_.advance();
    }
    ipsExhausted = true;
    AG_LOGW(TAG, "All %u IP(s) failed; falling back to hostname resolution", total);
  }

  // Phase 2: hostname fallback. Same WiFiClientSecure + HTTPClient API,
  // just let WiFiClientSecure do its own DNS resolution.
  if (_httpGetSecure(IPAddress(static_cast<uint32_t>(0)), host, path, responseCode, responseBody)) {
    if (ipsExhausted) {
      AG_LOGI(TAG, "Hostname fallback succeeded after IP exhaustion; refreshing DNS");
      selector_.refresh();
    }
    return true;
  }
  AG_LOGE(TAG, "Both IP and hostname paths failed");
  return false;
#else
  esp_http_client_config_t config = {};
  config.url = url.c_str();
  config.method = HTTP_METHOD_GET;
  config.cert_pem = AirgradientServerConfig::AG_SERVER_ROOT_CA;

  esp_http_client_handle_t client = esp_http_client_init(&config);

  if (esp_http_client_open(client, 0) != ESP_OK) {
    AG_LOGE(TAG, "Failed perform HTTP GET");
    esp_http_client_cleanup(client);
    return false;
  }
  esp_http_client_fetch_headers(client);
  responseCode = esp_http_client_get_status_code(client);

  int totalRead = 0;
  int readLen;
  while ((readLen = esp_http_client_read(client, responseBuffer + totalRead,
                                         MAX_RESPONSE_BUFFER - totalRead)) > 0) {
    totalRead += readLen;
  }

  esp_http_client_close(client);
  esp_http_client_cleanup(client);

  responseBuffer[totalRead] = '\0';
  responseBody = std::string(responseBuffer);

  return true;
#endif
}

bool AirgradientWifiClient::_httpPost(const std::string &url, const std::string &payload,
                                      int &responseCode) {
#ifdef ARDUINO
  _ensureSelectorReady();
  std::string path = _extractPath(url);
  if (path.empty()) {
    AG_LOGE(TAG, "Could not extract path from URL: %s", url.c_str());
    return false;
  }
  const char *host = httpDomain.c_str();

  // Phase 1: try every resolved IP exactly once, in cursor order. The
  // cursor stays put on success (sticky) and advances on failure.
  bool ipsExhausted = false;
  if (selectorReady_ && selector_.count() > 0) {
    const uint8_t total = selector_.count();
    for (uint8_t i = 0; i < total; ++i) {
      IPAddress ip = selector_.current();
      if (_httpPostSecure(ip, host, path, payload, responseCode)) {
        return true;
      }
      selector_.advance();
    }
    ipsExhausted = true;
    AG_LOGW(TAG, "All %u IP(s) failed; falling back to hostname resolution", total);
  }

  // Phase 2: hostname fallback via the same WiFiClientSecure API.
  if (_httpPostSecure(IPAddress(static_cast<uint32_t>(0)), host, path, payload, responseCode)) {
    if (ipsExhausted) {
      AG_LOGI(TAG, "Hostname fallback succeeded after IP exhaustion; refreshing DNS");
      selector_.refresh();
    }
    return true;
  }
  AG_LOGE(TAG, "Both IP and hostname paths failed");
  return false;
#else
  esp_http_client_config_t config = {};
  config.url = url.c_str();
  config.method = HTTP_METHOD_POST;
  config.cert_pem = AirgradientServerConfig::AG_SERVER_ROOT_CA;
  config.timeout_ms = timeoutMs;
  esp_http_client_handle_t client = esp_http_client_init(&config);

  esp_http_client_set_header(client, "Content-Type", "application/json");
  esp_http_client_set_post_field(client, payload.c_str(), payload.length());

  if (esp_http_client_perform(client) != ESP_OK) {
    AG_LOGE(TAG, "Failed perform HTTP POST");
    esp_http_client_cleanup(client);
    return false;
  }
  responseCode = esp_http_client_get_status_code(client);
  esp_http_client_cleanup(client);

  return true;
#endif
}

void AirgradientWifiClient::_serialize(JsonDocument &doc, const PayloadBuffer &payload) {
  // Check and add CO2 value
  if (IS_CO2_VALID(payload.common.rco2)) {
    doc[JSON_PROP_CO2] = payload.common.rco2;
  }

  // Check and add Particle Count
  {
    const bool pc0Ok = IS_PM_VALID(payload.common.particleCount003[0]);
    const bool pc1Ok = IS_PM_VALID(payload.common.particleCount003[1]);
    if (pc0Ok && pc1Ok) {
      doc[JSON_PROP_PM03_COUNT] =
          (payload.common.particleCount003[0] + payload.common.particleCount003[1]) / 2;
    } else if (pc0Ok) {
      doc[JSON_PROP_PM03_COUNT] = payload.common.particleCount003[0];
    } else if (pc1Ok) {
      doc[JSON_PROP_PM03_COUNT] = payload.common.particleCount003[1];
    }
  }

  // Check and add PM values
  if (IS_PM_VALID(payload.common.pm01)) {
    doc[JSON_PROP_PM01_AE] = payload.common.pm01;
  }
  {
    const bool pm0Ok = IS_PM_VALID(payload.common.pm25[0]);
    const bool pm1Ok = IS_PM_VALID(payload.common.pm25[1]);
    if (pm0Ok && pm1Ok) {
      doc[JSON_PROP_PM25_AE] = (payload.common.pm25[0] + payload.common.pm25[1]) / 2.0f;
    } else if (pm0Ok) {
      doc[JSON_PROP_PM25_AE] = payload.common.pm25[0];
    } else if (pm1Ok) {
      doc[JSON_PROP_PM25_AE] = payload.common.pm25[1];
    }
  }
  if (IS_PM_VALID(payload.common.pm10)) {
    doc[JSON_PROP_PM10_AE] = payload.common.pm10;
  }

  // Check and add TVOC and NOx values
  if (IS_TVOC_VALID(payload.common.tvoc)) {
    doc[JSON_PROP_TVOC] = payload.common.tvoc;
  }
  if (IS_TVOC_VALID(payload.common.tvocRaw)) {
    doc[JSON_PROP_TVOC_RAW] = payload.common.tvocRaw;
  }
  if (IS_NOX_VALID(payload.common.nox)) {
    doc[JSON_PROP_NOX] = payload.common.nox;
  }
  if (IS_NOX_VALID(payload.common.noxRaw)) {
    doc[JSON_PROP_NOX_RAW] = payload.common.noxRaw;
  }

  // Check and add Temperature and Humidity
  if (IS_TEMPERATURE_VALID(payload.common.atmp)) {
    doc[JSON_PROP_TEMP] = payload.common.atmp;
  }
  if (IS_HUMIDITY_VALID(payload.common.rhum)) {
    doc[JSON_PROP_RHUM] = payload.common.rhum;
  }

  // Check and add Voltage-related values
  if (IS_VOLT_VALID(payload.ext.extra.vBat)) {
    doc[JSON_PROP_VBATT] = payload.ext.extra.vBat;
  }
  if (IS_VOLT_VALID(payload.ext.extra.vPanel)) {
    doc[JSON_PROP_VPANEL] = payload.ext.extra.vPanel;
  }
  if (IS_VOLT_VALID(payload.ext.extra.o3WorkingElectrode)) {
    doc[JSON_PROP_O3_WE] = payload.ext.extra.o3WorkingElectrode;
  }
  if (IS_VOLT_VALID(payload.ext.extra.o3AuxiliaryElectrode)) {
    doc[JSON_PROP_O3_AE] = payload.ext.extra.o3AuxiliaryElectrode;
  }
  if (IS_VOLT_VALID(payload.ext.extra.no2WorkingElectrode)) {
    doc[JSON_PROP_NO2_WE] = payload.ext.extra.no2WorkingElectrode;
  }
  if (IS_VOLT_VALID(payload.ext.extra.no2AuxiliaryElectrode)) {
    doc[JSON_PROP_NO2_AE] = payload.ext.extra.no2AuxiliaryElectrode;
  }
  if (IS_VOLT_VALID(payload.ext.extra.afeTemp)) {
    doc[JSON_PROP_AFE_TEMP] = payload.ext.extra.afeTemp;
  }
}

#ifdef ARDUINO

bool AirgradientWifiClient::_httpGetSecure(const IPAddress &ip, const char *host,
                                           const std::string &path, int &responseCode,
                                           std::string &responseBody) {
  const bool usingIp = (static_cast<uint32_t>(ip) != 0);
  // Note: must extend the String lifetime to function scope; cannot use
  // `ip.toString().c_str()` directly (temporary gets destroyed).
  String ipStr = usingIp ? ip.toString() : String();
  const char *target = usingIp ? ipStr.c_str() : host;

  WiFiClientSecure secClient;
  secClient.setCACert(AirgradientServerConfig::AG_SERVER_ROOT_CA);
  // setTimeout() expects seconds.
  secClient.setTimeout((timeoutMs + 500) / 1000);

  // When we have a selected IP: 6-arg overload connects TCP to `ip` but
  // SNI + cert verification still use `host`. When we don't: let
  // WiFiClientSecure resolve `host` itself (which internally calls the
  // same 6-arg overload with the resolved IP).
  uint32_t t0 = MILLIS();
  int connectRet;
  if (usingIp) {
    connectRet = secClient.connect(ip, 443, host, AirgradientServerConfig::AG_SERVER_ROOT_CA,
                                   nullptr, nullptr);
  } else {
    connectRet = secClient.connect(host, 443, AirgradientServerConfig::AG_SERVER_ROOT_CA, nullptr,
                                   nullptr);
  }
  uint32_t connectDt = MILLIS() - t0;
  if (connectRet != 1) {
    AG_LOGW(TAG, "TLS connect to %s failed in %ums (ret=%d)", target, connectDt, connectRet);
    return false;
  }
  AG_LOGI(TAG, "TLS up to %s in %ums", target, connectDt);

  HTTPClient client;
  client.setConnectTimeout(timeoutMs);
  client.setTimeout(timeoutMs);
  // begin(WiFiClient&, host, port, uri, https) reuses the already-connected
  // socket and sets the Host header from `host`.
  if (client.begin(secClient, host, 443, path.c_str(), true) == false) {
    AG_LOGE(TAG, "Failed begin HTTPClient on pre-connected client");
    secClient.stop();
    return false;
  }

  responseCode = client.GET();
  if (responseCode <= 0) {
    AG_LOGW(TAG, "HTTP GET via %s failed: %d (%s)", target, responseCode,
            HTTPClient::errorToString(responseCode).c_str());
    client.end();
    return false;
  }
  responseBody = client.getString().c_str();
  client.end();
  return true;
}

bool AirgradientWifiClient::_httpPostSecure(const IPAddress &ip, const char *host,
                                            const std::string &path, const std::string &payload,
                                            int &responseCode) {
  const bool usingIp = (static_cast<uint32_t>(ip) != 0);
  String ipStr = usingIp ? ip.toString() : String();
  const char *target = usingIp ? ipStr.c_str() : host;

  WiFiClientSecure secClient;
  secClient.setCACert(AirgradientServerConfig::AG_SERVER_ROOT_CA);
  secClient.setTimeout((timeoutMs + 500) / 1000);

  uint32_t t0 = MILLIS();
  int connectRet;
  if (usingIp) {
    connectRet = secClient.connect(ip, 443, host, AirgradientServerConfig::AG_SERVER_ROOT_CA,
                                   nullptr, nullptr);
  } else {
    connectRet = secClient.connect(host, 443, AirgradientServerConfig::AG_SERVER_ROOT_CA, nullptr,
                                   nullptr);
  }
  uint32_t connectDt = MILLIS() - t0;
  if (connectRet != 1) {
    AG_LOGW(TAG, "TLS connect to %s failed in %ums (ret=%d)", target, connectDt, connectRet);
    return false;
  }
  AG_LOGI(TAG, "TLS up to %s in %ums", target, connectDt);

  HTTPClient client;
  client.setConnectTimeout(timeoutMs);
  client.setTimeout(timeoutMs);
  if (client.begin(secClient, host, 443, path.c_str(), true) == false) {
    AG_LOGE(TAG, "Failed begin HTTPClient on pre-connected client");
    secClient.stop();
    return false;
  }
  client.addHeader("content-type", "application/json");

  responseCode = client.POST(String(payload.c_str()));
  if (responseCode <= 0) {
    AG_LOGW(TAG, "HTTP POST via %s failed: %d (%s)", target, responseCode,
            HTTPClient::errorToString(responseCode).c_str());
    client.end();
    return false;
  }
  client.end();
  return true;
}

void AirgradientWifiClient::_ensureSelectorReady() {
  // Re-initialize on first call or if the domain changed at runtime
  // (e.g. setHttpDomain() was called by the application).
  bool domainChanged = (selectorHost_ != httpDomain);

  if (!selectorReady_ || domainChanged) {
    if (domainChanged && selectorReady_) {
      AG_LOGI(TAG, "HTTP domain changed (%s -> %s); re-initializing selector",
              selectorHost_.c_str(), httpDomain.c_str());
    }
    selectorReady_ = selector_.begin(httpDomain.c_str());
    if (selectorReady_) {
      selectorHost_ = httpDomain;
    } else {
      AG_LOGW(TAG, "Selector init failed for %s; will use domain-based path only",
              httpDomain.c_str());
    }
    return;
  }

  // Periodic refresh (1h default). No-op if interval not yet elapsed.
  selector_.maybeRefresh(MILLIS());
}

std::string AirgradientWifiClient::_extractPath(const std::string &url) const {
  // Expected format: "https://<httpDomain>/<path>". We strip the prefix
  // strictly so we don't accidentally hit a wrong path on malformed URLs.
  std::string prefix = "https://" + httpDomain;
  if (url.compare(0, prefix.size(), prefix) != 0) {
    return std::string();
  }
  return url.substr(prefix.size());
}

#endif // ARDUINO

#endif // ESP8266
