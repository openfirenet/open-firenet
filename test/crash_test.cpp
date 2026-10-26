#include "firenet_crash.h"
#include <iostream>
using namespace firenet;

int main() {
  int ok = 0, ko = 0;
  auto CH = [&](const char* n, bool c) { if (c) ok++; else { ko++; std::cout << "ECHEC " << n << "\n"; } };
  CrashInfo c;
  strcpy(c.task, "loopTask"); c.pc = 0x420123ab; c.cause = 28; c.faultAddress = 0; c.depth = 3; c.bt[0] = 0x420123ab; c.bt[1] = 0x42005678; c.bt[2] = 0x4037abcd;
  strcpy(c.elfSha, "0123456789abcdef");
  CH("crash json", crashJson(c) == "{\"task\":\"loopTask\",\"pc\":\"0x420123ab\",\"cause\":28,\"fault_address\":\"0x00000000\",\"backtrace\":[\"0x420123ab\",\"0x42005678\",\"0x4037abcd\"],\"backtrace_corrupted\":false,\"elf_sha256\":\"0123456789abcdef\"}");
  c.depth = 0; c.corrupted = true;
  CH("empty and corrupted backtrace", crashJson(c).find("\"backtrace\":[],\"backtrace_corrupted\":true") != std::string::npos);
  c.depth = 99;
  CH("depth is capped at 16 entries", crashJson(c).find("\"0x00000000\"]") != std::string::npos);
  strcpy(c.task, "a\"b\\c\n"); strcpy(c.elfSha, "zz1\"g");
  CH("a task name and a SHA are made safe for JSON", crashJson(c).find("\"task\":\"abc\"") != std::string::npos && crashJson(c).find("\"elf_sha256\":\"1\"") != std::string::npos);
  std::cout << "crash_test: " << ok << " ok, " << ko << " ko\n";
  return ko ? 1 : 0;
}
