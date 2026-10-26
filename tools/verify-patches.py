#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Verify the exact declared patch inventory, without fetching or applying it."""
from pathlib import Path
import hashlib
import json
root=Path(__file__).resolve().parents[1]
manifest=json.loads((root/'patches/manifest.json').read_text())
expected=set()
for row in manifest['patches']:
    path=Path(row['path'])
    if path.is_absolute() or '..' in path.parts or path.parts[0]!='patches':
        raise SystemExit('invalid patch path')
    if row['path'] in expected:
        raise SystemExit('duplicate patch path')
    expected.add(row['path'])
    data=(root/path).read_bytes()
    if len(data)!=row['bytes'] or hashlib.sha256(data).hexdigest()!=row['sha256']:
        raise SystemExit('patch identity mismatch: '+row['path'])
actual={str(p.relative_to(root)) for p in (root/'patches').rglob('*.patch')}
if actual!=expected:
    raise SystemExit('undeclared or missing patches')
print('PASS patch identities='+str(len(expected))+'; replay and runtime untested')
