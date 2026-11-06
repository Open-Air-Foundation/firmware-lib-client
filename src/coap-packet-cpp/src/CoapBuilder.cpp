/**
 * AirGradient
 * https://airgradient.com
 *
 * CC BY-SA 4.0 Attribution-ShareAlike 4.0 International License
 */

#include "CoapBuilder.h"
#include <cstring>
#include <string>

namespace CoapPacket {

CoapBuilder::CoapBuilder() : lastError_(CoapError::OK) {
    packet_.clear();
}

CoapBuilder& CoapBuilder::setType(CoapType type) {
    packet_.type = type;
    return *this;
}

CoapBuilder& CoapBuilder::setCode(CoapCode code) {
    packet_.code = code;
    return *this;
}

CoapBuilder& CoapBuilder::setMessageId(uint16_t id) {
    packet_.message_id = id;
    return *this;
}

CoapBuilder& CoapBuilder::setToken(const uint8_t* token, uint8_t length) {
    packet_.setToken(token, length);
    return *this;
}

CoapBuilder& CoapBuilder::addOption(CoapOptionNumber optionNum, const std::vector<uint8_t>& value) {
    packet_.options.emplace_back(static_cast<uint16_t>(optionNum), value);
    return *this;
}

CoapBuilder& CoapBuilder::addOption(CoapOptionNumber optionNum, const std::string& value) {
    std::vector<uint8_t> data(value.begin(), value.end());
    packet_.options.emplace_back(static_cast<uint16_t>(optionNum), data);
    return *this;
}

CoapBuilder& CoapBuilder::addOption(CoapOptionNumber optionNum, uint32_t value) {
    std::vector<uint8_t> encoded = encodeUint(value);
    packet_.options.emplace_back(static_cast<uint16_t>(optionNum), encoded);
    return *this;
}

CoapBuilder& CoapBuilder::setUriPath(const std::string& path) {
    // Split path by '/' and create URI_PATH options
    if (path.empty()) return *this;

    std::string pathCopy = path;
    // Remove leading slash if present
    if (pathCopy[0] == '/') {
        pathCopy = pathCopy.substr(1);
    }

    size_t start = 0;
    while (start <= pathCopy.size()) {
        size_t end = pathCopy.find('/', start);
        if (end == std::string::npos) {
            end = pathCopy.size();
        }
        if (end > start) {
            addUriPathSegment(pathCopy.substr(start, end - start));
        }
        start = end + 1;
    }
    return *this;
}

CoapBuilder& CoapBuilder::addUriPathSegment(const std::string& segment) {
    addOption(CoapOptionNumber::URI_PATH, segment);
    return *this;
}

CoapBuilder& CoapBuilder::addUriQuery(const std::string& key, const std::string& value) {
    std::string query = key + "=" + value;
    addOption(CoapOptionNumber::URI_QUERY, query);
    return *this;
}

CoapBuilder& CoapBuilder::setContentFormat(CoapContentFormat format) {
    addOption(CoapOptionNumber::CONTENT_FORMAT, static_cast<uint32_t>(format));
    return *this;
}

CoapBuilder& CoapBuilder::setBlock1(uint32_t num, bool more, uint8_t szx) {
    if (szx > 7) {
        lastError_ = CoapError::INVALID_ARGUMENT;
        return *this;
    }

    const uint32_t mBit = more ? 1U : 0U;
    const uint32_t value = (num << 4) | (mBit << 3) | (uint32_t)(szx & 0x07);
    addOption(CoapOptionNumber::BLOCK1, value);
    return *this;
}

CoapBuilder& CoapBuilder::setPayload(const std::vector<uint8_t>& data) {
    packet_.payload = data;
    return *this;
}

CoapBuilder& CoapBuilder::setPayload(const std::string& data) {
    packet_.payload.assign(data.begin(), data.end());
    return *this;
}

CoapBuilder& CoapBuilder::setPayload(const uint8_t* data, size_t length) {
    packet_.payload.assign(data, data + length);
    return *this;
}

CoapError CoapBuilder::build(CoapPacket& packet) {
    // Validate packet
    CoapError err = validate();
    if (err != CoapError::OK) {
        lastError_ = err;
        return err;
    }

    // Sort options before building
    sortOptions();

    // Copy packet
    packet = packet_;
    lastError_ = CoapError::OK;
    return CoapError::OK;
}

CoapError CoapBuilder::buildBuffer(std::vector<uint8_t>& buffer) {
    // Validate packet
    CoapError err = validate();
    if (err != CoapError::OK) {
        lastError_ = err;
        return err;
    }

    // Sort options before building
    sortOptions();

    buffer.clear();

    // 1. Build 4-byte header
    buffer.resize(4);
    buffer[0] = (COAP_VERSION & 0x03) << 6;  // Version (2 bits)
    buffer[0] |= (static_cast<uint8_t>(packet_.type) & 0x03) << 4;  // Type (2 bits)
    buffer[0] |= (packet_.token_length & 0x0F);  // Token length (4 bits)

    buffer[1] = static_cast<uint8_t>(packet_.code);  // Code (8 bits)

    buffer[2] = static_cast<uint8_t>(packet_.message_id >> 8);    // Message ID high byte
    buffer[3] = static_cast<uint8_t>(packet_.message_id & 0xFF);  // Message ID low byte

    // 2. Add token (0-8 bytes)
    if (packet_.token_length > 0) {
        buffer.insert(buffer.end(), packet_.token, packet_.token + packet_.token_length);
    }

    // 3. Pack options (delta-encoded, sorted)
    if (!packet_.options.empty()) {
        std::vector<uint8_t> optionsBuffer;
        err = packOptions(optionsBuffer);
        if (err != CoapError::OK) {
            lastError_ = err;
            return err;
        }
        buffer.insert(buffer.end(), optionsBuffer.begin(), optionsBuffer.end());
    }

    // 4. Add payload marker and payload (if any)
    if (!packet_.payload.empty()) {
        buffer.push_back(PAYLOAD_MARKER);  // 0xFF marker
        buffer.insert(buffer.end(), packet_.payload.begin(), packet_.payload.end());
    }

    lastError_ = CoapError::OK;
    return CoapError::OK;
}

CoapError CoapBuilder::getLastError() const {
    return lastError_;
}

void CoapBuilder::reset() {
    packet_.clear();
    lastError_ = CoapError::OK;
}

void CoapBuilder::sortOptions() {
    std::sort(packet_.options.begin(), packet_.options.end(),
        [](const CoapOption& a, const CoapOption& b) {
            return a.number < b.number;
        });
}

size_t CoapBuilder::encodeOptionDeltaLength(uint8_t* buffer, uint16_t delta, uint16_t length) {
    size_t offset = 0;
    buffer[0] = 0;

    // Encode delta (upper 4 bits)
    if (delta < 13) {
        buffer[0] |= (delta & 0x0F) << 4;
    } else if (delta < 269) {
        buffer[0] |= 13 << 4;
        buffer[++offset] = static_cast<uint8_t>(delta - 13);
    } else {
        buffer[0] |= 14 << 4;
        uint16_t extDelta = delta - 269;
        buffer[++offset] = static_cast<uint8_t>(extDelta >> 8);
        buffer[++offset] = static_cast<uint8_t>(extDelta & 0xFF);
    }

    // Encode length (lower 4 bits)
    if (length < 13) {
        buffer[0] |= (length & 0x0F);
    } else if (length < 269) {
        buffer[0] |= 13;
        buffer[++offset] = static_cast<uint8_t>(length - 13);
    } else {
        buffer[0] |= 14;
        uint16_t extLength = length - 269;
        buffer[++offset] = static_cast<uint8_t>(extLength >> 8);
        buffer[++offset] = static_cast<uint8_t>(extLength & 0xFF);
    }

    return offset + 1;  // Return total bytes written
}

std::vector<uint8_t> CoapBuilder::encodeUint(uint32_t value) {
    std::vector<uint8_t> result;

    if (value == 0) {
        // Zero is encoded as empty (0-length option)
        return result;
    }

    // Encode as big-endian, minimum bytes needed
    if (value <= 0xFF) {
        result.push_back(static_cast<uint8_t>(value));
    } else if (value <= 0xFFFF) {
        result.push_back(static_cast<uint8_t>(value >> 8));
        result.push_back(static_cast<uint8_t>(value & 0xFF));
    } else if (value <= 0xFFFFFF) {
        result.push_back(static_cast<uint8_t>(value >> 16));
        result.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
        result.push_back(static_cast<uint8_t>(value & 0xFF));
    } else {
        result.push_back(static_cast<uint8_t>(value >> 24));
        result.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
        result.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
        result.push_back(static_cast<uint8_t>(value & 0xFF));
    }

    return result;
}

CoapError CoapBuilder::packOptions(std::vector<uint8_t>& buffer) {
    buffer.clear();

    uint16_t lastOptionNumber = 0;

    for (const auto& option : packet_.options) {
        // Calculate delta
        uint16_t delta = option.number - lastOptionNumber;
        uint16_t length = static_cast<uint16_t>(option.value.size());

        // Check for option too long
        if (length > MAX_OPTION_VALUE_SIZE) {
            return CoapError::OPTION_TOO_LONG;
        }

        // Encode delta and length (max 5 bytes: 1 base + 2 for delta + 2 for length)
        uint8_t deltaLengthBuf[5];
        size_t headerSize = encodeOptionDeltaLength(deltaLengthBuf, delta, length);

        // Add header bytes
        buffer.insert(buffer.end(), deltaLengthBuf, deltaLengthBuf + headerSize);

        // Add option value
        if (length > 0) {
            buffer.insert(buffer.end(), option.value.begin(), option.value.end());
        }

        lastOptionNumber = option.number;
    }

    return CoapError::OK;
}

CoapError CoapBuilder::validate() {
    // Honor any earlier builder error
    if (lastError_ != CoapError::OK) {
        return lastError_;
    }

    // Check token length
    if (packet_.token_length > 8) {
        return CoapError::INVALID_TOKEN_LENGTH;
    }

    // Check code class validity
    uint8_t codeClass = getCodeClass(packet_.code);
    if (!isValidCodeClass(codeClass)) {
        return CoapError::INVALID_CODE_CLASS;
    }

    // Check payload size
    if (packet_.payload.size() > MAX_PAYLOAD_SIZE) {
        return CoapError::PAYLOAD_TOO_LARGE;
    }

    // Empty messages must have no token, options, or payload
    if (packet_.code == CoapCode::EMPTY) {
        if (packet_.token_length != 0 || !packet_.options.empty() || !packet_.payload.empty()) {
            return CoapError::INVALID_FORMAT;
        }
    }

    return CoapError::OK;
}

} // namespace CoapPacket
