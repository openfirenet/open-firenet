// Host test of the release-signature plumbing (firenet_update_verify.h) on the real v4.3.1 files.
// BLAKE2b and Ed25519 are replaced by checks on their inputs; the same bytes were verified for real with
// Python (blake2b + cryptography Ed25519) when this test was written.
#include "firenet_update_verify.h"
#include "firenet_update_engine.h"
#include <cstdio>
using namespace firenet::upd;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
static std::string hex(const uint8_t* p, size_t n) { std::string o; char t[3]; for (size_t i = 0; i < n; i++) { snprintf(t, 3, "%02x", p[i]); o += t; } return o; }
static const char* SUMS = "c69b3177d713644b833a28038474e54a6867da5325080b1284f979467ba6c2a1  open-firenet-factory.bin\n73faf2d8136e59627391e4638e8e94496c8cba97fde28ce77db21f7299c95265  open-firenet-ota.bin\n5f95cbc8b40b7a373f9cb85b534cb7a570cdad30ca27c7021775d830ca03276b  open-firenet-bootloader.bin\n0a8b5720e7b77ff11f1462458c3a509dee79224e5279898f26d6a2e3ae0517b7  open-firenet-partitions.bin\n";
static const char* SIG = "untrusted comment: signature from minisign secret key\nRUQhzS5dR0kodPSvY9gW2n+EdpPEG72Z9JxV0SWV33vEuP+HQ2L2Tt7O1G3Ky7eAqv1zamLumLh6nfZfMgey9TNBny2d4d2kTw8=\ntrusted comment: Release v4.3.1\n5U9i6ksp1zJlTwp6788BGpOPA8RMI/ax1Q8PksV/aUgrl1wnE+Uw84sc/1rxisal1BP2mieZCnEqb3i4X+g6DQ==\n";
static const char* PUB = "RWQhzS5dR0kodMCMiFMESBDRDhxMyRaA4yNbIcAMD2B9kQS21I+FABT4";
static const std::string DIGEST = "2d687d1b51e47d794821a3ebd48069d751a8ad529470c8e2b59097dccb8e6f89bb47d7cc7ff53f8604be1955d32086639084e6c082d2433957ef15f258208550";
int main() {
  std::string sums = SUMS;
  bool hashed = false;
  auto hash = [&](const uint8_t* m, size_t n, uint8_t* out) {
    hashed = (std::string((const char*)m, n) == sums);
    for (int i = 0; i < 64; i++) { unsigned v; sscanf(DIGEST.c_str() + 2 * i, "%2x", &v); out[i] = (uint8_t)v; }
  };
  std::string seenSig, seenKey, seenMsg;
  auto ed = [&](const uint8_t* m, size_t n, const uint8_t* s, const uint8_t* k) {
    seenMsg = hex(m, n); seenSig = hex(s, 64); seenKey = hex(k, 32); return true; };
  CHECK(verifySums(sums, SIG, PUB, hash, ed));
  CHECK(hashed);
  CHECK(seenMsg == DIGEST);
  CHECK(seenSig == "f4af63d816da7f847693c41bbd99f49c55d12595df7bc4b8ff874362f64edeced46dcacbb780aafd736a62ee98b87a9df65f3207b2f533419f2d9de1dda44f0f");
  CHECK(seenKey == "c08c8853044810d10e1c4cc91680e3235b21c00c0f607d9104b6d48f850014f8");
  // A failing Ed25519 check fails the whole check.
  CHECK(!verifySums(sums, SIG, PUB, hash, [](const uint8_t*, size_t, const uint8_t*, const uint8_t*) { return false; }));
  // Another key id, a raw-mode ("Ed") signature, truncated or garbled inputs are all refused.
  std::string otherPub = PUB; otherPub[10] = otherPub[10] == 'A' ? 'B' : 'A';
  CHECK(!verifySums(sums, SIG, otherPub, hash, ed));
  std::string rawSig = SIG; rawSig[rawSig.find('\n') + 2] = 'W';   // "RU.." -> "RW..": 'E','d' instead of 'E','D'
  CHECK(!verifySums(sums, rawSig, PUB, hash, ed));
  CHECK(!verifySums(sums, "untrusted comment: x\n", PUB, hash, ed));
  CHECK(!verifySums(sums, "", PUB, hash, ed));
  CHECK(!verifySums(sums, SIG, "", hash, ed));
  CHECK(!verifySums(sums, SIG, "not base64!", hash, ed));
  // Hash lookup.
  CHECK(hashFor(sums, "open-firenet-ota.bin") == "73faf2d8136e59627391e4638e8e94496c8cba97fde28ce77db21f7299c95265");
  CHECK(hashFor(sums, "open-firenet-partitions.bin") == "0a8b5720e7b77ff11f1462458c3a509dee79224e5279898f26d6a2e3ae0517b7");
  CHECK(hashFor(sums, "ota.bin").empty());
  CHECK(hashFor(sums, "").empty());
  CHECK(hashFor("zz" + sums.substr(2), "open-firenet-factory.bin").empty());
  CHECK(hashFor(sums, "open-firenet-ota.bin ").empty());
  CHECK(hashFor("73faf2d8  open-firenet-ota.bin\n", "open-firenet-ota.bin").empty());
  // Trusted comment (version) and its own signature.
  {
    std::string seen;
    auto ed2 = [&](const uint8_t* m, size_t n, const uint8_t*, const uint8_t*) { if (n != 64) seen = std::string((const char*)m + 64, n - 64); return true; };
    std::string comment;
    CHECK(verifyRelease(sums, SIG, PUB, hash, ed2, comment));
    CHECK(comment == "Release v4.3.1");
    CHECK(seen == "Release v4.3.1");   // signed message = 64 signature bytes + the comment text
    CHECK(versionFromComment(comment).ok && versionFromComment(comment).patch == 1);
    // A bad global signature (altered comment) refuses the release.
    std::string altered = SIG; size_t at = altered.find("Release v4.3.1"); altered.replace(at, 14, "Release v9.9.9");
    int calls = 0;
    auto edGlobalFails = [&](const uint8_t*, size_t n, const uint8_t*, const uint8_t*) { calls++; return n == 64; };
    CHECK(!verifyRelease(sums, altered, PUB, hash, edGlobalFails, comment));
    CHECK(comment.empty() && calls == 2);
    // No trusted comment at all: refused.
    CHECK(!verifyRelease(sums, std::string(SIG).substr(0, std::string(SIG).find("trusted comment")), PUB, hash, ed, comment));
  }
  // Versions.
  CHECK(parseVersion("4.3.1").ok && parseVersion("v4.3.1").ok);
  CHECK(!parseVersion("4.3").ok && !parseVersion("4.3.1-rc1").ok && !parseVersion("").ok && !parseVersion("4.3.1.2").ok);
  CHECK(!parseVersion("a.b.c").ok && !parseVersion("4..1").ok && !parseVersion("v").ok);
  CHECK(!versionFromComment("Release 4.3.1").ok && !versionFromComment("release v4.3.1").ok && !versionFromComment("Release v4.3.1 extra").ok);
  CHECK(isNewer(parseVersion("4.3.2"), parseVersion("4.3.1")));
  CHECK(isNewer(parseVersion("4.10.0"), parseVersion("4.9.9")));
  CHECK(isNewer(parseVersion("5.0.0"), parseVersion("4.99.99")));
  CHECK(!isNewer(parseVersion("4.3.1"), parseVersion("4.3.1")));   // same: no update
  CHECK(!isNewer(parseVersion("4.3.0"), parseVersion("4.3.1")));   // older: refused (downgrade)
  CHECK(!isNewer(parseVersion("3.99.99"), parseVersion("4.0.0")));
  CHECK(!isNewer(Version(), parseVersion("4.3.1")) && !isNewer(parseVersion("4.3.2"), Version()));
  // Stove state.
  CHECK(stoveAllowsUpdate("off"));
  CHECK(!stoveAllowsUpdate("standby") && !stoveAllowsUpdate("heating") && !stoveAllowsUpdate("ignition"));
  CHECK(!stoveAllowsUpdate("unknown") && !stoveAllowsUpdate("") && !stoveAllowsUpdate("splitlog_check"));
  if (fails) { printf("%d failed\n", fails); return 1; }
  printf("update verify: all checks passed\n");
  return 0;
}
