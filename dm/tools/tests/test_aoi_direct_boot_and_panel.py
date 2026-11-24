"""AoI Native single-game direct boot, Zonai Quick Menu theme, and launcher trims."""
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]
ANDROID = REPO / 'android/app'
JAVA = ANDROID / 'src/main/java/dev/nxnative/android'
DRAWABLE = ANDROID / 'src/main/res/drawable'


class AoiDirectBootAndPanelTests(unittest.TestCase):
    """Verify single-game configuration, direct-boot wiring, trims, and Zonai theme."""

    def test_single_game_properties_and_removed_baggage(self) -> None:
        props = (ANDROID / 'gradle.properties').read_text(encoding='utf-8')
        self.assertIn('\nnxGames=04003F40232DE000\n', f'\n{props}\n')
        gone_java = ('BananaBackgroundView.java', 'BananaGrid.java', 'Covers.java',
                     'CoverUi.java', 'CropActivity.java', 'CropMath.java',
                     'GameOrder.java', 'HomeLayout.java')
        for name in gone_java:
            self.assertFalse((JAVA / name).exists(), name)
        for art in ('ic_banana.xml', 'ic_banana_happy.xml', 'ic_banana_mono.xml', 'ic_banana_happy_mono.xml'):
            self.assertFalse((DRAWABLE / art).exists(), art)

    def test_direct_boot_and_clean_exit(self) -> None:
        ma = (JAVA / 'MainActivity.java').read_text(encoding='utf-8')
        self.assertIn('tryDirectBoot(', ma)
        self.assertIn('"open_setup"', ma)
        self.assertIn('04003F40232DE000', ma)
        self.assertNotIn('BananaBackgroundView', ma)
        self.assertNotIn('refreshDemo', ma)
        ga = (JAVA / 'GameActivity.java').read_text(encoding='utf-8')
        self.assertIn('"direct_boot"', ga)
        self.assertIn('finishAndRemoveTask()', ga)
        self.assertNotIn('if (ExitWatch.cause(this, rec, false) != null) return false;', ma)
        self.assertIn('DisplayLaunch.startOnMainDisplay', ma)
        dl = (JAVA / 'DisplayLaunch.java').read_text(encoding='utf-8')
        self.assertIn('setLaunchDisplayId(Display.DEFAULT_DISPLAY)', dl)

    def test_zonai_quick_menu_and_ablation(self) -> None:
        sp = (JAVA / 'SidePanel.java').read_text(encoding='utf-8')
        for tok in ('0xFFE8B84B', '0xF4171614', '0xFF5AC8B8', 'ic_aoi_foreground', 'GpuDriver', '"open_setup"'):
            self.assertIn(tok, sp, tok)
        for dropped in ('0xFFFFD54F', 'DrsSlider', 'ResPresets', 'DlssTune', 'cheatRows', 'renderQuality', 'GameUpscaler'):
            self.assertNotIn(dropped, sp, dropped)


if __name__ == '__main__':
    unittest.main()
