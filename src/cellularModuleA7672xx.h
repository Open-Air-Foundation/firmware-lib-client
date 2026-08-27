/**
 * AirGradient
 * https://airgradient.com
 *
 * CC BY-SA 4.0 Attribution-ShareAlike 4.0 International License
 */

#ifndef CELLULAR_MODULE_A7672XX_H
#define CELLULAR_MODULE_A7672XX_H

#ifndef ESP8266

#include <string>
#include <vector>

#include "driver/gpio.h"

#ifdef ARDUINO
#include "agSerial.h"
#else
#include "AirgradientSerial.h"
#endif
#include "atCommandHandler.h"
#include "cellularModule.h"

#ifndef CONFIG_HTTPREAD_CHUNK_SIZE
// This configuration define by kconfig
#define CONFIG_HTTPREAD_CHUNK_SIZE 200
#endif

class CellularModuleA7672XX : public CellularModule {
public:
#ifdef ARDUINO
  // NOTE: Temporarily accomodate ununified AirgradientSerial
  typedef AgSerial AirgradientSerial;
#endif

private:
  const char *const TAG = "A7672XX";

  bool _initialized = false;
  uint32_t _warmUpTimeMs;

  AirgradientSerial *agSerial_ = nullptr;
  gpio_num_t _powerIO = GPIO_NUM_NC;
  ATCommandHandler *at_ = nullptr;

  // Structure to hold operator information for manual selection
  struct OperatorInfo {
    uint32_t operatorId;  // Numeric MCC+MNC (e.g., 46001)
    int accessTech;       // Access technology: 0=GSM, 2=UTRAN, 7=E-UTRAN(LTE)
  };

  static constexpr uint32_t MAX_REGISTRATION_FAILURES = 3;

  // Operator selection for manual network registration
  std::vector<OperatorInfo> availableOperators_;  // Persisted operator list with IDs and access tech
  size_t currentOperatorIndex_ = 0;               // Track position in manual mode
  uint32_t currentOperatorId_ = 0;                // Current operator PLMN ID (saved successful operator)
  uint32_t registrationFailCount_ = 0;            // Consecutive registration failures (persisted via setOperators)

public:
  // Structure to hold detailed registration status
  struct RegistrationStatus {
    int mode;  // URC reporting mode (0 or 1)
    int stat;  // registration status (0=not searching, 1=registered home, 2=searching, 3=denied, 5=registered roaming, 11=searching/trying)
  };

  enum NetworkRegistrationState {
    // Check if AT ready, Check if SIM ready
    CHECK_MODULE_READY,
    // Disable network registration URC, Set cellular technology, Set APN
    PREPARE_MODULE,
    // Scan available operators (AT+COPS=?) and populate operator list
    SCAN_OPERATOR,
    // Configure manual operator selection by iterating through scanned operator list
    CONFIGURE_MANUAL_NETWORK,
    // All operators in list have been tried without success
    OPERATOR_LIST_EXHAUSTED,
    // Check network registration status (CREG/CEREG/CGREG) and signal quality
    CHECK_NETWORK_REGISTRATION,
    // Ensure service available (CNSMOD), Activate PDP context, Check packet domain attached
    CHECK_SERVICE_STATUS,
    // Final checks: signal quality, IP address retrieval
    NETWORK_READY
  };

  CellularModuleA7672XX(AirgradientSerial *agSerial, uint32_t warmUpTimeMs = 0);
  CellularModuleA7672XX(AirgradientSerial *agSerial, int powerPin, uint32_t warmUpTimeMs = 0);
  ~CellularModuleA7672XX();

  bool init();
  void powerOn();
  void powerOff(bool force);
  bool reset();
  void sleep();
  CellResult<std::string> getModuleInfo();
  CellResult<std::string> retrieveSimCCID();
  CellReturnStatus isSimReady();
  CellResult<int> retrieveSignal();
  CellResult<std::string> retrieveIPAddr();
  CellReturnStatus isNetworkRegistered(CellTechnology ct);
  CellResult<std::vector<CellularModule::OperatorRecord>>
  scanAvailableOperators(uint32_t timeoutMs = 600000);
  CellResult<std::string> startNetworkRegistration(CellTechnology ct, const std::string &apn,
                                                   uint32_t operationTimeoutMs = 90000,
                                                   uint32_t scanTimeoutMs = 600000);
  CellReturnStatus reinitialize();
  CellResult<CellularModule::HttpResponse>
  httpGet(const std::string &url, int connectionTimeout = -1, int responseTimeout = -1);
  CellResult<CellularModule::HttpResponse> httpPost(const std::string &url, const std::string &body,
                                                    const std::string &headContentType = "",
                                                    int connectionTimeout = -1,
                                                    int responseTimeout = -1);
  CellReturnStatus mqttConnect(const std::string &clientId, const std::string &host,
                               int port = 1883, std::string username = "",
                               std::string password = "");
  CellReturnStatus mqttDisconnect();
  CellReturnStatus mqttPublish(const std::string &topic, const std::string &payload, int qos = 1,
                               int retain = 0, int timeoutS = 15);

  CellReturnStatus udpConnect(const std::string &host, int port = 5683);
  CellReturnStatus udpDisconnect();
  CellReturnStatus udpSend(const CellularModule::UdpPacket &packet, const std::string &host,
                           uint16_t port);
  CellResult<CellularModule::UdpPacket> udpReceive(uint32_t timeout);
  CellResult<std::string> resolveDNS(const std::string &hostname);
  // Operator serialization/deserialization
  bool setOperators(const std::string &serialized, uint32_t operatorId,
                    uint32_t registrationFailCount = 0);
  std::string getSerializedOperators() const;
  uint32_t getCurrentOperatorId() const;
  uint32_t getRegistrationFailCount() const;

private:
  const int DEFAULT_HTTP_CONNECT_TIMEOUT = 120; // seconds
  const int DEFAULT_HTTP_RESPONSE_TIMEOUT = 20; // seconds
  const int HTTPREAD_CHUNK_SIZE = CONFIG_HTTPREAD_CHUNK_SIZE;
  const int UDP_LINK_ID = 0;

  // Network Registration implementation for each state
  NetworkRegistrationState _implCheckModuleReady();
  NetworkRegistrationState _implPrepareModule(CellTechnology ct, const std::string &apn);
  NetworkRegistrationState _implScanOperator(uint32_t scanTimeoutMs);
  NetworkRegistrationState _implConfigureManualNetwork();
  NetworkRegistrationState _implCheckNetworkRegistration(CellTechnology ct,
                                                          uint32_t manualOperatorStartTime);
  NetworkRegistrationState _implCheckServiceStatus();
  NetworkRegistrationState _implNetworkReady();

  // AT Command functions
  CellReturnStatus _disableNetworkRegistrationURC(CellTechnology ct); // depend on CellTech
  CellReturnStatus _checkAllRegistrationStatusCommand();
  CellReturnStatus _applyCellularTechnology(CellTechnology ct);
  CellReturnStatus _applyOperatorSelection(uint32_t operatorId, int accessTech = -1);
  CellReturnStatus _checkOperatorSelection();
  CellReturnStatus _isServiceAvailable();
  CellReturnStatus _applyAPN(const std::string &apn);
  CellReturnStatus _ensurePacketDomainAttached(bool forceAttach);
  CellReturnStatus _activatePDPContext();

  // Operator scanning and registration status parsing
  CellResult<std::vector<OperatorInfo>> _scanAvailableOperators(uint32_t timeoutMs);
  CellResult<RegistrationStatus> _parseRegistrationStatus(const std::string &response);
  CellResult<RegistrationStatus> _checkDetailedRegistrationStatus(CellTechnology ct);
  CellResult<std::string> _detectCurrentOperatorMode();
  CellReturnStatus _httpInit();
  CellReturnStatus _httpSetParamTimeout(int connectionTimeout, int responseTimeout);
  CellReturnStatus _httpSetUrl(const std::string &url);
  CellReturnStatus _httpAction(int httpMethodCode, int connectionTimeout, int responseTimeout,
                               int *oResponseCode, int *oBodyLen);
  CellReturnStatus _httpTerminate();
  CellReturnStatus _startUDP();
  CellReturnStatus _stopUDP();
  CellReturnStatus _connectUDP(const std::string &host, int port);
  CellReturnStatus _disconnectUDP();

  int _mapCellTechToMode(CellTechnology ct);
  std::string _mapCellTechToNetworkRegisCmd(CellTechnology ct);

  /**
   * @brief Calculate timeout in ms to wait +HTTPACTION request finish
   *
   * @param connectionTimeout connectionTimeout provided when call http request
   * in seconds
   * @param responseTimeout responseTimeout provided when call http request in
   * seconds
   * @return int total time in ms to wait for +HTTPACTION
   */
  int _calculateResponseTimeout(int connectionTimeout, int responseTimeout);
};

#endif // ESP8266
#endif // CELLULAR_MODULE_A7672XX_H
