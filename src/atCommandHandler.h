/**
 * AirGradient
 * https://airgradient.com
 *
 * CC BY-SA 4.0 Attribution-ShareAlike 4.0 International License
 */

#ifndef AT_COMMAND_HANDLER_H
#define AT_COMMAND_HANDLER_H

#ifndef ESP8266

#include <cstdint>
#include <string>

#ifdef ARDUINO
#include "agSerial.h"
#else
#include "AirgradientSerial.h"
#endif

#define AT_DEBUG
#define AT_OK "OK"
#define AT_ERROR "ERROR"
#define AT_NL "\r\n"
#define DEFAULT_RESPONSE_DATA_LEN 64
#define DEFAULT_WAIT_RESPONSE_TIMEOUT 9000 // ms
#ifdef CONFIG_BUFFER_LENGTH_ALLOCATION
#define DEFAULT_BUFFER_ALLOC CONFIG_BUFFER_LENGTH_ALLOCATION
#else
#define DEFAULT_BUFFER_ALLOC 4000
#endif

static const char RESP_AT_OK[] = AT_OK AT_NL;
static const char RESP_AT_ERROR[] = AT_ERROR AT_NL;
static const char RESP_ERROR_CME[] = "+CME ERROR:";
static const char RESP_ERROR_CMS[] = "+CMS ERROR:";

class ATCommandHandler {
public:
#ifdef ARDUINO
  // NOTE: Temporarily accomodate ununified AirgradientSerial
  typedef AgSerial AirgradientSerial;
#endif

private:
  const char *const TAG = "ATCMD";
  AirgradientSerial *agSerial_ = nullptr;

public:
  enum Response { ExpArg1, ExpArg2, ExpArg3, Timeout, CMxError, Overflow };

  ATCommandHandler(AirgradientSerial *agSerial);
  ~ATCommandHandler() {};

  bool testAT(uint32_t timeoutMs = 60000);

  /**
   * @brief send AT command while "AT" prefix and linebreak already provided
   *
   * Example:
   *
   * ```
   * at.sendAT("+CPIN?")
   * ```
   *
   * @param cmd command to send without AT prefix and linebreak
   */
  void sendAT(const char *cmd);

  /**
   * @brief send command without "AT" prefix but linebreak already provided
   *
   * Example:
   *
   * ```
   * at.sendAT("AT+CPIN?")
   * ```
   *
   * @param cmd command to send without linebreak
   */
  void sendRaw(const char *raw);

  void sendRaw(const char *buf, int size);

  /**
   * @brief Wait for AT response with multiple response expectation in the form of argument
   * Call this function after sending AT command and expect a response
   *
   * Example:
   * ```
   * at.sendRaw("AT");
   * Response resp = at.waitResponse(); // Not provide expArg
   * resp == ExpArg1 // receive "OK" response
   * resp == ExpArg2 // receive "ERROR" response
   * resp == Timeout // timeout wait for response
   *
   * at.sendAT("+CPIN?");
   * Response resp = at.waitResponse(1000, "+CPIN:");
   * resp == ExpArg1 // receive "+CPIN:" in response
   * resp == ExpArg2 // receive "ERROR" response
   * resp == Timeout // timeout wait for response
   * ```
   *
   * @param timeoutMs how long to wait for response
   * @param expArg1 expected response 1 (Default: OK)
   * @param expArg2 expected response 2 (Default: ERROR)
   * @param expArg3 expected response 2 (Default: null)
   * @return Response response enum member
   */
  Response waitResponse(uint32_t timeoutMs, const char *expArg1 = RESP_AT_OK,
                        const char *expArg2 = RESP_AT_ERROR, const char *expArg3 = nullptr);

  /**
   * @brief Wait for AT response with multiple response expectation in the form of argument
   * Call this function after sending AT command and expect a response
   *
   * Example:
   * ```
   * at.sendRaw("AT");
   * Response resp = at.waitResponse(); // Not provide expArg
   * resp == ExpArg1 // receive "OK" response
   * resp == ExpArg2 // receive "ERROR" response
   * resp == Timeout // timeout wait for response
   *
   * at.sendAT("+CPIN?");
   * Response resp = at.waitResponse("+CPIN:");
   * resp == ExpArg1 // receive "+CPIN:" in response
   * resp == ExpArg2 // receive "ERROR" response
   * resp == Timeout // timeout wait for response
   * ```
   * @param expArg1 expected response 1 (Default: OK)
   * @param expArg2 expected response 2 (Default: ERROR)
   * @param expArg3 expected response 2 (Default: null)
   * @return Response response enum member
   */
  Response waitResponse(const char *expArg1 = RESP_AT_OK, const char *expArg2 = RESP_AT_ERROR,
                        const char *expArg3 = nullptr);

  /**
   * @brief Collect a complete AT response into a caller-provided buffer
   *
   * Reads response data until a terminal OK/error response or timeout. The
   * received buffer is null-terminated when memorySize is greater than zero.
   *
   * @param received buffer where the response will be placed
   * @param memorySize size of the received buffer
   * @param timeoutMs how long to wait for a terminal response
   * @return Response response enum member
   */
  Response waitResponseAndCollect(char *received, int memorySize, uint32_t timeoutMs);

  /**
   * @brief receive the rest of response on rx buffer until linebreak
   *
   * ```
   * at.sendAT("+CPIN?");
   * Response resp = at.waitResponse("+CPIN:"); // assume resp ExpArg1
   * std::string data;
   * at.waitAndRecvRespLine(data);
   * data == "READY" // or other value
   * ```
   *
   * @param received where response will placed
   * @param length expected received buffer length (NOTE: don't too conservative about this!)
   * @param timeoutMs how long to wait until linebreak received
   * @param excludeWhitespace exclude whitespace at the first received byte
   * @return -1 timeout, 1 data received
   */
  int waitAndRecvRespLine(std::string &received, int length = DEFAULT_RESPONSE_DATA_LEN,
                          uint32_t timeoutMs = 3000, bool exclueWhitespace = true);

  /**
   * @brief receive the rest of response on rx buffer until linebreak
   *
   * Caller needs to provide char array with memory that already allocated with size of 'memorySize'
   *
   * ```
   * at.sendAT("+CPIN?");
   * Response resp = at.waitResponse("+CPIN:"); // assume resp ExpArg1
   * char data[20] = {0};
   * at.waitAndRecvRespLine(data, 20);
   * data == "READY" // or other value
   * ```
   *
   * @param received where response will placed
   * @param memorySize the memory size allocated for 'received' params, to avoid overflow
   * @param timeoutMs how long to wait until linebreak received
   * @param excludeWhitespace exclude whitespace at the first received byte
   * @return -1 timeout, 1 data received
   */
  int waitAndRecvRespLine(char *received, int memorySize, uint32_t timeoutMs = 3000,
                          bool excludeWhitespace = true);

  /**
   * @brief retrieve buffer from AT command serial rx buffer
   *
   * @param output where result will placed
   * @param length the expected size to retrieve from buffer
   * @param timeoutMs how long to wait until all expected data retrieved
   * @return -1 timeout, >0 actual bytes received
   */
  int retrieveBuffer(char *output, int length, uint32_t timeoutMs = 3000);

  void clearBuffer();

private:
  bool _endsWith(const char *str, const char *target);

  char _buffer[DEFAULT_BUFFER_ALLOC];
};

#endif // ESP8266
#endif // AT_COMMAND_HANDLER_H
