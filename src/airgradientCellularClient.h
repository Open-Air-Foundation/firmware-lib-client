/**
 * AirGradient
 * https://airgradient.com
 *
 * CC BY-SA 4.0 Attribution-ShareAlike 4.0 International License
 */

#ifndef AIRGRADIENT_CELLULAR_CLIENT_H
#define AIRGRADIENT_CELLULAR_CLIENT_H

#include <cstddef>
#include <cstdio>
#include <string>
#include <type_traits>

/**
 * Builds payload text the way std::ostream formats values by default
 * (%g for floating point, decimal for integers), without std::ostringstream.
 * iostream pulls the libstdc++ locale support into the firmware, about 180 KB
 * of flash on ESP32-C3 with arduino-esp32 3.x.
 */
class AgPayloadStream {
public:
  AgPayloadStream &operator<<(const char *v) {
    s_ += v;
    return *this;
  }
  AgPayloadStream &operator<<(const std::string &v) {
    s_ += v;
    return *this;
  }
  AgPayloadStream &operator<<(char v) {
    s_ += v;
    return *this;
  }
  AgPayloadStream &operator<<(double v) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%g", v);
    s_ += buf;
    return *this;
  }
  AgPayloadStream &operator<<(float v) { return *this << static_cast<double>(v); }
  template <typename T, typename std::enable_if<std::is_integral<T>::value, int>::type = 0>
  AgPayloadStream &operator<<(T v) {
    s_ += std::to_string(v);
    return *this;
  }
  const std::string &str() const { return s_; }

private:
  std::string s_;
};

#ifndef ESP8266

#include <string>

#include "airgradientClient.h"
#include "cellularModule.h"

#include "coap-packet-cpp/src/CoapPacket.h"
#include "coap-packet-cpp/src/CoapError.h"

#define DEFAULT_AIRGRADIENT_APN "iot.1nce.net"

class AirgradientCellularClient : public AirgradientClient {
private:
  const char *const TAG = "AgCellClient";
  // Maximum binary measures payload we allow the encoder to produce.
  // CoAP packets are sent in 1024-byte blocks (Block1) when needed.
  static constexpr size_t MAX_PAYLOAD_SIZE = 8192;
  std::string _apn = DEFAULT_AIRGRADIENT_APN;
  std::string _iccid = "";
  CellularModule *cell_ = nullptr;
  int _networkRegistrationTimeoutMs = (3 * 60000);
  bool _extendedPmMeasures = false;
  bool _isCoapConnected = false;

public:
  AirgradientCellularClient(CellularModule *cellularModule);
  ~AirgradientCellularClient() {};

  bool begin(std::string sn, PayloadType pt);
  void setAPN(const std::string &apn);
  void setExtendedPmMeasures(bool enable);
  void setNetworkRegistrationTimeoutMs(int timeoutMs);
  std::string getICCID();
  bool ensureClientConnection(bool reset);
  std::string httpFetchConfig();
  bool httpPostMeasures(const std::string &payload);
  bool httpPostMeasures(const AirgradientPayload &payload);
  bool mqttConnect();
  bool mqttConnect(const char *uri);
  bool mqttConnect(const std::string &host, int port, std::string username = "",
                   std::string password = "");
  bool mqttDisconnect();
  bool mqttPublishMeasures(const std::string &payload);
  bool mqttPublishMeasures(const AirgradientPayload &payload);
  std::string coapFetchConfig(bool keepConnection = false);
  bool coapPostMeasures(const uint8_t* buffer, size_t length, bool keepConnection = false);
  bool coapPostMeasures(const AirgradientPayload &payload, bool keepConnection = false);

 private:
  std::string _getEndpoint();
  void _serialize(AgPayloadStream &oss, int signal, const PayloadBuffer &payloadBuffer);
  bool _encodeBinaryPayload(const AirgradientPayload &payload, std::vector<uint8_t> &out);

  CoapPacket::CoapError _buildCoapPostPacket(std::vector<uint8_t> &outPacket,
                                            uint16_t messageId, const uint8_t *token,
                                            uint8_t tokenLen, const uint8_t *payload,
                                            size_t payloadLen, bool useBlock1,
                                            uint32_t blockNum, bool more, size_t totalLen,
                                            bool includeSize1);

  // Send CoAP POST measures, using Block1 when payload exceeds 1024 bytes.
  // Generates token and base messageId internally.
  bool _coapPost(const uint8_t *payload, size_t payloadLen, CoapPacket::CoapPacket *respPacket);

  bool _coapConnect();
  void _coapDisconnect(bool keepConnection);

  // Single CoAP request attempt - handles Piggyback and Separate ACK
  CellReturnStatus _coapRequest(const std::vector<uint8_t> &reqBuffer, uint16_t expectedMessageId,
                                const uint8_t *expectedToken, uint8_t expectedTokenLen,
                                CoapPacket::CoapPacket *respPacket, int timeoutMs = 60000);
  // CoAP request with retry logic (up to 3 attempts)
  bool _coapRequestWithRetry(const std::vector<uint8_t> &reqBuffer, uint16_t expectedMessageId,
                             const uint8_t *expectedToken, uint8_t expectedTokenLen,
                             CoapPacket::CoapPacket *respPacket, int timeoutMs = 60000,
                             int maxRetries = 3);
  void _generateTokenMessageId(uint8_t token[2], uint16_t *messageId);
};

#endif // ESP8266
#endif // AIRGRADIENT_CELLULAR_CLIENT_H
