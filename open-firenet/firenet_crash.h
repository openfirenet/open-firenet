// What the chip left in flash about the last crash (core dump summary), as the JSON of the "crash" object of /api/state > health.
// Arduino-free so that the text can be tested on a computer (test/crash_test.cpp). The addresses are those of the firmware that
// crashed: with the ELF file of that release (its SHA-256 is given) a tool such as addr2line turns them into functions.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace firenet {

struct CrashInfo {
  bool valid = false;
  char task[17] = "";              // name of the task that took the exception
  uint32_t pc = 0, cause = 0, faultAddress = 0;
  uint32_t bt[16] = {0};
  uint32_t depth = 0;
  bool corrupted = false;          // the chip could not follow the stack to its end
  char elfSha[17] = "";            // first characters of the SHA-256 of the firmware that crashed
};

inline std::string crashJson(const CrashInfo& c) {
  char b[128];
  std::string j = "{\"task\":\"";
  for (const char* p = c.task; *p && p < c.task + 16; p++) if (*p >= 0x20 && *p < 0x7f && *p != '"' && *p != '\\') j += *p;   // a task name is plain text
  snprintf(b, sizeof b, "\",\"pc\":\"0x%08x\",\"cause\":%u,\"fault_address\":\"0x%08x\",\"backtrace\":[", (unsigned)c.pc, (unsigned)c.cause, (unsigned)c.faultAddress);
  j += b;
  uint32_t n = c.depth > 16 ? 16 : c.depth;
  for (uint32_t i = 0; i < n; i++) { snprintf(b, sizeof b, "%s\"0x%08x\"", i ? "," : "", (unsigned)c.bt[i]); j += b; }
  j += "],\"backtrace_corrupted\":"; j += c.corrupted ? "true" : "false";
  j += ",\"elf_sha256\":\"";
  for (const char* p = c.elfSha; *p && p < c.elfSha + 16; p++) if ((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f')) j += *p;
  j += "\"}";
  return j;
}

}  // namespace firenet
