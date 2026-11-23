# AoI Native for Android (Switch 2 Edition)

[![Support with Bitcoin](https://img.shields.io/badge/Support_with-Bitcoin-F7931A?style=for-the-badge&logo=bitcoin&logoColor=white)](#support)

**AoI Native** (`io.github.p1r4n351.hwaoi`, beta `v0.1.0-beta.1`; bring-up package `io.github.p1r4n351.hwaoi.dev`) brings **Hyrule Warriors: Age of Imprisonment** (`04003F40232DE000`, update `v196608`) natively to 64-bit Android handhelds with Qualcomm Adreno GPUs — targeting the 16 GB **AYN Thor** (`QCS8550` / Adreno 740, Android 13 SDK 33).

It is **not a CPU emulator**: the game's AArch64 modules (`main`, `subsdk0`, `sdk`) are rewritten once to execute directly on the phone's ARMv9 cores, Horizon / Switch 2 (`Ounce`, `NintendoSDK 20.5.15`) services are answered natively by the host runtime, VP9 movies play through host `libvpx`, and NVN 2 + Ampere (`sm86`) shaders execute on Vulkan 1.3 with Mesa Turnip. The repository and APKs contain **no game files, keys, firmware, shaders, saves, or Nintendo artwork** — you supply your own legally obtained dump from your own Switch 2.

---

## Release & Signing Identity (`v0.1.0-beta.1`)

- **Release package:** `io.github.p1r4n351.hwaoi` (`versionName` `0.1.0-beta.1`, `versionCode` `1`, non-debuggable release build; bring-up dev builds use `io.github.p1r4n351.hwaoi.dev`).
- **Signing certificate SHA-256:** `8F:2A:C4:84:41:50:B6:84:46:BF:3F:69:23:5C:43:EF:45:6C:DF:22:D3:5A:B6:D7:74:3C:D4:EA:2D:C7:A7:2C`
- **In-app updater source:** [`P1R4N351/hwaoi-android`](https://github.com/P1R4N351/hwaoi-android/releases) (`UpdateCheck` tag `vX.Y.Z` / release asset `.apk` SHA-256 verification).

---

## Verified Telemetry (16 GB AYN Thor)

Tested live on AYN Thor (`QCS8550` / Adreno 740, `14.9 GiB` RAM, Android 13 SDK 33, Mesa `Turnip 27.0.0-devel` `git-3e8621cc0e`, Vulkan `1.4.363`) with `AoI-Native-0.1.0-dev.apk` (`8,590,504` B, SHA-256 `ada9cd5893c39b80a34e7899e784960c357a4f2dc92bd90f1575d46d`) through initial settings, save creation, VP9 opening movie, realtime intro cutscene, and the first playable 3D forest field:

- **Frame Rate & Rendering (`1920x1080`):** `59.8 fps` in menus & VP9 playback (`GPU busy p50/p90 1.7–2.2 ms`), `46.6 fps` in opening 3D cutscene (`881` draws/frame), `52.3–55.7 fps` in the first playable 3D field (`300–357` draws/frame, `7` compute dispatches, `53` render passes, `GPU 16.10–17.57 ms`, `0` skipped draws at steady state, `316` graphics + `14` compute pipelines with `0` failed). Pause menu (`Plus` / `B`) runs at `59.8 fps` (`28` draws/frame, `0` dropped frames).
- **Video & Audio:** Host `libvpx v1.17.0` decoded `2,363` VP9 packets (`2,359` frames out, `8.17 ms/packet` avg); AAudio `48000 Hz` stereo output ran with `0` underruns across callbacks `4001–20000` (`0.0 ms` zero-padded) after `3` initial boot underruns.
- **Saves & Controls:** Flat `8 MB` (`8,388,608` B) `DATA.BIN` initializes cleanly; menu (`A`), movement (`LUp`/`LRight`), camera (`RLeft`/`RRight`), and pause/unpause (`Plus`/`B`) verified live.
- **Memory Margin (`frame 20812`, first field):** `TOTAL PSS: 7,183,483 kB` (`6.85 GiB`), `TOTAL RSS: 7,298,216 kB` (`6.96 GiB`), `MemAvailable: 6,183,676 kB` (`5.90 GiB` free), `SwapFree: 3,587,172 kB`, `0` `lmkd` kills, `0` crashes.
- **Extended Battle Telemetry (71.4 min, `61,140` frames, `DM_LSFG=0`):** Across `67` 5-second field/combat windows (`337-1,546` draws/frame, median `824`), measured `p10 35.2 fps`, `p50 46.4 fps`, `p90 52.5 fps`, `max 55.7 fps`, holding `35.5 fps` at peak `1,546` draws/frame (`GPU busy p50/p90 25.0/29.4 ms`); `498` graphics + `50` compute pipelines (`0` failed, `0` shared-memory rejections), `4` live indirect `QUADS` conversions (`0` pass splits), `1,922 MiB` Vulkan memory at frame `60,900` (`4,693-5,546 MiB` RSS), `0` crashes, `0` `lmkd` events (after fixing `-DNDEBUG` `vka_job_set_handles` in `cff8035`).
- **Open / Untested:** Qualcomm stock system driver, `<16 GB` devices, and chapter-end save write/reload remain untested.

---

## Direct Boot, Second-Screen Companion & Upstream Contributions

- **Direct-Boot & Zonai Quick Menu:** Launches straight into `04003F40232DE000` (`GameActivity`) with single-title obsidian/gold/teal styling and a trimmed in-game quick menu (`af06f05`, `14b543d`).
- **Converged Warriors Second-Screen Companion (`Display 4` & Opt-In Drawer):** Clean-room MIT companion (`ktgl_companion.h`, shared with [AoC Native](https://github.com/P1R4N351/hwaoc-android) and published standalone in [`P1R4N351/nx-second-screen` `v0.3.0`](https://github.com/P1R4N351/nx-second-screen/releases/tag/v0.3.0)). Attaches a `FLAG_NOT_FOCUSABLE | FLAG_KEEP_SCREEN_ON` `Presentation` on AYN Thor Display 4 (`1240x1080`) without stealing Display 0 gamepad focus, featuring 4 evenly-spaced tabs (`RADAR`, `ACTIONS`, `COMBOS`, `STATUS`), 128-point 3D inertial trail + 8 waypoint pins, fail-closed `R`/`L`/party/combo chords, cutscene auto-dimming, 1 px / 60 s AMOLED burn-in orbit, and Bifrost LED sync (`AOI_BASE` / `AOI_OPEN`, `com.moonbench.bifrost.permission.CONTROL_LEDS`).
- **AYN Controller Support & Upstream `BananaNative` PRs:** Auto-detects AYN Thor/Odin `"Odin Controller"` (`0x2020:0x0111`) face-button labels (`OdinPad`). Upstream `moceure/BananaNative` contributions: [PR #559](https://git.eden-emu.dev/moceure/BananaNative/pulls/559) (merged, AoI Android bring-up), [PR #625](https://git.eden-emu.dev/moceure/BananaNative/pulls/625) (open, AoI `Supported` promotion), and [PR #631](https://git.eden-emu.dev/moceure/BananaNative/pulls/631) (open, `OdinPad` AYN controller mapping).

---

## Port Architecture & Dump Details

- **Dump & Container Verified:** Switch 2 NUP v1 `PKG[1]` `v196608` (`main=659cb288b5c5b2b49853add616c0d5bf`, `subsdk0=2f1ad4154aecbb0c128030906af67685`, `sdk=820cd7d096aac448693f504400cc2375`), KTGL RDB under `b6e28c40` + archives under `ebc8c781`, `101,860` catalogued shaders (`0` mesh/task shaders), and `15,449` runtime-seen shader keys (`25,591` SPIR-V modules, `478.7 MB` `dmsh_vk.lib`) in [`dm/games/04003F40232DE000/shader_seen.tsv`](dm/games/04003F40232DE000/shader_seen.tsv).
- **Android Port Additions:** Enabled `04003F40232DE000` in [`dm/games/04003F40232DE000/readiness.tsv`](dm/games/04003F40232DE000/readiness.tsv) and [`android/app/gradle.properties`](android/app/gradle.properties), and ported non-indexed indirect `QUADS` draw conversion (`quad_indirect_spv` compute pass + shared quad index buffer in [`dm/gfx/vk_android/vka_exec.c`](dm/gfx/vk_android/vka_exec.c) and [`dm/gfx/vk_android/vka_draw.c`](dm/gfx/vk_android/vka_draw.c)).
- **Profile & Technical Notes:** See [`dm/games/04003F40232DE000/README.md`](dm/games/04003F40232DE000/README.md) for memory pools, shader translation notes, flat `8 MB` `DATA.BIN` save behavior, and full Thor bring-up telemetry.
- **KTGL Commonalities & Cross-Venture Optimizations (`AoC` + `AoI`):** See [`docs/ktgl-commonalities.md`](docs/ktgl-commonalities.md) for shared KTGL/KTSL2/RDB/VP9/QUADS findings and `DM_VK_EDGE_UNPIN=1` (`99.8%` page-fault reduction; `1,621.7 -> 16.2 ns/sub` cold, `1,288.5 -> 8.7 ns/sub` warm), `DM_VK_KTGL_CACHE=1` (`884,000 -> 0` steady `g_mu` locks, `6.58x–6.98x`, narrow `qdx_convert` barriers), and `DM_MOVIE_FAST_NV12=1` / `DM_MOVIE_LOOP_OPT=1` (`720p -> 1080p` border-padded NV12 `1.25x–1.38x`).

---

## Building from Source

```bash
cd android/app
./gradlew assembleRelease   -PnxLive=true   -PnxGames=04003F40232DE000   -PnxApplicationId=io.github.p1r4n351.hwaoi   -PnxLabel="AoI Native"   -PnxVersion=0.1.0-beta.1   -PnxVersionCode=1   -PnxUpdateRepo=P1R4N351/hwaoi-android   -PnxOfficialSigner=8f2ac4844150b68446bf3f69235c43ef456cdf22d35ab6d7743cd4ea2dc7a72c   -PnxWikiUrl="https://github.com/P1R4N351/hwaoi-android#readme"   -PnxPackEngine=fake   -PnxLsfg=true
```

Prepare the Android pack and SPIR-V shader library (`shader_seen.tsv`) from your own dumped NUP / folder with `scripts/pack_android.py`.

---

## Support

If this work is useful to you, donations are gratefully accepted. They are voluntary, unlock nothing, and go to this port's maintainers (PiSCES), not to the upstream Banana Native project.

- Bitcoin: `bc1q4a3lqcg87rvw7vxulausdqhhuwkwwa6w6v4mxx`
- VitaDB profile: [PiSCES](https://www.rinnegatamante.eu/vitadb/#/user/PiSCES)

Download builds only from this repository's [Releases](https://github.com/P1R4N351/hwaoi-android/releases). Nobody will ever ask you for keys, game files or account details.

---

## Credits & Upstream Ecosystem

- **[Banana Native](https://git.eden-emu.dev/moceure/BananaNative)** by moceure & contributors (MIT core; GPL-3.0-or-later compute & LSFG components), **[nxnative](https://github.com/seedds/nxnative)**, **[Anka Native](https://github.com/EddizEge/AnkaNative)**, **[MacNativeGames](https://github.com/ly05010419/MacNativeGames)**.
- **[OoT Native](https://github.com/P1R4N351/oot-android)**, **[AoC Native](https://github.com/P1R4N351/hwaoc-android)**, **[BotW Native](https://github.com/P1R4N351/botw-android)**, **[TotK Native (fork)](https://github.com/P1R4N351/totk-android)**.
- **[a55uka/nup](https://github.com/a55uka/nup)** (MIT), **Ryujinx** (MIT), and **[Mesa](https://mesa3d.org) Turnip** (MIT).

---

## License & Legal

Repository-authored documentation, scripts, and clean interfaces follow [`LICENSE`](LICENSE); builds integrating GPL-3.0-or-later components (such as LSFG and compute shader tooling) are distributed under GPL-3.0-or-later, with corresponding source published in full at the matching release tag in [`P1R4N351/hwaoi-android`](https://github.com/P1R4N351/hwaoi-android). **You must own the game** and supply your own legally dumped Nintendo Switch 2 copy of *Hyrule Warriors: Age of Imprisonment*. This is an independent interoperability and preservation research project with no affiliation with Nintendo or Koei Tecmo.
