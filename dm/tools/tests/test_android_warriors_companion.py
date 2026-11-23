# SPDX-License-Identifier: MIT
"""Host JVM verification and pure-Java checks for WarriorsCompanionModel and CompanionDisplay."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]
JAVA = REPO / 'android/app/src/main/java/dev/nxnative/android'
SRCS = [JAVA / 'CompanionState.java', JAVA / 'CompanionDisplay.java', JAVA / 'WarriorsCompanionModel.java']
TEST = REPO / 'android/app/src/jvmtest/java/dev/nxnative/android/WarriorsCompanionJvmTest.java'


class WarriorsCompanionTests(unittest.TestCase):
    """Compile and execute pure-JDK Warriors companion tests without the Android SDK."""

    def test_pure_jvm_sources_have_no_android_imports(self):
        self.assertGreater(len(SRCS), 0)
        for p in SRCS:
            self.assertNotIn('import android.', p.read_text(encoding='utf-8'), p.name)

    @unittest.skipUnless(shutil.which('javac') and shutil.which('java'), 'no JDK')
    def test_warriors_companion_jvm_suite(self):
        with tempfile.TemporaryDirectory(prefix='wc_jvm_') as tmp:
            classes = Path(tmp) / 'classes'
            classes.mkdir()
            c = subprocess.run(
                ['javac', '-Xlint:all', '-Werror', '-encoding', 'UTF-8', '-d', str(classes), *map(str, SRCS), str(TEST)],
                capture_output=True, text=True, encoding='utf-8',
            )
            self.assertEqual(c.returncode, 0, c.stderr)
            r = subprocess.run(
                ['java', '-Dstdout.encoding=UTF-8', '-cp', str(classes), 'dev.nxnative.android.WarriorsCompanionJvmTest'],
                capture_output=True, text=True, encoding='utf-8',
            )
            self.assertEqual(r.returncode, 0, r.stderr + r.stdout)
            self.assertIn('WarriorsCompanionJvmTest: PASS', r.stdout)


if __name__ == '__main__':
    unittest.main()
