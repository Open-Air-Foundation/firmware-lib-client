/**
 * AirGradient
 * https://airgradient.com
 *
 * CC BY-SA 4.0 Attribution-ShareAlike 4.0 International License
 */

#include <cstdint>
#ifndef ESP8266

#include "atCommandHandler.h"
#include <cstring>
#include "common.h"
#include "agLogger.h"

#define AT_YIELD()                                                                                 \
  {                                                                                                \
    DELAY_MS(2);                                                                                   \
  }

ATCommandHandler::ATCommandHandler(AirgradientSerial *agSerial) : agSerial_(agSerial) {}

bool ATCommandHandler::testAT(uint32_t timeoutMs) {
  for (uint32_t start = MILLIS(); (MILLIS() - start) < timeoutMs;) {
    sendRaw("AT");
    if (waitResponse(500) == ExpArg1) {
      return true;
    }
    DELAY_MS(100);
  }

  return false;
}

void ATCommandHandler::sendAT(const char *cmd) {
  agSerial_->print("AT");
  agSerial_->print(cmd);
  agSerial_->print("\r\n");
  AT_YIELD();
}

void ATCommandHandler::sendRaw(const char *raw) {
  agSerial_->print(raw);
  agSerial_->print("\r\n");
  AT_YIELD();
}

void ATCommandHandler::sendRaw(const char *buf, int size) {
#ifdef ARDUINO
  // AgSerial::write takes const char*, AirgradientSerial (ESP-IDF) takes const uint8_t*
  agSerial_->write(buf, size);
#else
  agSerial_->write(reinterpret_cast<const uint8_t *>(buf), size);
#endif
  agSerial_->print("\r\n");
  AT_YIELD();
}

ATCommandHandler::Response ATCommandHandler::waitResponse(uint32_t timeoutMs, const char *expArg1,
                                                          const char *expArg2,
                                                          const char *expArg3) {
  // Reset buffer
  memset(_buffer, 0, DEFAULT_BUFFER_ALLOC);

  int idx = 0;
  Response response = Timeout;
  uint32_t waitStartTime = MILLIS();

  do {
    while (agSerial_->available() && response == Timeout) {
      // buffer overflow check
      if (idx >= DEFAULT_BUFFER_ALLOC) {
        AG_LOGE(TAG, "waitResponse() buffer overflow");
        return Response::CMxError; // TODO: Handle better, should not CMxError
      }
      _buffer[idx] = agSerial_->read();
      idx++;

      if (expArg1 && _endsWith(_buffer, expArg1)) {
        response = ExpArg1;
      } else if (expArg2 && _endsWith(_buffer, expArg2)) {
        response = ExpArg2;
      } else if (expArg3 && _endsWith(_buffer, expArg3)) {
        response = ExpArg3;
      }
      // CME/CMS error check
      else if (_endsWith(_buffer, RESP_ERROR_CME) || _endsWith(_buffer, RESP_ERROR_CMS)) {
        std::string errMsg;
        waitAndRecvRespLine(errMsg);
        AG_LOGW(TAG, "CMx error message: %s", errMsg.c_str());
        response = CMxError;
      }
    }

    DELAY_MS(10);
  } while ((MILLIS() - waitStartTime) < timeoutMs && response == Timeout);

  return response;
}

ATCommandHandler::Response ATCommandHandler::waitResponse(const char *expArg1, const char *expArg2,
                                                          const char *expArg3) {
  return waitResponse(DEFAULT_WAIT_RESPONSE_TIMEOUT, expArg1, expArg2, expArg3);
}

ATCommandHandler::Response ATCommandHandler::waitResponseAndCollect(char *received, int memorySize,
                                                                     uint32_t timeoutMs) {
  if (received == nullptr || memorySize <= 0) {
    return CMxError;
  }

  memset(received, 0, memorySize);
  char linePrefix[32] = {0};
  size_t lineLength = 0;
  size_t receivedLength = 0;
  bool overflow = false;
  uint32_t waitStartTime = MILLIS();

  do {
    while (agSerial_->available()) {
      char b = agSerial_->read();
      if (receivedLength + 1 < static_cast<size_t>(memorySize)) {
        received[receivedLength++] = b;
        received[receivedLength] = '\0';
      } else {
        overflow = true;
      }

      if (lineLength < sizeof(linePrefix) - 1) {
        linePrefix[lineLength] = b;
        linePrefix[lineLength + 1] = '\0';
      }
      lineLength++;

      if (b == '\n') {
        bool isOk = lineLength == sizeof(RESP_AT_OK) - 1 &&
                    strcmp(linePrefix, RESP_AT_OK) == 0;
        bool isError = lineLength == sizeof(RESP_AT_ERROR) - 1 &&
                       strcmp(linePrefix, RESP_AT_ERROR) == 0;
        bool isCmxError = strncmp(linePrefix, RESP_ERROR_CME, strlen(RESP_ERROR_CME)) == 0 ||
                          strncmp(linePrefix, RESP_ERROR_CMS, strlen(RESP_ERROR_CMS)) == 0;

        if (isOk || isError || isCmxError) {
          if (isCmxError) {
            AG_LOGW(TAG, "CMx error message: %s", linePrefix);
          }
          if (overflow) {
            AG_LOGW(TAG, "AT response buffer overflow");
            return Overflow;
          }
          return isOk ? ExpArg1 : (isError ? ExpArg2 : CMxError);
        }

        lineLength = 0;
        linePrefix[0] = '\0';
      }
    }

    DELAY_MS(10);
  } while ((MILLIS() - waitStartTime) < timeoutMs);

  if (overflow) {
    AG_LOGW(TAG, "AT response buffer overflow");
    return Overflow;
  }
  return Timeout;
}

int ATCommandHandler::waitAndRecvRespLine(char *received, int memorySize, uint32_t timeoutMs,
                                          bool excludeWhitespace) {
  int idx = 0;
  bool finish = false;
  uint32_t waitStartTime = MILLIS();

  // Sanity check, making sure 'received' has empty memory
  memset(received, 0, memorySize);

  do {
    while (agSerial_->available() && !finish) {
      // Read per 1 byte
      char b = agSerial_->read();
      if (excludeWhitespace) {
        // Exclude whitespace on first character by skipping first array index
        // Usually if received line like "CPIN: READY"
        if (idx == 0 && b == ' ') {
          continue;
        }
      }

      // Check if there's an end line sequence
      if (b == '\r') {
        b = agSerial_->read();
        if (b == '\n') {
          finish = true;
          break;
        }
      }

      // buffer overflow check
      if (idx >= memorySize) {
        AG_LOGE(TAG, "waitAndRecvRespLine() buffer overflow");
        return 0; // TODO: Handle better
      }
      // Append to buffer
      received[idx] = b;
      idx++;
      AT_YIELD();
    }

    AT_YIELD();
  } while ((MILLIS() - waitStartTime) < timeoutMs && !finish);

  if (!finish) {
    // Timeout
    return -1;
  }

  return 1;
}

int ATCommandHandler::waitAndRecvRespLine(std::string &received, int length, uint32_t timeoutMs,
                                          bool excludeWhitespace) {
  char buff[length];
  int result = waitAndRecvRespLine(buff, length, timeoutMs);
  received = std::string(buff);
  return result;
}

int ATCommandHandler::retrieveBuffer(char *output, int length, uint32_t timeoutMs) {

  int idx = 0;
  bool finish = false;
  uint32_t waitStartTime = MILLIS();

  // Sanity check, making sure 'output' has empty memory
  memset(output, 0, sizeof(length));

  do {
    while (agSerial_->available() && !finish) {
      // Read per 1 bytes and append to buffer
      output[idx] = agSerial_->read();
      idx++;
      // Check if its already the expected length to retrieve
      if (idx >= length) {
        finish = true;
      }
    }

    AT_YIELD();
  } while ((MILLIS() - waitStartTime) < timeoutMs && !finish);

  if (!finish) {
    // Timeout
    return -1;
  }

  return idx;
}

void ATCommandHandler::clearBuffer() {
  while (agSerial_->available()) {
    agSerial_->read();
  }
}

bool ATCommandHandler::_endsWith(const char *str, const char *target) {
  if (!str || !target) {
    // One or both not provided
    return false;
  }

  size_t lenStr = strlen(str);
  size_t lenTarget = strlen(target);
  if (lenTarget > lenStr) {
    return false;
  }

  return strncmp(str + lenStr - lenTarget, target, lenTarget) == 0;
}

#endif // ESP8266
