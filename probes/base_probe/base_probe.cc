// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
//
// Chromium's //base on AArch64 AROS: the thread pool runs 200 tasks that
// each sum a range, a WaitableEvent collects them, and a SingleThreadTask
// Executor's RunLoop (the AROS message pump) fires a 50 ms delayed task and
// a posted reply from a pool task. Prints [BASEPROBE] lines and result=PASS.

#include <atomic>
#include <cstdio>

#include "base/at_exit.h"
#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/run_loop.h"
#include "base/synchronization/waitable_event.h"
#include "base/task/single_thread_task_executor.h"
#include "base/task/thread_pool.h"
#include "base/task/thread_pool/thread_pool_instance.h"
#include "base/time/time.h"

namespace {

constexpr int kTasks = 200;
std::atomic<int> g_done{0};
std::atomic<long long> g_sum{0};
int g_failures = 0;

void Check(const char* what, bool ok) {
  std::printf("[BASEPROBE] %s %s\n", what, ok ? "ok" : "FAIL");
  if (!ok)
    g_failures++;
}

}  // namespace

int main(int argc, char** argv) {
  base::AtExitManager at_exit;
  base::CommandLine::Init(argc, argv);
  base::SingleThreadTaskExecutor executor;
  base::ThreadPoolInstance::CreateAndStartWithDefaultParams("BaseProbe");
  std::printf("[BASEPROBE] started\n");

  base::WaitableEvent all_done;
  for (int t = 0; t < kTasks; t++) {
    base::ThreadPool::PostTask(FROM_HERE, base::BindOnce(
                                              [](int t, base::WaitableEvent* done) {
                                                long long s = 0;
                                                for (int i = 0; i < 10000; i++)
                                                  s += (long long)t * i;
                                                g_sum += s;
                                                if (++g_done == kTasks)
                                                  done->Signal();
                                              },
                                              t, &all_done));
  }
  bool signalled = all_done.TimedWait(base::Seconds(30));
  long long expected = 0;
  for (int t = 0; t < kTasks; t++)
    expected += (long long)t * (10000LL * 9999 / 2);
  Check("thread pool", signalled && g_done == kTasks && g_sum == expected);

  base::RunLoop run_loop;
  const base::TimeTicks start = base::TimeTicks::Now();
  bool replied = false;
  base::ThreadPool::PostTaskAndReply(FROM_HERE, base::DoNothing(),
                                     base::BindOnce([](bool* r) { *r = true; }, &replied));
  executor.task_runner()->PostDelayedTask(FROM_HERE, run_loop.QuitClosure(),
                                          base::Milliseconds(50));
  run_loop.Run();
  const int64_t waited = (base::TimeTicks::Now() - start).InMilliseconds();
  std::printf("[BASEPROBE] delayed task after %lld ms\n", (long long)waited);
  Check("message loop", waited >= 50 && waited < 5000);
  Check("reply on the main thread", replied);

  base::ThreadPoolInstance::Get()->Shutdown();
  std::printf("[BASEPROBE] result=%s\n", g_failures ? "FAIL" : "PASS");
  return g_failures ? 20 : 0;
}
