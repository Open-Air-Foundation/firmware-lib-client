# AirGradient Client

Client library to communication with the AirGradient backend through WiFi or Cellular

## MQTT URI rules

`mqttConnect(uri)` accepts `[mqtt://][username[:password]@]host[:port]`.

- When credentials are present, exactly one `@` separates them from the host.
  Additional `@` characters are rejected.
- Credentials may contain at most one `:`, separating username and password. Username-only
  authentication and connections without credentials are supported. An explicitly empty
  username or password is rejected.
- Credential characters are ASCII letters, digits, and `-._~!$&'()*+,;=` only. There is
  no percent encoding or decoding: `%`, `@`, whitespace, `/`, `?`, `#`, double quotes,
  backslashes, control characters, non-ASCII characters and all other punctuation are rejected.
- The host must be nonempty and contain only ASCII letters,
  digits, dots and hyphens. DNS/IPv4 validation is left to the network stack.
  IPv6, paths, queries and fragments are unsupported.
- A supplied port must consist entirely of digits and have a value from 1 to 65535.
  An omitted port defaults to 1883; an explicitly empty port is invalid.
- Only the optional `mqtt://` scheme is accepted. TLS schemes are not supported by this path.
- The URI is limited to 165 ASCII characters, excluding the optional `mqtt://` prefix.
  This includes credentials, separators, host and any supplied port.
- The cellular connection-command buffer accommodates this limit even when `:1883` must
  be added. Oversized commands are rejected before any MQTT commands are sent.

For example, `user:password@my.broker.com:2345` connects to
`my.broker.com:2345` using username `user` and password `password`.
Invalid URI input returns failure and logs `Invalid MQTT URI` without logging credentials.

Call `Common::normalizeMqttUri(uri)` before passing a configured URI to ESP-MQTT.
It validates these rules and prepends `mqtt://` if omitted, leaving invalid input unchanged.
For example, `broker:1883` becomes `mqtt://broker:1883`.
The current cellular path uses parsed connection fields directly; Wi-Fi MQTT is not yet implemented.
