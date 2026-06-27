cmake_minimum_required(VERSION 3.10)
project(AirGradientPayloadEncoder VERSION 1.0.0 LANGUAGES C CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Compiler flags for embedded compatibility
if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Source files
set(ENCODER_SOURCES
    src/payload_encoder.cpp
)

set(ENCODER_HEADERS
    src/payload_types.h
    src/payload_encoder.h
)

# Library target
add_library(payload_encoder STATIC ${ENCODER_SOURCES})
target_include_directories(payload_encoder PUBLIC src)

# Installation (optional)
install(TARGETS payload_encoder
    ARCHIVE DESTINATION lib
)

install(FILES ${ENCODER_HEADERS}
    DESTINATION include/airgradient
)
