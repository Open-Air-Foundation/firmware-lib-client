/**
 * AirGradient
 * https://airgradient.com
 *
 * CC BY-SA 4.0 Attribution-ShareAlike 4.0 International License
 */

#ifndef AIRGRADIENT_CLIENT_H
#define AIRGRADIENT_CLIENT_H

#include "common.h"
#include "airgradientServerConfig.h"
#include <string>
#include <vector>

#define AIRGRADIENT_COAP_DOMAIN "coap.airgradient.com"
#define AIRGRADIENT_COAP_IP "128.140.49.53"

#define MAXIMUM_PAYLOAD_BUFFER 100

class AirgradientClient {
private:
public:
  AirgradientClient() {};
  virtual ~AirgradientClient() {};

  struct CommonPayload {
    int rco2;
    float atmp;
    float rhum;
    int particleCount003[2];
    int particleCount005;
    int particleCount01;
    int particleCount02;
    int particleCount50;
    int particleCount10;
    float pm01;
    float pm25[2];
    float pm10;
    float pm25Sp[2];
    int tvocRaw;
    int tvoc;
    int noxRaw;
    int nox;
  };

  struct ExtraPayload {
    float vBat;
    float vPanel;
    float o3WorkingElectrode;
    float o3AuxiliaryElectrode;
    float no2WorkingElectrode;
    float no2AuxiliaryElectrode;
    float afeTemp;
  };

  enum PayloadType {
    MAX_WITH_O3_NO2 = 0,
    MAX_WITHOUT_O3_NO2 = 1,
    ONE_OPENAIR,
    ONE_OPENAIR_TWO_PMS
  };

  struct PayloadBuffer {
    CommonPayload common;
    union {
      ExtraPayload extra;
    } ext;
  };

  struct AirgradientPayload {
    int measureInterval;
    int signal;
    PayloadType payloadType;
    PayloadBuffer payloadBuffer[MAXIMUM_PAYLOAD_BUFFER];
    int bufferCount;
  };

  virtual bool begin(std::string sn, PayloadType pt);
  virtual void setAPN(const std::string &apn);
  virtual void setExtendedPmMeasures(bool enable);
  virtual void setNetworkRegistrationTimeoutMs(int timeoutMs);
  virtual std::string getICCID();
  virtual bool ensureClientConnection(bool reset);
  virtual std::string httpFetchConfig();
  virtual bool httpPostMeasures(const std::string &payload);
  virtual bool httpPostMeasures(const AirgradientPayload &payload);
  virtual bool mqttConnect();
  virtual bool mqttConnect(const char *uri);
  virtual bool mqttConnect(const std::string &host, int port, std::string username = "",
                           std::string password = "");
  virtual bool mqttDisconnect();
  virtual bool mqttPublishMeasures(const std::string &payload);
  virtual bool mqttPublishMeasures(const AirgradientPayload &payload);
  virtual std::string coapFetchConfig(bool keepConnection = false);
  virtual bool coapPostMeasures(const uint8_t* buffer, size_t length, bool keepConnection = false);
  virtual bool coapPostMeasures(const AirgradientPayload &payload, bool keepConnection = false);

  // Implemented on base class, not override function

  /**
   * @brief set http url domain for http request. Eg: hw.airgradient.com
   *
   * @param target target domain that will be used
   */
  void setHttpDomain(const std::string &target);
  void setHttpDomainDefault();
  void setCoapDomain(const std::string &target);
  void setCoapDomainDefault();
  bool isClientReady();
  void setClientReady(bool isReady);
  void resetFetchConfigurationStatus();
  void resetPostMeasuresStatus();
  bool isLastFetchConfigSucceed();
  bool isLastPostMeasureSucceed();
  bool isRegisteredOnAgServer();

protected:
  PayloadType payloadType;
  std::string httpDomain = AIRGRADIENT_HTTP_DOMAIN;
  std::string coapHostTarget = AIRGRADIENT_COAP_IP;
  const int coapPort = 5683;
  const char *const mqttDomain = "api.airgradient.com";
  const int mqttPort = 1883;

  std::string buildFetchConfigUrl(bool useHttps = false);
  std::string buildPostMeasuresUrl(bool useHttps = false);
  std::string buildMqttTopicPublishMeasures();

  std::string serialNumber;
  bool lastPostMeasuresSucceed = true;
  bool lastFetchConfigSucceed = true;
  bool registeredOnAgServer = true;
  bool clientReady = true;
};
#endif // AIRGRADIENT_CLIENT_H
