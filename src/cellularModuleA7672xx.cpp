/**
 * AirGradient
 * https://airgradient.com
 *
 * CC BY-SA 4.0 Attribution-ShareAlike 4.0 International License
 */

#ifndef ESP8266

#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
#include "cellularModuleA7672xx.h"
#include <cstdint>
#include <memory>
#include <new>
#include <cstring>

#include "common.h"
#include "agLogger.h"
#include "agSerial.h"
#include "cellularModule.h"
#include "atCommandHandler.h"
#include "cellularModule.h"

#define REGIS_RETRY_DELAY() DELAY_MS(1000);
#define TIMEOUT_WAIT_REGISTERED 60000

CellularModuleA7672XX::CellularModuleA7672XX(AirgradientSerial *agSerial, uint32_t warmUpTimeMs) {
  agSerial_ = agSerial;
  _warmUpTimeMs = warmUpTimeMs;
}

CellularModuleA7672XX::CellularModuleA7672XX(AirgradientSerial *agSerial, int powerPin,
                                             uint32_t warmUpTimeMs) {
  agSerial_ = agSerial;
  _powerIO = static_cast<gpio_num_t>(powerPin);
  _warmUpTimeMs = warmUpTimeMs;
}

CellularModuleA7672XX::~CellularModuleA7672XX() {
  if (at_ != nullptr) {
    delete at_;
    at_ = nullptr;
  }
}

bool CellularModuleA7672XX::init() {
  if (_initialized) {
    AG_LOGI(TAG, "Already initialized");
    return true;
  }

  if (_powerIO != GPIO_NUM_NC) {
    gpio_reset_pin(_powerIO);
    gpio_set_direction(_powerIO, GPIO_MODE_OUTPUT);
    powerOn();
  }

  //! Here assume agSerial_ already initialized and opened
  //! NO! it should initialized here! Right?
  // TODO: Add sanity check

  // Initialize cellular module and wait for module to ready
  at_ = new ATCommandHandler(agSerial_);
  AG_LOGI(TAG, "Checking module readiness...");
  if (!at_->testAT()) {
    AG_LOGW(TAG, "Failed wait cellular module to ready");
    delete at_;
    at_ = nullptr;
    return false;
  }

  // Reset module, to reset previous session
  // TODO: Add option to reset or not
  // reset();
  // DELAY_MS(20000);

  // TODO: Need to validate the response?
  // Disable echo
  at_->sendAT("E0");
  at_->waitResponse();
  DELAY_MS(2000);

  // TODO: Need to validate the response?
  // Disable GPRS event reporting (URC)
  at_->sendAT("+CGEREP=0");
  at_->waitResponse();
  DELAY_MS(2000);

  // Print product identification information
  at_->sendRaw("ATI");
  at_->waitResponse();

  _initialized = true;
  return true;
}

void CellularModuleA7672XX::powerOn() {
  gpio_set_level(_powerIO, 0);
  DELAY_MS(500);
  gpio_set_level(_powerIO, 1);
  DELAY_MS(100);
  gpio_set_level(_powerIO, 0);
  DELAY_MS(100);
}

void CellularModuleA7672XX::powerOff(bool force) {
  if (force) {
    // Force power off
    AG_LOGW(TAG, "Force module to power off");
    gpio_set_level(_powerIO, 1);
    DELAY_MS(1300);
    gpio_set_level(_powerIO, 0);
    return;
  }

  at_->sendAT("+CPOF");
  if (at_->waitResponse() != ATCommandHandler::ExpArg1) {
    // Force power off
    AG_LOGW(TAG, "Force module to power off");
    gpio_set_level(_powerIO, 1);
    DELAY_MS(1300);
    gpio_set_level(_powerIO, 0);
    return;
  }

  AG_LOGI(TAG, "Module powered off");
}

bool CellularModuleA7672XX::reset() {
  at_->sendAT("+CRESET");
  if (at_->waitResponse() != ATCommandHandler::ExpArg1) {
    AG_LOGW(TAG, "Failed reset module");
    return false;
  }

  AG_LOGI(TAG, "Success reset module");
  return true;
}

void CellularModuleA7672XX::sleep() {}

CellResult<std::string> CellularModuleA7672XX::getModuleInfo() { return CellResult<std::string>(); }

CellResult<std::string> CellularModuleA7672XX::retrieveSimCCID() {
  CellResult<std::string> result;
  result.status = CellReturnStatus::Timeout;

  at_->sendAT("+CICCID");
  if (at_->waitResponse("+ICCID:") != ATCommandHandler::ExpArg1) {
    return result;
  }

  std::string ccid;
  if (at_->waitAndRecvRespLine(ccid) == -1) {
    return result;
  }

  // receive OK response from the buffer, ignore it
  at_->waitResponse();

  result.status = CellReturnStatus::Ok;
  result.data = ccid;

  return result;
}

CellReturnStatus CellularModuleA7672XX::isSimReady() {
  at_->sendAT("+CPIN?");
  if (at_->waitResponse("+CPIN:") != ATCommandHandler::ExpArg1) {
    return CellReturnStatus::Timeout;
  }

  // NOTE: Add other possible response and maybe add an enum then set it to result.data
  if (at_->waitResponse("READY") != ATCommandHandler::ExpArg1) {
    return CellReturnStatus::Failed;
  }

  // receive OK response from the buffer, ignore it
  at_->waitResponse();

  return CellReturnStatus::Ok;
}

CellResult<int> CellularModuleA7672XX::retrieveSignal() {
  CellResult<int> result;
  result.status = CellReturnStatus::Timeout;

  at_->sendAT("+CSQ");
  if (at_->waitResponse("+CSQ:") != ATCommandHandler::ExpArg1) {
    return result;
  }

  std::string received;
  if (at_->waitAndRecvRespLine(received) == -1) {
    return result;
  }

  // TODO: Make common function to seperate two values by comma
  int signal = 99;
  size_t pos = received.find(",");
  if (pos != std::string::npos) {
    // Ignore <ber> value, only <rssi>
    std::string signalStr = received.substr(0, pos);
    signal = std::stoi(signalStr);
  }

  // receive OK response from the buffer, ignore it
  at_->waitResponse();

  result.status = CellReturnStatus::Ok;
  result.data = signal;

  return result;
}

CellResult<std::string> CellularModuleA7672XX::retrieveIPAddr() {
  // CGPADDR
  CellResult<std::string> result;
  result.status = CellReturnStatus::Timeout;

  // Retrieve address from pdp cid 1
  at_->sendAT("+CGPADDR=1");
  if (at_->waitResponse("+CGPADDR: 1,") != ATCommandHandler::ExpArg1) {
    return result;
  }

  std::string ipaddr;
  if (at_->waitAndRecvRespLine(ipaddr) == -1) {
    return result;
  }

  // receive OK response from the buffer, ignore it
  at_->waitResponse();

  result.status = CellReturnStatus::Ok;
  result.data = ipaddr;

  return result;
}

CellReturnStatus CellularModuleA7672XX::isNetworkRegistered(CellTechnology ct) {
  auto cmdNR = _mapCellTechToNetworkRegisCmd(ct);
  if (cmdNR.empty()) {
    return CellReturnStatus::Error;
  }

  char buf[15] = {0};
  sprintf(buf, "+%s?", cmdNR.c_str());
  at_->sendAT(buf);
  int resp = at_->waitResponse("+CREG:", "+CEREG:", "+CGREG:");
  if (resp != ATCommandHandler::ExpArg1 && resp != ATCommandHandler::ExpArg2 &&
      resp != ATCommandHandler::ExpArg3) {
    return CellReturnStatus::Timeout;
  }

  std::string recv;
  if (at_->waitAndRecvRespLine(recv) == -1) {
    return CellReturnStatus::Timeout;
  }

  auto crs = CellReturnStatus::Ok;
  if (recv != "0,1" && recv != "0,5" && recv != "1,1" && recv != "1,5") {
    crs = CellReturnStatus::Failed;
  }

  // receive OK response from the buffer, ignore it
  at_->waitResponse();

  return crs;
}

CellResult<std::string>
CellularModuleA7672XX::startNetworkRegistration(CellTechnology ct, const std::string &apn,
                                                uint32_t operationTimeoutMs,
                                                uint32_t scanTimeoutMs) {
  CellResult<std::string> result;
  result.status = CellReturnStatus::Timeout;

  // Make sure CT is supported
  if (_mapCellTechToMode(ct) == -1) {
    result.status = CellReturnStatus::Error;
    return result;
  }

  // Time tracking
  uint32_t startOperationTime = MILLIS();
  uint32_t manualOperatorStartTime = 0;  // Track time per operator in manual mode (60 sec timeout)
  uint32_t serviceStatusStartTime = 0;   // Track time in CHECK_SERVICE_STATUS (30 sec timeout)

  const uint32_t SERVICE_STATUS_TIMEOUT = 30000;  // 30 seconds

  NetworkRegistrationState state = CHECK_MODULE_READY;
  bool finish = false;

  AG_LOGI(TAG, "Starting network registration (operation timeout: %" PRIu32 " ms, scan timeout: %" PRIu32 " ms)",
          operationTimeoutMs, scanTimeoutMs);

  while ((MILLIS() - startOperationTime) < operationTimeoutMs && !finish) {
    switch (state) {
    case CHECK_MODULE_READY: {
      state = _implCheckModuleReady();
      if (state == CHECK_MODULE_READY) {
        // Module or SIM not ready - cannot proceed
        AG_LOGE(TAG, "Module or SIM card is not ready");
        finish = true;
        continue;
      }
      break;
    }

    case PREPARE_MODULE:
      state = _implPrepareModule(ct, apn);
      break;

    case SCAN_OPERATOR:
      state = _implScanOperator(scanTimeoutMs);
      // Exclude scan time (up to scanTimeoutMs) from the operation budget:
      // restart the timer so operators get the full operationTimeoutMs.
      if (state == CONFIGURE_MANUAL_NETWORK) {
        startOperationTime = MILLIS();
        AG_LOGI(TAG, "Scan complete, restarting operation timer for registration");
      }
      break;

    case CONFIGURE_MANUAL_NETWORK:
      state = _implConfigureManualNetwork();
      // Reset manual operator timer when selecting new operator
      manualOperatorStartTime = MILLIS();
      break;

    case OPERATOR_LIST_EXHAUSTED: {
      // All operators exhausted, increment exhaustion counter
      registrationFailCount_++;
      AG_LOGW(TAG, "Operator list exhausted (attempt %" PRIu32 " of %" PRIu32 ")",
              registrationFailCount_, MAX_REGISTRATION_FAILURES);

      if (registrationFailCount_ >= MAX_REGISTRATION_FAILURES) {
        // Reached maximum exhaustion attempts, fail registration
        AG_LOGE(TAG, "Failed after %" PRIu32 " full iterations through operator list",
                registrationFailCount_);
        // Clear operator list and saved operator, reset fail counter
        availableOperators_.clear();
        currentOperatorId_ = 0;
        currentOperatorIndex_ = 0;
        registrationFailCount_ = 0;
        finish = true;
        continue;
      }

      // Reset module to ensure the next registration attempt in clean state
      // In case every operator return 3 or 11
      if (reset() == false) {
        AG_LOGW(TAG, "Reset failed, power cycle module...");
        powerOff(true);
        DELAY_MS(2000);
        powerOn();
      }
      AG_LOGI(TAG, "Wait for 10s for module to warming up");
      DELAY_MS(10000);
      reinitialize();

      // Haven't reached max attempts yet, reset state to start over.
      AG_LOGI(TAG, "Resetting operator state to retry from beginning");
      currentOperatorId_ = 0;
      currentOperatorIndex_ = 0;
      state = CHECK_MODULE_READY;
      break;
    }

    case CHECK_NETWORK_REGISTRATION:
      state = _implCheckNetworkRegistration(ct, manualOperatorStartTime);
      // Reset service status timer when entering CHECK_SERVICE_STATUS
      if (state == CHECK_SERVICE_STATUS) {
        serviceStatusStartTime = MILLIS();
      }
      break;

    case CHECK_SERVICE_STATUS: {
      state = _implCheckServiceStatus();
      // Check if checking service status is timeout
      if ((MILLIS() - serviceStatusStartTime) > SERVICE_STATUS_TIMEOUT) {
        AG_LOGW(TAG, "Service status check timed out after 30s, re-checking registration");
        manualOperatorStartTime = MILLIS();  // Fresh 60s for operator
        serviceStatusStartTime = 0;           // Reset for next service check
        state = CHECK_NETWORK_REGISTRATION;
        continue;
      }
      break;
    }

    case NETWORK_READY:
      state = _implNetworkReady();
      if (state == NETWORK_READY) {
        // Network registration complete!
        finish = true;
        continue;
      }
      break;
    }

    // Give CPU a break
    DELAY_MS(10);
  }

  if (state != NETWORK_READY) {
    AG_LOGW(TAG, "Network registration failed! Final state: %d (fail count: %" PRIu32 " of %" PRIu32 ")",
            state, registrationFailCount_, MAX_REGISTRATION_FAILURES);
    return result;
  }

  // Registration succeeded, reset fail counter
  registrationFailCount_ = 0;

  AG_LOGI(TAG, "Warming up for %" PRIu32 "ms...", _warmUpTimeMs);
  DELAY_MS(_warmUpTimeMs);

  result.status = CellReturnStatus::Ok;
  return result;
}

CellReturnStatus CellularModuleA7672XX::reinitialize() {
  AG_LOGI(TAG, "Initialize module");
  if (!at_->testAT()) {
    AG_LOGW(TAG, "Failed wait cellular module to ready");
    return CellReturnStatus::Error;
  }

  // Disable echo
  at_->sendAT("E0");
  at_->waitResponse();
  DELAY_MS(2000);

  // Disable GPRS event reporting (URC)
  at_->sendAT("+CGEREP=0");
  at_->waitResponse();
  DELAY_MS(2000);

  return CellReturnStatus::Ok;
}

CellResult<CellularModule::HttpResponse>
CellularModuleA7672XX::httpGet(const std::string &url, int connectionTimeout, int responseTimeout) {
  CellResult<CellularModule::HttpResponse> result;
  result.status = CellReturnStatus::Error;
  ATCommandHandler::Response response;

  // TODO: Sanity check registration status?

  // +HTTPINIT
  result.status = _httpInit();
  if (result.status != CellReturnStatus::Ok) {
    return result;
  }

  // +HTTPPARA set RECVTO and CONNECTTO
  result.status = _httpSetParamTimeout(connectionTimeout, responseTimeout);
  if (result.status != CellReturnStatus::Ok) {
    // NOTE: Failed set timeout parameter, just continue with default?
    _httpTerminate();
    return result;
  }

  // +HTTPPARA set URL
  result.status = _httpSetUrl(url);
  if (result.status != CellReturnStatus::Ok) {
    _httpTerminate();
    return result;
  }

  // +HTTPACTION
  /// Execute HTTP request with 3 times retry when request failed, not error or timeout from CE card
  int statusCode, bodyLen, counter = 0;
  do {
    statusCode = -1;
    bodyLen = -1;

    // 0 is GET method defined valus for this module
    result.status = _httpAction(0, connectionTimeout, responseTimeout, &statusCode, &bodyLen);
    if (result.status == CellReturnStatus::Ok) {
      break;
    }

    ESP_LOGW(TAG, "Retry HTTP request in 2s");
    counter += 1;
    DELAY_MS(2000);
  } while (counter < 3 && result.status == CellReturnStatus::Failed);

  // Final check if request is successful or not
  if (result.status != CellReturnStatus::Ok) {
    AG_LOGE(TAG, "HTTP request failed!");
    _httpTerminate();
    return result;
  }
  AG_LOGI(TAG, "HTTP response code %d with body len: %d. Retrieving response body...", statusCode,
          bodyLen);

  uint32_t retrieveStartTime = MILLIS();
  char *bodyResponse = nullptr;
  if (bodyLen > 0) {
    // Create temporary memory to handle the buffer
    bodyResponse = new char[bodyLen + 1];
    memset(bodyResponse, 0, bodyLen + 1);

    // +HTTPREAD
    int offset = 0;
    int receivedBufferLen;
    char *buf = new char[HTTPREAD_CHUNK_SIZE + 1]; // Add +1 to give a space at the end

    do {
      memset(buf, 0, (HTTPREAD_CHUNK_SIZE + 1));
      sprintf(buf, "+HTTPREAD=%d,%d", offset, HTTPREAD_CHUNK_SIZE);
      at_->sendAT(buf);
      response = at_->waitResponse("+HTTPREAD:"); // Wait for first +HTTPREAD, skip the OK
      if (response == ATCommandHandler::Timeout) {
        AG_LOGW(TAG, "Timeout wait response +HTTPREAD");
        break;
      } else if (response == ATCommandHandler::ExpArg2) {
        AG_LOGW(TAG, "Error execute HTTPREAD");
        break;
      }

      // Get first +HTTPREAD value
      if (at_->waitAndRecvRespLine(buf, HTTPREAD_CHUNK_SIZE) == -1) {
        AG_LOGW(TAG, "Failed retrieve +HTTPREAD value length");
        break;
      }
      receivedBufferLen = std::stoi(buf);

      // Receive body from http response with include whitespace since its a binary
      // Directly retrieve buffer with expected the expected length
      int receivedActual = at_->retrieveBuffer(buf, receivedBufferLen);
      if (receivedActual != receivedBufferLen) {
        // Size received not the same as expected, handle better
        AG_LOGE(TAG, "receivedBufferLen: %d | receivedActual: %d", receivedBufferLen,
                receivedActual);
        break;
      }
      at_->waitResponse("+HTTPREAD: 0");
      at_->clearBuffer();

      AG_LOGV(TAG, "Received body len from buffer: %d", receivedBufferLen);

      // Append response body chunk to result
      memcpy(bodyResponse + offset, buf, receivedBufferLen);

      // Continue to retrieve another 200 bytes
      offset = offset + HTTPREAD_CHUNK_SIZE;

#if CONFIG_DELAY_HTTPREAD_ITERATION_ENABLED
      vTaskDelay(pdMS_TO_TICKS(10));
#endif
    } while (offset < bodyLen);

    delete[] buf;

    // Check if all response body data received
    if (offset < bodyLen) {
      AG_LOGE(TAG, "Failed to retrieve all response body data from module");
      _httpTerminate();
      delete[] bodyResponse;
      return result;
    }
  }

  AG_LOGD(TAG, "Finish retrieve response body from module buffer in %.2fs",
          ((float)MILLIS() - retrieveStartTime) / 1000);

  // set status code and response body for return function
  result.data.statusCode = statusCode;
  result.data.bodyLen = bodyLen;
  if (bodyLen > 0) {
    // // Debug purpose
    // Serial.println("Repsonse body:");
    // for (int i = 0; i < bodyLen; i++) {
    //   if (bodyResponse[i] < 0x10)
    //     Serial.print("0");
    //   Serial.print((uint8_t)bodyResponse[i], HEX);
    // }

    result.data.body = std::unique_ptr<char[]>(bodyResponse);
  }

  _httpTerminate();
  AG_LOGI(TAG, "httpGet() finish");

  result.status = CellReturnStatus::Ok;
  return result;
}

CellResult<CellularModule::HttpResponse>
CellularModuleA7672XX::httpPost(const std::string &url, const std::string &body,
                                const std::string &headContentType, int connectionTimeout,
                                int responseTimeout) {

  CellResult<CellularModule::HttpResponse> result;
  result.status = CellReturnStatus::Error;
  ATCommandHandler::Response response;

  // TODO: Sanity check Registration Status?

  // +HTTPINIT
  result.status = _httpInit();
  if (result.status != CellReturnStatus::Ok) {
    return result;
  }

  // +HTTPPARA set RECVTO and CONNECTTO
  result.status = _httpSetParamTimeout(connectionTimeout, responseTimeout);
  if (result.status != CellReturnStatus::Ok) {
    // NOTE: Failed set timeout parameter, just continue with default?
    _httpTerminate();
    return result;
  }

  if (headContentType != "") {
    // AT+HTTPPARA="CONTENT", contenttype
    char buffer[100] = {0};
    sprintf(buffer, "+HTTPPARA=\"CONTENT\",\"%s\"", headContentType.c_str());
    at_->sendAT(buffer);
    response = at_->waitResponse();
    if (response == ATCommandHandler::Timeout) {
      AG_LOGW(TAG, "Timeout wait response +HTTPPARA CONTENT");
      _httpTerminate();
      result.status = CellReturnStatus::Timeout;
      return result;
    } else if (response == ATCommandHandler::ExpArg2) {
      AG_LOGW(TAG, "Error set HTTP param CONTENT");
      _httpTerminate();
      result.status = CellReturnStatus::Error;
      return result;
    }
  }

  // TODO: Another +HTTPPARA to handle https request SSLCFG

  // +HTTPPARA set URL
  result.status = _httpSetUrl(url);
  if (result.status != CellReturnStatus::Ok) {
    _httpTerminate();
    return result;
  }

  // +HTTPDATA ; Body len needs to be the same as length send after DOWNLOAD, otherwise error
  char buf[25] = {0};
  sprintf(buf, "+HTTPDATA=%d,10", body.length());
  at_->sendAT(buf);
  if (at_->waitResponse("DOWNLOAD") != ATCommandHandler::ExpArg1) {
    // Either timeout wait for expected response or return ERROR
    AG_LOGW(TAG, "Error +HTTPDATA wait for \"DOWNLOAD\" response");
    _httpTerminate();
    result.status = CellReturnStatus::Error;
    return result;
  }

  AG_LOGI(TAG, "Receive \"DOWNLOAD\" event, adding request body");
  at_->sendRaw(body.c_str());
  // Wait for 'OK' after send request body
  // Timeout set based on +HTTPDATA param
  if (at_->waitResponse(10000) != ATCommandHandler::ExpArg1) {
    // Timeout wait "OK"
    AG_LOGW(TAG, "Error +HTTPDATA wait for \"DOWNLOAD\" response");
    _httpTerminate();
    result.status = CellReturnStatus::Error;
    return result;
  }

  // +HTTPACTION
  int statusCode = -1;
  int bodyLen = -1;
  // 1 is GET method defined valus for this module
  result.status = _httpAction(1, connectionTimeout, responseTimeout, &statusCode, &bodyLen);
  if (result.status != CellReturnStatus::Ok) {
    _httpTerminate();
    return result;
  }

  AG_LOGI(TAG, "HTTP response code %d with body len: %d", statusCode, bodyLen);

  // set status code, and ignore response body
  result.data.statusCode = statusCode;
  // TODO: In the future retrieve the response body

  _httpTerminate();
  AG_LOGI(TAG, "httpPost() finish");

  result.status = CellReturnStatus::Ok;
  return result;
}

CellReturnStatus CellularModuleA7672XX::mqttConnect(const std::string &clientId,
                                                    const std::string &host, int port,
                                                    std::string username, std::string password) {
  char buf[200] = {0};
  std::string result;

  // +CMQTTSTART
  at_->sendAT("+CMQTTSTART");
  auto atResult = at_->waitResponse(12000, "+CMQTTSTART:");
  if (atResult == ATCommandHandler::Timeout || atResult == ATCommandHandler::CMxError) {
    AG_LOGW(TAG, "Timeout wait for +CMQTTSTART response");
    return CellReturnStatus::Timeout;
  } else if (atResult == ATCommandHandler::ExpArg1) {
    // +CMQTTSTART response received as arg1
    // Get value of CMQTTSTART, expected is 0
    if (at_->waitAndRecvRespLine(result) == -1) {
      return CellReturnStatus::Timeout;
    }
    if (result != "0") {
      // Failed to start
      AG_LOGE(TAG, "CMQTTSTART failed with value %s", result.c_str());
      return CellReturnStatus::Error;
    }
    // CMQTTSTART ok
  } else if (atResult == ATCommandHandler::ExpArg2) {
    // Here it return error, but based on the document module MQTT context already started
    // Do nothing
    AG_LOGI(TAG, "+CMQTTSTART return error, which means mqtt context already started");
  }

  // +CMQTTACCQ
  sprintf(buf, "+CMQTTACCQ=0,\"%s\",0", clientId.c_str());
  at_->sendAT(buf);
  if (at_->waitResponse() != ATCommandHandler::ExpArg1) {
    // ERROR or TIMEOUT, doesn't matter
    return CellReturnStatus::Error;
  }

  DELAY_MS(3000);

  // +CMQTTCONNECT
  // keep alive 120; cleansession 1
  memset(buf, 0, 200);
  if (username != "" && password != "") {
    // Both username and password provided
    AG_LOGI(TAG, "Connect with username and password");
    sprintf(buf, "+CMQTTCONNECT=0,\"tcp://%s:%d\",120,1,\"%s\",\"%s\"", host.c_str(), port,
            username.c_str(), password.c_str());
  } else if (username != "") {
    // Only username that is provided
    AG_LOGI(TAG, "Connect with username only");
    sprintf(buf, "+CMQTTCONNECT=0,\"tcp://%s:%d\",120,1,\"%s\"", host.c_str(), port,
            username.c_str());
  } else {
    // No credentials
    sprintf(buf, "+CMQTTCONNECT=0,\"tcp://%s:%d\",120,1", host.c_str(), port);
  }
  at_->sendAT(buf);
  if (at_->waitResponse(30000, "+CMQTTCONNECT: 0,") != ATCommandHandler::ExpArg1) {
    at_->clearBuffer();
    return CellReturnStatus::Error;
  }

  if (at_->waitAndRecvRespLine(result) == -1) {
    return CellReturnStatus::Timeout;
  }

  // If result not 0, then error occur
  if (result != "0") {
    AG_LOGE(TAG, "+CMQTTCONNECT error result: %s", result.c_str());
    return CellReturnStatus::Error;
  }
  at_->clearBuffer();

  return CellReturnStatus::Ok;
}

CellReturnStatus CellularModuleA7672XX::mqttDisconnect() {
  std::string result;
  // +CMQTTDISC
  at_->sendAT("+CMQTTDISC=0,60"); // Timeout 60s
  /// wait +CMTTDISC until client_index
  if (at_->waitResponse(60000, "+CMQTTDISC: 0,") != ATCommandHandler::ExpArg1) {
    at_->clearBuffer();
    // Error or timeout
    return CellReturnStatus::Error;
  }

  if (at_->waitAndRecvRespLine(result) == -1) {
    return CellReturnStatus::Timeout;
  }

  if (result != "0") {
    AG_LOGE(TAG, "+CMQTTDISC error result: %s", result.c_str());
    return CellReturnStatus::Error;
  }
  at_->clearBuffer();

  // +CMQTTREL
  at_->sendAT("+CMQTTREL=0");
  if (at_->waitResponse() != ATCommandHandler::ExpArg1) {
    // Ignore response err code
    at_->clearBuffer();
    return CellReturnStatus::Error;
  }
  at_->clearBuffer();

  // +CMQTTSTOP
  at_->sendAT("+CMQTTSTOP");
  if (at_->waitResponse() != ATCommandHandler::ExpArg1) {
    // Ignore response err code
    return CellReturnStatus::Error;
  }
  at_->clearBuffer();

  return CellReturnStatus::Ok;
}

CellReturnStatus CellularModuleA7672XX::mqttPublish(const std::string &topic,
                                                    const std::string &payload, int qos, int retain,
                                                    int timeoutS) {
  char buf[50] = {0};
  std::string result;

  // +CMQTTTOPIC
  sprintf(buf, "+CMQTTTOPIC=0,%d", topic.length());
  at_->sendAT(buf);
  if (at_->waitResponse(">") != ATCommandHandler::ExpArg1) {
    // Either timeout wait for expected response or return ERROR
    AG_LOGW(TAG, "Error +CMQTTTOPIC wait for \">\" response");
    return CellReturnStatus::Error;
  }

  AG_LOGI(TAG, "Receive \">\" event, adding topic");
  at_->sendRaw(topic.c_str());
  // Wait for 'OK' after send topic
  if (at_->waitResponse() != ATCommandHandler::ExpArg1) {
    // Timeout wait "OK"
    AG_LOGW(TAG, "Error +CMQTTTOPIC wait for \"OK\" response");
    return CellReturnStatus::Error;
  }

  // +CMQTTPAYLOAD
  memset(buf, 0, 50);
  sprintf(buf, "+CMQTTPAYLOAD=0,%d", payload.length());
  at_->sendAT(buf);
  if (at_->waitResponse(">") != ATCommandHandler::ExpArg1) {
    // Either timeout wait for expected response or return ERROR
    AG_LOGW(TAG, "Error +CMQTTPAYLOAD wait for \">\" response");
    return CellReturnStatus::Error;
  }

  AG_LOGI(TAG, "Receive \">\" event, adding payload");
  at_->sendRaw(payload.c_str());
  // Wait for 'OK' after send payload
  if (at_->waitResponse() != ATCommandHandler::ExpArg1) {
    // Timeout wait "OK"
    AG_LOGW(TAG, "Error +CMQTTPAYLOAD wait for \"OK\" response");
    return CellReturnStatus::Error;
  }

  memset(buf, 0, 50);
  sprintf(buf, "+CMQTTPUB=0,%d,%d,%d", qos, timeoutS, retain);
  int timeoutMs = timeoutS * 1000;
  at_->sendAT(buf);
  if (at_->waitResponse(timeoutMs, "+CMQTTPUB: 0,") != ATCommandHandler::ExpArg1) {
    AG_LOGW(TAG, "+CMQTTPUBLISH error");
    return CellReturnStatus::Error;
  }

  // Retrieve the value
  if (at_->waitAndRecvRespLine(result) == -1) {
    AG_LOGW(TAG, "+CMQTTPUB retrieve value timeout");
    return CellReturnStatus::Timeout;
  }

  if (result != "0") {
    AG_LOGE(TAG, "Failed +CMQTTPUB with value %s", result.c_str());
    return CellReturnStatus::Error;
  }

  // Make sure buffer clean
  at_->clearBuffer();

  return CellReturnStatus::Ok;
}

CellReturnStatus CellularModuleA7672XX::udpConnect(const std::string &host, int port) {
  AG_LOGI(TAG, "Establish UDP connection to %s:%d", host.c_str(), port);

  auto status = _startUDP();
  if (status != CellReturnStatus::Ok) {
    return status;
  }

  status = _connectUDP(host, port);
  if (status != CellReturnStatus::Ok) {
    return status;
  }

  at_->sendAT("+CIPRXGET=1");
  ATCommandHandler::Response resp = at_->waitResponse();
  if (resp != ATCommandHandler::ExpArg1) {
    AG_LOGE(TAG, "Failed set UDP socket receive mode to manual");
    return CellReturnStatus::Failed;
  }

  AG_LOGI(TAG, "Success establish UDP connection");
  return CellReturnStatus::Ok;
}

CellReturnStatus CellularModuleA7672XX::udpDisconnect() {
  auto status = _disconnectUDP();
  if (status != CellReturnStatus::Ok) {
    return status;
  }

  status = _stopUDP();
  if (status != CellReturnStatus::Ok) {
    return status;
  }

  AG_LOGI(TAG, "Success disconnect UDP connection");
  return CellReturnStatus::Ok;
}

CellReturnStatus CellularModuleA7672XX::udpSend(const CellularModule::UdpPacket &packet,
                                                const std::string &host, uint16_t port) {
  // Send via AT+CIPSEND with hex mode
  char cmd[64];
  snprintf(cmd, sizeof(cmd), "+CIPSEND=%d,%d,\"%s\",%d", UDP_LINK_ID, packet.size, host.c_str(),
           port);

  // Serial.printf("[CoAP] Sending %d bytes to %s:%d...\n", bufferSize, host, port);

  at_->sendAT(cmd);
  auto response = at_->waitResponse(5000, ">", "+CIPERROR:");
  if (response != ATCommandHandler::ExpArg1) {
    // TODO: Get the CIPERROR error number and timeout
    AG_LOGW(TAG, "Error +CIPSEND wait for \">\" response");
    return CellReturnStatus::Error;
  }

  // Send the hex data
  at_->sendRaw((const char *)packet.buff.data(), packet.size);
  response = at_->waitResponse(5000, "+CIPSEND: 0,"); // until connection link
  if (response != ATCommandHandler::ExpArg1) {
    AG_LOGW(TAG, "Error +CIPSEND wait for \"+CIPSEND: 0,\" response");
    return CellReturnStatus::Error;
  }

  // Confirm CIPSEND sent length
  std::string data;
  at_->waitAndRecvRespLine(data);
  // Sanity check if value is empty
  if (data.empty()) {
    AG_LOGW(TAG, "+CIPSEND result value empty");
    return CellReturnStatus::Error;
  }
  int rsl = 0, cnf = 0;
  Common::splitByDelimiter(data, &rsl, &cnf);
  if (rsl != cnf) {
    ESP_LOGE(TAG, "CIPSEND expected bytes send and confirmation different (rsl:%d;cnf:%d)", rsl,
             cnf);
    return CellReturnStatus::Error;
  }

  return CellReturnStatus::Ok;
}

CellResult<CellularModule::UdpPacket> CellularModuleA7672XX::udpReceive(uint32_t timeout) {
  CellResult<CellularModule::UdpPacket> result;
  result.status = CellReturnStatus::Error;
  ATCommandHandler::Response response;

  // Wait for URC notification
  response = at_->waitResponse(3000, "+CIPRXGET: 1,0");
  if (response == ATCommandHandler::Timeout) {
    AG_LOGE(TAG, "Wait +CIPRXGET URC timeout");
    result.status = CellReturnStatus::Timeout;
    return result;
  } else if (response != ATCommandHandler::ExpArg1) {
    AG_LOGE(TAG, "Wait +CIPRXGET URC Error");
    result.status = CellReturnStatus::Error;
    return result;
  }

  at_->clearBuffer();

  // Get packet length available in buffer CIPRXGET=4 (query available data length)
  char buf[32];
  snprintf(buf, sizeof(buf), "+CIPRXGET=4,%d", UDP_LINK_ID);
  at_->sendAT(buf);

  // Response format: +CIPRXGET: 4,<link_num>,<data_len>
  memset(buf, 0, 32);
  snprintf(buf, 32, "+CIPRXGET: 4,%d,", UDP_LINK_ID);
  response = at_->waitResponse(9000, buf, "+IP ERROR:");
  if (response != ATCommandHandler::ExpArg1) {
    // TODO: Check +IP ERROR err_info
    AG_LOGE(TAG, "Error CIPRXGET=4 to receive UDP packet total length");
    result.status = CellReturnStatus::Error;
    return result;
  }

  // Get the <data_len>
  memset(buf, 0, 32);
  at_->waitAndRecvRespLine(buf, 32);
  char *end;
  int udpPacketSize = 0;
  udpPacketSize = strtol(buf, &end, 10);
  if (udpPacketSize == 0 || end == buf) {
    AG_LOGE(TAG, "No available data on the buffer");
    result.status = CellReturnStatus::Failed;
    return result;
  }
  AG_LOGI(TAG, "UDP packet size in buffer: %d. Retrieving buffer... ", udpPacketSize);

  // Allocate dynamic buffer to hold the complete UDP packet
  char *udpPacket = new char[udpPacketSize];
  memset(udpPacket, 0, udpPacketSize);

  // Retrieve packet from buffer in chunks
  int offset = 0;
  int totalReceived = 0;
  char *chunkBuf = new char[HTTPREAD_CHUNK_SIZE + 1];

  do {
    // Determine chunk size to request (last chunk might be smaller)
    int requestSize = std::min(HTTPREAD_CHUNK_SIZE, udpPacketSize - totalReceived);

    memset(buf, 0, 32);
    snprintf(buf, 32, "+CIPRXGET=2,%d,%d", UDP_LINK_ID, requestSize);
    at_->sendAT(buf);

    // Response format: +CIPRXGET: 2,<link_num>,<read_len>,<rest_len>
    memset(buf, 0, 32);
    snprintf(buf, 32, "+CIPRXGET: 2,%d,", UDP_LINK_ID); // until <link_num>,
    response = at_->waitResponse(5000, buf);
    if (response != ATCommandHandler::ExpArg1) {
      AG_LOGE(TAG, "Failed to retrieve UDP packet chunk from buffer (CIPRXGET:2)");
      result.status = CellReturnStatus::Error;
      delete[] udpPacket;
      delete[] chunkBuf;
      return result;
    }

    // Get the <read_len> and <rest_len>
    std::string tmp;
    at_->waitAndRecvRespLine(tmp);
    // Sanity check if value is empty
    if (tmp.empty()) {
      AG_LOGW(TAG, "Timeout wait the rest of \"+CIPRXGET:2\" response");
      result.status = CellReturnStatus::Error;
      delete[] udpPacket;
      delete[] chunkBuf;
      return result;
    }

    int readLen = 0, restLen = 0;
    Common::splitByDelimiter(tmp, &readLen, &restLen);
    AG_LOGD(TAG, "read_len: %d | rest_len: %d", readLen, restLen);

    // Retrieve the actual chunk data
    memset(chunkBuf, 0, HTTPREAD_CHUNK_SIZE + 1);
    int receivedActual = at_->retrieveBuffer(chunkBuf, readLen);
    if (receivedActual != readLen) {
      AG_LOGE(TAG, "Failed retrieve UDP chunk. Expected: %d, Received: %d", readLen,
              receivedActual);
      result.status = CellReturnStatus::Failed;
      delete[] udpPacket;
      delete[] chunkBuf;
      return result;
    }

    // Copy chunk to result buffer
    memcpy(udpPacket + offset, chunkBuf, readLen);
    offset += readLen;
    totalReceived += readLen;

    AG_LOGV(TAG, "Received UDP chunk: %d bytes, total: %d/%d", readLen, totalReceived,
            udpPacketSize);

    // Continue until no more data remains
    if (restLen == 0 || totalReceived >= udpPacketSize) {
      break;
    }

  } while (totalReceived < udpPacketSize);

  delete[] chunkBuf;

  // Verify received all expected data
  if (totalReceived != udpPacketSize) {
    AG_LOGE(TAG, "Incomplete UDP packet received. Expected: %d, Got: %d", udpPacketSize,
            totalReceived);
    result.status = CellReturnStatus::Failed;
    delete[] udpPacket;
    return result;
  }

  result.data.buff.assign(udpPacket, udpPacket + udpPacketSize);
  result.data.size = udpPacketSize;
  result.status = CellReturnStatus::Ok;

  delete[] udpPacket;
  return result;
}

CellularModuleA7672XX::NetworkRegistrationState CellularModuleA7672XX::_implCheckModuleReady() {
  // Check if module responds to AT commands
  if (at_->testAT() == false) {
    REGIS_RETRY_DELAY();
    // TODO: If too long, try reset module
    return CHECK_MODULE_READY;
  }

  // Check if SIM card is ready
  if (isSimReady() != CellReturnStatus::Ok) {
    REGIS_RETRY_DELAY();
    return CHECK_MODULE_READY;
  }

  // Module and SIM ready, always prepare module
  AG_LOGI(TAG, "Module and SIM ready, continue to: PREPARE_MODULE");
  return PREPARE_MODULE;
}

CellularModuleA7672XX::NetworkRegistrationState
CellularModuleA7672XX::_implCheckNetworkRegistration(CellTechnology ct,
                                                      uint32_t manualOperatorStartTime) {
  // Get detailed registration status
  CellResult<RegistrationStatus> statusResult = _checkDetailedRegistrationStatus(ct);

  if (statusResult.status == CellReturnStatus::Timeout) {
    AG_LOGW(TAG, "Timeout checking registration status");
    return CHECK_MODULE_READY;
  }

  if (statusResult.status != CellReturnStatus::Ok) {
    REGIS_RETRY_DELAY();
    return CHECK_NETWORK_REGISTRATION;
  }

  int stat = statusResult.data.stat;

  // Always query signal strength for logging
  CellResult<int> signalResult = retrieveSignal();
  int signal = (signalResult.status == CellReturnStatus::Ok) ? signalResult.data : 99;

  // Log status and signal for debugging
  AG_LOGI(TAG, "Registration check - Status: %d, Signal: %d", stat, signal);

  // Check for registered states (1 = home, 5 = roaming)
  if (stat == 1 || stat == 5) {
    // Registered! Validate signal before proceeding
    if (signalResult.status == CellReturnStatus::Timeout) {
      return CHECK_MODULE_READY;
    }

    // Check if returned signal is valid
    if (signal < 1 || signal > 31) {
      AG_LOGW(TAG, "Invalid signal: %d", signal);
      REGIS_RETRY_DELAY();
      return CHECK_NETWORK_REGISTRATION;
    } else if (signal < 10) {
      AG_LOGW(TAG,
              "This operator %" PRIu32 " has really low signal %d (csq), moving on..",
              currentOperatorId_, signal);
      currentOperatorIndex_++;
      REGIS_RETRY_DELAY();
      return CONFIGURE_MANUAL_NETWORK;
    }

    AG_LOGI(TAG, "Registered successfully, continue to: CHECK_SERVICE_STATUS");
    return CHECK_SERVICE_STATUS;
  }

  // Check for denied (3) or emergency bearer only (11) - fail fast with confirmation
  if (stat == 3 || stat == 11) {
    AG_LOGW(TAG, "Registration denied or emergency only (status=%d), confirming for 10 seconds", stat);

    // Wait 10 seconds to confirm it's persistent (not transient)
    uint32_t deniedStartTime = MILLIS();
    while ((MILLIS() - deniedStartTime) < 10000) {  // 10 second confirmation
      DELAY_MS(1000);

      // Re-check status
      statusResult = _checkDetailedRegistrationStatus(ct);
      if (statusResult.status == CellReturnStatus::Ok) {
        int newStat = statusResult.data.stat;
        if (newStat == 1 || newStat == 5) {
          // Status changed to registered during confirmation period
          AG_LOGI(TAG, "Status changed to registered (stat=%d) during confirmation", newStat);
          return CHECK_NETWORK_REGISTRATION;
        }
        stat = newStat;
      }
    }

    // Still denied/emergency after confirmation period
    if (stat == 3 || stat == 11) {
      AG_LOGW(TAG, "Registration still denied/emergency (status=%d) after 10s, trying next operator", stat);
      currentOperatorIndex_++;
      return CONFIGURE_MANUAL_NETWORK;
    }
  }

  // Not registered, check timeout
  if ((MILLIS() - manualOperatorStartTime) > TIMEOUT_WAIT_REGISTERED) {
    AG_LOGW(TAG, "Not registered with current operator after 60 seconds, trying next");
    currentOperatorIndex_++;
    return CONFIGURE_MANUAL_NETWORK;
  }

  // Still trying current operator
  // REGIS_RETRY_DELAY();
  DELAY_MS(3000);
  return CHECK_NETWORK_REGISTRATION;
}

// New state machine implementations

CellularModuleA7672XX::NetworkRegistrationState
CellularModuleA7672XX::_implPrepareModule(CellTechnology ct, const std::string &apn) {
  AG_LOGI(TAG, "Preparing module for registration");

  // Disable network registration URC
  CellReturnStatus crs = _disableNetworkRegistrationURC(ct);
  if (crs == CellReturnStatus::Timeout) {
    return CHECK_MODULE_READY;
  }

  // Apply cellular technology
  crs = _applyCellularTechnology(ct);
  if (crs != CellReturnStatus::Ok) {
    AG_LOGW(TAG, "Failed to apply cellular technology");
    return CHECK_MODULE_READY;
  }

  // Apply APN
  crs = _applyAPN(apn);
  if (crs == CellReturnStatus::Timeout) {
    return CHECK_MODULE_READY;
  }

  // Check if we have operator list
  if (availableOperators_.empty()) {
    AG_LOGI(TAG, "No operator list available, continue to: SCAN_OPERATOR");
    return SCAN_OPERATOR;
  } else {
    AG_LOGI(TAG, "Operator list available (%zu operators), continue to: CONFIGURE_MANUAL_NETWORK",
            availableOperators_.size());
    return CONFIGURE_MANUAL_NETWORK;
  }
}

CellularModuleA7672XX::NetworkRegistrationState
CellularModuleA7672XX::_implScanOperator(uint32_t scanTimeoutMs) {
  AG_LOGI(TAG, "Scanning for available operators");

  CellResult<std::vector<OperatorInfo>> scanResult = _scanAvailableOperators(scanTimeoutMs);

  if (scanResult.status == CellReturnStatus::Timeout) {
    AG_LOGW(TAG, "Operator scan timed out");
    return CHECK_MODULE_READY;
  } else if (scanResult.status != CellReturnStatus::Ok || scanResult.data.empty()) {
    AG_LOGW(TAG, "Operator scan failed or returned no operators");
    return CHECK_MODULE_READY;
  }

  // Store operator list
  availableOperators_ = scanResult.data;
  currentOperatorIndex_ = 0;

  AG_LOGI(TAG, "Operator scan complete, continue to: CONFIGURE_MANUAL_NETWORK");
  return CONFIGURE_MANUAL_NETWORK;
}

CellularModuleA7672XX::NetworkRegistrationState
CellularModuleA7672XX::_implConfigureManualNetwork() {
  // If we have a saved successful operator, try to find it and use it first
  if (currentOperatorId_ != 0 && currentOperatorIndex_ == 0) {
    AG_LOGI(TAG, "Searching for saved operator %" PRIu32 " in list", currentOperatorId_);
    for (size_t i = 0; i < availableOperators_.size(); i++) {
      if (availableOperators_[i].operatorId == currentOperatorId_) {
        currentOperatorIndex_ = i;
        AG_LOGI(TAG, "Found saved operator at index %zu, trying it first", i);
        break;
      }
    }
    // If not found, currentOperatorIndex_ stays at 0 (start from beginning)
    if (currentOperatorIndex_ == 0 && (availableOperators_.empty() ||
        availableOperators_[0].operatorId != currentOperatorId_)) {
      AG_LOGW(TAG, "Saved operator %" PRIu32 " not found in list, starting from beginning", currentOperatorId_);
    }
  }

  // Check if we have exhausted all operators
  if (availableOperators_.empty() || currentOperatorIndex_ >= availableOperators_.size()) {
    AG_LOGE(TAG, "No more operators to try, all exhausted");
    return OPERATOR_LIST_EXHAUSTED;
  }

  OperatorInfo opInfo = availableOperators_[currentOperatorIndex_];
  currentOperatorId_ = opInfo.operatorId;  // Track last attempted operator for persistence
  AG_LOGI(TAG, "Configuring manual operator: %" PRIu32 " with AcT: %d (index %zu of %zu)",
          opInfo.operatorId, opInfo.accessTech, currentOperatorIndex_ + 1, availableOperators_.size());
  DELAY_MS(5000);

  CellReturnStatus crs = _applyOperatorSelection(opInfo.operatorId, opInfo.accessTech);
  if (crs == CellReturnStatus::Timeout) {
    currentOperatorIndex_++;
    return CHECK_MODULE_READY;
  } else if (crs != CellReturnStatus::Ok) {
    AG_LOGW(TAG, "Failed to select operator %" PRIu32 ", trying next", opInfo.operatorId);
    currentOperatorIndex_++;
    return CONFIGURE_MANUAL_NETWORK;
  }

  AG_LOGI(TAG, "Manual operator configured, continue to: CHECK_NETWORK_REGISTRATION");
  return CHECK_NETWORK_REGISTRATION;
}

CellularModuleA7672XX::NetworkRegistrationState
CellularModuleA7672XX::_implCheckServiceStatus() {
  AG_LOGI(TAG, "Checking service status");

  // Inquiring UE system information
  at_->sendAT("+CPSI?");
  at_->waitResponse();

  // Check if service is available
  CellReturnStatus crs = _isServiceAvailable();
  if (crs == CellReturnStatus::Timeout) {
    return CHECK_MODULE_READY;
  } else if (crs == CellReturnStatus::Failed || crs == CellReturnStatus::Error) {
    REGIS_RETRY_DELAY();
    return CHECK_SERVICE_STATUS;
  }

  // Activate PDP context
  crs = _activatePDPContext();
  if (crs == CellReturnStatus::Timeout) {
    return CHECK_MODULE_READY;
  } else if (crs == CellReturnStatus::Error) {
    AG_LOGW(TAG, "Failed to activate PDP context");
    REGIS_RETRY_DELAY();
    return CHECK_SERVICE_STATUS;
  }

  // Ensure packet domain is attached
  crs = _ensurePacketDomainAttached(true);
  if (crs == CellReturnStatus::Timeout) {
    return CHECK_MODULE_READY;
  } else if (crs == CellReturnStatus::Failed || crs == CellReturnStatus::Error) {
    REGIS_RETRY_DELAY();
    return CHECK_SERVICE_STATUS;
  }

  AG_LOGI(TAG, "Service ready, continue to: NETWORK_READY");
  return NETWORK_READY;
}

CellularModuleA7672XX::NetworkRegistrationState CellularModuleA7672XX::_implNetworkReady() {
  AG_LOGI(TAG, "Verifying network is ready");

  // Check signal quality
  CellResult<int> signalResult = retrieveSignal();
  if (signalResult.status == CellReturnStatus::Timeout) {
    return CHECK_MODULE_READY;
  }

  // Check if returned signal is valid
  if (signalResult.data < 1 || signalResult.data > 31) {
    AG_LOGW(TAG, "Invalid signal strength: %d", signalResult.data);
    REGIS_RETRY_DELAY();
    return CHECK_SERVICE_STATUS;
  }

  AG_LOGI(TAG, "Signal ready at: %d", signalResult.data);

  // Retrieve IP address
  CellResult<std::string> ipResult = retrieveIPAddr();
  if (ipResult.data.empty()) {
    AG_LOGW(TAG, "Failed to retrieve IP address");
    return CHECK_SERVICE_STATUS;
  }

  AG_LOGI(TAG, "IP Addr: %s", ipResult.data.c_str());

  // Save the successful operator for future connections
  if (currentOperatorIndex_ < availableOperators_.size()) {
    OperatorInfo opInfo = availableOperators_[currentOperatorIndex_];
    currentOperatorId_ = opInfo.operatorId;
    AG_LOGI(TAG, "Successfully registered with operator: %" PRIu32 " (AcT: %d), saved for next connection",
            opInfo.operatorId, opInfo.accessTech);
  }

  AG_LOGI(TAG, "Network registration complete!");
  return NETWORK_READY;
}

CellReturnStatus CellularModuleA7672XX::_disableNetworkRegistrationURC(CellTechnology ct) {
  if (ct == CellTechnology::Auto) {
    // Send every network registration command
    at_->sendAT("+CREG=0");
    if (at_->waitResponse() != ATCommandHandler::ExpArg1) {
      return CellReturnStatus::Timeout;
    }
    at_->sendAT("+CGREG=0");
    if (at_->waitResponse() != ATCommandHandler::ExpArg1) {
      return CellReturnStatus::Timeout;
    }
    at_->sendAT("+CEREG=0");
    if (at_->waitResponse() != ATCommandHandler::ExpArg1) {
      return CellReturnStatus::Timeout;
    }
  } else {
    auto cmdNR = _mapCellTechToNetworkRegisCmd(ct);
    if (cmdNR.empty()) {
      return CellReturnStatus::Error;
    }

    char buf[15] = {0};
    sprintf(buf, "+%s=0", cmdNR.c_str());
    at_->sendAT(buf);
    if (at_->waitResponse() != ATCommandHandler::ExpArg1) {
      return CellReturnStatus::Timeout;
    }
  }

  return CellReturnStatus::Ok;
}

CellReturnStatus CellularModuleA7672XX::_checkAllRegistrationStatusCommand() {
  // 2G or 3G
  auto crs = isNetworkRegistered(CellTechnology::Auto);
  if (crs == CellReturnStatus::Timeout || crs == CellReturnStatus::Ok) {
    return crs;
  }

  // 2G or 3G
  crs = isNetworkRegistered(CellTechnology::TWO_G);
  if (crs == CellReturnStatus::Timeout || crs == CellReturnStatus::Ok) {
    return crs;
  }

  // 4G
  crs = isNetworkRegistered(CellTechnology::LTE);
  if (crs == CellReturnStatus::Timeout || crs == CellReturnStatus::Ok) {
    return crs;
  }

  // If after all command check its not return OK, then network still not attached
  return CellReturnStatus::Failed;
}

CellReturnStatus CellularModuleA7672XX::_applyCellularTechnology(CellTechnology ct) {
  // with assumption CT already validate before calling this function
  int mode = _mapCellTechToMode(ct);
  std::string cmd = std::string("+CNMP=") + std::to_string(mode);
  at_->sendAT(cmd.c_str());
  if (at_->waitResponse() != ATCommandHandler::ExpArg1) {
    // TODO: This should be error or timeout
    return CellReturnStatus::Error;
  }

  return CellReturnStatus::Ok;
}

CellReturnStatus CellularModuleA7672XX::_applyOperatorSelection(uint32_t operatorId, int accessTech) {
  char buf[50] = {0};

  if (operatorId == 0) {
    // Automatic operator selection
    AG_LOGI(TAG, "Setting operator selection to automatic mode");
    at_->sendAT("+COPS=0,2");
  } else {
    // Manual operator selection with operator ID and access technology
    if (accessTech >= 0) {
      AG_LOGI(TAG, "Setting operator: %" PRIu32 " with AcT: %d", operatorId, accessTech);
      sprintf(buf, "+COPS=1,2,\"%" PRIu32 "\",%d", operatorId, accessTech);
    } else {
      AG_LOGI(TAG, "Setting operator: %" PRIu32 " (no AcT specified)", operatorId);
      sprintf(buf, "+COPS=1,2,\"%" PRIu32 "\"", operatorId);
    }
    at_->sendAT(buf);
  }

  // Timeout based on datasheet
  auto result = at_->waitResponse(60000);
  if (result == ATCommandHandler::Timeout) {
    AG_LOGW(TAG, "Timeout to apply operator selection");
    return CellReturnStatus::Timeout;
  }
  // Only "OK" is success; plain ERROR and +CME/+CMS ERROR both mean failure.
  else if (result != ATCommandHandler::ExpArg1) {
    AG_LOGW(TAG, "Error to apply operator selection");
    return CellReturnStatus::Error;
  }

  return CellReturnStatus::Ok;
}

CellReturnStatus CellularModuleA7672XX::_checkOperatorSelection() {
  at_->sendAT("+COPS?");
  if (at_->waitResponse("+COPS:") != ATCommandHandler::ExpArg1) {
    // TODO: This should have better error check
    return CellReturnStatus::Timeout;
  }

  auto crs = CellReturnStatus::Ok;

  // ignore <oper> value
  if (at_->waitResponse(" 0,2,\"") != ATCommandHandler::ExpArg1) {
    crs = CellReturnStatus::Failed;
  }

  // receive OK response from the buffer, ignore it
  at_->waitResponse();

  return crs;
}

CellReturnStatus CellularModuleA7672XX::_isServiceAvailable() {
  at_->sendAT("+CNSMOD?");
  if (at_->waitResponse("+CNSMOD:") != ATCommandHandler::ExpArg1) {
    return CellReturnStatus::Timeout;
  }

  std::string status;
  if (at_->waitAndRecvRespLine(status) == -1) {
    return CellReturnStatus::Timeout;
  }

  auto crs = CellReturnStatus::Ok;

  // Second value '0' is NO SERVICE, expect other than NO SERVICE
  if (status == "0,0" || status == "1,0") {
    crs = CellReturnStatus::Failed;
  }

  // receive OK response from the buffer, ignore it
  at_->waitResponse();

  return crs;
}

CellReturnStatus CellularModuleA7672XX::_applyAPN(const std::string &apn) {
  // set APN to pdp cid 1
  char buf[100] = {0};
  sprintf(buf, "+CGDCONT=1,\"IP\",\"%s\"", apn.c_str());
  at_->sendAT(buf);
  if (at_->waitResponse() != ATCommandHandler::ExpArg1) {
    return CellReturnStatus::Error;
  }

  return CellReturnStatus::Ok;
}

CellReturnStatus CellularModuleA7672XX::_ensurePacketDomainAttached(bool forceAttach) {
  at_->sendAT("+CGATT?");
  if (at_->waitResponse("+CGATT:") != ATCommandHandler::ExpArg1) {
    // If return error or not response consider "error"
    return CellReturnStatus::Error;
  }

  std::string state;
  if (at_->waitAndRecvRespLine(state) == -1) {
    // TODO: What to do?
  }

  if (state == "1") {
    // Already attached
    return CellReturnStatus::Ok;
  }

  if (!forceAttach) {
    // Not expect to attach it manually, then return failed because its not attached
    return CellReturnStatus::Failed;
  }

  // Not attached, attempt to
  at_->sendAT("+CGATT=1");
  if (at_->waitResponse() != ATCommandHandler::ExpArg1) {
    return CellReturnStatus::Failed;
  }

  return CellReturnStatus::Ok;
}

CellReturnStatus CellularModuleA7672XX::_activatePDPContext() {
  at_->sendAT("+CGACT=1,1");
  if (at_->waitResponse() != ATCommandHandler::ExpArg1) {
    return CellReturnStatus::Error;
  }

  return CellReturnStatus::Ok;
}

CellResult<std::vector<CellularModule::OperatorRecord>>
CellularModuleA7672XX::scanAvailableOperators(uint32_t timeoutMs) {
  CellResult<std::vector<CellularModule::OperatorRecord>> result;
  result.status = CellReturnStatus::Timeout;

  AG_LOGI(TAG, "Scanning available operators (this may take up to 10 minutes)...");
  // Read the complete scan response before sending another command.
  constexpr int OPERATOR_LIST_BUFFER_LENGTH = 2000;
  std::unique_ptr<char[]> operatorListBuffer(
      new (std::nothrow) char[OPERATOR_LIST_BUFFER_LENGTH]);
  if (!operatorListBuffer) {
    AG_LOGW(TAG, "Failed to allocate operator list buffer");
    result.status = CellReturnStatus::Error;
    return result;
  }

  at_->sendAT("+COPS=?");
  ATCommandHandler::Response response =
      at_->waitResponseAndCollect(operatorListBuffer.get(), OPERATOR_LIST_BUFFER_LENGTH, timeoutMs);
  if (response != ATCommandHandler::ExpArg1) {
    if (response == ATCommandHandler::Timeout) {
      AG_LOGW(TAG, "Operator scan timed out");
    } else {
      AG_LOGW(TAG, "Error scanning operators");
      result.status = CellReturnStatus::Error;
    }
    return result;
  }
  std::string operatorListRaw(operatorListBuffer.get());

  AG_LOGD(TAG, "Operator scan response: %s", operatorListRaw.c_str());

  // Parse operator list: (status,"long","short","numeric",tech),(status,...),...
  // We want to extract "numeric" IDs and tech where status is 1 (available) or 2 (current)
  std::vector<CellularModule::OperatorRecord> operators;
  size_t pos = 0;

  while (pos < operatorListRaw.length()) {
    // Find opening parenthesis
    size_t openParen = operatorListRaw.find('(', pos);
    if (openParen == std::string::npos) {
      break;
    }

    // Find closing parenthesis
    size_t closeParen = operatorListRaw.find(')', openParen);
    if (closeParen == std::string::npos) {
      break;
    }

    // Extract the operator entry
    std::string entry = operatorListRaw.substr(openParen + 1, closeParen - openParen - 1);

    // Parse: status,"long","short","numeric",tech
    // Split by comma, but be careful with quoted strings
    std::vector<std::string> parts;
    bool inQuotes = false;
    std::string currentPart;

    for (size_t i = 0; i < entry.length(); i++) {
      char c = entry[i];
      if (c == '"') {
        inQuotes = !inQuotes;
      } else if (c == ',' && !inQuotes) {
        parts.push_back(currentPart);
        currentPart.clear();
      } else {
        currentPart += c;
      }
    }
    if (!currentPart.empty()) {
      parts.push_back(currentPart);
    }

    // Validate we have enough parts and extract numeric ID and access tech
    if (parts.size() >= 5) {
      // Parse status manually using atoi (no exceptions)
      int status = atoi(parts[0].c_str());

      // Only include available (1) or current (2) operators
      if (status == 1 || status == 2) {
        // Parse operator ID and access tech as integers
        uint32_t operatorId = (uint32_t)atoi(parts[3].c_str());
        int accessTech = atoi(parts[4].c_str());

        if (operatorId > 0) {
          std::string operatorName = parts[1];
          if (operatorName.empty()) {
            operatorName = parts[2];
          }

          CellularModule::OperatorRecord operatorRecord;
          operatorRecord.operatorId = operatorId;
          operatorRecord.accessTech = accessTech;
          operatorRecord.operatorName = operatorName;
          operators.push_back(operatorRecord);
          AG_LOGI(TAG, "Found operator: name=\"%s\", ID=%" PRIu32 ", AcT=%d (status=%d)",
                  operatorName.c_str(), operatorId, accessTech, status);
        }
      }
    }

    pos = closeParen + 1;
  }

  if (operators.empty()) {
    AG_LOGW(TAG, "No available operators found in scan");
    result.status = CellReturnStatus::Failed;
    return result;
  }

  AG_LOGI(TAG, "Found %zu available operator(s)", operators.size());
  result.status = CellReturnStatus::Ok;
  result.data = operators;
  return result;
}

CellResult<std::vector<CellularModuleA7672XX::OperatorInfo>>
CellularModuleA7672XX::_scanAvailableOperators(uint32_t timeoutMs) {
  CellResult<std::vector<OperatorInfo>> result;

  auto scanResult = scanAvailableOperators(timeoutMs);
  result.status = scanResult.status;
  if (scanResult.status != CellReturnStatus::Ok) {
    return result;
  }

  for (const CellularModule::OperatorRecord &operatorRecord : scanResult.data) {
    OperatorInfo operatorInfo;
    operatorInfo.operatorId = operatorRecord.operatorId;
    operatorInfo.accessTech = operatorRecord.accessTech;
    result.data.push_back(operatorInfo);
  }

  return result;
}

CellResult<CellularModuleA7672XX::RegistrationStatus>
CellularModuleA7672XX::_parseRegistrationStatus(const std::string &response) {
  CellResult<RegistrationStatus> result;
  result.status = CellReturnStatus::Failed;

  // Expected format: "0,1" or "1,5" or "0,3" etc.
  // Format: <n>,<stat>[,<lac>,<ci>,<AcT>]

  // Split by comma to get mode and stat
  size_t firstComma = response.find(',');
  if (firstComma == std::string::npos) {
    AG_LOGW(TAG, "Invalid registration status format: %s", response.c_str());
    return result;
  }

  // Parse mode manually using atoi (no exceptions)
  std::string modeStr = response.substr(0, firstComma);
  result.data.mode = atoi(modeStr.c_str());

  // Find second comma (if exists) to isolate stat value
  size_t secondComma = response.find(',', firstComma + 1);
  std::string statStr;
  if (secondComma != std::string::npos) {
    statStr = response.substr(firstComma + 1, secondComma - firstComma - 1);
  } else {
    statStr = response.substr(firstComma + 1);
  }

  // Parse stat manually using atoi (no exceptions)
  result.data.stat = atoi(statStr.c_str());

  // Basic validation (mode should be 0, 1, or 2)
  if (result.data.mode < 0 || result.data.mode > 2) {
    AG_LOGE(TAG, "Invalid registration mode: %d", result.data.mode);
    result.status = CellReturnStatus::Error;
    return result;
  }

  result.status = CellReturnStatus::Ok;
  AG_LOGD(TAG, "Parsed registration status: mode=%d, stat=%d", result.data.mode, result.data.stat);

  return result;
}

CellResult<CellularModuleA7672XX::RegistrationStatus>
CellularModuleA7672XX::_checkDetailedRegistrationStatus(CellTechnology ct) {
  CellResult<RegistrationStatus> result;
  result.status = CellReturnStatus::Timeout;

  auto cmdNR = _mapCellTechToNetworkRegisCmd(ct);
  if (cmdNR.empty()) {
    result.status = CellReturnStatus::Error;
    return result;
  }

  char buf[15] = {0};
  sprintf(buf, "+%s?", cmdNR.c_str());
  at_->sendAT(buf);

  int resp = at_->waitResponse("+CREG:", "+CEREG:", "+CGREG:");
  if (resp != ATCommandHandler::ExpArg1 && resp != ATCommandHandler::ExpArg2 &&
      resp != ATCommandHandler::ExpArg3) {
    AG_LOGW(TAG, "Timeout waiting for registration status response");
    return result;
  }

  std::string recv;
  if (at_->waitAndRecvRespLine(recv) == -1) {
    AG_LOGW(TAG, "Failed to receive registration status line");
    return result;
  }

  // Wait for OK
  at_->waitResponse();

  // Parse the response
  return _parseRegistrationStatus(recv);
}

CellResult<std::string> CellularModuleA7672XX::_detectCurrentOperatorMode() {
  CellResult<std::string> result;
  result.status = CellReturnStatus::Timeout;

  at_->sendAT("+COPS?");
  if (at_->waitResponse("+COPS:") != ATCommandHandler::ExpArg1) {
    AG_LOGW(TAG, "Timeout querying current operator mode");
    return result;
  }

  std::string response;
  if (at_->waitAndRecvRespLine(response) == -1) {
    AG_LOGW(TAG, "Failed to receive COPS? response");
    return result;
  }

  // Wait for OK
  at_->waitResponse();

  // Parse response: <mode>,<format>,"<oper>"[,<AcT>]
  // mode: 0=automatic, 1=manual, 4=manual/automatic
  size_t firstComma = response.find(',');
  if (firstComma == std::string::npos) {
    AG_LOGW(TAG, "Invalid COPS? response format: %s", response.c_str());
    result.status = CellReturnStatus::Failed;
    return result;
  }

  // Parse mode manually using atoi (no exceptions)
  std::string modeStr = response.substr(0, firstComma);
  int mode = atoi(modeStr.c_str());

  // Validate mode value (0=auto, 1=manual, 2=deregister, 3=set format only, 4=manual/auto)
  if (mode < 0 || mode > 4) {
    AG_LOGW(TAG, "Invalid COPS mode: %d", mode);
    result.status = CellReturnStatus::Failed;
    return result;
  }

  if (mode == 0) {
    result.data = "auto";
    AG_LOGI(TAG, "Current operator mode: automatic");
  } else if (mode == 1 || mode == 4) {
    // Extract operator ID from response
    // Format after mode: <format>,"<oper>"
    size_t firstQuote = response.find('"', firstComma);
    size_t secondQuote = response.find('"', firstQuote + 1);

    if (firstQuote != std::string::npos && secondQuote != std::string::npos) {
      result.data = response.substr(firstQuote + 1, secondQuote - firstQuote - 1);
      AG_LOGI(TAG, "Current operator mode: manual, operator=%s", result.data.c_str());
    } else {
      result.data = "manual";
      AG_LOGI(TAG, "Current operator mode: manual (operator ID not parsed)");
    }
  } else {
    result.data = "unknown";
    AG_LOGW(TAG, "Unknown operator mode: %d", mode);
  }

  result.status = CellReturnStatus::Ok;

  return result;
}

CellReturnStatus CellularModuleA7672XX::_httpInit() {
  at_->sendAT("+HTTPINIT");
  auto response = at_->waitResponse(20000);
  if (response == ATCommandHandler::Timeout) {
    AG_LOGW(TAG, "Timeout wait response +HTTPINIT");
    return CellReturnStatus::Timeout;
  } else if (response == ATCommandHandler::ExpArg2) {
    AG_LOGW(TAG, "Error initialize module HTTP service, retry once more in 2s");
    DELAY_MS(2000);

    // Re-send HTTPINIT again
    at_->sendAT("+HTTPINIT");
    response = at_->waitResponse();
    if (response == ATCommandHandler::Timeout) {
      AG_LOGW(TAG, "Timeout wait response +HTTPINIT");
      return CellReturnStatus::Timeout;
    } else if (response == ATCommandHandler::ExpArg2) {
      AG_LOGW(TAG, "Still return error to initialize module HTTP service");
      return CellReturnStatus::Error;
    }
  }

  return CellReturnStatus::Ok;
}

CellReturnStatus CellularModuleA7672XX::_httpSetParamTimeout(int connectionTimeout,
                                                             int responseTimeout) {
  // Add threshold guard based on module specification (20 - 120). Default 120
  if (connectionTimeout != -1) {
    if (connectionTimeout < 20) {
      connectionTimeout = 20;
    } else if (connectionTimeout > 120) {
      connectionTimeout = 120;
    }
  }

  // Add threshold guard based on module specification (2 - 120). Default 20
  if (responseTimeout != -1) {
    if (responseTimeout < 2) {
      responseTimeout = 2;
    } else if (responseTimeout > 120) {
      responseTimeout = 120;
    }
  }

  // +HTTPPARA set connection timeout if provided
  if (connectionTimeout != -1) {
    // AT+HTTPPARA="CONNECTTO",<conntimeout>
    std::string cmd = std::string("+HTTPPARA=\"CONNECTTO\",") + std::to_string(connectionTimeout);
    at_->sendAT(cmd.c_str());
    auto response = at_->waitResponse();
    if (response == ATCommandHandler::Timeout) {
      AG_LOGW(TAG, "Timeout wait response +HTTPPARA CONNECTTO");
      return CellReturnStatus::Timeout;
    } else if (response == ATCommandHandler::ExpArg2) {
      AG_LOGW(TAG, "Error set HTTP param CONNECTTO");
      return CellReturnStatus::Error;
    }
  }

  // +HTTPPARA set response timeout if provided
  if (responseTimeout != -1) {
    // AT+HTTPPARA="RECVTO",<recv_timeout>
    std::string cmd = std::string("+HTTPPARA=\"RECVTO\",") + std::to_string(responseTimeout);
    at_->sendAT(cmd.c_str());
    auto response = at_->waitResponse();
    if (response == ATCommandHandler::Timeout) {
      AG_LOGW(TAG, "Timeout wait response +HTTPPARA RECVTO");
      return CellReturnStatus::Timeout;
    } else if (response == ATCommandHandler::ExpArg2) {
      AG_LOGW(TAG, "Error set HTTP param RECVTO");
      return CellReturnStatus::Error;
    }
  }

  return CellReturnStatus::Ok;
}

CellReturnStatus CellularModuleA7672XX::_httpSetUrl(const std::string &url) {
  char buf[200] = {0};
  sprintf(buf, "+HTTPPARA=\"URL\", \"%s\"", url.c_str());
  at_->sendAT(buf);
  auto response = at_->waitResponse();
  if (response == ATCommandHandler::Timeout) {
    AG_LOGW(TAG, "Timeout wait response +HTTPPARA URL");
    return CellReturnStatus::Timeout;
  } else if (response == ATCommandHandler::ExpArg2) {
    AG_LOGW(TAG, "Error set HTTP param URL");
    return CellReturnStatus::Error;
  }

  return CellReturnStatus::Ok;
}

CellReturnStatus CellularModuleA7672XX::_httpAction(int httpMethodCode, int connectionTimeout,
                                                    int responseTimeout, int *oResponseCode,
                                                    int *oBodyLen) {
  int code = -1, bodyLen = 0;
  int waitActionTimeout;
  std::string data;

  // +HTTPACTION
  data = std::string("+HTTPACTION=") + std::to_string(httpMethodCode);
  at_->sendAT(data.c_str());
  auto response = at_->waitResponse(); // Wait for OK
  if (response == ATCommandHandler::Timeout) {
    AG_LOGW(TAG, "Timeout wait response +HTTPACTION GET");
    return CellReturnStatus::Timeout;
  } else if (response == ATCommandHandler::ExpArg2) {
    AG_LOGW(TAG, "Error execute HTTPACTION GET");
    return CellReturnStatus::Error;
  }

  // calculate how long to wait for +HTTPACTION
  waitActionTimeout = _calculateResponseTimeout(connectionTimeout, responseTimeout);

  // +HTTPACTION: <method>,<statuscode>,<datalen>
  // +HTTPACTION: <method>,<errcode>,<datalen>
  // Wait for +HTTPACTION finish execute
  response = at_->waitResponse(waitActionTimeout, "+HTTPACTION:");
  if (response == ATCommandHandler::Timeout) {
    AG_LOGW(TAG, "Timeout wait +HTTPACTION success execution");
    return CellReturnStatus::Timeout;
  }

  // Retrieve +HTTPACTION response value
  at_->waitAndRecvRespLine(data);
  // Sanity check if value is empty
  if (data.empty()) {
    AG_LOGW(TAG, "+HTTPACTION result value empty");
    return CellReturnStatus::Failed;
  }

  AG_LOGI(TAG, "+HTTPACTION finish! retrieve its values");

  // 0,code,size
  // start from code, ignore 0 (GET)
  Common::splitByDelimiter(data.substr(2, data.length()), &code, &bodyLen);
  if (code == -1 || (code > 700 && code < 720)) {
    // -1 means string cannot splitted by comma
    // 7xx This is error code <errcode> not http <status_code>
    // 16.3.2 Description of<errcode> datasheet
    AG_LOGW(TAG, "+HTTPACTION error with module errcode: %d", code);
    return CellReturnStatus::Failed;
  }

  // Assign result to the output variable
  *oResponseCode = code;
  *oBodyLen = bodyLen;

  return CellReturnStatus::Ok;
}

CellReturnStatus CellularModuleA7672XX::_httpTerminate() {
  // +HTTPTERM to stop http service
  // If previous AT return timeout, here just attempt
  at_->sendAT("+HTTPTERM");
  auto response = at_->waitResponse();
  if (response == ATCommandHandler::Timeout) {
    AG_LOGW(TAG, "Timeout wait response +HTTPTERM");
    return CellReturnStatus::Timeout;
  } else if (response == ATCommandHandler::ExpArg2) {
    AG_LOGW(TAG, "Error stop module HTTP service");
    return CellReturnStatus::Error;
  }

  return CellReturnStatus::Ok;
}

CellReturnStatus CellularModuleA7672XX::_startUDP() {
  at_->sendAT("+NETOPEN");
  auto response = at_->waitResponse(60000, "+NETOPEN:", "+IP ERROR:", "ERROR");
  if (response == ATCommandHandler::ExpArg2) {
    at_->clearBuffer();
    AG_LOGI(TAG, "+NETOPEN PDP context has been activated successfully");
    return CellReturnStatus::Ok;
  } else if (response == ATCommandHandler::ExpArg3) {
    AG_LOGE(TAG, "+NETOPEN Error start UDP service");
    return CellReturnStatus::Error;
  } else if (response == ATCommandHandler::Timeout) {
    AG_LOGE(TAG, "+NETOPEN Timeout start UDP service");
    return CellReturnStatus::Timeout;
  }

  char result[1];
  at_->waitAndRecvRespLine(result, 1);
  if (result[0] != '0') {
    AG_LOGE(TAG, "+NETOPEN Failed to open UDP network with code %c", result[0]);
  }

  return CellReturnStatus::Ok;
}

CellReturnStatus CellularModuleA7672XX::_stopUDP() {
  at_->sendAT("+NETCLOSE");
  auto response = at_->waitResponse(60000, "+NETCLOSE:");
  if (response == ATCommandHandler::Timeout) {
    AG_LOGE(TAG, "+NETCLOSE Timeout stop UDP service");
    return CellReturnStatus::Timeout;
  } else if (response == ATCommandHandler::ExpArg2) {
    AG_LOGE(TAG, "+NETCLOSE Error stop UDP service");
    return CellReturnStatus::Error;
  }

  char result[1];
  at_->waitAndRecvRespLine(result, 1);
  if (result[0] != '0') {
    AG_LOGE(TAG, "+NETCLOSE Failed to stop UDP service with code %c", result[0]);
  }

  return CellReturnStatus::Ok;
}

CellReturnStatus CellularModuleA7672XX::_connectUDP(const std::string &host, int port) {
  char cmd[128];
  snprintf(cmd, sizeof(cmd), "+CIPOPEN=%d,\"UDP\",\"%s\",%d,0", UDP_LINK_ID, host.c_str(), port);

  at_->sendAT(cmd);
  ATCommandHandler::Response resp = at_->waitResponse(10000);
  if (resp != ATCommandHandler::ExpArg1) {
    AG_LOGE(TAG, "+CIPOPEN failed to open UDP socket");
    return CellReturnStatus::Failed;
  }

  return CellReturnStatus::Ok;
}

CellReturnStatus CellularModuleA7672XX::_disconnectUDP() {
  char cmd[32];
  snprintf(cmd, sizeof(cmd), "+CIPCLOSE=%d", UDP_LINK_ID);

  at_->sendAT(cmd);
  ATCommandHandler::Response resp = at_->waitResponse(5000);
  if (resp != ATCommandHandler::ExpArg1) {
    AG_LOGE(TAG, "+CIPCLOSE Failed to close UDP socket");
    return CellReturnStatus::Failed;
  }

  return CellReturnStatus::Ok;
}

CellResult<std::string> CellularModuleA7672XX::resolveDNS(const std::string &hostname) {
  CellResult<std::string> result;
  result.status = CellReturnStatus::Error;

  // Build AT+CDNSGIP command
  char cmd[128];
  snprintf(cmd, sizeof(cmd), "+CDNSGIP=\"%s\"", hostname.c_str());

  at_->sendAT(cmd);

  // Wait for response: +CDNSGIP: 1,"hostname","ip_address"
  ATCommandHandler::Response response = at_->waitResponse(10000, "+CDNSGIP:");
  if (response != ATCommandHandler::ExpArg1) {
    AG_LOGE(TAG, "+CDNSGIP timeout or error");
    result.status = CellReturnStatus::Failed;
    return result;
  }

  // Read the response line
  std::string responseLine;
  if (at_->waitAndRecvRespLine(responseLine) == -1) {
    AG_LOGW(TAG, "+CDNSGIP retrieve response timeout");
    result.status = CellReturnStatus::Timeout;
    return result;
  }

  // Parse response: format is "1,\"hostname\",\"ip_address\""
  // Find the last quoted string (IP address)
  size_t lastQuoteStart = responseLine.rfind('"');
  if (lastQuoteStart == std::string::npos || lastQuoteStart == 0) {
    AG_LOGE(TAG, "+CDNSGIP failed to parse IP address");
    result.status = CellReturnStatus::Error;
    return result;
  }

  size_t secondLastQuoteStart = responseLine.rfind('"', lastQuoteStart - 1);
  if (secondLastQuoteStart == std::string::npos) {
    AG_LOGE(TAG, "+CDNSGIP failed to parse IP address");
    result.status = CellReturnStatus::Error;
    return result;
  }

  std::string ipAddress =
      responseLine.substr(secondLastQuoteStart + 1, lastQuoteStart - secondLastQuoteStart - 1);

  if (ipAddress.empty()) {
    AG_LOGE(TAG, "+CDNSGIP returned empty IP address");
    result.status = CellReturnStatus::Error;
    return result;
  }

  AG_LOGI(TAG, "DNS resolved %s to %s", hostname.c_str(), ipAddress.c_str());

  result.data = ipAddress;
  result.status = CellReturnStatus::Ok;
  return result;
}

int CellularModuleA7672XX::_mapCellTechToMode(CellTechnology ct) {
  int mode = -1;
  switch (ct) {
  case CellTechnology::Auto:
    mode = 2;
    break;
  case CellTechnology::TWO_G:
    mode = 13;
    break;
  case CellTechnology::LTE:
    mode = 38;
    break;
  default:
    AG_LOGE(TAG, "CellTechnology not supported for this module");
    break;
  }

  return mode;
}

std::string CellularModuleA7672XX::_mapCellTechToNetworkRegisCmd(CellTechnology ct) {
  std::string cmd;
  switch (ct) {
  case CellTechnology::Auto:
    cmd = "CREG"; // TODO: Is it thou?
    break;
  case CellTechnology::TWO_G:
    cmd = "CGREG";
    break;
  case CellTechnology::LTE:
    cmd = "CEREG";
    break;
  default:
    AG_LOGE(TAG, "CellTechnology not supported for this module");
    break;
  }

  return cmd;
}

int CellularModuleA7672XX::_calculateResponseTimeout(int connectionTimeout, int responseTimeout) {
  int waitActionTimeout = 0;
  if (connectionTimeout == -1 && responseTimeout == -1) {
    waitActionTimeout = (DEFAULT_HTTP_CONNECT_TIMEOUT + DEFAULT_HTTP_RESPONSE_TIMEOUT) * 1000;
  } else if (connectionTimeout == -1) {
    waitActionTimeout = (DEFAULT_HTTP_CONNECT_TIMEOUT + responseTimeout) * 1000;
  } else if (responseTimeout == -1) {
    waitActionTimeout = (connectionTimeout + DEFAULT_HTTP_RESPONSE_TIMEOUT) * 1000;
  } else {
    waitActionTimeout = (connectionTimeout + responseTimeout) * 1000;
  }

  return waitActionTimeout;
}

bool CellularModuleA7672XX::setOperators(const std::string &serialized, uint32_t operatorId,
                                         uint32_t registrationFailCount) {
  AG_LOGI(TAG, "Setting operators from serialized string: %s, current operatorId: %" PRIu32
          ", failCount: %" PRIu32,
          serialized.c_str(), operatorId, registrationFailCount);
  registrationFailCount_ = registrationFailCount;

  // Clear existing operators
  availableOperators_.clear();
  currentOperatorId_ = operatorId;
  currentOperatorIndex_ = 0;

  // Handle empty string
  if (serialized.empty()) {
    AG_LOGI(TAG, "Empty operator string, cleared operator list");
    return true;
  }

  // Parse the serialized string format: "46001:7,46002:2,50501:7"
  size_t start = 0;
  size_t foundIndex = 0;
  bool currentOperatorFound = false;

  while (start < serialized.length()) {
    // Find next comma or end of string
    size_t commaPos = serialized.find(',', start);
    if (commaPos == std::string::npos) {
      commaPos = serialized.length();
    }

    // Extract entry
    std::string entry = serialized.substr(start, commaPos - start);

    // Find colon separator
    size_t colonPos = entry.find(':');
    if (colonPos == std::string::npos) {
      AG_LOGW(TAG, "Malformed entry (no colon): %s", entry.c_str());
      start = commaPos + 1;
      continue;
    }

    // Parse operator ID and access tech
    std::string idStr = entry.substr(0, colonPos);
    std::string techStr = entry.substr(colonPos + 1);

    uint32_t id = atoi(idStr.c_str());
    int tech = atoi(techStr.c_str());

    // Validate
    if (id == 0 && idStr != "0") {
      AG_LOGW(TAG, "Invalid operator ID in entry: %s", entry.c_str());
      start = commaPos + 1;
      continue;
    }

    // Add to vector
    OperatorInfo info;
    info.operatorId = id;
    info.accessTech = tech;
    availableOperators_.push_back(info);

    // Check if this is the current operator
    if (id == operatorId && !currentOperatorFound) {
      currentOperatorIndex_ = foundIndex;
      currentOperatorFound = true;
      AG_LOGI(TAG, "Found current operator at index %zu", currentOperatorIndex_);
    }

    foundIndex++;
    start = commaPos + 1;
  }

  AG_LOGI(TAG, "Loaded %zu operators from serialized string", availableOperators_.size());

  if (!currentOperatorFound && operatorId != 0) {
    AG_LOGW(TAG, "Current operator ID %" PRIu32 " not found in operator list", operatorId);
  }

  return true;
}

std::string CellularModuleA7672XX::getSerializedOperators() const {
  if (availableOperators_.empty()) {
    return "";
  }

  std::string result;
  for (size_t i = 0; i < availableOperators_.size(); i++) {
    if (i > 0) {
      result += ",";
    }

    char buf[32];
    sprintf(buf, "%" PRIu32 ":%d", availableOperators_[i].operatorId, availableOperators_[i].accessTech);
    result += buf;
  }

  return result;
}

uint32_t CellularModuleA7672XX::getCurrentOperatorId() const {
  return currentOperatorId_;
}

uint32_t CellularModuleA7672XX::getRegistrationFailCount() const {
  return registrationFailCount_;
}

#endif // ESP8266
