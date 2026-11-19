# AoI Native for Android (Switch 2 Edition)

[![Support with Bitcoin](https://img.shields.io/badge/Support_with-Bitcoin-F7931A?style=for-the-badge&logo=bitcoin&logoColor=white)](#support)

**AoI Native** (`io.github.p1r4n351.hwaoi.dev`, bring-up) brings **Hyrule Warriors: Age of Imprisonment** (`04003F40232DE000`, update `v196608`) natively to 64-bit Android handhelds with Qualcomm Adreno GPUs — targeting the 16 GB **AYN Thor** (`QCS8550` / Adreno 740, Android 13 SDK 33).

It is **not a CPU emulator**: the game's AArch64 modules (`main`, `subsdk0`, `sdk`) are rewritten once to execute directly on the phone's ARMv9 cores, Horizon / Switch 2 (`Ounce`, `NintendoSDK 20.5.15`) services are answered natively by the host runtime, VP9 movies play through host `libvpx`, and NVN 2 + Ampere (`sm86`) shaders execute on Vulkan 1.3 with Mesa Turnip. The repository and APKs contain **no game files, keys, firmware, shaders, saves, or Nintendo artwork** — you supply your own legally obtained dump from your own Switch 2.

---

## Current Status

- **Dump & Container Verified:** Switch 2 NUP v1 `PKG[1]` `v196608` (`main=659cb288b5c5b2b49853add616c0d5bf`, `subsdk0=2f1ad4154aecbb0c128030906af67685`, `sdk=820cd7d096aac448693f504400cc2375`), KTGL RDB under `b6e28c40` + archives under `ebc8c781`, `101,860` catalogued shaders (`0` mesh/task shaders), and `15,449` runtime-seen shader keys in [`dm/games/04003F40232DE000/shader_seen.tsv`](dm/games/04003F40232DE000/shader_seen.tsv).
- **Android Port Additions:** Enabled `04003F40232DE000` in [`dm/games/04003F40232DE000/readiness.tsv`](dm/games/04003F40232DE000/readiness.tsv) and [`android/app/gradle.properties`](android/app/gradle.properties), and ported non-indexed indirect `QUADS` draw conversion (`quad_indirect_spv` compute pass + shared quad index buffer in [`dm/gfx/vk_android/vka_exec.c`](dm/gfx/vk_android/vka_exec.c) and [`dm/gfx/vk_android/vka_draw.c`](dm/gfx/vk_android/vka_draw.c)).
- **Profile & Technical Notes:** See [`dm/games/04003F40232DE000/README.md`](dm/games/04003F40232DE000/README.md) for memory pools, shader translation notes, and flat `8 MB` `DATA.BIN` save behavior.

---

## Building from Source

```bash
cd android/app
./gradlew assembleDebug \
  -PnxGames=04003F40232DE000 \
  -PnxApplicationId=io.github.p1r4n351.hwaoi.dev \
  -PnxLabel="AoI Native" \
  -PnxPackEngine=fake \
  -PnxLsfg=true
```

Prepare the Android pack and SPIR-V shader library (`shader_seen.tsv`) from your own dumped NUP / folder with `scripts/pack_android.py`.

---

## Support

If this work is useful to you, donations are gratefully accepted:

- Bitcoin: `bc1q4a3lqcg87rvw7vxulausdqhhuwkwwa6w6v4mxx`
- VitaDB profile: [PiSCES](https://www.rinnegatamante.eu/vitadb/#/user/PiSCES)

---

## Credits & Upstream Ecosystem

- **[Banana Native](https://git.eden-emu.dev/moceure/BananaNative)** by moceure & contributors (MIT core; GPL-3.0-or-later compute & LSFG components), **[nxnative](https://github.com/seedds/nxnative)**, **[Anka Native](https://github.com/EddizEge/AnkaNative)**, **[MacNativeGames](https://github.com/ly05010419/MacNativeGames)**.
- **[OoT Native](https://github.com/P1R4N351/oot-android)**, **[AoC Native](https://github.com/P1R4N351/hwaoc-android)**, **[BotW Native](https://github.com/P1R4N351/botw-android)**, **[TotK Native (fork)](https://github.com/P1R4N351/totk-android)**.
- **[a55uka/nup](https://github.com/a55uka/nup)** (MIT), **Ryujinx** (MIT), and **[Mesa](https://mesa3d.org) Turnip** (MIT).

---

## License & Legal

Repository-authored documentation, scripts, and clean interfaces follow [`LICENSE`](LICENSE); builds integrating GPL-3.0-or-later components (such as LSFG and compute shader tooling) are distributed under GPL-3.0-or-later. **You must own the game** and supply your own legally dumped Nintendo Switch 2 copy of *Hyrule Warriors: Age of Imprisonment*. This is an independent interoperability and preservation research project with no affiliation with Nintendo or Koei Tecmo.
