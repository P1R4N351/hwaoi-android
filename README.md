# OoT Native for Android (Switch 2 Edition)

[![Support with Bitcoin](https://img.shields.io/badge/Support_with-Bitcoin-F7931A?style=for-the-badge&logo=bitcoin&logoColor=white)](#support)

**OoT Native** (`io.github.p1r4n351.oot`, work in progress) brings the Nintendo Switch 2 edition of **The Legend of Zelda: Ocarina of Time** (`0400486024DEC000`, update `v131072`) natively to 64-bit Android handhelds with Qualcomm Adreno GPUs — targeting the dual-screen **AYN Thor** (`QCS8550` / Adreno 740, 16 GB RAM) with a second-screen companion on the bottom display.

It is **not a CPU emulator**: the game's AArch64 modules (`main`, `subsdk0`, `sdk`) are rewritten once to execute directly on the phone's ARMv9 cores, Horizon / Switch 2 (`Ounce`) services are answered natively by the host runtime, and NVN 2 + Ampere (`sm86`) shaders execute on Vulkan 1.3 with Mesa Turnip. The repository and APKs contain **no game files, keys, firmware, shaders, saves, or Nintendo artwork** — you supply your own legally obtained dump from your own Switch 2.

---

## Current Status

- **M0 (Dump, Container & Shader Inventory) — Complete:** Verified NUP v1 layout, SDK 22.2.8 (`Ounce`) module digests (`PKG[0]` `v65536` + `PKG[1]` `v131072`), `ZBIC` segment encoding, `186,583`-file RomFS, and `42,845`-shader Ampere `sm86` (`0x98761234` v`0xc9`) corpus (`15,851` VS, `22,352` FS, `20` task, `500` mesh, `4,122` CS).
- **M1 (Rewriter, NVN 2 & Adreno 740 Gap Audit) — Complete:** Verified `patch_nso.py` (`DM_HOST_TP=tpidr_el0`) across `subsdk0`, `sdk`, and `main` (`51,654` slot sites, `0` gates), catalogued all `259` NVN 2 entry points beyond Switch 1 NVN 1, and verified live Adreno 740 Vulkan extensions (`VK_EXT_shader_atomic_float` present; `VK_EXT_map_memory_placed` via bundled Mesa Turnip `27.0-devel`; `VK_EXT_mesh_shader` absent, requiring Vulkan compute-mesh lowering).
- **M2a / M2b (Host, Pack & Shader Library) — Complete:** Integrated BananaNative (`af1ed596`, `v0.3.43`), built `v131072` `android-pack` (`main`/`sdk`/`subsdk0`), and compiled `42,843` SPIR-V shaders (`52,858` modules, `695.6 MB` `dmsh_vk.lib`).
- **M2c (16 GB AYN Thor Boot & Live Gameplay Verification) — Complete:** Verified `22.4 min` cold-to-3D run (`26,109` frames, `0` crashes, `170 MiB` cached Turnip pipelines, `267,000` `audren` frames with `0` underruns, `25,246 / 65,530` VMAs = `38.5%`), fixed Android 13 `vkMapMemory2` + `nn::codec` AV1 `60 fps` lock + `driver.id=18` Turnip defaults (`a5fc6b359894`), and verified `206 ms` warm pipeline load, `60.0–60.2 fps` title, and stable `15 fps` Kokiri Forest playback with synchronized audio.
- **M3 (Production Identity, Companion Waist, Bifrost & Controller Focus) — Complete:** Added `companion_state.h` + `oot_companion.h/.c` waist (`20 Hz` reader, `3`-frame pause hold from `0x4BE4FB0`, Ocarina `page 4`), `BifrostBridge` stick-LED controller + localized `GameSettings` toggle, procedural emerald/brass vector icons & banner (`scripts/make_oot_launcher_art.py`, zero Nintendo art), `PadFocus.hasGamepad()` initial focus, and verified `OoT-Native-0.2.0.apk` (`io.github.p1r4n351.oot`, `9,278,180` B, SHA-256 `20d6dc4aa3ff2d4820694c8567c8d6753762205e2373d3dc09a14c7c01db92ee`) + `OoT-Native-0.2.0-dev.apk` (`io.github.p1r4n351.oot.dev`, `9,278,356` B, SHA-256 `042324786f867e6b2a305634f8f4ba3efb1178c0dbaadf50b8d15e23f1442040`).
- **Second-Screen Tie-In Ledger — Active:** `30+` `gmd::GameDataMgr` hashes, live `Player` XYZ + camera pointers, transition/dim guards, `OOT-29` pause-canvas detection, and save structures documented in [games/oot/SECOND_SCREEN_NOTES.md](games/oot/SECOND_SCREEN_NOTES.md).
- **Next Up (M4 — Second-Screen Companion):** Build the Display 4 `Presentation` companion (`1240×1080` declarative UI, Rodin NTLG B RomFS font decode, north-up/camera-up maps, C-button touch assignment, and Ocarina page) alongside on-device `0.2.0` Bifrost verification and Kokiri Forest GPU profiling.

Full technical inventory, reference audit, and gap analysis: **[games/oot/README.md](games/oot/README.md)**.
Roadmap, completed milestones, and upcoming technical hurdles (`H1`–`H5`): **[MILESTONES.md](MILESTONES.md)**.

---

## Help Wanted / Contributing

Community contributors and testers are welcome! High-leverage areas open right now:

1. **Adreno 740 / Mesa Turnip `ir3` Profiling:** Reducing cold-compile stalls and register spills on large Ampere-translated vertex shaders and compute-lowered mesh passes (`20` task + `500` mesh shaders).
2. **Pause / Menu & Active-Cutscene Memory Signals (`v131072` `e0f3ccfa`):** Pinpointing the exact pause-menu-open and active-Navi-hint addresses in `main` to pair with the 5 verified scene-transition booleans in [games/oot/SECOND_SCREEN_NOTES.md](games/oot/SECOND_SCREEN_NOTES.md) (`OOT-24`, `OOT-25`).
3. **Second-Screen Companion (`0400486024DEC000`):** Declarative `1240×1080` dual-screen pages (north-up / camera-up scene & dungeon maps, item/equipment grids, guarded C-button touch assignment, Spiritual Stones / Medallions / Gold Skulltula status, and learned Ocarina song reference).
4. **Device Testing (12 GB vs 16 GB Snapdragon / Xclipse handhelds):** Tracking `/proc/<pid>/maps` VMA coalescing (`vm.max_map_count`) and thermal stability across extended dungeon transitions.

Please **never** include game binaries, keys, firmware, extracted assets, raw shader dumps, or save files in issues or pull requests.

---

## Support

If this work is useful to you, donations are gratefully accepted:

- Bitcoin: `bc1q4a3lqcg87rvw7vxulausdqhhuwkwwa6w6v4mxx`
- VitaDB profile: [PiSCES](https://www.rinnegatamante.eu/vitadb/#/user/PiSCES)

---

## Credits & Upstream Ecosystem

This project builds on and coordinates with open-source Switch 1 / Switch 2 native port and companion efforts:

- **[BotW Native](https://github.com/P1R4N351/botw-android)** & **[TotK Native (fork)](https://github.com/P1R4N351/totk-android)** (based on **[AurelioB/totk-android](https://github.com/AurelioB/totk-android)**, MIT): Android controller menu, Mesa Turnip integration, `companion_state.h` waist, and [Bifrost](https://github.com/KuriGohan-Kamehameha/Bifrost) stick-LED bridge.
- **[nxnative](https://github.com/seedds/nxnative)** by seedds (MIT), **[Banana Native](https://git.eden-emu.dev/moceure/BananaNative)** by moceure & contributors (MIT core; GPL-3.0-or-later mesh/compute & LSFG components), **[Anka Native](https://github.com/EddizEge/AnkaNative)** by EddizEge (MIT), and **[MacNativeGames](https://github.com/ly05010419/MacNativeGames)** by ly05010419 & contributors (MIT).
- **[a55uka/nup](https://github.com/a55uka/nup)** (MIT), **Ryujinx** (MIT), and **[Mesa](https://mesa3d.org) Turnip** (MIT).
- **Eden Duo** & **[eden-duo-companions](https://github.com/P1R4N351/eden-duo-companions)** (GPL-3.0-or-later) for the dual-screen companion runtime.
- **[NaviScout](https://github.com/jfhutchi/NaviScout)** by jfhutchi (MIT), **[OOT-SW2SE](https://github.com/Traiver/OOT-SW2SE)** by Traiver (MIT), **[oot-editor](https://github.com/christianfurr/oot-editor)** by christianfurr (MIT), and **[NX2navi-Trainer](https://github.com/Miragui/NX2navi-Trainer)** by Miragui (MIT) for verified `v131072` `gmd::GameDataMgr` and actor layouts.

---

## License

Repository-authored documentation, scripts, and clean interfaces are licensed under the **MIT License** ([LICENSE](LICENSE)). Any build or APK integrating GPL-3.0-or-later components (such as the **Eden Duo** companion host or GPL-3.0 compute-mesh / shader-translation components) is distributed as a whole under **GPL-3.0-or-later** with complete corresponding source published in this repository. Third-party components retain their respective licenses.

---

## Legal

**You must own the game.** Use this project only with a copy of *The Legend of Zelda: Ocarina of Time* that you legally purchased and dumped from your own Nintendo Switch 2 console. This is an independent, non-commercial interoperability and preservation research project; it is not affiliated with, endorsed by, or sponsored by Nintendo. *The Legend of Zelda*, *Ocarina of Time*, and *Nintendo Switch* are trademarks of Nintendo.
