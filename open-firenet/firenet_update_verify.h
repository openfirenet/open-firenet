// Checks a firmware download against the release signature, with no network and no crypto library of its own:
// the Ed25519 check is passed in, so the same code runs in the host tests and on the ESP32 (libsodium).
//
// The release publishes SHA256SUMS and SHA256SUMS.minisig (Minisign, prehashed "ED" mode: Ed25519 over the
// BLAKE2b-512 of the file; the public key line starts with "Ed").
// Trust chain: public key built into the firmware -> signature of SHA256SUMS -> SHA-256 of the .bin -> the .bin.
#pragma once
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace firenet {
namespace upd {

// Base64 (standard alphabet, padding optional). Returns false on any other character.
inline bool b64decode(const std::string& in, std::vector<uint8_t>& out) {
  out.clear();
  uint32_t acc = 0; int bits = 0;
  for (char c : in) {
    int v;
    if (c >= 'A' && c <= 'Z') v = c - 'A';
    else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
    else if (c >= '0' && c <= '9') v = c - '0' + 52;
    else if (c == '+') v = 62;
    else if (c == '/') v = 63;
    else if (c == '=') break;
    else if (c == '\r' || c == '\n' || c == ' ') continue;
    else return false;
    acc = (acc << 6) | (uint32_t)v; bits += 6;
    if (bits >= 8) { bits -= 8; out.push_back((uint8_t)((acc >> bits) & 0xff)); }
  }
  return true;
}

struct PublicKey { uint8_t keyId[8]; uint8_t key[32]; };
struct Signature { uint8_t keyId[8]; uint8_t sig[64]; std::string trusted; uint8_t globalSig[64]; };

// The base64 line of a .pub file: "Ed" + 8 bytes key id + 32 bytes key.
inline bool parsePublicKey(const std::string& b64, PublicKey& pk) {
  std::vector<uint8_t> raw;
  if (!b64decode(b64, raw) || raw.size() != 42 || raw[0] != 'E' || raw[1] != 'd') return false;
  memcpy(pk.keyId, &raw[2], 8); memcpy(pk.key, &raw[10], 32);
  return true;
}

// A .minisig file: "untrusted comment: ...", base64 of "ED" + key id + 64 bytes signature, "trusted comment: ...",
// base64 of the 64-byte signature of (signature + trusted comment).
// Only the prehashed mode ("ED", what minisign -S writes) is accepted; the raw mode ("Ed") is refused.
inline bool parseSignature(const std::string& file, Signature& sg) {
  std::vector<std::string> L; size_t pos = 0;
  while (pos <= file.size() && L.size() < 5) {
    size_t e = file.find('\n', pos);
    std::string l = file.substr(pos, e == std::string::npos ? std::string::npos : e - pos);
    if (!l.empty() && l.back() == '\r') l.pop_back();
    L.push_back(l);
    if (e == std::string::npos) break;
    pos = e + 1;
  }
  if (L.size() < 2) return false;
  std::vector<uint8_t> raw;
  if (!b64decode(L[1], raw) || raw.size() != 74 || raw[0] != 'E' || raw[1] != 'D') return false;
  memcpy(sg.keyId, &raw[2], 8); memcpy(sg.sig, &raw[10], 64);
  sg.trusted.clear();
  static const char TC[] = "trusted comment: ";
  if (L.size() >= 4 && L[2].compare(0, sizeof TC - 1, TC) == 0) {
    std::vector<uint8_t> g;
    if (b64decode(L[3], g) && g.size() == 64) { sg.trusted = L[2].substr(sizeof TC - 1); memcpy(sg.globalSig, g.data(), 64); }
  }
  return true;
}

// BLAKE2b-512 of (msg, len) into out[64]; Ed25519 verification of (msg, len, sig64, key32) -> valid.
using Blake2b512Fn = std::function<void(const uint8_t*, size_t, uint8_t*)>;
using Ed25519Fn = std::function<bool(const uint8_t*, size_t, const uint8_t*, const uint8_t*)>;

inline bool verifySums(const std::string& sums, const std::string& minisig, const std::string& pubB64, Blake2b512Fn hash,
                       Ed25519Fn ed) {
  PublicKey pk; Signature sg;
  if (!parsePublicKey(pubB64, pk) || !parseSignature(minisig, sg)) return false;
  if (memcmp(pk.keyId, sg.keyId, 8) != 0) return false;   // signed by another key
  uint8_t digest[64];
  hash((const uint8_t*)sums.data(), sums.size(), digest);
  return ed(digest, sizeof digest, sg.sig, pk.key);
}

// verifySums, and also the trusted comment: its signature must hold too. On success `comment` is that text.
inline bool verifyRelease(const std::string& sums, const std::string& minisig, const std::string& pubB64,
                          Blake2b512Fn hash, Ed25519Fn ed, std::string& comment) {
  comment.clear();
  PublicKey pk; Signature sg;
  if (!verifySums(sums, minisig, pubB64, hash, ed)) return false;
  if (!parsePublicKey(pubB64, pk) || !parseSignature(minisig, sg) || sg.trusted.empty()) return false;
  std::string msg((const char*)sg.sig, 64);
  msg += sg.trusted;
  if (!ed((const uint8_t*)msg.data(), msg.size(), sg.globalSig, pk.key)) return false;
  comment = sg.trusted;
  return true;
}

// SHA-256 (64 lowercase hex) listed for fileName in a SHA256SUMS text ("<hex>  <name>" per line); empty if absent
// or malformed.
inline std::string hashFor(const std::string& sums, const std::string& fileName) {
  size_t pos = 0;
  while (pos < sums.size()) {
    size_t e = sums.find('\n', pos);
    std::string line = sums.substr(pos, e == std::string::npos ? std::string::npos : e - pos);
    pos = e == std::string::npos ? sums.size() : e + 1;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.size() < 67 || line[64] != ' ') continue;
    size_t n = line.find_first_not_of(' ', 64);
    if (n == std::string::npos || line.substr(n) != fileName) continue;
    std::string h = line.substr(0, 64);
    for (char c : h) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return std::string();
    return h;
  }
  return std::string();
}

}  // namespace upd
}  // namespace firenet
