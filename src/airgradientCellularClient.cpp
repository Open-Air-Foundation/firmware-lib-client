/**
 * AirGradient
 * https://airgradient.com
 *
 * CC BY-SA 4.0 Attribution-ShareAlike 4.0 International License
 */

#include "airgradientClient.h"
#include <cmath>
#ifndef ESP8266

#include "airgradientCellularClient.h"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <memory>
#include <new>
#include "cellularModule.h"
#include "common.h"
#include "agLogger.h"
#include "config.h"

#include "coap-packet-cpp/src/CoapError.h"
#include "coap-packet-cpp/src/CoapPacket.h"
#include "coap-packet-cpp/src/CoapBuilder.h"
#include "coap-packet-cpp/src/CoapParser.h"
#include "coap-packet-cpp/src/CoapTypes.h"

#include "payload-encoder/src/PayloadEncoder.h"
#include "payload-encoder/src/PayloadTypes.h"

#include "esp_random.h"


#define ONE_OPENAIR_POST_MEASURES_ENDPOINT "cts"
#define OPENAIR_MAX_POST_MEASURES_ENDPOINT "cvn"
#ifdef ARDUINO
#define POST_MEASURES_ENDPOINT ONE_OPENAIR_POST_MEASURES_ENDPOINT
#else
#define POST_MEASURES_ENDPOINT OPENAIR_MAX_POST_MEASURES_ENDPOINT
#endif


static void logBinaryPayloadHexPreview(const char *tag, const std::vector<uint8_t> &payload,
                                       size_t previewBytes) {
  if (payload.empty()) {
    AG_LOGI(tag, "Binary payload (0 bytes)");
    return;
  }

  if (previewBytes == 0) {
    AG_LOGI(tag, "Binary payload (%d bytes)", (int)payload.size());
    return;
  }

  std::ostringstream oss;
  oss << std::hex << std::uppercase;

  const auto appendByte = [&](uint8_t b) {
    oss << std::setfill('0') << std::setw(2) << static_cast<int>(b);
  };

  const auto appendRange = [&](size_t from, size_t to) {
    for (size_t i = from; i < to; i++) {
      appendByte(payload[i]);
      if (i + 1 < to) {
        oss << " ";
      }
    }
  };

  if (payload.size() <= (previewBytes * 2)) {
    appendRange(0, payload.size());
  } else {
    appendRange(0, previewBytes);
    oss << " ... ";
    appendRange(payload.size() - previewBytes, payload.size());
  }

  AG_LOGI(tag, "Binary payload (%d bytes) [first/last %d]: %s", (int)payload.size(),
          (int)previewBytes, oss.str().c_str());
}

AirgradientCellularClient::AirgradientCellularClient(CellularModule *cellularModule)
    : cell_(cellularModule) {}

bool AirgradientCellularClient::begin(std::string sn, PayloadType pt) {
  // Update parent serialNumber variable
  serialNumber = sn;
  payloadType = pt;
  clientReady = false;

  if (!cell_->init()) {
    AG_LOGE(TAG, "Cannot initialized cellular client");
    return false;
  }

  // To make sure module ready to use
  if (cell_->isSimReady() != CellReturnStatus::Ok) {
    AG_LOGE(TAG, "SIM is not ready, please check if SIM is inserted properly!");
    return false;
  }

  // Printout SIM CCID
  auto result = cell_->retrieveSimCCID();
  if (result.status != CellReturnStatus::Ok) {
    AG_LOGE(TAG, "Failed get SIM CCID, please check if SIM is inserted properly!");
    return false;
  }
  _iccid = result.data;
  AG_LOGI(TAG, "SIM CCID: %s", result.data.c_str());

  // Register network
  result =
      cell_->startNetworkRegistration(CellTechnology::LTE, _apn, _networkRegistrationTimeoutMs);
  if (result.status != CellReturnStatus::Ok) {
    AG_LOGE(TAG, "Cellular client failed, module cannot register to network");
    return false;
  }
  clientReady = true;

  return true;
}

void AirgradientCellularClient::setAPN(const std::string &apn) { _apn = apn; }

void AirgradientCellularClient::setExtendedPmMeasures(bool enable) { _extendedPmMeasures = enable; }

void AirgradientCellularClient::setNetworkRegistrationTimeoutMs(int timeoutMs) {
  _networkRegistrationTimeoutMs = timeoutMs;
  AG_LOGI(TAG, "Timeout set to %d seconds", (_networkRegistrationTimeoutMs / 1000));
}

std::string AirgradientCellularClient::getICCID() { return _iccid; }

bool AirgradientCellularClient::ensureClientConnection(bool reset) {
  AG_LOGI(TAG, "Ensuring client connection, restarting cellular module");
  _isCoapConnected = false;

  if (reset) {
    if (cell_->reset() == false) {
      AG_LOGW(TAG, "Reset failed, power cycle module...");
      cell_->powerOff(true);
      DELAY_MS(2000);
      cell_->powerOn();
    }

    AG_LOGI(TAG, "Wait for 10s for module to warming up");
    DELAY_MS(10000);
  }

  if (cell_->reinitialize() != CellReturnStatus::Ok) {
    AG_LOGE(TAG, "Failed to reinitialized the cellular module");
    clientReady = false;
    return false;
  }

  // Register network
  auto result =
      cell_->startNetworkRegistration(CellTechnology::LTE, _apn, _networkRegistrationTimeoutMs);
  if (result.status != CellReturnStatus::Ok) {
    AG_LOGE(TAG, "Cellular client failed, module cannot register to network");
    clientReady = false;
    return false;
  }

  AG_LOGI(TAG, "Cellular client ready, module registered to network. Warming up for 10s...");
  clientReady = true;
  DELAY_MS(10000);

  return true;
}

std::string AirgradientCellularClient::httpFetchConfig() {
  std::string url = buildFetchConfigUrl();
  AG_LOGI(TAG, "Fetch configuration from %s", url.c_str());

  auto result = cell_->httpGet(url); // TODO: Define timeouts
  if (result.status != CellReturnStatus::Ok) {
    AG_LOGE(TAG, "Module not return OK when call httpGet()");
    lastFetchConfigSucceed = false;
    clientReady = false;
    return {};
  }

  // Reset client ready state
  clientReady = true;

  // Response status check if fetch failed
  if (result.data.statusCode != 200) {
    AG_LOGW(TAG, "Failed fetch configuration from server with return code %d",
            result.data.statusCode);
    // Return code 400 means device not registered on ag server
    if (result.data.statusCode == 400) {
      registeredOnAgServer = false;
    }
    lastFetchConfigSucceed = false;

    return {};
  }

  // Set success state flag
  registeredOnAgServer = true;
  lastFetchConfigSucceed = true;

  // Sanity check if response body is empty
  if (result.data.bodyLen == 0 || result.data.body == nullptr) {
    // TODO: How to handle this? perhaps cellular module failed to read the buffer
    AG_LOGW(TAG, "Success fetch configuration from server but somehow body is empty");
    return {};
  }

  AG_LOGI(TAG, "Received configuration: (%d) %s", result.data.bodyLen, result.data.body.get());

  // Move the string from unique_ptr
  std::string body = std::string(result.data.body.get());

  AG_LOGI(TAG, "Success fetch configuration from server, still needs to be parsed and validated");

  return body;
}

bool AirgradientCellularClient::httpPostMeasures(const std::string &payload) {
  // Format url
  char url[80] = {0};
  sprintf(url, "http://%s/sensors/%s/%s", httpDomain.c_str(), serialNumber.c_str(),
          _getEndpoint().c_str());

  AG_LOGI(TAG, "Post measures to %s", url);
  AG_LOGI(TAG, "Payload: %s", payload.c_str());

  auto result = cell_->httpPost(url, payload); // TODO: Define timeouts
  if (result.status != CellReturnStatus::Ok) {
    AG_LOGE(TAG, "Module not return OK when call httpPost()");
    lastPostMeasuresSucceed = false;
    clientReady = false;
    return false;
  }

  // Reset client ready state
  clientReady = true;

  // Response status check if post failed
  if ((result.data.statusCode != 200) && (result.data.statusCode != 429) &&
      (result.data.statusCode != 201)) {
    AG_LOGW(TAG, "Failed post measures to server with response code %d", result.data.statusCode);
    lastPostMeasuresSucceed = false;
    return false;
  }

  lastPostMeasuresSucceed = true;
  AG_LOGI(TAG, "Success post measures to server with response code %d", result.data.statusCode);

  return true;
}

bool AirgradientCellularClient::httpPostMeasures(const AirgradientPayload &payload) {
  // Build payload using oss, easier to manage if there's an invalid value that should not included
  std::ostringstream oss;

  // Add interval at the first position
  oss << payload.measureInterval;

  for (int i = 0; i < payload.bufferCount; i++) {
    // Seperator between measures cycle
    oss << ",";
    // Serialize each measurement
    _serialize(oss, payload.signal, payload.payloadBuffer[i]);
  }

  // Compile it
  std::string payloadStr = oss.str();

  return httpPostMeasures(payloadStr);
}

bool AirgradientCellularClient::mqttConnect() { return mqttConnect(mqttDomain, mqttPort); }

bool AirgradientCellularClient::mqttConnect(const char *uri) {
  // Get connection properties from uri; Eg: mqtt://username:password@mqttbroker.com:1883
  std::string protocol;
  std::string username;
  std::string password;
  std::string host;
  int port = -1;
  Common::parseUri(uri, protocol, username, password, host, port);

  if (host.empty()) {
    AG_LOGE(TAG, "MQTT host or port is empty");
    return false;
  }

  if (port == -1) {
    port = 1883;
  }

  return mqttConnect(host, port, username, password);
}

bool AirgradientCellularClient::mqttConnect(const std::string &host, int port, std::string username,
                                            std::string password) {

  AG_LOGI(TAG, "Attempt connection to MQTT broker: %s:%d", host.c_str(), port);
  auto result = cell_->mqttConnect(serialNumber, host, port, username, password);
  if (result != CellReturnStatus::Ok) {
    AG_LOGE(TAG, "Failed connect to mqtt broker");
    return false;
  }
  AG_LOGI(TAG, "Success connect to mqtt broker");

  return true;
}

bool AirgradientCellularClient::mqttDisconnect() {
  if (cell_->mqttDisconnect() != CellReturnStatus::Ok) {
    AG_LOGE(TAG, "Failed disconnect from mqtt broker");
    return false;
  }
  AG_LOGI(TAG, "Success disconnect from mqtt broker");

  return true;
}

bool AirgradientCellularClient::mqttPublishMeasures(const std::string &payload) {
  // TODO: Ensure mqtt connection
  auto topic = buildMqttTopicPublishMeasures();
  AG_LOGI(TAG, "Publish to %s", topic.c_str());
  AG_LOGI(TAG, "Payload: %s", payload.c_str());
  auto result = cell_->mqttPublish(topic, payload);
  if (result != CellReturnStatus::Ok) {
    AG_LOGE(TAG, "Failed publish measures to mqtt server");
    return false;
  }
  AG_LOGI(TAG, "Success publish measures to mqtt server");

  return true;
}

bool AirgradientCellularClient::mqttPublishMeasures(const AirgradientPayload &payload) {
  // Build payload using oss, easier to manage if there's an invalid value that should not included
  std::ostringstream oss;

  // Add interval at the first position
  oss << payload.measureInterval;

  for (int i = 0; i < payload.bufferCount; i++) {
    // Seperator between measures cycle
    oss << ",";
    // Serialize each measurement
    _serialize(oss, payload.signal, payload.payloadBuffer[i]);
  }

  // Compile it
  std::string toSend = oss.str();

  return mqttPublishMeasures(toSend);
}

std::string AirgradientCellularClient::coapFetchConfig(bool keepConnection) {
  if (!_coapConnect()) {
    lastFetchConfigSucceed = false;
    return {};
  }

  // Create token and messageId
  uint8_t token[2];
  uint16_t messageId;
  std::vector<uint8_t> buffer;
  _generateTokenMessageId(token, &messageId);

  // Format coap packet
  CoapPacket::CoapBuilder builder;
  auto err = builder.setType(CoapPacket::CoapType::CON)
                 .setCode(CoapPacket::CoapCode::GET)
                 .setMessageId(messageId)
                 .setToken(token, 2)
                 .setUriPath(serialNumber)
                 .buildBuffer(buffer);
  if (err != CoapPacket::CoapError::OK) {
    AG_LOGE(TAG, "CoAP fetch config packet build failed %s", CoapPacket::getErrorMessage(err));
    lastFetchConfigSucceed = false;
    _coapDisconnect(keepConnection);
    return {};
  }

  // TODO: Add URI to the path
  AG_LOGI(TAG, "CoAP fetch configuration from %s:%d", coapHostTarget.c_str(), coapPort);

  CoapPacket::CoapPacket responsePacket;
  bool success = _coapRequestWithRetry(buffer, messageId, token, 2, &responsePacket);
  if (!success) {
    lastFetchConfigSucceed = false;
    _coapDisconnect(keepConnection);
    return {};
  }

  // Check request response code
  uint8_t codeClass = CoapPacket::getCodeClass(responsePacket.code);
  uint8_t codeDetail = CoapPacket::getCodeDetail(responsePacket.code);
  if (codeClass != 2) {
    AG_LOGE(TAG, "CoAP fetch configuration response failed (%d.%02d)", codeClass, codeDetail);
    if (codeClass == 4) {
      // Return code 400 means device not registered on ag server
      registeredOnAgServer = false;
    }
    lastFetchConfigSucceed = false;
    _coapDisconnect(keepConnection);
    return {};
  }

  std::string response(responsePacket.payload.begin(), responsePacket.payload.end());
  AG_LOGI(TAG, "Received configuration: (%d) %s", response.length(), response.c_str());

  // Set state to succeed
  lastFetchConfigSucceed = true;
  registeredOnAgServer = true;

  // Handling disconnection decision
  _coapDisconnect(keepConnection);

  AG_LOGI(TAG, "Success fetch configuration from server, still needs to be parsed and validated");
  return response;
}

bool AirgradientCellularClient::coapPostMeasures(const uint8_t *buffer, size_t length,
                                                 bool keepConnection) {
  if (!_coapConnect()) {
    lastPostMeasuresSucceed = false;
    return false;
  }

  AG_LOGI(TAG, "CoAP post measures to %s:%d", coapHostTarget.c_str(), coapPort); // TODO: Add path
  AG_LOGI(TAG, "Payload size: %d bytes (binary)", length);

  CoapPacket::CoapPacket responsePacket;
  const bool success = _coapPost(buffer, length, &responsePacket);
  lastPostMeasuresSucceed = success;
  _coapDisconnect(keepConnection);
  return success;
}

bool AirgradientCellularClient::coapPostMeasures(const AirgradientPayload &payload,
                                                  bool keepConnection) {
  std::vector<uint8_t> binaryPayload;
  if (!_encodeBinaryPayload(payload, binaryPayload)) {
    AG_LOGE(TAG, "Failed to create binary payload");
    return false;
  }

  constexpr size_t kPreviewBytes = 10;
  logBinaryPayloadHexPreview(TAG, binaryPayload, kPreviewBytes);

  return coapPostMeasures(binaryPayload.data(), binaryPayload.size(), keepConnection);
}

CoapPacket::CoapError AirgradientCellularClient::_buildCoapPostPacket(
    std::vector<uint8_t> &outPacket, uint16_t messageId, const uint8_t *token, uint8_t tokenLen,
    const uint8_t *payload, size_t payloadLen, bool useBlock1, uint32_t blockNum, bool more,
    size_t totalLen, bool includeSize1) {
  outPacket.clear();

  CoapPacket::CoapBuilder builder;
  builder.setType(CoapPacket::CoapType::CON)
      .setCode(CoapPacket::CoapCode::POST)
      .setMessageId(messageId)
      .setToken(token, tokenLen)
      .setUriPath(serialNumber)
      .setContentFormat(CoapPacket::CoapContentFormat::OCTET_STREAM);

  if (useBlock1) {
    constexpr uint8_t kBlockSzx = 6; // 1024-byte blocks
    builder.setBlock1(blockNum, more, kBlockSzx);
    if (includeSize1) {
      builder.addOption(CoapPacket::CoapOptionNumber::SIZE1, (uint32_t)totalLen);
    }
  }

  builder.setPayload(payload, payloadLen);
  return builder.buildBuffer(outPacket);
}

bool AirgradientCellularClient::_coapPost(const uint8_t *payload, size_t payloadLen,
                                         CoapPacket::CoapPacket *respPacket) {
  if (payload == nullptr || payloadLen == 0) {
    AG_LOGE(TAG, "CoAP post invalid payload");
    return false;
  }

  if (respPacket == nullptr) {
    AG_LOGE(TAG, "CoAP post invalid response packet");
    return false;
  }

  // Create token and base messageId once for the transfer (Block1 requires stable token).
  uint8_t token[2];
  uint16_t baseMessageId;
  _generateTokenMessageId(token, &baseMessageId);

  std::vector<uint8_t> packetBuffer;
  constexpr size_t kCoapBlockSize = CoapPacket::MAX_PAYLOAD_SIZE;
  constexpr uint8_t kBlockSzx = 6; // 2^(6+4) = 1024

  // If payloadLen less then the maximum payload size, then no need to proceed using chunking
  if (payloadLen <= kCoapBlockSize) {
    const auto err =
        _buildCoapPostPacket(packetBuffer, baseMessageId, token, 2, payload, payloadLen, false, 0,
                             false, payloadLen, false);
    if (err != CoapPacket::CoapError::OK) {
      AG_LOGE(TAG, "CoAP post measures packet build failed %s", CoapPacket::getErrorMessage(err));
      return false;
    }

    const bool success = _coapRequestWithRetry(packetBuffer, baseMessageId, token, 2, respPacket);
    if (!success) {
      AG_LOGE(TAG, "CoAP post measures request failed");
      return false;
    }

    const uint8_t codeClass = CoapPacket::getCodeClass(respPacket->code);
    const uint8_t codeDetail = CoapPacket::getCodeDetail(respPacket->code);
    if (codeClass != 2) {
      AG_LOGE(TAG, "CoAP post measures response failed (%d.%02d)", codeClass, codeDetail);
      return false;
    }

    AG_LOGI(TAG, "CoAP post measures response success (%d.%02d)", codeClass, codeDetail);
    return true;
  }

  AG_LOGI(TAG, "CoAP payload > %d bytes, using Block1 transfer", kCoapBlockSize);

  size_t offset = 0;
  uint32_t blockNum = 0;
  while (offset < payloadLen) {
    const size_t chunkLen = std::min(kCoapBlockSize, payloadLen - offset);
    const bool more = (offset + chunkLen) < payloadLen;
    const uint16_t messageId = static_cast<uint16_t>(baseMessageId + blockNum);

    const auto err = _buildCoapPostPacket(packetBuffer, messageId, token, 2,
                                          payload + offset, chunkLen, true, blockNum, more,
                                          payloadLen, (blockNum == 0));
    if (err != CoapPacket::CoapError::OK) {
      AG_LOGE(TAG, "CoAP Block1 packet build failed (block %d) %s", (int)blockNum,
              CoapPacket::getErrorMessage(err));
      return false;
    }

    AG_LOGI(TAG, "CoAP Block1 send block=%d m=%d szx=%d bytes=%d/%d", (int)blockNum,
            more ? 1 : 0, kBlockSzx, (int)chunkLen, (int)payloadLen);

    const bool success = _coapRequestWithRetry(packetBuffer, messageId, token, 2, respPacket);
    if (!success) {
      AG_LOGE(TAG, "CoAP Block1 request failed (block %d)", (int)blockNum);
      return false;
    }

    const uint8_t codeClass = CoapPacket::getCodeClass(respPacket->code);
    const uint8_t codeDetail = CoapPacket::getCodeDetail(respPacket->code);
    if (codeClass != 2) {
      AG_LOGE(TAG, "CoAP Block1 response failed (block %d) (%d.%02d)", (int)blockNum, codeClass,
              codeDetail);
      return false;
    }

    if (more && respPacket->code != CoapPacket::CoapCode::CONTINUE_2_31) {
      AG_LOGE(TAG, "CoAP Block1 expected 2.31 Continue (block %d) got (%d.%02d)", (int)blockNum,
              codeClass, codeDetail);
      return false;
    }

    offset += chunkLen;
    blockNum++;
  }

  AG_LOGI(TAG, "CoAP Block1 transfer completed, blocks=%d", (int)blockNum);
  return true;
}

bool AirgradientCellularClient::_coapConnect() {
  if (_isCoapConnected) {
    AG_LOGI(TAG, "CoAP already connected");
    return true;
  }

  if (cell_->udpConnect(coapHostTarget, coapPort) != CellReturnStatus::Ok) {
    clientReady = false;
    AG_LOGI(TAG, "Failed connect to CoAP server");
    return false;
  }

  clientReady = true;
  _isCoapConnected = true;
  clientReady = true;
  return true;
}

void AirgradientCellularClient::_coapDisconnect(bool keepConnection) {
  if (keepConnection) {
    _isCoapConnected = true;
    return;
  }

  if (cell_->udpDisconnect() == CellReturnStatus::Ok) {
    _isCoapConnected = false;
    return;
  }

  AG_LOGI(TAG, "Failed disconnect to CoAP server");
  _isCoapConnected = false;
  clientReady = false;
}

CellReturnStatus AirgradientCellularClient::_coapRequest(
    const std::vector<uint8_t> &reqBuffer, uint16_t expectedMessageId, const uint8_t *expectedToken,
    uint8_t expectedTokenLen, CoapPacket::CoapPacket *respPacket, int timeoutMs) {
  // 1. Prepare UDP packet from request buffer
  CellularModule::UdpPacket udpPacket;
  udpPacket.size = reqBuffer.size();
  udpPacket.buff = std::move(reqBuffer); // Move buffer, not copy

  // 2. Send request
  if (cell_->udpSend(udpPacket, coapHostTarget, coapPort) != CellReturnStatus::Ok) {
    AG_LOGE(TAG, "Failed to send CoAP request via UDP");
    return CellReturnStatus::Failed;
  }

  AG_LOGI(TAG, "CoAP request sent, waiting for response...");

  // 3. Receive response
  auto response = cell_->udpReceive(timeoutMs);
  if (response.status != CellReturnStatus::Ok) {
    AG_LOGE(TAG, "Failed to receive CoAP response (timeout or error)");
    return response.status;
  }

  // 4. Parse response
  CoapPacket::CoapError parseErr = CoapPacket::CoapParser::parse(response.data.buff, *respPacket);
  if (parseErr != CoapPacket::CoapError::OK) {
    AG_LOGE(TAG, "Failed to parse CoAP response: %s", CoapPacket::getErrorMessage(parseErr));
    return CellReturnStatus::Failed;
  }

  // 5. Validate message ID
  if (respPacket->message_id != expectedMessageId) {
    AG_LOGW(TAG, "Response message ID mismatch: expected %d, got %d", expectedMessageId,
            respPacket->message_id);
    return CellReturnStatus::Failed;
  }

  AG_LOGD(TAG, "Message ID validated");

  // 6. Handle response type and validate token appropriately
  if (respPacket->type == CoapPacket::CoapType::ACK &&
      respPacket->code == CoapPacket::CoapCode::EMPTY) {

    // Empty ACK - Separate response pattern
    // Do NOT validate token on Empty ACK (it has no token)
    AG_LOGI(TAG, "Received empty ACK (Separate response pattern), waiting for actual response...");

    // Receive separate response
    auto separateResp = cell_->udpReceive(timeoutMs);
    if (separateResp.status != CellReturnStatus::Ok) {
      AG_LOGE(TAG, "Failed to receive separate CoAP response");
      return separateResp.status;
    }

    // Parse separate response
    parseErr = CoapPacket::CoapParser::parse(separateResp.data.buff, *respPacket);
    if (parseErr != CoapPacket::CoapError::OK) {
      AG_LOGE(TAG, "Failed to parse separate CoAP response: %s",
              CoapPacket::getErrorMessage(parseErr));
      return CellReturnStatus::Failed;
    }

    // NOW validate token on the actual separate response (message ID may differ)
    if (respPacket->token_length != expectedTokenLen) {
      AG_LOGW(TAG, "Separate response token length mismatch: expected %d, got %d", expectedTokenLen,
              respPacket->token_length);
      return CellReturnStatus::Failed;
    }

    for (uint8_t i = 0; i < expectedTokenLen; i++) {
      if (respPacket->token[i] != expectedToken[i]) {
        AG_LOGW(TAG, "Separate response token mismatch at byte %d", i);
        return CellReturnStatus::Failed;
      }
    }

    AG_LOGD(TAG, "Separate response received and token validated");

    // If separate response is CON, send ACK back
    if (respPacket->type == CoapPacket::CoapType::CON) {
      AG_LOGI(TAG, "Separate response is CON, sending ACK...");

      // Build ACK inline (ACK with EMPTY code, no token per RFC 7252)
      CoapPacket::CoapBuilder ackBuilder;
      std::vector<uint8_t> ackBuffer;

      auto err = ackBuilder.setType(CoapPacket::CoapType::ACK)
                     .setCode(CoapPacket::CoapCode::EMPTY)
                     .setMessageId(respPacket->message_id)
                     .buildBuffer(ackBuffer);

      if (err == CoapPacket::CoapError::OK) {
        CellularModule::UdpPacket ackPacket;
        ackPacket.size = ackBuffer.size();
        ackPacket.buff = std::move(ackBuffer);

        if (cell_->udpSend(ackPacket, coapHostTarget, coapPort) == CellReturnStatus::Ok) {
          AG_LOGD(TAG, "ACK sent for separate CON response");
        } else {
          AG_LOGW(TAG, "Failed to send ACK for separate CON response");
        }
      } else {
        AG_LOGW(TAG, "Failed to build ACK packet: %s", CoapPacket::getErrorMessage(err));
      }
    }
  } else {
    // Piggyback ACK or direct CON response
    // Validate token NOW (not Empty ACK, so token should be present)
    if (respPacket->token_length != expectedTokenLen) {
      AG_LOGW(TAG, "Response token length mismatch: expected %d, got %d", expectedTokenLen,
              respPacket->token_length);
      return CellReturnStatus::Failed;
    }

    for (uint8_t i = 0; i < expectedTokenLen; i++) {
      if (respPacket->token[i] != expectedToken[i]) {
        AG_LOGW(TAG, "Response token mismatch at byte %d", i);
        return CellReturnStatus::Failed;
      }
    }

    AG_LOGD(TAG, "Response token validated");

    // If CON response, send ACK
    if (respPacket->type == CoapPacket::CoapType::CON) {
      AG_LOGD(TAG, "Received CON response, sending ACK...");

      // Build and send ACK (ACK with EMPTY code, no token per RFC 7252)
      CoapPacket::CoapBuilder ackBuilder;
      std::vector<uint8_t> ackBuffer;

      auto err = ackBuilder.setType(CoapPacket::CoapType::ACK)
                     .setCode(CoapPacket::CoapCode::EMPTY)
                     .setMessageId(respPacket->message_id)
                     .buildBuffer(ackBuffer);

      if (err == CoapPacket::CoapError::OK) {
        CellularModule::UdpPacket ackPacket;
        ackPacket.size = ackBuffer.size();
        ackPacket.buff = std::move(ackBuffer);

        if (cell_->udpSend(ackPacket, coapHostTarget, coapPort) == CellReturnStatus::Ok) {
          AG_LOGI(TAG, "ACK sent for CON response");
        } else {
          AG_LOGW(TAG, "Failed to send ACK for CON response");
        }
      } else {
        AG_LOGW(TAG, "Failed to build ACK packet: %s", CoapPacket::getErrorMessage(err));
      }
    }
    // Otherwise it's a piggyback ACK (Type=ACK with response code) - no ACK needed
  }

  AG_LOGI(TAG, "CoAP request successful");
  return CellReturnStatus::Ok;
}

bool AirgradientCellularClient::_coapRequestWithRetry(
    const std::vector<uint8_t> &reqBuffer, uint16_t expectedMessageId, const uint8_t *expectedToken,
    uint8_t expectedTokenLen, CoapPacket::CoapPacket *respPacket, int timeoutMs, int maxRetries) {
  bool allFailuresWereTimeouts = true;

  for (int attempt = 1; attempt <= maxRetries; attempt++) {
    AG_LOGI(TAG, "CoAP request attempt %d/%d", attempt, maxRetries);

    CellReturnStatus status = _coapRequest(reqBuffer, expectedMessageId, expectedToken,
                                           expectedTokenLen, respPacket, timeoutMs);
    if (status == CellReturnStatus::Ok) {
      // Success!
      return true;
    }

    // Track if this failure was NOT a timeout
    if (status != CellReturnStatus::Timeout) {
      allFailuresWereTimeouts = false;
    }

    // Failed, log and retry if attempts remain
    if (attempt < maxRetries) {
      AG_LOGW(TAG, "CoAP request failed, retrying...");
    }
  }

  // All attempts failed - check if we should try DNS fallback
  if (allFailuresWereTimeouts && coapHostTarget == AIRGRADIENT_COAP_IP) {
    AG_LOGI(TAG, "All retries timed out with default IP, attempting DNS fallback");

    // Disconnect from current connection
    _coapDisconnect(false);

    // Resolve DNS
    auto dnsResult = cell_->resolveDNS(AIRGRADIENT_COAP_DOMAIN);
    if (dnsResult.status != CellReturnStatus::Ok) {
      AG_LOGE(TAG, "DNS resolution failed for %s", AIRGRADIENT_COAP_DOMAIN);
      clientReady = false;
      return false;
    }

    // Update target with resolved IP
    coapHostTarget = dnsResult.data;
    AG_LOGI(TAG, "DNS resolved to %s, reconnecting and retrying", coapHostTarget.c_str());

    // Reconnect
    if (!_coapConnect()) {
      AG_LOGE(TAG, "Failed to reconnect after DNS resolution");
      clientReady = false;
      return false;
    }

    // Retry request with same retry count
    for (int attempt = 1; attempt <= maxRetries; attempt++) {
      AG_LOGI(TAG, "CoAP request attempt %d/%d (after DNS fallback)", attempt, maxRetries);

      CellReturnStatus status = _coapRequest(reqBuffer, expectedMessageId, expectedToken,
                                             expectedTokenLen, respPacket, timeoutMs);
      if (status == CellReturnStatus::Ok) {
        // Success with DNS-resolved IP!
        AG_LOGI(TAG, "CoAP request succeeded after DNS fallback");
        return true;
      }

      if (attempt < maxRetries) {
        AG_LOGW(TAG, "CoAP request failed, retrying...");
      }
    }

    AG_LOGE(TAG, "CoAP request failed after %d attempts with DNS-resolved IP", maxRetries);
  } else {
    AG_LOGE(TAG, "CoAP request failed after %d attempts", maxRetries);
  }

  clientReady = false;
  return false;
}

void AirgradientCellularClient::_generateTokenMessageId(uint8_t token[2], uint16_t *messageId) {
  uint32_t r = esp_random();
  token[0] = (uint8_t)(r & 0xFF);
  token[1] = (uint8_t)((r >> 8) & 0xFF);
  *messageId = (uint16_t)((r >> 16) & 0xFFFF);
}

void AirgradientCellularClient::_serialize(std::ostringstream &oss, int signal,
                                           const PayloadBuffer &payloadBuffer) {
  // CO2
  if (IS_CO2_VALID(payloadBuffer.common.rco2)) {
    oss << std::round(payloadBuffer.common.rco2);
  }
  oss << ",";
  // Temperature
  if (IS_TEMPERATURE_VALID(payloadBuffer.common.atmp)) {
    oss << std::round(payloadBuffer.common.atmp * 10);
  }
  oss << ",";
  // Humidity
  if (IS_HUMIDITY_VALID(payloadBuffer.common.rhum)) {
    oss << std::round(payloadBuffer.common.rhum * 10);
  }
  oss << ",";
  // PM1.0 atmospheric environment
  if (IS_PM_VALID(payloadBuffer.common.pm01)) {
    oss << std::round(payloadBuffer.common.pm01 * 10);
  }
  oss << ",";
  // PM2.5 atmospheric environment
  {
    const bool pm0Ok = IS_PM_VALID(payloadBuffer.common.pm25[0]);
    const bool pm1Ok = IS_PM_VALID(payloadBuffer.common.pm25[1]);
    if (pm0Ok && pm1Ok) {
      const float pm25 = (payloadBuffer.common.pm25[0] + payloadBuffer.common.pm25[1]) / 2.0f;
      oss << std::round(pm25 * 10);
    } else if (pm0Ok) {
      const float pm25 = payloadBuffer.common.pm25[0];
      oss << std::round(pm25 * 10);
    } else if (pm1Ok) {
      const float pm25 = payloadBuffer.common.pm25[1];
      oss << std::round(pm25 * 10);
    }
  }
  oss << ",";
  // PM10 atmospheric environment
  if (IS_PM_VALID(payloadBuffer.common.pm10)) {
    oss << std::round(payloadBuffer.common.pm10 * 10);
  }
  oss << ",";
  // TVOC
  if (IS_TVOC_VALID(payloadBuffer.common.tvocRaw)) {
    oss << payloadBuffer.common.tvocRaw;
  }
  oss << ",";
  // NOx
  if (IS_NOX_VALID(payloadBuffer.common.noxRaw)) {
    oss << payloadBuffer.common.noxRaw;
  }
  oss << ",";
  // PM 0.3 particle count
  {
    const bool pc0Ok = IS_PM_VALID(payloadBuffer.common.particleCount003[0]);
    const bool pc1Ok = IS_PM_VALID(payloadBuffer.common.particleCount003[1]);
    if (pc0Ok && pc1Ok) {
      const int pc = (payloadBuffer.common.particleCount003[0] + payloadBuffer.common.particleCount003[1]) / 2.0f;
      oss << pc;
    } else if (pc0Ok) {
      oss << payloadBuffer.common.particleCount003[0];
    } else if (pc1Ok) {
      oss << payloadBuffer.common.particleCount003[1];
    }
  }
  oss << ",";
  // Radio signal
  oss << signal;

  // Only continue for MAX model
  if (payloadType == MAX_WITH_O3_NO2 || payloadType == MAX_WITHOUT_O3_NO2) {
    oss << ",";
    // V Battery
    if (IS_VOLT_VALID(payloadBuffer.ext.extra.vBat)) {
      oss << std::round(payloadBuffer.ext.extra.vBat * 100);
    }
    oss << ",";
    // V Solar Panel
    if (IS_VOLT_VALID(payloadBuffer.ext.extra.vPanel)) {
      oss << std::round(payloadBuffer.ext.extra.vPanel * 100);
    }

    if (payloadType == MAX_WITH_O3_NO2) {
      oss << ",";
      // Working Electrode O3
      if (IS_VOLT_VALID(payloadBuffer.ext.extra.o3WorkingElectrode)) {
        oss << std::round(payloadBuffer.ext.extra.o3WorkingElectrode * 1000);
      }
      oss << ",";
      // Auxiliary Electrode O3
      if (IS_VOLT_VALID(payloadBuffer.ext.extra.o3AuxiliaryElectrode)) {
        oss << std::round(payloadBuffer.ext.extra.o3AuxiliaryElectrode * 1000);
      }
      oss << ",";
      // Working Electrode NO2
      if (IS_VOLT_VALID(payloadBuffer.ext.extra.no2WorkingElectrode)) {
        oss << std::round(payloadBuffer.ext.extra.no2WorkingElectrode * 1000);
      }
      oss << ",";
      // Auxiliary Electrode NO2
      if (IS_VOLT_VALID(payloadBuffer.ext.extra.no2AuxiliaryElectrode)) {
        oss << std::round(payloadBuffer.ext.extra.no2AuxiliaryElectrode * 1000);
      }
      oss << ",";
      // AFE Temperature
      if (IS_VOLT_VALID(payloadBuffer.ext.extra.afeTemp)) {
        oss << std::round(payloadBuffer.ext.extra.afeTemp * 10);
      }
    }
  }

  if (!_extendedPmMeasures) {
    return;
  }

  // Extended measures
  oss << ",";
  // PM 0.5 particle count
  if (IS_PM_VALID(payloadBuffer.common.particleCount005)) {
    oss << payloadBuffer.common.particleCount005;
  }
  oss << ",";
  // PM 1.0 particle count
  if (IS_PM_VALID(payloadBuffer.common.particleCount01)) {
    oss << payloadBuffer.common.particleCount01;
  }
  oss << ",";
  // PM 2.5 particle count
  if (IS_PM_VALID(payloadBuffer.common.particleCount02)) {
    oss << payloadBuffer.common.particleCount02;
  }
  oss << ",";
  // PM 5.0 particle count
  if (IS_PM_VALID(payloadBuffer.common.particleCount50)) {
    oss << payloadBuffer.common.particleCount50;
  }
  oss << ",";
  // PM 10 particle count
  if (IS_PM_VALID(payloadBuffer.common.particleCount10)) {
    oss << payloadBuffer.common.particleCount10;
  }
  oss << ",";
  // PM 2.5 standard particle
  {
    const bool sp0Ok = IS_PM_VALID(payloadBuffer.common.pm25Sp[0]);
    const bool sp1Ok = IS_PM_VALID(payloadBuffer.common.pm25Sp[1]);
    if (sp0Ok && sp1Ok) {
      const float pm25Sp = (payloadBuffer.common.pm25Sp[0] + payloadBuffer.common.pm25Sp[1]) / 2.0f;
      oss << std::round(pm25Sp);
    } else if (sp0Ok) {
      oss << std::round(payloadBuffer.common.pm25Sp[0]);
    } else if (sp1Ok) {
      oss << std::round(payloadBuffer.common.pm25Sp[1]);
    }
  }
}

bool AirgradientCellularClient::_encodeBinaryPayload(const AirgradientPayload &payload,
                                                     std::vector<uint8_t> &out) {
  out.clear();

  std::unique_ptr<PayloadEncoder> encoder(new (std::nothrow) PayloadEncoder());
  if (!encoder) {
    AG_LOGE(TAG, "Failed to allocate binary payload encoder");
    return false;
  }

  PayloadHeader header = {static_cast<uint8_t>(payload.measureInterval / 60)};
  encoder->init(header);

  // Convert each PayloadBuffer to SensorReading and add to encoder
  for (int i = 0; i < payload.bufferCount; i++) {
    SensorReading reading;
    initSensorReading(&reading);

    const PayloadBuffer &buf = payload.payloadBuffer[i];

    if (buf.timestamp != 0) {
      setFlag(&reading, FLAG_TIMESTAMP);
      reading.timestamp = buf.timestamp;
    }

    // Common sensors
    if (IS_CO2_VALID(buf.common.rco2)) {
      setFlag(&reading, FLAG_CO2);
      reading.co2 = static_cast<uint16_t>(buf.common.rco2);
    }

    if (IS_TEMPERATURE_VALID(buf.common.atmp)) {
      setFlag(&reading, FLAG_TEMP);
      reading.temp = static_cast<int16_t>(std::round(buf.common.atmp * 100));
    }

    if (IS_HUMIDITY_VALID(buf.common.rhum)) {
      setFlag(&reading, FLAG_HUM);
      reading.hum = static_cast<uint16_t>(std::round(buf.common.rhum * 100));
    }

    if (IS_PM_VALID(buf.common.pm01)) {
      setFlag(&reading, FLAG_PM_01);
      reading.pm_01 = static_cast<uint16_t>(std::round(buf.common.pm01 * 10));
    }

    if (IS_PM_VALID(buf.common.pm25[0])) {
      setFlag(&reading, FLAG_PM_25_CH1);
      reading.pm_25[0] = static_cast<uint16_t>(std::round(buf.common.pm25[0] * 10));
    }

    if (IS_PM_VALID(buf.common.pm25[1])) {
      setFlag(&reading, FLAG_PM_25_CH2);
      reading.pm_25[1] = static_cast<uint16_t>(std::round(buf.common.pm25[1] * 10));
    }

    if (IS_PM_VALID(buf.common.pm10)) {
      setFlag(&reading, FLAG_PM_10);
      reading.pm_10 = static_cast<uint16_t>(std::round(buf.common.pm10 * 10));
    }

    if (IS_TVOC_VALID(buf.common.tvoc)) {
      setFlag(&reading, FLAG_TVOC);
      reading.tvoc = static_cast<uint16_t>(buf.common.tvoc);
    }

    if (IS_TVOC_VALID(buf.common.tvocRaw)) {
      setFlag(&reading, FLAG_TVOC_RAW);
      reading.tvoc_raw = static_cast<uint16_t>(buf.common.tvocRaw);
    }

    if (IS_TVOC_VALID(buf.common.nox)) {
      setFlag(&reading, FLAG_NOX);
      reading.nox = static_cast<uint16_t>(buf.common.nox);
    }

    if (IS_NOX_VALID(buf.common.noxRaw)) {
      setFlag(&reading, FLAG_NOX_RAW);
      reading.nox_raw = static_cast<uint16_t>(buf.common.noxRaw);
    }

    if (IS_PM_VALID(buf.common.particleCount003[0])) {
      setFlag(&reading, FLAG_PM_03_PC_CH1);
      reading.pm_03_pc[0] = static_cast<uint16_t>(buf.common.particleCount003[0]);
    }

    if (IS_PM_VALID(buf.common.particleCount003[1])) {
      setFlag(&reading, FLAG_PM_03_PC_CH2);
      reading.pm_03_pc[1] = static_cast<uint16_t>(buf.common.particleCount003[1]);
    }

    if (IS_PM_VALID(buf.common.particleCount005)) {
      setFlag(&reading, FLAG_PM_05_PC);
      reading.pm_05_pc = static_cast<uint16_t>(buf.common.particleCount005);
    }

    if (IS_PM_VALID(buf.common.particleCount01)) {
      setFlag(&reading, FLAG_PM_01_PC);
      reading.pm_01_pc = static_cast<uint16_t>(buf.common.particleCount01);
    }

    if (IS_PM_VALID(buf.common.particleCount02)) {
      setFlag(&reading, FLAG_PM_25_PC);
      reading.pm_25_pc = static_cast<uint16_t>(buf.common.particleCount02);
    }

    if (IS_PM_VALID(buf.common.particleCount50)) {
      setFlag(&reading, FLAG_PM_5_PC);
      reading.pm_5_pc = static_cast<uint16_t>(buf.common.particleCount50);
    }

    if (IS_PM_VALID(buf.common.particleCount10)) {
      setFlag(&reading, FLAG_PM_10_PC);
      reading.pm_10_pc = static_cast<uint16_t>(buf.common.particleCount10);
    }

    if (IS_PM_VALID(buf.common.pm25Sp[0])) {
      setFlag(&reading, FLAG_PM_25_SP_CH1);
      reading.pm_25_sp[0] = static_cast<uint16_t>(std::round(buf.common.pm25Sp[0] * 10));
    }

    if (IS_PM_VALID(buf.common.pm25Sp[1])) {
      setFlag(&reading, FLAG_PM_25_SP_CH2);
      reading.pm_25_sp[1] = static_cast<uint16_t>(std::round(buf.common.pm25Sp[1] * 10));
    }

    // Signal strength
    setFlag(&reading, FLAG_SIGNAL);
    reading.signal = static_cast<int8_t>(payload.signal);

    // Extended payload for MAX models
    if (payloadType == MAX_WITH_O3_NO2 || payloadType == MAX_WITHOUT_O3_NO2) {
      if (IS_VOLT_VALID(buf.ext.extra.vBat)) {
        setFlag(&reading, FLAG_VBAT);
        reading.vbat = static_cast<uint16_t>(std::round(buf.ext.extra.vBat * 100));
      }

      if (IS_VOLT_VALID(buf.ext.extra.vPanel)) {
        setFlag(&reading, FLAG_VPANEL);
        reading.vpanel = static_cast<uint16_t>(std::round(buf.ext.extra.vPanel * 100));
      }

      if (payloadType == MAX_WITH_O3_NO2) {
        if (IS_VOLT_VALID(buf.ext.extra.o3WorkingElectrode)) {
          setFlag(&reading, FLAG_O3_WE);
          reading.o3_we =
              static_cast<uint32_t>(std::round(buf.ext.extra.o3WorkingElectrode * 1000));
        }

        if (IS_VOLT_VALID(buf.ext.extra.o3AuxiliaryElectrode)) {
          setFlag(&reading, FLAG_O3_AE);
          reading.o3_ae =
              static_cast<uint32_t>(std::round(buf.ext.extra.o3AuxiliaryElectrode * 1000));
        }

        if (IS_VOLT_VALID(buf.ext.extra.no2WorkingElectrode)) {
          setFlag(&reading, FLAG_NO2_WE);
          reading.no2_we =
              static_cast<uint32_t>(std::round(buf.ext.extra.no2WorkingElectrode * 1000));
        }

        if (IS_VOLT_VALID(buf.ext.extra.no2AuxiliaryElectrode)) {
          setFlag(&reading, FLAG_NO2_AE);
          reading.no2_ae =
              static_cast<uint32_t>(std::round(buf.ext.extra.no2AuxiliaryElectrode * 1000));
        }

        if (IS_VOLT_VALID(buf.ext.extra.afeTemp)) {
          setFlag(&reading, FLAG_AFE_TEMP);
          reading.afe_temp = static_cast<uint16_t>(std::round(buf.ext.extra.afeTemp * 10));
        }
      }
    }

    //NOTE: This should not happen. Prevent before happen
    if (!encoder->addReading(reading)) {
      AG_LOGE(TAG, "Binary payload encoder batch full (bufferCount=%d max=%d)", payload.bufferCount,
              (int)MAX_BATCH_SIZE);
      return false;
    }
  }

  const uint32_t needed = encoder->calculateTotalSize();
  if (needed == 0) {
    AG_LOGE(TAG, "Binary payload encoder produced empty payload");
    return false;
  }

  if (needed > MAX_PAYLOAD_SIZE) {
    AG_LOGE(TAG, "Binary payload too large (needed=%d cap=%d)", (int)needed, (int)MAX_PAYLOAD_SIZE);
    return false;
  }

  out.resize(needed);

  const int32_t size = encoder->encode(out.data(), (uint32_t)out.size());
  if (size < 0) {
    AG_LOGE(TAG, "Failed to encode binary payload");
    return false;
  }

  out.resize((size_t)size);
  AG_LOGI(TAG, "Binary payload encoded %d readings into %d bytes", (int)encoder->getReadingCount(),
          (int)out.size());
  return true;
}

std::string AirgradientCellularClient::_getEndpoint() {
  if (_extendedPmMeasures) {
    return "cpm"; // special case
  }

  std::string endpoint;
  switch (payloadType) {
  case AirgradientClient::MAX_WITHOUT_O3_NO2:
    endpoint = "cvl";
    break;
  case AirgradientClient::MAX_WITH_O3_NO2:
    endpoint = "cvn";
    break;
  case AirgradientClient::ONE_OPENAIR:
  case AirgradientClient::ONE_OPENAIR_TWO_PMS:
    endpoint = "cts";
    break;
  };

  return endpoint;
}

#endif // ESP8266
