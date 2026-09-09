/**
 * AirGradient
 * https://airgradient.com
 *
 * CC BY-SA 4.0 Attribution-ShareAlike 4.0 International License
 */

#ifndef PAYLOAD_TYPES_H
#define PAYLOAD_TYPES_H

#include <stdint.h>
#include <stdbool.h>

// Maximum number of readings in a batch
// Keep in sync with AirgradientClient::MAXIMUM_PAYLOAD_BUFFER.
#define MAX_BATCH_SIZE 100

// Payload schema version
#define AG_PAYLOAD_VERSION 1

// Metadata bit layout
// - Bits 0-4: VERSION
// - Bit 5: SHARED_PRESENCE_MASK
// - Bits 6-7: RESERVED
#define AG_METADATA_SHARED_PRESENCE_MASK_BIT 5

// Presence mask is 64-bit on the wire (8 bytes, little-endian)
typedef struct {
  uint32_t lo;
  uint32_t hi;
} PresenceMask;

// Sensor flags enum (matches presence mask bits)
typedef enum {
  FLAG_TEMP = 0,
  FLAG_HUM = 1,
  FLAG_CO2 = 2,
  FLAG_TVOC = 3,
  FLAG_TVOC_RAW = 4,
  FLAG_NOX = 5,
  FLAG_NOX_RAW = 6,
  FLAG_PM_01 = 7,
  FLAG_PM_25_CH1 = 8,
  FLAG_PM_25_CH2 = 9,
  FLAG_PM_10 = 10,
  FLAG_PM_01_SP = 11,
  FLAG_PM_25_SP_CH1 = 12,
  FLAG_PM_25_SP_CH2 = 13,
  FLAG_PM_10_SP = 14,
  FLAG_PM_03_PC_CH1 = 15,
  FLAG_PM_03_PC_CH2 = 16,
  FLAG_PM_05_PC = 17,
  FLAG_PM_01_PC = 18,
  FLAG_PM_25_PC = 19,
  FLAG_PM_5_PC = 20,
  FLAG_PM_10_PC = 21,
  FLAG_VBAT = 22,
  FLAG_VPANEL = 23,
  FLAG_O3_WE = 24,
  FLAG_O3_AE = 25,
  FLAG_NO2_WE = 26,
  FLAG_NO2_AE = 27,
  FLAG_AFE_TEMP = 28,
  FLAG_SIGNAL = 29,
  FLAG_TIMESTAMP = 30
} SensorFlag;

// Helper to check if a bit is set in a 64-bit mask
static inline bool isBitSet64(const PresenceMask *mask, uint8_t bit_index) {
  if (bit_index < 32) {
    return ((mask->lo >> bit_index) & 1U) != 0;
  }
  return ((mask->hi >> (bit_index - 32)) & 1U) != 0;
}

// Sensor reading structure
typedef struct {
  PresenceMask presence_mask; // Which fields are present

  // Sensor values (only valid if corresponding bit set in presence_mask)
  int16_t temp;      // Temperature * 100 (Celsius)
  uint16_t hum;      // Humidity * 100 (%)
  uint16_t co2;      // CO2 ppm
  uint16_t tvoc;     // TVOC index
  uint16_t tvoc_raw; // TVOC raw
  uint16_t nox;      // NOx index
  uint16_t nox_raw;  // NOx raw

  uint16_t pm_01;       // PM1.0 * 10 (Atmospheric)
  uint16_t pm_25[2];    // PM2.5 * 10 (Atmospheric) [CH1, CH2]
  uint16_t pm_10;       // PM10 * 10 (Atmospheric)
  uint16_t pm_01_sp;    // PM1.0 * 10 (Standard Particle)
  uint16_t pm_25_sp[2]; // PM2.5 * 10 (Standard Particle) [CH1, CH2]
  uint16_t pm_10_sp;    // PM10 * 10 (Standard Particle)
  uint16_t pm_03_pc[2]; // PM0.3 count [CH1, CH2]
  uint16_t pm_05_pc;    // PM0.5 count
  uint16_t pm_01_pc;    // PM1.0 count
  uint16_t pm_25_pc;    // PM2.5 count
  uint16_t pm_5_pc;     // PM5.0 count
  uint16_t pm_10_pc;    // PM10 count

  uint16_t vbat;     // Battery voltage (mV)
  uint16_t vpanel;   // Panel/Charger voltage (mV)
  uint32_t o3_we;    // O3 Working Electrode (mV/Raw)
  uint32_t o3_ae;    // O3 Aux Electrode (mV/Raw)
  uint32_t no2_we;   // NO2 Working Electrode (mV/Raw)
  uint32_t no2_ae;   // NO2 Aux Electrode (mV/Raw)
  uint16_t afe_temp; // AFE Chip Temperature * 10
  int8_t signal;     // Signal strength (dBm)

  uint32_t timestamp; // UTC Unix seconds at measurement completion
} SensorReading;

// Payload header (Byte 1: Interval). Byte 0 (Metadata) is derived by encoder.
typedef struct {
  uint8_t interval_minutes; // Measurement interval in minutes
} PayloadHeader;

// Encoder context
typedef struct {
  PayloadHeader header;
  SensorReading readings[MAX_BATCH_SIZE];
  uint8_t reading_count;
} EncoderContext;

// Helper to initialize a sensor reading
static inline void initSensorReading(SensorReading *reading) {
  reading->presence_mask.lo = 0;
  reading->presence_mask.hi = 0;
}

// Helper to set a flag in presence mask
static inline void setFlag(SensorReading *reading, SensorFlag flag) {
  uint8_t bit_index = (uint8_t)flag;
  if (bit_index < 32) {
    reading->presence_mask.lo |= (1UL << bit_index);
  } else {
    reading->presence_mask.hi |= (1UL << (bit_index - 32));
  }
}

// Helper to clear a flag in presence mask
static inline void clearFlag(SensorReading *reading, SensorFlag flag) {
  uint8_t bit_index = (uint8_t)flag;
  if (bit_index < 32) {
    reading->presence_mask.lo &= ~(1UL << bit_index);
  } else {
    reading->presence_mask.hi &= ~(1UL << (bit_index - 32));
  }
}

// Helper to check if a flag is set
static inline bool isFlagSet(const SensorReading *reading, SensorFlag flag) {
  return isBitSet64(&reading->presence_mask, (uint8_t)flag);
}

#endif // PAYLOAD_TYPES_H
