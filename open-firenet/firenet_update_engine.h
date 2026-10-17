// Decisions of the self-update, with no I/O so that they run in the host tests: which version a release is, whether
// it is newer, and whether the stove state allows an installation.
#pragma once
#include <cstdlib>
#include <string>

namespace firenet {
namespace upd {

struct Version { int major = 0, minor = 0, patch = 0; bool ok = false; };

// "4.3.1" or "v4.3.1"; anything else (pre-release suffix, missing part, extra text) is refused.
inline Version parseVersion(const std::string& s) {
  Version v; size_t i = (!s.empty() && s[0] == 'v') ? 1 : 0;
  int* part[3] = {&v.major, &v.minor, &v.patch};
  for (int p = 0; p < 3; p++) {
    size_t st = i;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') i++;
    if (i == st || i - st > 4) return Version();
    *part[p] = atoi(s.substr(st, i - st).c_str());
    if (p < 2) { if (i >= s.size() || s[i] != '.') return Version(); i++; }
  }
  if (i != s.size()) return Version();
  v.ok = true;
  return v;
}

// The version a signed trusted comment announces: it must read exactly "Release vX.Y.Z".
inline Version versionFromComment(const std::string& comment) {
  static const char P[] = "Release ";
  if (comment.compare(0, sizeof P - 1, P) != 0) return Version();
  std::string v = comment.substr(sizeof P - 1);
  if (v.empty() || v[0] != 'v') return Version();
  return parseVersion(v);
}

// True only when `cand` is strictly newer than `cur` (anti-downgrade: equal or older is refused).
inline bool isNewer(const Version& cand, const Version& cur) {
  if (!cand.ok || !cur.ok) return false;
  if (cand.major != cur.major) return cand.major > cur.major;
  if (cand.minor != cur.minor) return cand.minor > cur.minor;
  return cand.patch > cur.patch;
}

// Installing restarts the bridge, so it is only allowed while the stove is switched off. "standby" is a stove that is
// switched on and waits for a heat demand (it can ignite at any time), so it is refused, as is anything unknown.
inline bool stoveAllowsUpdate(const std::string& stateName) { return stateName == "off"; }

}  // namespace upd
}  // namespace firenet
