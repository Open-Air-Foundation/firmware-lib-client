# AirGradient Cellular Payload Encoder (C++)

C++ encoder for AirGradient binary cellular payload format, optimized for embedded systems.

## Features

- ✅ Embedded-friendly (no STL containers, static memory)
- ✅ 64-bit presence mask (8 bytes, little-endian)
- ✅ Shared presence mask (automatically used when all reading masks match)
- ✅ Supports explicit two-channel flags for selected sensors
- ✅ Batch encoding (up to 20 readings)
- ✅ Little-endian encoding
- ✅ Unit tests

## Building

```bash
cd client
mkdir -p build
cd build
cmake ..
make
```

## Running Examples

After building, run the example program:

```bash
./encoder_example
```

This will demonstrate:
1. Single reading encoding
2. Batch encoding (shared mask)
3. Two-channel PM2.5 flags
4. Error handling

## Running Tests

```bash
# Run all tests
ctest --output-on-failure

# Or run individual test executables
./test/test_encoder
./test/test_single_channel
./test/test_dual_channel
./test/test_batching

# Or use the custom target
make run_tests
```

## Quick Start

```cpp
#include "payload_encoder.h"

// Initialize encoder
PayloadEncoder encoder;
PayloadHeader header = {5};  // 5 minute interval (version is fixed in firmware)
encoder.init(header);

// Create a sensor reading
SensorReading reading;
initSensorReading(&reading);

// Add temperature sensor
setFlag(&reading, FLAG_TEMP);
reading.temp = 2500;  // 25.00°C (scaled by 100)

// Add CO2 sensor
setFlag(&reading, FLAG_CO2);
reading.co2 = 412;  // 412 ppm

// Add reading to encoder
encoder.addReading(reading);

// Encode to buffer
uint8_t buffer[256];
int32_t size = encoder.encode(buffer, sizeof(buffer));

if (size > 0) {
    // Successfully encoded, transmit buffer[0..size-1]
    transmit_over_cellular(buffer, size);
}
```

## API Reference

### PayloadEncoder Methods

#### `void init(const PayloadHeader& header)`
Initialize encoder with header configuration.

#### `bool addReading(const SensorReading& reading)`
Add a sensor reading to the batch. Returns `false` if batch is full.

#### `int32_t encode(uint8_t* buffer, uint32_t buffer_size)`
Encode all readings to buffer. Returns bytes written, or `-1` on error.

#### `void reset()`
Clear all readings and reset encoder.

#### `uint8_t getReadingCount() const`
Get current number of readings in batch.

#### `uint32_t calculateTotalSize() const`
Calculate total bytes needed for encoding current batch.

### Helper Functions

```cpp
// Initialize a reading (clear all flags)
void initSensorReading(SensorReading* reading);

// Set a flag in presence mask
void setFlag(SensorReading* reading, SensorFlag flag);

// Check if a flag is set
bool isFlagSet(const SensorReading* reading, SensorFlag flag);
```

## Scaling Factors

When setting sensor values, apply these scaling factors:

| Sensor | Scale | Example |
|--------|-------|---------|
| Temperature | × 100 | 25.50°C → 2550 |
| Humidity | × 100 | 65.25% → 6525 |
| CO2 | × 1 | 412 ppm → 412 |
| PM sensors | × 10 | 12.5 µg/m³ → 125 |
| Battery voltage | × 100 | 3700 mV → 3700 |
| O3/NO2 electrodes | × 1000 | 1.234 mV → 1234 |

## Files

- `src/payload_types.h` - Type definitions and constants
- `src/payload_encoder.h` - Encoder class declaration
- `src/payload_encoder.cpp` - Encoder implementation

## License

MIT
