// ESP32 side of the release-signature check: BLAKE2b-512 and Ed25519 from the libsodium bundled with the core.
#pragma once
#include <sodium.h>
#include "firenet_update_verify.h"

namespace firenet {
namespace upd {
inline bool verifySumsEsp(const std::string& sums, const std::string& minisig, const std::string& pubB64) {
  return verifySums(sums, minisig, pubB64,
    [](const uint8_t* m, size_t n, uint8_t* out) { crypto_generichash(out, 64, m, n, nullptr, 0); },
    [](const uint8_t* m, size_t n, const uint8_t* s, const uint8_t* k) { return crypto_sign_verify_detached(s, m, n, k) == 0; });
}
}  // namespace upd
}  // namespace firenet
