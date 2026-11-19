# PC Decomp / Recomp Audit — Reusing Upstream Work for `0400486024DEC000`

This audit evaluates what can be reused from existing PC decompilation and static-recompilation projects targeting **The Legend of Zelda: Ocarina of Time** for `oot-android` (`0400486024DEC000`, Switch 2 update `v131072` / `2.0.0`, `main` NSO Build ID `e0f3ccfaf50065ae4ea01eb5aeb13b1a45da3348`).

---

## 1. Two Distinct Ecosystems (Architectural Boundary)

1. **Classic 1998 N64 Decompilation (`zeldaret/oot` + `HarbourMasters/Shipwright`)**
   - Reconstructs the 1998 MIPS N64 ROM (`gPlayState` / `gSaveContext`).
   - **Cannot** replace Switch 2 OoT binary logic (`0400486024DEC000`), which runs on Nintendo EPD's 2026 **Project Owl** engine (`sead` / `agl` / `gsys` / `Phive` / `gmd::GameDataMgr`, `NintendoSDK 22.2.8`).
   - **Reusable here:** Canonical Ocarina song note sequences (all 12 songs), Gold Skulltula regional breakdown (`100`), Heart Piece grouping (`36`), and *Ship of Harkinian* tracker/songbook UX patterns for M4.

2. **Switch 2 `0400486024DEC000` PC Static Recompilations, Emulators & Mod Tools**
   - Because `oot-android` executes the owner's `v131072` ARM64 NSO code **natively on ARM64 Android** (`patch_nso.py --target android`), CPU lifters (`a64-kit` / `suyu` AArch64->C) are unnecessary, while **game-specific memory offsets, shader keys, Engine/SDK fixes, and runtime hooks (`v131072`) transfer 1:1** after binary verification against the unpacked `main` image.

---

## 2. License Triage (Hard Boundary)

`oot-android` distributes compiled APKs under **GPL-3.0-or-later** (`THIRD_PARTY_NOTICES.md`, `android/NOTICE.md`). Only license-compatible upstream sources may be referenced or adapted:

| Upstream Project | License | Status for `oot-android` | Scope |
|---|---|---|---|
| **BananaNative** (`moceure/BananaNative` `af1ed596`) / `seedds/nxnative` | `MIT` core + `GPL-3.0-or-later` (`InstEmitAmpereMesh`, `vka_memu`, `lsfg`, `oot_wide`) | **Integrated (M2a base)** | Host, NVN2->Vulkan, `vka_memu`, `dmsh_xlate`, `oot_wide/foliage/shadow/fog`, `frame_step` |
| **Hookshot** (`NotRexed/Hookshot`, taken down) / **Longshot** (MagicBeanDev / `Dimi42FR/OcaDim` mirror) | **GPL-3.0** | **Reusable** (respecting GPL-3.0 & excluding copyrighted game saves/binaries) | `oot_state_tap.c`, `oot_patches.py`, `oot_autoload.c`, `oot_text_size.c`, `oot_high_fps.h` |
| **Squadala!** (`daurosk/oot-remake-multiplayer`) | **GPL-3.0** | **Reusable** | Verified `v131072` `addresses.hpp` (`GameUI`, `SceneModule->Player`, `BancModule`, `EventMgr`, `AS` controller, `EquipmentUser`) & `scene.hpp` (`DemoOcarina` vs cutscene rules) |
| **Longshot-Narrator** (`Predator04/Longshot-Narrator`) | **GPL-3.0** | **Reusable** | `MSG_HOLDER` (`main + 0x4e790b8`) & `CHOICE_ROOT` (`main + 0x4e84510`) dialogue-box layouts |
| **Lichii** (`zortos293/Lichii`) / **Eglantine** (`Eglantine-Community/Eglantine`) | **GPL-3.0** | **Reusable** | `oot-network-bypass.md` (post-tonemap sharpen bypass), `ocarina-dynamic-fps.md`, `tools/nx2/catalog/0400486024DEC000.json` |
| **Links-Toolbox** (`losava/Links-Toolbox`), **NX2-ZOOTR** (`abslswrld/NX2-ZOOTR`), **NaviScout**, **OOT-SW2SE**, **Peanuts Native** (`igawa6/peanuts-native`) | **MIT** | **Reusable** (code/IDs only; exclude derived map PNGs) | Item/quest `MurmurHash3` catalogs, `Companion.java`/`CompanionPresentation.java` AYN Thor `Screen-2` fixes |
| **NX2navi** (`tangerineturtle1/NX2navi`) | **EXCLUDED on post-base commits** (`LICENSE-ADDITIONS`: `Copyright (c) 2026 chrg6. All rights reserved`) | **DO NOT COPY CODE** from post-base commits or closed-source `Triforce` | Documented license red line only |

---

## 3. Disassembly-Verified `v131072` Pointer Bridge (`main` = `e0f3ccfa`)

PC recompilations (`Longshot`) map `main` statically at `0x80020000`, whereas Android `BananaNative` maps `main` dynamically at `main_base` and NaviScout reads GOT slots in `.data.rel.ro`. Direct inspection of GOT entries and instructions in `v131072` `main` confirms that **all three coordinate systems resolve to identical objects**:

| Logical Object | GOT Slot (`main + off`) | Verified GOT Target (`main + off`) | `Longshot` VA (`main @ 0x80020000`) | First Verified Code Site (`v131072`) |
|---|---|---|---|---|
| `gmd::GameDataMgr*` | `0x4c7e180` (`NaviScout`) | **`0x4e842a8`** (`BananaNative` cheats) | `0x84c9e180` | `main + 0x11fcffc`: `str x20, [x22, #0x2a8]` |
| `GameUI*` | `0x4c7e2a8` | **`0x4e7cfa8`** (`Squadala!`) | `0x84c9e2a8` | `main + 0x32a510c`: `adrp x8, #0x4c7e000; ldr x8, [x8, #0x2a8]` |
| `SceneModule*` | `0x4c7e178` | **`0x4e97648`** (`Squadala!`) | — | `main + 0x37c97c`: `adrp x8, #0x4c7e000; ldr x8, [x8, #0x178]` |
| `BancModule*` | `0x4c7e8a0` | **`0x4e919d0`** (`Squadala!`) | — | `main + 0x58aadc`: `adrp x22, #0x4c7e000; ldr x22, [x22, #0x8a0]` |
| `EventMgr*` | `0x4c7e090` | **`0x4e94840`** (`Squadala!`) | `0x84c9e090` | `main + 0xbcc100`: `ldr x9, [x0, #0x68]` |
| `Views*` (Camera) | `0x4c7e888` (`NaviScout`) | **`0x4e919e0`** | `0x84ca6bc0` (`cam` mgr) | `NaviScout::camera_direction` |
| `MessageObject*` (`MSG_HOLDER`) | `0x4c7e908` | **`0x4e790b8`** (`Longshot-Narrator`) | `0x84e990b8` | GOT exact match verified |
| `ChoiceWindow` (`CHOICE_ROOT`) | — | **`0x4e84510`** (`Longshot-Narrator`) | `0x84ea4510` | `main + 0x37818ac` direct `ADRP+LDR` verified |

Also verified with `-0x80020000` rebase against `.text`: `LetterAnimControl` text speed at `main + 0xafe140` (`pacibsp`), `GameDataMgr::getBool` at `main + 0x20d9c0`, and New-Game opening skip hooks at `main + 0xd7a820`, `main + 0xdc6cb0`, `main + 0x3b3bfb0`.

---

## 4. Integrated Now vs Planned Next Milestones

### Integrated Now (M2c Prep)
- **`dm/host/android/oot_companion_tap.h`** (`GPL-3.0-or-later`) + **`dm/host/tests/t_oot_companion_tap.c`**:
  - Zero-malloc, NASA-Power-of-Ten bounded read-only state sampler reading `GameDataMgr` typed containers (`Bool`, `Int`, `Float`, `String64`), `GameUI`, `SceneModule -> Player`, `AS` slot `0` animation command (`"DemoOcarina"` vs `"EvtWait"` vs `"Horse*"`), `EventMgr` + player performer link (`+0x88` / `+0x90`), `BancModule`, `MSG_HOLDER`, all `3` Spiritual Stones, all `6` Medallions, all `12` `IsGet.Item_Melody_*` Ocarina songs, and equipped gear.
  - Exports `oot_companion_write_safe(snap)` gating companion writes against loading, transitions, non-ocarina cutscenes, and open dialogue boxes.
- **Root-caused `H1` `5` Point-Mesh Shaders:**
  - Confirmed `vk_android_build.py` on a Linux host with `SPIRV-Tools v2025.1` rejected `2582bd7ea6490d33`, `28f82c129deea140`, `3a6340a04bf09797`, `58d06e7230b59741`, and `a203347bb7bdbd03` on a known `v2025.1` scalar-vs-array validator defect (`VUID-CullPrimitiveEXT-CullPrimitiveEXT-07036`), whereas upstream `BananaNative` pins **`vulkan-sdk-1.4.363.0` (`spirv-val v2026.4`)** (`build_spirv_tools.sh`) where `vk_delta.tsv` records all 5 as valid.

### Planned Adoption in M3–M4
1. **H2 Shader/GPU Relief (`Lichii` `oot-network-bypass`):** If Kokiri Forest / Market is fragment/compute-bound on Adreno 740 Turnip, evaluate bypassing OoT's post-tonemap compute smoother + 2.2x halo sharpener (`6f9367bd41dc0b58`, `6093c9143ba0904d`, `cb0+0xc0` -> `cb0+0xd0`), measured at +22% FPS in upstream `Lichii`.
2. **M3/M4 AYN Thor `Screen-2` Presentation Hardening (`peanuts-native`, `MIT`):** Adopt `WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE` (so bottom-screen taps never steal gamepad focus from Display 0), `away()`/`back()` `hide()`/`show()` lifecycle handling on `onStop`/`onStart`, and system `SCREEN_BRIGHTNESS` synchronization on Display 4.
3. **M4 Automatic Companion Views (`Squadala!` + `Shipwright` UX):** Trigger automatic Songbook overlay when `playing_ocarina != 0` (`AS` command `"DemoOcarina"` / `"Ocarina*"`), surface Gold Skulltulas + Heart Piece trackers per current `Sequence_CurrentBanc`, and gate all C-button / inventory mutations via `oot_companion_write_safe()`.
