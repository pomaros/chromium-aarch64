#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Check Chromium launcher failure propagation without starting a guest."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class Launcher(unittest.TestCase):
    def test_input_output_and_checker_boundaries(self):
        with tempfile.TemporaryDirectory(prefix='chrome launch ') as folder:
            root = Path(folder)
            product = root / 'product with spaces'
            product.mkdir()
            for name in ('System.img', 'edk2-aarch64-code.fd'):
                (product / name).write_bytes(b'fixture')
            inputs = {}
            for name in ('CHROME_DISK', 'CHROME_MOUNT', 'NO_REQUESTERS', 'QEMU_PRODUCT_CHECK'):
                inputs[name] = str(root / name)
                Path(inputs[name]).write_bytes(b'immutable input')
            start = root / 'start script'
            start.write_text('fixture')
            bindir = root / 'bin'
            bindir.mkdir()
            fake = bindir / 'python3'
            fake.write_text('#!' + sys.executable + '\n'
                'import os,sys,json\nfrom pathlib import Path\n'
                'Path(os.environ["CALLS"]).write_text(json.dumps(sys.argv[1:]))\n'
                'Path(sys.argv[2]).mkdir()\n'
                'sys.exit(int(os.environ["CHECK_EXIT"]))\n')
            fake.chmod(0o755)
            (bindir / 'mtype').write_text('#!/bin/sh\nexit 9\n')
            (bindir / 'mtype').chmod(0o755)
            env = dict(os.environ, **inputs, CALLS=str(root / 'calls.json'),
                       PATH=str(bindir) + os.pathsep + os.environ['PATH'])
            script = ROOT / 'tools/vm-chrome-run'
            def invoke(run, environment):
                return subprocess.run([str(script), str(run), str(product),
                    str(start), '1'], env=environment, capture_output=True, text=True)
            missing = dict(env)
            del missing['CHROME_DISK']
            refused = root / 'missing'
            self.assertNotEqual(invoke(refused, missing).returncode, 0)
            self.assertFalse(Path(str(refused) + '.disk.img').exists())
            occupied = root / 'occupied'
            occupied.mkdir()
            sentinel = occupied / 'preserve'
            sentinel.write_text('untouched')
            self.assertNotEqual(invoke(occupied, env).returncode, 0)
            self.assertEqual(sentinel.read_text(), 'untouched')
            before = hashlib.sha256(Path(inputs['CHROME_DISK']).read_bytes()).hexdigest()
            for code in (0, 7):
                result = invoke(root / ('run ' + str(code)), dict(env, CHECK_EXIT=str(code)))
                self.assertEqual(result.returncode, code, result.stderr)
                args = json.loads((root / 'calls.json').read_text())
                self.assertEqual(args[0], inputs['QEMU_PRODUCT_CHECK'])
                self.assertIn('chrome.mount=' + inputs['CHROME_MOUNT'], args)
                self.assertIn('StartChrome=' + str(start), args)
            self.assertEqual(hashlib.sha256(Path(inputs['CHROME_DISK']).read_bytes()).hexdigest(), before)


if __name__ == '__main__':
    unittest.main()
