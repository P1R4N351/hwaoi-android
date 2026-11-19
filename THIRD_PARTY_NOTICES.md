# Third-Party Notices

`oot-android` builds on upstream [BananaNative](https://git.eden-emu.dev/moceure/BananaNative) (`android` branch, commit `af1ed5967cfdca16b6908bb738503c6d8b0d101f`), itself derived from [nxnative](https://github.com/seedds/nxnative). Full upstream module-level license notices and source attributions live in [android/NOTICE.md](android/NOTICE.md), [dm/third_party/README.md](dm/third_party/README.md), and [LICENSE](LICENSE).

## Summary of Upstream & Bundled Components

| Component | Source / Reference | License | Notes |
|---|---|---|---|
| BananaNative core & offline tools | [moceure/BananaNative](https://git.eden-emu.dev/moceure/BananaNative) (`af1ed5967cfd`) | MIT (`LICENSE`) | Merged base runtime, host, rewriter, and launcher |
| nxnative core | [seedds/nxnative](https://github.com/seedds/nxnative) | MIT | Upstream host/rewriter foundation |
| File-backed memory & shader pools | [EddizEge/AnkaNative](https://github.com/EddizEge/AnkaNative) v0.2.2 | MIT | Integrated in upstream `vka_fileback.h` / `hle_nvn.c` |
| NUP format specification | [a55uka/nup](https://github.com/a55uka/nup) (`92089de3`) | MIT | Read-only container format (`dm/tools/nup.py`) |
| Ryujinx shader translator subset | [Ryujinx](https://github.com/Ryujinx/Ryujinx) 1.3.2 | MIT + GPL-3.0-or-later | `InstEmitAmpereMesh.cs` is GPL-3.0-or-later (`dm/third_party/ryujinx/COPYING.GPL-3.0`) |
| Compute-mesh lowering (`vka_memu.c`) | After [Hokkaido](https://github.com/pavelgajdo/Hokkaido) #569 / #642 | GPL-3.0-or-later | Required on Adreno 740 without `VK_EXT_mesh_shader` |
| LSFG frame generation (`dm/gfx/vk_android/lsfg`) | [Eden](https://git.eden-emu.dev/eden-emu/eden) / `lsfg-vk` | GPL-3.0-or-later | Included in Android APK by default (`android/NOTICE.md`) |
| `oot_wide.c` camera & HUD aspect | After GameBanana 737495 / NX2navi / [Hookshot](https://github.com/NotRexed/Hookshot) | GPL-3.0-or-later | See `android/NOTICE.md` |
| `dav1d` decoder | VideoLAN (`dm/third_party/dav1d/`) | BSD-2-Clause | AV1 decoding support |
| `libvpx`, `libadrenotools`, `zstd`, `zstd-zbic`, `stb_vorbis`, FSR 1/3, Noto Sans | `dm/third_party/` | BSD / MIT / SIL OFL 1.1 | See [dm/third_party/README.md](dm/third_party/README.md) |
| Mesa Turnip (`libtotk_turnip.so`) | Mesa 3D (`turnip` Adreno Vulkan driver) | MIT | Bundled via AdrenoTools driver zip (`minApi: 33`) |
| Eden Duo second-screen architecture (planned M4) | [Eden](https://git.eden-emu.dev/eden-emu/eden) | GPL-3.0-or-later | Planned companion display integration after gameplay |
| Switch 2 OoT guest state sampler (`oot_companion_tap.h`) | [Longshot](https://github.com/Dimi42FR/OcaDim) (`oot_state_tap.c`), [daurosk/oot-remake-multiplayer](https://github.com/daurosk/oot-remake-multiplayer), [Longshot-Narrator](https://github.com/Predator04/Longshot-Narrator) | GPL-3.0 | Read-only `v131072` `GameDataMgr`, `GameUI`, `SceneModule->Player`, `AS` controller, `EventMgr`, and `MSG_HOLDER` offsets & cutscene rules |
| `v131072` item, song & quest hash tables; Thor `Screen-2` focus/brightness patterns | [NaviScout](https://github.com/jfhutchi/NaviScout), [OOT-SW2SE](https://github.com/Traiver/OOT-SW2SE), [Links-Toolbox](https://github.com/losava/Links-Toolbox), [NX2-ZOOTR](https://github.com/abslswrld/NX2-ZOOTR), [peanuts-native](https://github.com/igawa6/peanuts-native) | MIT | Verified `MurmurHash3` seed-0 flag/item keys and non-focusable `Presentation` patterns (copyrighted game images/saves excluded) |
| Post-tonemap sharpener & dynamic-FPS documentation reference | [Lichii](https://github.com/zortos293/Lichii), [Eglantine](https://github.com/Eglantine-Community/Eglantine) | GPL-3.0-or-later | Upstream H2 sharpener bypass analysis & catalog reference (see [docs/PC_RECOMP_AUDIT.md](docs/PC_RECOMP_AUDIT.md)) |

Note on `tangerineturtle1/NX2navi`: its `LICENSE-ADDITIONS` revokes reuse rights on post-base commits; no post-base `NX2navi` or closed-source `Triforce` code is used in `oot-android`.

Because `libnxhost_0400486024DEC000.so` links GPL-3.0-or-later components (`vka_memu.c`, `lsfg`, `oot_wide.c`) and the shader translator includes `InstEmitAmpereMesh.cs`, distributed APKs are governed as a whole under **GPL-3.0-or-later** (`android/NOTICE.md`), while individual MIT/BSD/OFL source files retain their respective licenses.
