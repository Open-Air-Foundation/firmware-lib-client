set(srcs
  "src/airgradientClient.cpp"
  "src/airgradientCellularClient.cpp"
  "src/airgradientWifiClient.cpp"
  "src/endpointSelector.cpp"
  "src/atCommandHandler.cpp"
  "src/cellularModule.cpp"
  "src/cellularModuleA7672xx.cpp"

  # CoAP
  "src/coap-packet-cpp/src/CoapBuilder.cpp"
  "src/coap-packet-cpp/src/CoapParser.cpp"

  # Payload Encoder
  "src/payload-encoder/src/PayloadEncoder.cpp"
)

idf_component_register(SRCS "${srcs}"
                    INCLUDE_DIRS "src"
                    REQUIRES esp_timer AirgradientSerial esp_driver_gpio esp_http_client arduinojson
                    )
