// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
//
// V8Bench: how much the JIT is worth on AArch64 AROS. Runs the same
// JavaScript workloads in a fresh isolate twice, once with the JIT when the
// kernel offers JIT pages and once --jitless (V8 flags are process-wide, so
// the mode is the argument: "V8Bench jit" or "V8Bench jitless"), and prints
// the milliseconds each workload took, measured inside JavaScript.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libplatform/libplatform.h"
#include "v8-context.h"
#include "v8-exception.h"
#include "v8-initialization.h"
#include "v8-isolate.h"
#include "v8-local-handle.h"
#include "v8-primitive.h"
#include "v8-script.h"

extern "C" void v8hello_debug(const char* line);
extern "C" int aros_v8_jit_mode(void);

static const char kWorkloads[] = R"(
function time(name, f) {
  const t0 = Date.now();
  const r = f();
  const ms = Date.now() - t0;
  return name + " ms=" + ms + " check=" + r;
}
const out = [];
out.push(time("loop", () => {
  let s = 0;
  for (let i = 0; i < 200000000; i++) s = (s + i * 7) % 1000003;
  return s;
}));
out.push(time("objects", () => {
  let total = 0;
  for (let i = 0; i < 3000000; i++) {
    const p = {x: i, y: i * 2, z: [i, i + 1, i + 2]};
    total += p.x + p.y + p.z[2];
  }
  return total % 1000003;
}));
out.push(time("strings", () => {
  let parts = [];
  for (let i = 0; i < 1000000; i++) parts.push("item" + i);
  const joined = parts.join(",");
  return joined.split(",").filter(s => s.endsWith("7")).length;
}));
out.push(time("json", () => {
  const data = [];
  for (let i = 0; i < 20000; i++) data.push({id: i, name: "n" + i, tags: ["a", "b"]});
  let n = 0;
  for (let k = 0; k < 50; k++) n += JSON.parse(JSON.stringify(data)).length;
  return n;
}));
out.join("\n");
)";

int main(int argc, char* argv[]) {
  bool want_jit = argc > 1 && strcmp(argv[1], "jit") == 0;
  int mode = aros_v8_jit_mode();
  if (!want_jit || mode == 0) v8::V8::SetFlagsFromString("--jitless");
  const char* running = (want_jit && mode != 0) ? "jit" : "jitless";

  std::unique_ptr<v8::Platform> platform = v8::platform::NewDefaultPlatform();
  v8::V8::InitializePlatform(platform.get());
  v8::V8::Initialize();
  v8::Isolate::CreateParams params;
  params.array_buffer_allocator = v8::ArrayBuffer::Allocator::NewDefaultAllocator();
  params.constraints.set_code_range_size_in_bytes(8u << 20);
  v8::Isolate* isolate = v8::Isolate::New(params);
  int rc = 0;
  {
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = v8::Context::New(isolate);
    v8::Context::Scope context_scope(context);
    v8::TryCatch try_catch(isolate);
    v8::Local<v8::Script> script;
    v8::Local<v8::Value> result;
    if (v8::Script::Compile(context, v8::String::NewFromUtf8Literal(isolate, kWorkloads))
            .ToLocal(&script) &&
        script->Run(context).ToLocal(&result)) {
      v8::String::Utf8Value text(isolate, result);
      char line[1024];
      const char* cursor = *text;
      while (cursor && *cursor) {
        const char* end = strchr(cursor, '\n');
        size_t length = end ? (size_t)(end - cursor) : strlen(cursor);
        snprintf(line, sizeof line, "[V8BENCH] %s %.*s\n", running, (int)length, cursor);
        printf("%s", line);
        v8hello_debug(line);
        cursor = end ? end + 1 : nullptr;
      }
    } else {
      v8::String::Utf8Value error(isolate, try_catch.Exception());
      printf("[V8BENCH] %s exception: %s\n", running, *error ? *error : "?");
      rc = 10;
    }
  }
  isolate->Dispose();
  v8::V8::Dispose();
  v8::V8::DisposePlatform();
  delete params.array_buffer_allocator;
  printf("[V8BENCH] %s result=%s\n", running, rc ? "FAIL" : "PASS");
  return rc;
}
