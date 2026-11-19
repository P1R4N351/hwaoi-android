# AoI Native for Android (Switch 2 Edition)

[![Support with Bitcoin](https://img.shields.io/badge/Support_with-Bitcoin-F7931A?style=for-the-badge&logo=bitcoin&logoColor=white)](#support)

**AoI Native** (`io.github.p1r4n351.hwaoi.dev`, playable in opening field on AYN Thor) brings **Hyrule Warriors: Age of Imprisonment** (`04003F40232DE000`, update `v196608`) natively to 64-bit Android handhelds with Qualcomm Adreno GPUs — targeting the 16 GB **AYN Thor** (`QCS8550` / Adreno 740, Android 13 SDK 33).

It is **not a CPU emulator**: the game's AArch64 modules (`main`, `subsdk0`, `sdk`) are rewritten once to execute directly on the phone's ARMv9 cores, Horizon / Switch 2 (`Ounce`, `NintendoSDK 20.5.15`) services are answered natively by the host runtime, VP9 movies play through host `libvpx`, and NVN 2 + Ampere (`sm86`) shaders execute on Vulkan 1.3 with Mesa Turnip. The repository and APKs contain **no game files, keys, firmware, shaders, saves, or Nintendo artwork** — you supply your own legally obtained dump from your own Switch 2.

---

## Verified Telemetry (16 GB AYN Thor)

Tested live on AYN Thor (`QCS8550` / Adreno 740, `14.9 GiB` RAM, Android 13 SDK 33, Mesa `Turnip 27.0.0-devel` `git-3e8621cc0e`, Vulkan `1.4.363`) with `AoI-Native-0.1.0-dev.apk` (`8,590,504` B, SHA-256 `ada9cd5893c39b80a34e7899e784960c357a4f2dc92bd90f1575d46d`) through initial settings, save creation, VP9 opening movie, realtime intro cutscene, and the first playable 3D forest field:

- **Frame Rate & Rendering (`1920x1080`):** `59.8 fps` in menus & VP9 playback (`GPU busy p50/p90 1.7–2.2 ms`), `46.6 fps` in opening 3D cutscene (`881` draws/frame), `52.3–55.7 fps` in the first playable 3D field (`300–357` draws/frame, `7` compute dispatches, `53` render passes, `GPU 16.10–17.57 ms`, `0` skipped draws at steady state, `316` graphics + `14` compute pipelines with `0` failed). Pause menu (`Plus` / `B`) runs at `59.8 fps` (`28` draws/frame, `0` dropped frames).
- **Video & Audio:** Host `libvpx v1.17.0` decoded `2,363` VP9 packets (`2,359` frames out, `8.17 ms/packet` avg); AAudio `48000 Hz` stereo output ran with `0` underruns across callbacks `4001–20000` (`0.0 ms` zero-padded) after `3` initial boot underruns.
- **Saves & Controls:** Flat `8 MB` (`8,388,608` B) `DATA.BIN` initializes cleanly; menu (`A`), movement (`LUp`/`LRight`), camera (`RLeft`/`RRight`), and pause/unpause (`Plus`/`B`) verified live.
- **Memory Margin (`frame 20812`, first field):** `TOTAL PSS: 7,183,483 kB` (`6.85 GiB`), `TOTAL RSS: 7,298,216 kB` (`6.96 GiB`), `MemAvailable: 6,183,676 kB` (`5.90 GiB` free), `SwapFree: 3,587,172 kB`, `0` `lmkd` kills, `0` crashes.
- **Open / Untested:** Later combat missions (including whether indirect `QUADS` triggers there) and mid-stage autosave write/reload remain to be exercised.

---

## Port Architecture & Dump Details

- **Dump & Container Verified:** Switch 2 NUP v1 `PKG[1]` `v196608` (`main=659cb288b5c5b2b49853add616c0d5bf`, `subsdk0=2f1ad4154aecbb0c128030906af67685`, `sdk=820cd7d096aac448693f504400cc2375`), KTGL RDB under `b6e28c40` + archives under `ebc8c781`, `101,860` catalogued shaders (`0` mesh/task shaders), and `15,449` runtime-seen shader keys (`25,591` SPIR-V modules, `478.7 MB` `dmsh_vk.lib`) in [`dm/games/04003F40232DE000/shader_seen.tsv`](dm/games/04003F40232DE000/shader_seen.tsv).
- **Android Port Additions:** Enabled `04003F40232DE000` in [`dm/games/04003F40232DE000/readiness.tsv`](dm/games/04003F40232DE000/readiness.tsv) and [`android/app/gradle.properties`](android/app/gradle.properties), and ported non-indexed indirect `QUADS` draw conversion (`quad_indirect_spv` compute pass + shared quad index buffer in [`dm/gfx/vk_android/vka_exec.c`](dm/gfx/vk_android/vka_exec.c) and [`dm/gfx/vk_android/vka_draw.c`](dm/gfx/vk_android/vka_draw.c)).
- **Profile & Technical Notes:** See [`dm/games/04003F40232DE000/README.md`](dm/games/04003F40232DE000/README.md) for memory pools, shader translation notes, flat `8 MB` `DATA.BIN` save behavior, and full Thor bring-up telemetry.

---

## Building from Source

```bash
cd android/app
./gradlew assembleDebug   -PnxGames=04003F40232DE000   -PnxApplicationId=io.github.p1r4n351.hwaoi.dev   -PnxLabel="AoI Native"   -PnxPackEngine=fake   -PnxLsfg=true
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
