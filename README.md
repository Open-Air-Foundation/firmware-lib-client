# AirGradient Client

Client library to communication with the AirGradient backend through WiFi or Cellular

## MQTT URI rules

`mqttConnect(uri)` accepts `[mqtt://][username[:password]@]host[:port]`.

- **Length:** At most 165 ASCII characters, excluding `mqtt://`.
- **Scheme:** Optional; only `mqtt://` is supported.
- **Credentials:** Optional, with exactly one `@` separator and at most one `:`.
  Username-only authentication is allowed; explicitly empty credentials are rejected.
  Characters: ASCII letters, digits, and `-._~!$&'()*+,;=`. No percent encoding or decoding.
- **Host:** Required; ASCII letters, digits, dots and hyphens only. Resolution is left to the network stack.
- **Port:** Optional (default `1883`); digits only, value 1–65535. An explicitly empty port is rejected.
- **Unsupported:** TLS, IPv6, whitespace, paths, queries and fragments.

Invalid input returns failure before connecting. Example: `mqtt://user:password@broker.example.com:1883`.
