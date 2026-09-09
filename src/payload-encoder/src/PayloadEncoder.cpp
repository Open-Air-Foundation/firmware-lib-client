/**
 * AirGradient
 * https://airgradient.com
 *
 * CC BY-SA 4.0 Attribution-ShareAlike 4.0 International License
 */

#include "PayloadEncoder.h"
#include <string.h>

static inline bool presenceMaskIsZero(const PresenceMask &mask) {
  return mask.lo == 0 && mask.hi == 0;
}

static inline bool presenceMaskEquals(const PresenceMask &a,
                                      const PresenceMask &b) {
  return a.lo == b.lo && a.hi == b.hi;
}

static bool getSharedPresenceMaskForBatch(const EncoderContext &ctx,
                                          PresenceMask *out_mask) {
  if (ctx.reading_count == 0) {
    return false;
  }

  const PresenceMask first = ctx.readings[0].presence_mask;
  for (uint8_t i = 1; i < ctx.reading_count; i++) {
    if (!presenceMaskEquals(first, ctx.readings[i].presence_mask)) {
      return false;
    }
  }

  if (presenceMaskIsZero(first)) {
    return false;
  }

  if (out_mask != nullptr) {
    *out_mask = first;
  }
  return true;
}

static uint32_t calculateSensorDataSizeForMask(const PresenceMask &mask) {
  uint32_t size = 0;
  for (uint8_t flag = 0; flag <= (uint8_t)FLAG_TIMESTAMP; flag++) {
    if (!isBitSet64(&mask, flag)) {
      continue;
    }

    if (flag == (uint8_t)FLAG_SIGNAL) {
      size += 1;
      continue;
    }

    if (flag == (uint8_t)FLAG_O3_WE || flag == (uint8_t)FLAG_O3_AE ||
        flag == (uint8_t)FLAG_NO2_WE || flag == (uint8_t)FLAG_NO2_AE ||
        flag == (uint8_t)FLAG_TIMESTAMP) {
      size += 4;
      continue;
    }

    // The rest of flag data type size
    size += 2;
  }

  return size;
}

PayloadEncoder::PayloadEncoder() { reset(); }

void PayloadEncoder::init(const PayloadHeader &header) {
  reset();
  ctx.header = header;
}

bool PayloadEncoder::addReading(const SensorReading &reading) {
  if (ctx.reading_count >= MAX_BATCH_SIZE) {
    return false;
  }

  ctx.readings[ctx.reading_count++] = reading;
  return true;
}

void PayloadEncoder::reset() { memset(&ctx, 0, sizeof(EncoderContext)); }

uint8_t PayloadEncoder::getReadingCount() const { return ctx.reading_count; }

uint8_t PayloadEncoder::encodeMetadata() const {
  uint8_t metadata = 0;

  // Bits 0-4: VERSION
  metadata |= (AG_PAYLOAD_VERSION & 0x1F);

  // Bit 5: SHARED_PRESENCE_MASK
  if (getSharedPresenceMaskForBatch(ctx, nullptr)) {
    metadata |= (1U << AG_METADATA_SHARED_PRESENCE_MASK_BIT);
  }

  // Bits 6-7: RESERVED (0)

  return metadata;
}

void PayloadEncoder::encodePresenceMask(uint8_t *buffer,
                                       const PresenceMask &mask) const {
  // Write as little-endian 64-bit integer (lo then hi)
  writeUint32(&buffer[0], mask.lo);
  writeUint32(&buffer[4], mask.hi);
}

void PayloadEncoder::writeUint16(uint8_t *buffer, uint16_t value) const {
  // Little-endian encoding
  buffer[0] = (value >> 0) & 0xFF;
  buffer[1] = (value >> 8) & 0xFF;
}

void PayloadEncoder::writeInt16(uint8_t *buffer, int16_t value) const {
  // Little-endian encoding
  uint16_t unsigned_value = *((uint16_t *)&value);
  writeUint16(buffer, unsigned_value);
}

void PayloadEncoder::writeUint32(uint8_t *buffer, uint32_t value) const {
  // Little-endian encoding
  buffer[0] = (value >> 0) & 0xFF;
  buffer[1] = (value >> 8) & 0xFF;
  buffer[2] = (value >> 16) & 0xFF;
  buffer[3] = (value >> 24) & 0xFF;
}

int32_t PayloadEncoder::encodeSensorData(uint8_t *buffer, uint32_t buffer_size,
                                         const SensorReading &reading,
                                         const PresenceMask &mask) const {
  uint32_t offset = 0;

  // Iterate through defined flags in ascending presence-bit order.
  for (uint8_t flag = 0; flag <= (uint8_t)FLAG_TIMESTAMP; flag++) {
    if (!isBitSet64(&mask, flag)) {
      continue; // Skip if flag not set
    }

    // Encode based on flag type
    switch ((SensorFlag)flag) {
    case FLAG_TEMP:
      if (offset + 2 > buffer_size)
        return -1;
      writeInt16(&buffer[offset], reading.temp);
      offset += 2;
      break;

    case FLAG_HUM:
      if (offset + 2 > buffer_size)
        return -1;
      writeUint16(&buffer[offset], reading.hum);
      offset += 2;
      break;

    case FLAG_CO2:
      if (offset + 2 > buffer_size)
        return -1;
      writeUint16(&buffer[offset], reading.co2);
      offset += 2;
      break;

    case FLAG_TVOC:
      if (offset + 2 > buffer_size)
        return -1;
      writeUint16(&buffer[offset], reading.tvoc);
      offset += 2;
      break;

    case FLAG_TVOC_RAW:
      if (offset + 2 > buffer_size)
        return -1;
      writeUint16(&buffer[offset], reading.tvoc_raw);
      offset += 2;
      break;

    case FLAG_NOX:
      if (offset + 2 > buffer_size)
        return -1;
      writeUint16(&buffer[offset], reading.nox);
      offset += 2;
      break;

    case FLAG_NOX_RAW:
      if (offset + 2 > buffer_size)
        return -1;
      writeUint16(&buffer[offset], reading.nox_raw);
      offset += 2;
      break;

    case FLAG_PM_01:
      if (offset + 2 > buffer_size)
        return -1;
      writeUint16(&buffer[offset], reading.pm_01);
      offset += 2;
      break;

    case FLAG_PM_25_CH1:
      if (offset + 2 > buffer_size)
        return -1;
      writeUint16(&buffer[offset], reading.pm_25[0]);
      offset += 2;
      break;

    case FLAG_PM_25_CH2:
      if (offset + 2 > buffer_size)
        return -1;
      writeUint16(&buffer[offset], reading.pm_25[1]);
      offset += 2;
      break;

    case FLAG_PM_10:
      if (offset + 2 > buffer_size)
        return -1;
      writeUint16(&buffer[offset], reading.pm_10);
      offset += 2;
      break;

    case FLAG_PM_01_SP:
      if (offset + 2 > buffer_size)
        return -1;
      writeUint16(&buffer[offset], reading.pm_01_sp);
      offset += 2;
      break;

    case FLAG_PM_25_SP_CH1:
      if (offset + 2 > buffer_size)
        return -1;
      writeUint16(&buffer[offset], reading.pm_25_sp[0]);
      offset += 2;
      break;

    case FLAG_PM_25_SP_CH2:
      if (offset + 2 > buffer_size)
        return -1;
      writeUint16(&buffer[offset], reading.pm_25_sp[1]);
      offset += 2;
      break;

    case FLAG_PM_10_SP:
      if (offset + 2 > buffer_size)
        return -1;
      writeUint16(&buffer[offset], reading.pm_10_sp);
      offset += 2;
      break;

    case FLAG_PM_03_PC_CH1:
      if (offset + 2 > buffer_size)
        return -1;
      writeUint16(&buffer[offset], reading.pm_03_pc[0]);
      offset += 2;
      break;

    case FLAG_PM_03_PC_CH2:
      if (offset + 2 > buffer_size)
        return -1;
      writeUint16(&buffer[offset], reading.pm_03_pc[1]);
      offset += 2;
      break;

    case FLAG_PM_05_PC:
      if (offset + 2 > buffer_size)
        return -1;
      writeUint16(&buffer[offset], reading.pm_05_pc);
      offset += 2;
      break;

    case FLAG_PM_01_PC:
      if (offset + 2 > buffer_size)
        return -1;
      writeUint16(&buffer[offset], reading.pm_01_pc);
      offset += 2;
      break;

    case FLAG_PM_25_PC:
      if (offset + 2 > buffer_size)
        return -1;
      writeUint16(&buffer[offset], reading.pm_25_pc);
      offset += 2;
      break;

    case FLAG_PM_5_PC:
      if (offset + 2 > buffer_size)
        return -1;
      writeUint16(&buffer[offset], reading.pm_5_pc);
      offset += 2;
      break;

    case FLAG_PM_10_PC:
      if (offset + 2 > buffer_size)
        return -1;
      writeUint16(&buffer[offset], reading.pm_10_pc);
      offset += 2;
      break;

    case FLAG_VBAT:
      if (offset + 2 > buffer_size)
        return -1;
      writeUint16(&buffer[offset], reading.vbat);
      offset += 2;
      break;

    case FLAG_VPANEL:
      if (offset + 2 > buffer_size)
        return -1;
      writeUint16(&buffer[offset], reading.vpanel);
      offset += 2;
      break;

    case FLAG_O3_WE:
      if (offset + 4 > buffer_size)
        return -1;
      writeUint32(&buffer[offset], reading.o3_we);
      offset += 4;
      break;

    case FLAG_O3_AE:
      if (offset + 4 > buffer_size)
        return -1;
      writeUint32(&buffer[offset], reading.o3_ae);
      offset += 4;
      break;

    case FLAG_NO2_WE:
      if (offset + 4 > buffer_size)
        return -1;
      writeUint32(&buffer[offset], reading.no2_we);
      offset += 4;
      break;

    case FLAG_NO2_AE:
      if (offset + 4 > buffer_size)
        return -1;
      writeUint32(&buffer[offset], reading.no2_ae);
      offset += 4;
      break;

    case FLAG_AFE_TEMP:
      if (offset + 2 > buffer_size)
        return -1;
      writeUint16(&buffer[offset], reading.afe_temp);
      offset += 2;
      break;

    case FLAG_SIGNAL:
      if (offset + 1 > buffer_size)
        return -1;
      buffer[offset] = (uint8_t)reading.signal;
      offset += 1;
      break;

    case FLAG_TIMESTAMP:
      if (offset + 4 > buffer_size)
        return -1;
      writeUint32(&buffer[offset], reading.timestamp);
      offset += 4;
      break;
    }
  }

  return offset;
}

uint32_t PayloadEncoder::calculateReadingSize(const SensorReading &reading) const {
  // Per-reading mode size: 8-byte mask + sensor data
  return 8 + calculateSensorDataSizeForMask(reading.presence_mask);
}

uint32_t PayloadEncoder::calculateTotalSize() const {
  if (ctx.reading_count == 0) {
    return 0;
  }

  PresenceMask shared_mask;
  const bool shared = getSharedPresenceMaskForBatch(ctx, &shared_mask);

  if (shared) {
    const uint32_t data_size = calculateSensorDataSizeForMask(shared_mask);
    if (data_size == 0) {
      return 0;
    }
    return 2 + 8 + (uint32_t)ctx.reading_count * data_size;
  }

  uint32_t size = 2;
  for (uint8_t i = 0; i < ctx.reading_count; i++) {
    const uint32_t data_size = calculateSensorDataSizeForMask(ctx.readings[i].presence_mask);
    size += 8 + data_size;
  }
  return size;
}

int32_t PayloadEncoder::encode(uint8_t *buffer, uint32_t buffer_size) {
  if (buffer == nullptr) {
    return -1;
  }

  if (ctx.reading_count == 0) {
    return 0; // No readings to encode
  }

  PresenceMask shared_mask;
  const bool shared = getSharedPresenceMaskForBatch(ctx, &shared_mask);

  // Calculate total size (must match the encoding path below)
  uint32_t total_size = 0;
  if (shared) {
    const uint32_t data_size = calculateSensorDataSizeForMask(shared_mask);
    if (data_size == 0) {
      return -1;
    }
    total_size = 2 + 8 + (uint32_t)ctx.reading_count * data_size;
  } else {
    total_size = 2;
    for (uint8_t i = 0; i < ctx.reading_count; i++) {
      total_size += 8 + calculateSensorDataSizeForMask(ctx.readings[i].presence_mask);
    }
  }

  if (total_size > buffer_size) {
    return -1; // Buffer too small
  }

  uint32_t offset = 0;

  // Encode header (Byte 0: Metadata, Byte 1: Interval)
  buffer[offset++] = encodeMetadata();
  buffer[offset++] = ctx.header.interval_minutes;

  if (shared) {
    // Encode shared mask once
    encodePresenceMask(&buffer[offset], shared_mask);
    offset += 8;

    // Encode each reading data using shared mask
    for (uint8_t i = 0; i < ctx.reading_count; i++) {
      int32_t data_size =
          encodeSensorData(&buffer[offset], buffer_size - offset, ctx.readings[i], shared_mask);
      if (data_size < 0) {
        return -1;
      }
      offset += (uint32_t)data_size;
    }
  } else {
    // Encode each reading with its own mask
    for (uint8_t i = 0; i < ctx.reading_count; i++) {
      encodePresenceMask(&buffer[offset], ctx.readings[i].presence_mask);
      offset += 8;

      int32_t data_size = encodeSensorData(&buffer[offset], buffer_size - offset, ctx.readings[i],
                                           ctx.readings[i].presence_mask);
      if (data_size < 0) {
        return -1;
      }
      offset += (uint32_t)data_size;
    }
  }

  return offset;
}
