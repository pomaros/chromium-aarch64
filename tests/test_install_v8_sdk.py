# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Exercise installation closure and preservation of existing SDK outputs."""
from pathlib import Path
import subprocess
import tempfile
import unittest
SCRIPT=Path(__file__).resolve().parents[1]/'tools/v8/install-v8-sdk'
class Install(unittest.TestCase):
    def test_complete_copy_and_existing_destination_preserved(self):
        with tempfile.TemporaryDirectory(prefix='v8 sdk ') as directory:
            root=Path(directory);src=root/'source';out=root/'build';dest=root/'installed'
            (src/'include').mkdir(parents=True);(src/'include/v8.h').write_bytes(b'header')
            (out/'obj/v8').mkdir(parents=True);(out/'obj/v8/libv8_monolith.a').write_bytes(b'archive')
            (out/'gen/include').mkdir(parents=True);(out/'gen/include/v8-gn.h').write_bytes(b'generated')
            args=[str(SCRIPT),str(src),str(out),str(dest)]
            first=subprocess.run(args,capture_output=True,text=True);self.assertEqual(first.returncode,0,first.stderr)
            self.assertEqual((dest/'lib/libv8_monolith.a').read_bytes(),b'archive')
            self.assertEqual((dest/'include/v8.h').read_bytes(),b'header')
            self.assertEqual((dest/'include/v8-gn.h').read_bytes(),b'generated')
            (dest/'sentinel').write_bytes(b'preserve')
            second=subprocess.run(args,capture_output=True,text=True);self.assertNotEqual(second.returncode,0)
            self.assertEqual((dest/'sentinel').read_bytes(),b'preserve')
    def test_missing_archive_creates_no_output(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);(root/'src/include').mkdir(parents=True);(root/'src/include/v8.h').write_bytes(b'header')
            result=subprocess.run([str(SCRIPT),str(root/'src'),str(root/'out'),str(root/'dest')],capture_output=True)
            self.assertNotEqual(result.returncode,0);self.assertFalse((root/'dest').exists())
if __name__=='__main__':unittest.main()
