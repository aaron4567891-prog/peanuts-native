"""Compile and exercise the real exact-build timing adapter without a game/device."""
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
FIXTURE = REPO / 'peanuts/tests/native'
CC = shutil.which('gcc')
if CC is None and Path('C:/msys64/ucrt64/bin/gcc.exe').is_file():
    CC = 'C:/msys64/ucrt64/bin/gcc.exe'


@unittest.skipUnless(CC, 'a host GCC is required')
class PikachuFps(unittest.TestCase):
    def test_adapter(self):
        env = dict(os.environ)
        env['PATH'] = str(Path(CC).parent) + os.pathsep + env.get('PATH', '')
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / ('timing.exe' if os.name == 'nt' else 'timing')
            subprocess.run([CC, '-std=c11', '-Wall', '-Wextra', '-Werror', '-I' + str(FIXTURE / 'pikachu_fps_stub'), str(FIXTURE / 'test_pikachu_fps.c'), '-o', str(exe), '-lm'], check=True, capture_output=True, text=True, env=env)
            for mode, prototype in (('supported', False), ('unknown', False), ('oldpack', False),
                                    ('scenes', True), ('unknown', True), ('oldpack', True)):
                with self.subTest(mode=mode, scene60=prototype):
                    run_env = dict(env, PN_LGP_SCENE60_TEST='1' if prototype else '0')
                    result = subprocess.run([str(exe), mode], capture_output=True, text=True, env=run_env)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
