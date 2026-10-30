// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
// libc++ control probe: strings, vectors, iostream, exceptions off.
#include <cstdio>
#include <string>
#include <vector>
#include <map>
extern "C" void v8hello_debug(const char*);
int main() {
  std::vector<std::string> v{"a", "bb", "ccc"};
  std::map<std::string, int> m;
  for (auto& s : v) m[s] = (int)s.size();
  std::string out = "[CXXPROBE] sizes=" + std::to_string(m["a"]) + std::to_string(m["bb"]) + std::to_string(m["ccc"]);
  std::printf("%s\n", out.c_str());
  std::printf("[CXXPROBE] result=PASS\n");
  v8hello_debug("[CXXPROBE] result=PASS\n");
  return 0;
}
