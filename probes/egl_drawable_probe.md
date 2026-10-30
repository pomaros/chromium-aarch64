# Native EGL drawable persistence and separate read surface

`egl_drawable_probe.c` uses public EGL and dynamically resolved GLES functions.
It requires a coherent native EGL/Mesa implementation with persistent surface
storage and independent draw/read bindings. It uses no GLA-private API, HOST
provider, shared contexts, window presentation or framebuffer-object substitute.
An EGL library version number alone does not admit the implementation cohort.

Each suite creates two nonshared GLES2 contexts A/B and two pbuffers P/Q on one
initialized display/configuration. The equal suite uses32×32; the unequal suite
uses P29×17 and Q47×31. Actual queried dimensions must match. A renders; both contexts read the default framebuffer using the surface
dimensions, not a private FBO.

A first renders P red and Q green. Both contexts then revisit both surfaces
without repainting: colors must survive context switches and size changes.
A binds draw=P/read=Q, reads green, clears the draw target blue, and must still
read green. Binding P for reading must observe blue while Q remains green.
A is unbound and destroyed, then B must read both retained surfaces before
teardown. Surface storage is independent of the first rendering context.

Three RGBA pixels are required per read oracle: origin, center and far corner.
Each suite has36 pixel checks,11 identity-checked bindings and3 clear checks.
Only a real GLES GL_VERSION and nonempty GL_RENDERER are accepted; both are
logged for comparison with the admitted native provider. Any EGL/GL error, missing symbol,
size mismatch, color mismatch or failed unbind/destroy/terminate is FAIL.
Both equal and unequal suites run even if the first fails; neither is a skip.

Build against the actual native SDK, generated EGL2.1 proto header and libEGL.a
with GNU11/Wall/Wextra/Werror and retained MD/link trace/map. No new GLA headers
or guessed vector ABI are needed. Audit the resulting AROS AArch64 relocatable
module against the actual admitted loader before running.

The native supervisor must retain exact loaded EGL/Mesa/provider identities,
stdout and actual command return code. Require one equal PASS, one unequal PASS,
all72 pixel oracles, all binding/clear checks, both A-destroyed records, and exactly
`[EGLDRAW] FINAL result=PASS equal=PASS unequal=PASS rc=0` with actual rc0 and no
panic/fault. Earlier context-backed storage is expected to fail these oracles;
there is no compatibility fallback. GL readback qualifies pbuffer contents,
not screen presentation or Chromium/ANGLE acceptance.

The program has a fixed finite call sequence and no retry loop, but native
rendering/destruction calls can block. Use the independent native supervisor
with a60-second outer bound and capture registers/stack before recovery.
If cleanup cannot unbind, it reports cleanup=REFUSED and retains all contexts,
surfaces and library/code ownership in Delay under that supervisor. It does not
start the second suite or attempt destroy/terminate/unload in unknown state.
No asynchronous cancellation, forced unload or fabricated cleanup result is used.
