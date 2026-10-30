# Native two-task EGL presentation probe

`egl_task_context_probe.c` requires a coherent native EGL/Mesa cohort containing
calling-task EGL swap selection and native-owner `glAGetCurrentContext` semantics.
It opens the same EGL display with two distinct contexts and nonoverlapping
scale-aware Intuition windows. GL functions are resolved through EGL; GLA owner
handles are queried independently through the actual public GL vector.

The controlled sequence is A MakeCurrent, B MakeCurrent and green presentation,
A red Swap, A unbind and real thread join, then B blue Swap while its original
context remains current. Each swap checks GL readback and visible SCREEN RGB,
including preservation of the other window. `WA_ScaleAware` comes from the SDK;
the probe refuses missing headers, overlapping windows or invalid pixel bounds.
Window backing pixels alone do not establish visible presentation.

Build with the selected native AROS Clang/sysroot and generated EGL2.1 headers
and `libEGL.a`; link the native GL, cybergraphics and pthread libraries. Use
`-std=gnu11 -Wall -Wextra -Werror`, retain the MD dependency receipt, linker trace
and module relocation audit. No HOST provider or desktop GL substitution is used.

Native acceptance requires the exact admitted EGL/Mesa libraries, working native
timers/condition waits, an unobscured screen, both worker return PASS records,
all expected screen/readback stages, one `[EGLTASK] FINAL result=PASS rc=0 started=2`
and the actual command return code0. Preserve stdout, native library identities
and final screen on failure; absence of panic is required separately.

Condition waits share a30-second absolute deadline. Visible pixel polling is
bounded to120 WaitTOF iterations per check. Neither a blocked driver call nor
pthread_join can be safely cancelled by this probe: use the independent target
supervisor with a60-second outer bound and capture registers/stack before recovery.
If join itself reports failure, the probe deliberately retains resources/code
and waits for that supervisor. Libraries/windows/context destruction occurs only
after every created rendering thread has actually joined.

A linked binary is not native qualification, does not qualify Chromium/ANGLE,
SMP, context sharing, pbuffer or surfaceless rendering, and must not be run against
unadmitted libraries just because their version number matches.
