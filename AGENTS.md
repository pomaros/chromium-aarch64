# Working on AArch64 Chromium

Keep Terminills’ pinned repositories as attributed upstreams. Push only to owned PomAROS repositories. OS implementation changes belong in the common core; do not copy an OS tree, Apple drivers or firmware here. Original tooling is MIT, John Knipper; preserve dependency and patch licences.

Use explicit SDK inputs and isolated build/test state. Never claim a build from patch hash verification, or GPU/application acceptance from a substrate probe. Retain exact source and binary identities, and qualify debugger inspection before a workload that may hang. Keep shared VM tooling in its owning product repository rather than duplicating it.
