"""Bifrost stick-LED state machine and CompanionState tests on host JVM (no Android SDK needed)."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]
JAVA = REPO / 'android/app/src/main/java/dev/nxnative/android'
SRCS = [JAVA / 'CompanionState.java', JAVA / 'BifrostLooks.java']
TEST = REPO / 'android/app/src/jvmtest/java/dev/nxnative/android/BifrostJvmTest.java'


class BifrostTests(unittest.TestCase):
    def test_pure_jvm_sources_have_no_android_imports(self):
        for p in SRCS:
            txt = p.read_text(encoding='utf-8')
            self.assertNotIn('import android.', txt, p.name)

    @unittest.skipUnless(shutil.which('javac') and shutil.which('java'), 'no JDK')
    def test_bifrost_jvm_suite(self):
        with tempfile.TemporaryDirectory(prefix='bifrost_') as tmp:
            classes = Path(tmp) / 'classes'
            classes.mkdir()
            c = subprocess.run(
                ['javac', '-Xlint:all', '-Werror', '-encoding', 'UTF-8', '-d', str(classes), *map(str, SRCS), str(TEST)],
                capture_output=True, text=True, encoding='utf-8',
            )
            self.assertEqual(c.returncode, 0, c.stderr)
            r = subprocess.run(
                ['java', '-Dstdout.encoding=UTF-8', '-cp', str(classes), 'dev.nxnative.android.BifrostJvmTest'],
                capture_output=True, text=True, encoding='utf-8',
            )
            self.assertEqual(r.returncode, 0, r.stderr + r.stdout)
            self.assertIn('BifrostJvmTest: PASS', r.stdout)


if __name__ == '__main__':
    unittest.main()
