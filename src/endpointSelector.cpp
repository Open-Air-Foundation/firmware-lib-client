/**
 * AirGradient
 * https://airgradient.com
 *
 * CC BY-SA 4.0 Attribution-ShareAlike 4.0 International License
 */

#ifndef ESP8266
#ifdef ARDUINO

#include "endpointSelector.h"

#include <WiFi.h>
#include <WiFiUdp.h>
#include <esp_random.h>
#include <string.h>

#include "agLogger.h"
#include "common.h"

static const char *const TAG = "AgEpSel";

namespace {

constexpr uint16_t DNS_PORT = 53;
constexpr uint32_t DNS_QUERY_TIMEOUT_MS = 5000;
constexpr size_t DNS_MAX_PACKET_SIZE = 512;  // standard UDP DNS
constexpr uint16_t DNS_TYPE_A = 1;
constexpr uint16_t DNS_CLASS_IN = 1;
constexpr uint16_t DNS_TYPE_CNAME = 5;

inline uint16_t rd16_(const uint8_t *buf, size_t off) {
  return (static_cast<uint16_t>(buf[off]) << 8) | buf[off + 1];
}

}  // namespace

bool EndpointSelector::begin(const char *hostname) {
  if (hostname == nullptr || hostname[0] == '\0') {
    return false;
  }
  strncpy(hostname_, hostname, sizeof(hostname_) - 1);
  hostname_[sizeof(hostname_) - 1] = '\0';

  if (!resolveAndStore_()) {
    return false;
  }
  shuffle_();
  currentIdx_ = 0;
  lastRefreshMs_ = MILLIS();
  logState_("ready");
  return true;
}

IPAddress EndpointSelector::current() const {
  if (count_ == 0) {
    return IPAddress(static_cast<uint32_t>(0));
  }
  return ips_[currentIdx_];
}

void EndpointSelector::advance() {
  if (count_ == 0) {
    return;
  }
  uint8_t prevIdx = currentIdx_;
  currentIdx_ = (currentIdx_ + 1) % count_;
  AG_LOGW(TAG, "Advance %s -> %s (idx %u -> %u)",
          ips_[prevIdx].toString().c_str(),
          ips_[currentIdx_].toString().c_str(), prevIdx, currentIdx_);
}

bool EndpointSelector::maybeRefresh(uint32_t nowMs) {
  if (count_ == 0) {
    // Never initialized or last refresh failed; let begin()/refresh() run.
    return false;
  }
  if ((nowMs - lastRefreshMs_) < REFRESH_INTERVAL_MS) {
    return false;
  }
  return refresh();
}

bool EndpointSelector::refresh() {
  AG_LOGI(TAG, "Refreshing DNS list for %s", hostname_);
  IPAddress active = current();

  // Save the old list so we can restore on resolution failure.
  uint8_t oldCount = count_;
  IPAddress oldIps[MAX_IPS];
  for (uint8_t i = 0; i < oldCount; i++) {
    oldIps[i] = ips_[i];
  }

  if (!resolveAndStore_()) {
    AG_LOGW(TAG, "Refresh failed; keeping previous list");
    count_ = oldCount;
    for (uint8_t i = 0; i < oldCount; i++) {
      ips_[i] = oldIps[i];
    }
    lastRefreshMs_ = MILLIS();
    return false;
  }

  shuffle_();

  // Try to keep the previously-active IP if it is still present.
  bool stillThere = false;
  if ((uint32_t)active != 0) {
    for (uint8_t i = 0; i < count_; i++) {
      if (ips_[i] == active) {
        if (i != 0) {
          IPAddress tmp = ips_[0];
          ips_[0] = ips_[i];
          ips_[i] = tmp;
        }
        stillThere = true;
        break;
      }
    }
  }
  currentIdx_ = 0;
  if (!stillThere && (uint32_t)active != 0) {
    AG_LOGW(TAG, "Previously active IP %s no longer in DNS",
            active.toString().c_str());
  }
  lastRefreshMs_ = MILLIS();
  logState_("after-refresh");
  return true;
}

/* ---------- private helpers ---------- */

uint8_t EndpointSelector::resolveAll_(IPAddress *out, uint8_t maxOut) {
  if (out == nullptr || maxOut == 0) {
    return 0;
  }

  IPAddress dnsServer = WiFi.dnsIP(0);
  if (static_cast<uint32_t>(dnsServer) == 0) {
    AG_LOGW(TAG, "No DNS server configured; falling back to hostByName");
    IPAddress ip;
    if (WiFi.hostByName(hostname_, ip)) {
      out[0] = ip;
      return 1;
    }
    return 0;
  }
  AG_LOGI(TAG, "Querying DNS %s for %s (A)", dnsServer.toString().c_str(), hostname_);

  // ---- Build query ----
  uint8_t query[DNS_MAX_PACKET_SIZE];
  uint16_t txnId = 0;
  size_t queryLen = buildQuery_(hostname_, query, sizeof(query), txnId);
  if (queryLen == 0) {
    AG_LOGE(TAG, "DNS query build failed");
    return 0;
  }

  // ---- Send + receive ----
  WiFiUDP udp;
  if (!udp.begin(0)) {  // 0 = any local port
    AG_LOGW(TAG, "UDP begin failed; falling back to hostByName");
    IPAddress ip;
    if (WiFi.hostByName(hostname_, ip)) {
      out[0] = ip;
      return 1;
    }
    return 0;
  }

  bool sent = udp.beginPacket(dnsServer, DNS_PORT);
  if (sent) {
    udp.write(query, queryLen);
    sent = udp.endPacket();
  }
  if (!sent) {
    AG_LOGW(TAG, "DNS UDP send failed");
    udp.stop();
    IPAddress ip;
    if (WiFi.hostByName(hostname_, ip)) {
      out[0] = ip;
      return 1;
    }
    return 0;
  }

  uint32_t deadline = MILLIS() + DNS_QUERY_TIMEOUT_MS;
  int pktSize = 0;
  while ((pktSize = udp.parsePacket()) == 0) {
    if (static_cast<int32_t>(MILLIS() - deadline) >= 0) {
      AG_LOGW(TAG, "DNS response timeout");
      udp.stop();
      IPAddress ip;
      if (WiFi.hostByName(hostname_, ip)) {
        out[0] = ip;
        return 1;
      }
      return 0;
    }
    DELAY_MS(5);
  }

  uint8_t response[DNS_MAX_PACKET_SIZE];
  int respLen = udp.read(response, sizeof(response));
  udp.stop();

  if (respLen <= 0) {
    AG_LOGW(TAG, "DNS UDP read returned no data");
    return 0;
  }
  AG_LOGI(TAG, "DNS response %d bytes", respLen);

  uint8_t count =
      parseAnswers_(response, static_cast<size_t>(respLen), txnId, out, maxOut);
  if (count == 0) {
    AG_LOGW(TAG, "Raw DNS parse found 0 A records; falling back to hostByName");
    IPAddress ip;
    if (WiFi.hostByName(hostname_, ip)) {
      out[0] = ip;
      return 1;
    }
    return 0;
  }
  return count;
}

/* ---------- Raw DNS helpers (RFC 1035) ---------- */

size_t EndpointSelector::buildQuery_(const char *hostname, uint8_t *out, size_t outMax,
                                     uint16_t &txnIdOut) {
  if (outMax < 12 + 5) return 0;  // header + minimal question

  uint16_t txnId = static_cast<uint16_t>(esp_random() & 0xFFFF);
  txnIdOut = txnId;

  // Header (12 bytes)
  out[0] = (txnId >> 8) & 0xFF;
  out[1] = txnId & 0xFF;
  out[2] = 0x01;  // flags: standard query, RD=1
  out[3] = 0x00;
  out[4] = 0x00;
  out[5] = 0x01;  // QDCOUNT = 1
  out[6] = 0x00;
  out[7] = 0x00;  // ANCOUNT = 0
  out[8] = 0x00;
  out[9] = 0x00;  // NSCOUNT = 0
  out[10] = 0x00;
  out[11] = 0x00;  // ARCOUNT = 0
  size_t pos = 12;

  size_t nameLen = encodeName_(hostname, out + pos, outMax - pos);
  if (nameLen == 0) return 0;
  pos += nameLen;

  if (pos + 4 > outMax) return 0;
  out[pos++] = 0x00;
  out[pos++] = 0x01;  // QTYPE = A
  out[pos++] = 0x00;
  out[pos++] = 0x01;  // QCLASS = IN

  return pos;
}

size_t EndpointSelector::encodeName_(const char *hostname, uint8_t *out, size_t outMax) {
  size_t pos = 0;
  const char *s = hostname;
  while (*s != '\0') {
    const char *dot = strchr(s, '.');
    size_t labelLen = dot ? static_cast<size_t>(dot - s) : strlen(s);
    if (labelLen == 0 || labelLen > 63) return 0;
    if (pos + 1 + labelLen + 1 > outMax) return 0;  // length + label + trailing root
    out[pos++] = static_cast<uint8_t>(labelLen);
    memcpy(out + pos, s, labelLen);
    pos += labelLen;
    s += labelLen;
    if (*s == '.') s++;
  }
  if (pos + 1 > outMax) return 0;
  out[pos++] = 0;  // root label
  return pos;
}

size_t EndpointSelector::skipName_(const uint8_t *buf, size_t bufLen, size_t offset) {
  // Names are either a sequence of <len><label> terminated by 0x00, or
  // end with a 2-byte compression pointer (top two bits == 11).
  uint8_t hops = 0;
  while (offset < bufLen) {
    uint8_t b = buf[offset];
    if (b == 0) {
      return offset + 1;
    }
    if ((b & 0xC0) == 0xC0) {
      if (offset + 2 > bufLen) return 0;
      return offset + 2;
    }
    if ((b & 0xC0) != 0) {
      return 0;  // reserved top bits, invalid
    }
    if (b > 63) return 0;
    offset += 1 + b;
    if (++hops > 64) return 0;  // sanity bound on label count
  }
  return 0;
}

uint8_t EndpointSelector::parseAnswers_(const uint8_t *buf, size_t bufLen,
                                        uint16_t expectedTxnId, IPAddress *out,
                                        uint8_t maxOut) {
  if (bufLen < 12) {
    AG_LOGW(TAG, "DNS response too short for header");
    return 0;
  }

  uint16_t txnId = rd16_(buf, 0);
  if (txnId != expectedTxnId) {
    AG_LOGW(TAG, "DNS txnId mismatch: got 0x%04X expected 0x%04X", txnId, expectedTxnId);
    return 0;
  }

  uint16_t flags = rd16_(buf, 2);
  if ((flags & 0x8000) == 0) {
    AG_LOGW(TAG, "DNS: not a response (QR=0)");
    return 0;
  }
  uint8_t rcode = flags & 0x000F;
  if (rcode != 0) {
    AG_LOGW(TAG, "DNS response rcode=%u", rcode);
    return 0;
  }
  bool truncated = (flags & 0x0200) != 0;
  if (truncated) {
    AG_LOGW(TAG, "DNS response truncated (TC=1); using what fits");
  }

  uint16_t qdcount = rd16_(buf, 4);
  uint16_t ancount = rd16_(buf, 6);
  AG_LOGI(TAG, "DNS qd=%u an=%u%s", qdcount, ancount, truncated ? " (trunc)" : "");

  if (ancount == 0) {
    return 0;
  }

  size_t offset = 12;

  // Skip question section
  for (uint16_t i = 0; i < qdcount; i++) {
    offset = skipName_(buf, bufLen, offset);
    if (offset == 0 || offset + 4 > bufLen) {
      AG_LOGW(TAG, "DNS: malformed question");
      return 0;
    }
    offset += 4;  // QTYPE + QCLASS
  }

  // Parse answers
  uint8_t count = 0;
  uint16_t cnameCount = 0;
  uint16_t otherCount = 0;
  for (uint16_t i = 0; i < ancount && count < maxOut; i++) {
    offset = skipName_(buf, bufLen, offset);
    if (offset == 0 || offset + 10 > bufLen) {
      AG_LOGW(TAG, "DNS: malformed answer");
      return count;
    }
    uint16_t type = rd16_(buf, offset);
    uint16_t cls = rd16_(buf, offset + 2);
    // bytes [4..7] = TTL, not needed
    uint16_t rdlen = rd16_(buf, offset + 8);
    offset += 10;
    if (offset + rdlen > bufLen) {
      AG_LOGW(TAG, "DNS: rdlen overflow");
      return count;
    }

    if (type == DNS_TYPE_A && cls == DNS_CLASS_IN && rdlen == 4) {
      IPAddress ip(buf[offset], buf[offset + 1], buf[offset + 2], buf[offset + 3]);
      bool dup = false;
      for (uint8_t j = 0; j < count; j++) {
        if (out[j] == ip) {
          dup = true;
          break;
        }
      }
      if (!dup) {
        out[count++] = ip;
      }
    } else if (type == DNS_TYPE_CNAME) {
      cnameCount++;
    } else {
      otherCount++;
    }
    offset += rdlen;
  }

  if (cnameCount > 0 || otherCount > 0) {
    AG_LOGI(TAG, "DNS skipped: cname=%u other=%u", cnameCount, otherCount);
  }
  return count;
}

bool EndpointSelector::resolveAndStore_() {
  IPAddress tmp[MAX_IPS];
  uint8_t n = resolveAll_(tmp, MAX_IPS);
  if (n == 0) {
    return false;
  }
  count_ = n;
  for (uint8_t i = 0; i < n; i++) {
    ips_[i] = tmp[i];
  }
  AG_LOGI(TAG, "Resolved %u unique IP(s) for %s", n, hostname_);
  for (uint8_t i = 0; i < n; i++) {
    AG_LOGI(TAG, "  %s", ips_[i].toString().c_str());
  }
  return true;
}

void EndpointSelector::shuffle_() {
  if (count_ <= 1) {
    return;
  }
  // Fisher-Yates using ESP32 hardware RNG.
  for (uint8_t i = count_ - 1; i > 0; --i) {
    uint32_t r = esp_random() % (static_cast<uint32_t>(i) + 1U);
    if (r != i) {
      IPAddress tmp = ips_[i];
      ips_[i] = ips_[r];
      ips_[r] = tmp;
    }
  }
}

void EndpointSelector::logState_(const char *header) const {
  AG_LOGI(TAG, "State[%s] host=%s count=%u current=%u(%s)", header, hostname_,
          count_, currentIdx_,
          count_ > 0 ? ips_[currentIdx_].toString().c_str() : "none");
}

#endif // ARDUINO
#endif // ESP8266
