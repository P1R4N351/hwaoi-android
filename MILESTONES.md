# Milestones & Technical Hurdles — OoT Native for Android (Switch 2)

Target title: **The Legend of Zelda: Ocarina of Time** (`0400486024DEC000`, update `v131072` / `2.0.0`, `NintendoSDK 22.2.8` / `Ounce` / NVN 2).
Target device: **AYN Thor** (`QCS8550` / Adreno 740, `16 GB` RAM, Android 13, 1080p top screen + bottom companion `Presentation` on display 4).

---

## Completed Milestones

### M0 — Container, Binary & Shader Corpus Inventory (Complete, 2026-11-18)
- Verified owner's NUP v1 container (`25,622,267,860` B, SHA-256 `85b08e81a2a08612f88de1063c8581fe088f7554303aa1ff207972d729b5916f`, TOC SHA-256 `0cb62756768999d3f0eab2690d75a262521d4a5c3bebb7ea1cea5d3e60618ca3`).
- Catalogued `PKG[0]` (`v65536` / `1.0.0`) and `PKG[1]` (`v131072` / `2.0.0`, `main` Build ID `e0f3ccfaf50065ae4ea01eb5aeb13b1a45da3348`): `NintendoSDK 22.2.8`, Project Owl engine (`sead` / `agl` / `gsys` / `Phive`), `main` (`63.2 MB` `.text`, `ZBIC` `flags=0xff`), `subsdk0` (`NintendoSDK_movie-22_2_8`), `sdk` (`NintendoSDK_NVN-22_2_8`), `46` declared `ACI0` services, and `main.npdm` (`48 MiB` system resource, `6.20 GiB` startup heap).
- Verified `186,583`-file RomFS (`26,236,422,768` uncompressed bytes, `Pack/ZsDic.pack.zs` dictionaries), `16` message locales in `Mals/`, and TotK-compatible `slot_00`..`slot_05` save container (`0x01020304` v`0x002D80EB`).
- Sampled and verified Switch 2 shader archives (`5` `.Nin_Ounce_NVN2.bfsha.zs` archives -> `FSHA` / `BNSH`, NVN 2 control magic `0x98761234` v`0xc9`, **42,845 Ampere `sm86` shaders**: `15,851` VS, `22,352` FS, `20` task, `500` mesh, `4,122` CS).
- Pinned ExeFS & RomFS metadata in [games/oot/assets/expected-v131072.json](games/oot/assets/expected-v131072.json).

### M1 — Rewriter, NVN 2 & Adreno 740 Hardware Gap Audit (Complete, 2026-11-18)
- Audited reference codebases and licences: `pavelgajdo/Hokkaido` (`GPL-3.0-or-later`), `Eden SX2A` (`GPL-3.0-or-later`), `ly05010419/MacNativeGames/games/oot` (`MIT`, Apple Metal 4 only), `moceure/BananaNative` (`MIT` core + `GPL-3.0-or-later` `InstEmitAmpereMesh.cs` / `vka_memu.c` / `lsfg`), `jfhutchi/NaviScout` (`MIT`), `Miragui/NX2navi-Trainer` (`MIT`), and `Traiver/OOT-SW2SE` (`MIT`).
- Ran ARMv8/v9 ISA census on `main` (`126,456` `retab`/`retaa`, `70,970` `ldapr`, `24,536` LSE atomics, `0` plain `ldxr`) and verified `patch_nso.py` (`DM_HOST_TP=tpidr_el0`) across `subsdk0` (`3,369` sites), `sdk` (`25,983` sites, `102` gates), and `main` (`51,654` slot sites, `0` gates, `PATCH_OFF=0x4eb0000` once `ZBIC` decode + `DT_GNU_HASH` fallback are applied).
- Diffed all `775` `nvn*` functions referenced in `v131072` against Switch 1 NVN 1 (`523` functions): identified **`259` missing NVN 2 entry points** (`DrawMeshTasks*`, push constants, deferred descriptor binds, `ProgramInitializeFromBuilder`, `WriteDescriptorTableData`, `nvnEvent*`, `SyncpointsNVX`, `Dlss*`, Switch 2 GOB deswizzle).
- Audited live Adreno 740 Vulkan 1.3 capabilities on AYN Thor (`cmd gpu vkjson`): `VK_EXT_shader_atomic_float` is present; `VK_EXT_map_memory_placed` requires bundled Mesa Turnip `27.0-devel`; **`VK_EXT_mesh_shader` is absent** on both Qualcomm `1.3.128` and Mesa Turnip `a740`, establishing that Vulkan compute-mesh lowering (`vka_memu`) is mandatory.
- Established the second-screen tie-in ledger in [games/oot/SECOND_SCREEN_NOTES.md](games/oot/SECOND_SCREEN_NOTES.md) (`26` verified entries: `gmd::GameDataMgr`, `Player` XYZ, `<6d>` camera vectors, `Sequence_CurrentBanc`, `radar_location`, `Sequence_IsChildhood`, `5` transition/dim guards, vitals, inventory, small keys, bottles, songs, and C-button slots).

---

## Active & Planned Milestones

### M2 — Native Android Host, Offline Pack & 16 GB Thor Gameplay Bring-Up (Active)
- **M2a:** Import `moceure/BananaNative` (`android` branch) into `oot-android` while keeping `README.md`, `MILESTONES.md`, `FUNDING.yml`, and `games/oot/` intact; configure single-game build (`io.github.p1r4n351.oot.dev` / `0400486024DEC000`); build `OoT-Native-0.1.0-dev.apk` and bundle Mesa Turnip `27.0-devel` (`cs-slabs`) driver zip.
- **M2b:** Prepare verified offline `v131072` `android-pack` (rewritten `main`/`subsdk0`/`sdk` + `spirv-val`-checked `dmsh_vk.lib` SPIR-V shader library + cheats).
- **M2c:** With owner approval, stage and run on the 16 GB AYN Thor; validate Kokiri Forest 3D rendering, frame pacing (30/60 fps), AAudio, AV1 movies, vibration query, and save round-trips (`slot_00/progress.sav`).

### M3 — App Identity, Controller Menu, Waist & Bifrost Stick-LED Bridge (Planned)
- `io.github.p1r4n351.oot` package identity, controller-first launcher/settings, procedural launcher artwork (zero Nintendo assets), `companion_state.h` waist wiring, and Bifrost stick-LED state bridge.

### M4 — Second-Screen Companion on Display 4 (Planned)
- Author `OcarinaOfTime` (`0400486024DEC000`) Eden Duo native module + declarative `1240x1080` pages following `companion-convergence.md` rules (on-device Rodin NTLG B font decode from RomFS, north-up / camera-up scene & dungeon minimaps, item/equipment grids, guarded C-button touch assignment, Spiritual Stones / Medallions / Gold Skulltulas, and learned Ocarina song reference / guarded live playback).

---

## Upcoming Technical Hurdles (`H1`–`H5`)

| ID | Hurdle | Why It Matters & Planned Check / Mitigation |
|---|---|---|
| **H1** | **Compute-mesh lowering (`vka_memu`) & Turnip `ir3` register spills on Adreno 740** | Adreno 740 lacks `VK_EXT_mesh_shader`, so all `20` task + `500` mesh shaders must lower to compute dispatches. Large Ampere vertex shaders (e.g. `4671e5e647fc5455`, `f4e8b877f70ea62f`) have shown `ir3` register spills and 6–7 s cold compiles on Turnip; pre-compiling `dmsh_vk.lib` + embedding `vk_prewarm.bin` and profiling draw/compute passes on Thor is required. |
| **H2** | **Android `vm.max_map_count` (`65,530`) VMA exhaustion under `VK_EXT_map_memory_placed`** | Switch 2 OoT reserves a `9 GiB` pool (`6.20 GiB` startup heap) and rotates many placed pools across area transitions; VMA fragmentation caused crashes past ~150 s on other phones. Verify Turnip + host placed-pool coalescing and dirty-tracking release via `/proc/<pid>/maps` counts during area transitions. |
| **H3** | **Project Owl vsync-ratio vs real-time logic time-step (`main+0x6e6cc4`, `0x3b4eaf8`, `0x3b4ef60`)** | Without host-governed step folding / `Hold 60 fps`, the game drops to half-speed when frame times slip just past 16.7 ms on handhelds. Measure actual wall-clock game speed vs frame rate at 30 fps and 60 fps targets on Thor. |
| **H4** | **SDK 22 `Ounce` service & media edge cases** | (1) Bypass the `47.5 ms/frame` NDVE movie tensor network (`HMMA`) while validating `AMediaCodec` / `dav1d` AV1 decode; (2) verify `nn::hid::IsOunceVibrationDeviceMounted` so playing an Ocarina note does not abort; (3) clamp host locale to the `16` locales shipped in `Mals/` so `main+0xa7ec18` never faults. |
| **H5** | **Fine-grained pause-menu, cutscene & Navi-hint offsets in `v131072` (`e0f3ccfa`)** | `OOT-07` gives 5 safe scene-transition booleans, but live inventory/C-button writes from the bottom screen also need exact pause-menu-open and active-cutscene flags (`OOT-25`) plus live Navi hint IDs (`OOT-24`) verified in `e0f3ccfa`. |
