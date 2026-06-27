/**
 * AirGradient
 * https://airgradient.com
 *
 * CC BY-SA 4.0 Attribution-ShareAlike 4.0 International License
 */

#ifndef ENDPOINT_SELECTOR_H
#define ENDPOINT_SELECTOR_H

#ifndef ESP8266
#ifdef ARDUINO

#include <Arduino.h>
#include <IPAddress.h>

/**
 * EndpointSelector
 *
 * Owns a small list of resolved IPv4 A records for a single hostname and
 * exposes a sticky cursor over them. Selection semantics:
 *  - On begin(): resolve all unique IPs, random shuffle, point cursor at
 *    index 0.
 *  - current() returns the IP the cursor points at; stays put across
 *    successful requests (sticky-per-boot behavior).
 *  - advance() moves the cursor to the next IP, wrapping at the end.
 *    The caller drives the failover loop and decides when to stop.
 *  - maybeRefresh() re-resolves DNS on a long interval (default 1h),
 *    preserving the currently-active IP if it is still present in the
 *    new list.
 *
 * The selector intentionally has no notion of "failure threshold" or
 * "exhaustion" - the caller is expected to attempt every IP at most once
 * per request and then fall back to hostname-based resolution.
 *
 * Multi-A-record resolution is performed by sending a single raw UDP
 * DNS query (type A) directly to the system's configured DNS server
 * (WiFi.dnsIP(0)) and parsing every A record from the answer section.
 * This sidesteps lwIP's single-IP-per-lookup limitation - one query,
 * all answers visible. Falls back to WiFi.hostByName() only if the raw
 * UDP path fails entirely (e.g. UDP socket failure, DNS server
 * unreachable, malformed response).
 */
class EndpointSelector {
public:
  static constexpr uint8_t MAX_IPS = 8;
  static constexpr uint32_t REFRESH_INTERVAL_MS = 60UL * 60UL * 1000UL; // 1 hour

  EndpointSelector() = default;
  ~EndpointSelector() = default;

  /**
   * Resolve `hostname` and shuffle the resulting IP list.
   * @return true on success (at least one IP resolved), false otherwise.
   */
  bool begin(const char *hostname);

  /**
   * Currently selected IP. Returns 0.0.0.0 if no IPs available.
   */
  IPAddress current() const;

  /**
   * Hostname used for resolution and SNI/Host header. Returns the
   * empty string if begin() has never succeeded.
   */
  const char *hostname() const { return hostname_; }

  /** Number of unique IPs currently held. */
  uint8_t count() const { return count_; }

  /**
   * Advance the cursor to the next IP in the shuffled list, wrapping
   * around to index 0 after the last entry. No-op if count() == 0.
   */
  void advance();

  /**
   * Re-resolve at most once every REFRESH_INTERVAL_MS. The active IP is
   * preserved if it still appears in the new list.
   * @return true if a refresh actually happened.
   */
  bool maybeRefresh(uint32_t nowMs);

  /** Force an immediate re-resolve. */
  bool refresh();

private:
  char hostname_[64] = {0};
  IPAddress ips_[MAX_IPS];
  uint8_t count_ = 0;
  uint8_t currentIdx_ = 0;
  uint32_t lastRefreshMs_ = 0;

  /** Multi-A-record resolution via raw UDP DNS query. */
  uint8_t resolveAll_(IPAddress *out, uint8_t maxOut);

  // Raw DNS helpers (RFC 1035). All offsets validated against bufLen.
  static size_t buildQuery_(const char *hostname, uint8_t *out, size_t outMax,
                            uint16_t &txnIdOut);
  static size_t encodeName_(const char *hostname, uint8_t *out, size_t outMax);
  static size_t skipName_(const uint8_t *buf, size_t bufLen, size_t offset);
  static uint8_t parseAnswers_(const uint8_t *buf, size_t bufLen,
                               uint16_t expectedTxnId, IPAddress *out, uint8_t maxOut);

  bool resolveAndStore_();
  void shuffle_();
  void logState_(const char *header) const;
};

#endif // ARDUINO
#endif // ESP8266
#endif // ENDPOINT_SELECTOR_H
