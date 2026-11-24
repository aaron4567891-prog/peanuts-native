"""Profile option rows and environment variable generation (Profile.java with android/app/src/jvmtest's
ProfileOptionsJvmTest, and build.gradle's optionsOf). The JVM part is skipped without a JDK."""
# PEANUTS(upstream-candidate): omit the unused re import.
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]
SRC = REPO / 'android/app/src/main/java/dev/nxnative/android/Profile.java'
TEST = REPO / 'android/app/src/jvmtest/java/dev/nxnative/android/ProfileOptionsJvmTest.java'
GRADLE = REPO / 'android/app/build.gradle'
GENERATED_CONFIG = REPO / 'android/app/build/generated/source/buildConfig/release/dev/nxnative/android/BuildConfig.java'  # PEANUTS: real Gradle metadata integration, not a synthetic row.
OOT_TSV = REPO / 'dm/games/0400486024DEC000/game.tsv'

STUB = '''package dev.nxnative.android;
public final class BuildConfig {
    public static final String NX_PROFILES = "";
}
'''


class OptionTsv(unittest.TestCase):
    def test_oot_options_in_game_tsv(self):
        rows = [line.split('\t') for line in OOT_TSV.read_text(encoding='utf-8').splitlines() if line.startswith('option\t')]
        self.assertEqual(len(rows), 4, f'OoT must declare 4 options: {rows}')
        envs = [r[1].strip() for r in rows]
        self.assertEqual(envs, ['DM_OOT_MAX_RESOLUTION', 'DM_OOT_TEXTURE_POOL_X2', 'DM_OOT_FPS_UNLOCK', 'DM_VK_OQ_SPLIT_PASS'])
        labels = [r[2].strip() for r in rows]
        self.assertEqual(labels, ['Max resolution', 'Texture pool ×2', 'Unlocked frame rate', 'Keep cave walls (Qualcomm)'])
        # Check attributes
        max_res_attrs = [tok for col in rows[0][4:] for tok in col.split()]
        self.assertIn('restart', max_res_attrs)
        self.assertIn('implies=DM_OOT_TEXTURE_POOL_X2', max_res_attrs)
        self.assertIn('needs=OOT_PATCHES', max_res_attrs)

        pool_attrs = [tok for col in rows[1][4:] for tok in col.split()]
        self.assertIn('restart', pool_attrs)
        self.assertIn('needs=OOT_PATCHES', pool_attrs)

        fps_attrs = [tok for col in rows[2][4:] for tok in col.split()]
        self.assertIn('restart', fps_attrs)
        self.assertIn('needs=OOT_PATCHES', fps_attrs)

    def test_gradle_options_of_parser(self):
        gradle = GRADLE.read_text(encoding='utf-8')
        self.assertIn("def optionsOf = { String game ->", gradle)
        self.assertIn("optionsOf(g)", gradle)
        self.assertIn("implies=", gradle)
        self.assertIn("needs=", gradle)


@unittest.skipUnless(shutil.which('javac') and shutil.which('java'), 'no JDK')
class ProfileOptionsJvm(unittest.TestCase):
    # PEANUTS(upstream-candidate): catch TSV key/value-column mistakes in the actual Gradle-generated metadata.
    @unittest.skipUnless(GENERATED_CONFIG.is_file(), 'generateReleaseBuildConfig has not run')
    def test_generated_profile_ceiling_policy(self):
        expected = []
        for tsv in sorted((REPO / 'dm/games').glob('*/game.tsv')):
            rows = [line.split('\t') for line in tsv.read_text(encoding='utf-8').splitlines()]
            ceiling = any(len(row) > 1 and row[0] == 'fps_ceiling_only' and row[1].strip() == '1' for row in rows)
            expected.append(tsv.parent.name + '=' + ('1' if ceiling else '0'))
        driver = '''package dev.nxnative.android;
public final class GeneratedProfileCeilingTest {
    public static void main(String[] args) {
        java.util.Map<String, Boolean> expected = new java.util.HashMap<>();
        for (String arg : args) { String[] pair = arg.split("="); expected.put(pair[0], pair[1].equals("1")); }
        int checked = 0;
        for (Profile p : Profile.parse(BuildConfig.NX_PROFILES)) {
            if (!expected.containsKey(p.titleId)) throw new AssertionError("Unknown generated profile " + p.titleId);
            if (p.fpsCeilingOnly != expected.get(p.titleId)) throw new AssertionError("Gradle dropped ceiling policy for " + p.titleId);
            if (!p.fpsCeilingOnly) continue;
            checked++;
            for (int cap : new int[] {30,60,90,120}) {
                if (cap > p.fpsLimitMax) continue;
                int saved = p.fpsCeilingCap(cap);
                if (saved != (cap == 60 ? 0 : cap)) throw new AssertionError("Ceiling changed during normalization");
                java.util.List<String> env = p.fpsCeilingEnv(saved);
                for (String line : new String[] {"DM_FPS_LIMIT=" + cap, "DM_VSYNC_HZ=60", "DM_PRESENT_HZ=60", "DM_PRESENT_INTERVAL=game", "DM_FPS_TARGET=0", "DM_FPSLOCKER=", "DM_FPSLOCKER_PLAN=", "DM_FPSLOCKER_OWNED=1", "DM_FPSLOCKER_INTERVAL=0", "DM_FPS_DECOUPLE=0"})
                    if (!env.contains(line)) throw new AssertionError("Unsafe cap " + cap + ": missing " + line);
                if (p.usesFpsModeTarget() || p.fpsRestart(-2, saved)) throw new AssertionError("Ceiling changes native timing");
            }
        }
        System.out.println("ok " + checked);
    }
}
'''
        with tempfile.TemporaryDirectory() as d:
            source = Path(d) / 'GeneratedProfileCeilingTest.java'
            source.write_text(driver, encoding='utf-8')
            subprocess.run(['javac', '-Xlint:all', '-Werror', '-encoding', 'UTF-8', '-d', d, str(SRC), str(GENERATED_CONFIG), str(source)], check=True, capture_output=True, text=True)
            result = subprocess.run(['java', '-cp', d, 'dev.nxnative.android.GeneratedProfileCeilingTest', *expected], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertRegex(result.stdout.strip(), r'^ok \d+$')

    def test_jvm(self):
        with tempfile.TemporaryDirectory() as d:
            stub = Path(d) / 'BuildConfig.java'
            stub.write_text(STUB)
            subprocess.run(['javac', '-Xlint:all', '-Werror', '-encoding', 'UTF-8', '-d', d, str(SRC), str(TEST), str(stub)],
                           check=True, capture_output=True, text=True)
            r = subprocess.run(['java', '-cp', d, 'dev.nxnative.android.ProfileOptionsJvmTest'], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertEqual(r.stdout.strip(), 'ok')


if __name__ == '__main__':
    unittest.main()
