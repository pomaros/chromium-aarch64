# AArch64 Chromium for AROS

Downstream AArch64 work built on [Terminills’ Chromium patch queues](https://github.com/terminills/chromium-aros-patches) and [aros-web](https://github.com/terminills/aros-web). Their port and original contributors remain the upstream foundation. The engine is Chromium 124; target work does not upgrade its version.

This repository owns our downstream patches, build integration, packaging and debugger-assisted qualification. The common operating system belongs to [pomaros/aros-core](https://github.com/pomaros/aros-core). Physical Apple drivers and firmware are outside this repository. AROS is a distinct target with its native operating-system interfaces.

## Source inputs

`UPSTREAM.lock.json` records exact upstream inputs and the candidate common core. Fetch sources separately; this repository does not vendor a Chromium checkout or build outputs. The ordered upstream queues and their dependency bases are defined by the pinned upstream repository’s `PINS`, `EXPORT.json` and `series` files.

`patches/queue/` contains downstream additions by component. `patches/standalone/` contains separate dependency patches; these are not automatically concatenated with an upstream queue. The manifest records exact bytes, not proof that every patch replays or that the browser works.

## Import trampoline probe

Build the five-import ABI probe with explicit guest SDK and Chromium emitter inputs:

```sh
python3 tools/build-import-tramp-probe artifacts/import-probe \
  --sdk /absolute/path/to/configured/AROS/Developer \
  --chromium /absolute/path/to/prepared/chromium/src
```

`--generator` selects an emitter directly instead of a Chromium checkout.
`--crosstools`, `--generated-include` and `--host-tools` select other toolchain
inputs explicitly. The output directory must be new. A manifest retains input,
command and binary identities. Build success does not constitute guest execution.

## Standalone Dawn configuration

Generate arguments with an explicit matching SDK and its host tools:

```sh
python3 tools/configure-dawn /path/to/dawn/out/aros-arm64 \
  --sdk /path/to/configured-sdk/AROS/Developer \
  --host-tools /path/to/configured-sdk/host/tools \
  --crosstools /path/to/aros-crosstools
```

The output directory must be new. `args.gn` selects the Vulkan-only AArch64 AROS
configuration; `inputs.json` records compiler, startup, host-tool and template
hashes. No historical SDK is selected implicitly. This command configures
arguments only: dependency preparation, pinned patch replay, build, AROS-native
Vulkan loader integration and runtime qualification are separate requirements.
The template is not evidence that WebGPU or application GPU acceleration works.

## Qualification and migration

This initial snapshot contains reviewed generic patches. The import-probe builder is available; complete pinned queue replay, browser build commands, public VM test tooling migration, browser runtime and application GPU acceptance are pending. It is not a browser release. The build tooling is being extracted from its existing integration checkout; that checkout remains the consumer until the extraction is verified. No complete build or runtime qualification is claimed by this repository’s initial commit.

Run `python3 tools/verify-patches.py` to verify patch hashes and the declared patch inventory. Build and runtime evidence must retain source, SDK, binary and debugger identities separately.

## Licensing

Original repository tools and metadata are MIT, Copyright (c) 2026 John Knipper. Patches retain their original notices and the licences of the projects they modify; the MIT licence does not relicense Chromium, V8 or other dependency code. See [NOTICE.md](NOTICE.md).
