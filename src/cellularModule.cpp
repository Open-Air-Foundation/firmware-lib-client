/**
 * AirGradient
 * https://airgradient.com
 *
 * CC BY-SA 4.0 Attribution-ShareAlike 4.0 International License
 */

#include "cellularModule.h"

CellularModule::CellularModule() {}

CellularModule::~CellularModule() {}

bool CellularModule::init() { return false; }

void CellularModule::powerOn() {}

void CellularModule::powerOff(bool force) {}

bool CellularModule::reset() { return true; }

void CellularModule::sleep() {}

CellResult<std::string> CellularModule::getModuleInfo() { return CellResult<std::string>(); }

CellResult<std::string> CellularModule::retrieveSimCCID() { return CellResult<std::string>(); }

CellReturnStatus CellularModule::isSimReady() { return CellReturnStatus(); }

CellResult<int> CellularModule::retrieveSignal() { return CellResult<int>(); }

CellResult<std::string> CellularModule::retrieveIPAddr() { return CellResult<std::string>(); }

CellResult<std::string> CellularModule::resolveDNS(const std::string &hostname) {
  return CellResult<std::string>();
}

CellResult<int64_t> CellularModule::retrieveNetworkTime(const std::string &hostname,
                                                        uint32_t timeoutMs) {
  return {CellReturnStatus::Error, 0};
}

bool CellularModule::setOperators(const std::string &serialized, uint32_t operatorId,
                                  uint32_t registrationFailCount) {
  return false;
}

std::string CellularModule::getSerializedOperators() const { return std::string(); }

uint32_t CellularModule::getCurrentOperatorId() const { return 0; }

uint32_t CellularModule::getRegistrationFailCount() const { return 0; }

CellReturnStatus CellularModule::isNetworkRegistered(CellTechnology ct) {
  return CellReturnStatus();
}

CellResult<std::vector<CellularModule::OperatorRecord>>
CellularModule::scanAvailableOperators(uint32_t timeoutMs) {
  CellResult<std::vector<OperatorRecord>> result;
  result.status = CellReturnStatus::Error;
  return result;
}

CellResult<std::string> CellularModule::startNetworkRegistration(CellTechnology ct,
                                                                 const std::string &apn,
                                                                 uint32_t operationTimeoutMs,
                                                                 uint32_t scanTimeoutMs) {
  return CellResult<std::string>();
}

CellReturnStatus CellularModule::reinitialize() { return CellReturnStatus(); }

CellResult<CellularModule::HttpResponse>
CellularModule::httpGet(const std::string &url, int connectionTimeout, int responseTimeout) {
  return CellResult<HttpResponse>();
}

CellResult<CellularModule::HttpResponse>
CellularModule::httpPost(const std::string &url, const std::string &body,
                         const std::string &headContentType, int connectionTimeout,
                         int responseTimeout) {
  return CellResult<HttpResponse>();
}

CellReturnStatus CellularModule::mqttConnect(const std::string &clientId, const std::string &host,
                                             int port, std::string username, std::string password) {
  return CellReturnStatus::Error;
}

CellReturnStatus CellularModule::mqttDisconnect() { return CellReturnStatus::Error; }

CellReturnStatus CellularModule::mqttPublish(const std::string &topic, const std::string &payload,
                                             int qos, int retain, int timeoutS) {
  return CellReturnStatus::Error;
}

CellReturnStatus CellularModule::udpConnect(const std::string &host, int port) {
  return CellReturnStatus::Error;
}

CellReturnStatus CellularModule::udpDisconnect() { return CellReturnStatus::Error; }

CellReturnStatus CellularModule::udpSend(const UdpPacket &packet, const std::string &host,
                                         uint16_t port) {
  return CellReturnStatus::Error;
}

CellResult<CellularModule::UdpPacket> CellularModule::udpReceive(uint32_t timeout) {
  return CellResult<UdpPacket>();
}

int CellularModule::csqToDbm(int csq) {
  if (csq == 99) {
    // Unknown or undetectable
    return 0; // invalid value indication
  }
  if (csq == 0) {
    return -113;
  }
  if (csq == 1) {
    return -111;
  }
  if (csq >= 2 && csq <= 30) {
    return -109 + 2 * (csq - 2);
  }
  if (csq == 31) {
    return -51;
  }

  // invalid value indication
  return 0;
}
