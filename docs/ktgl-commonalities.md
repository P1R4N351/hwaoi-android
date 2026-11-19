# Koei Tecmo KTGL Commonalities & Cross-Venture Optimizations (`AoC` + `AoI`)

Both **[AoC Native](https://github.com/P1R4N351/hwaoc-android)** (*Hyrule Warriors: Age of Calamity*, `01002B00111A2000`, update `v327680` / `1.3.0`) and **[AoI Native](../README.md)** (*Hyrule Warriors: Age of Imprisonment*, `04003F40232DE000`, update `v196608`) are built by Koei Tecmo (Omega Force) on successive generations of the same **KTGL** + **KIDS** engine stack. Insights and fixes uncovered on either title carry directly across both ports.

---

## 1. Verified Engine, Graphics, Audio & Save Commonalities

| Subsystem | Age of Calamity (`01002B00111A2000`, `1.3.0`) | Age of Imprisonment (`04003F40232DE000`, `v196608`) | Shared Port Implications |
|---|---|---|---|
| **Engine & Job Graph** | `KTGL For NX(a64)` + `KIDSTaskScheduler` / `KIDSResourceScheduler`, `NintendoSDK 9.6.0` | `KTGL For Ounce(a64)` + `KIDS`, `NintendoSDK 20.5.15` | Multi-clock pacing (`KIDSTask`, KTGL device, scene float-fps); heavy atomics (`25,976` `ldaxr` / `24,855` `stlxr` in AoC `main`) |
| **Graphics API & Shaders** | NVN `53.312` (`NEAR_IS_MINUS_W`), Maxwell shaders, `230` GLSLC hits in `main` + KTGL `G2S` containers (`53` harvested `V` rows) | NVN 2 (`Ounce`), Ampere `sm86` (`161` Ampere + `322` GLSLC hits in `main`), `101,860` catalogued shaders (`15,449` runtime-seen), `0` mesh/task shaders | Separable `nvnCommandBufferBindProgram` (`VS`/`FS`/`CS`), dedicated `c1` constant-buffer slices, large descriptor/texture pools |
| **VFS & Containers** | `1,101` RomFS files (`11.72 GB`): `8` `.rdb` (`_DRK0000`), `24` `.rdb.bin*` (`IDRK0000`), `970` `asset/data/*.file`, `97` `.webm` | `2,174` RomFS files (`46 GB` NUP v1 `PKG[1]`): `root.rdb`/`system.rdb` + `1,922` `.fdata` (`PDRK0000`/`IDRK0000`) under `b6e28c40/`, `129` `.bin` under `ebc8c781/` | Streamed container-backed assets (`NUP` zstd chunks / merged `romfs.bin`), hash/offset indexed without loose-file explosions |
| **Audio Middleware** | `KTSL2` (`KTSL2HLUpdate`, `KTSL2_Render`, `ASRS`) + `OpusDecoder`; `1.3.0` requires `4 MiB` shift (`15+142 -> 19+138 MiB`) from data to engine heap | `KTSL2` over `nn::audio::OpenAudioOut` (6 ch -> `48,000 Hz` stereo AAudio, `0` underruns after boot) | Fixed upfront audio engine/data heap partition; zero CRIWARE ADX/Sofdec dependencies |
| **Video Playback** | `subsdk0` `NintendoSDK_movie` VP9 + Opus (`.webm`), decoded to `R8` Y + `RG8` UV NV12 textures | `subsdk0` `NintendoSDK_movie` VP9 (`.bin` EBML `.webm`), `1920x1080` at `59.7–59.8 fps` (`8.17 ms/packet`) | Host `libvpx` (`4` threads) + ARM NEON `vst2q_u8` planar YUV-to-NV12 interleave ([`dm/host/hle_movie.c`](../dm/host/hle_movie.c)) |
| **Texture Pool Sub-Allocation** | Small/static CPU-mapped KTGL textures packed into shared `64 KiB` dirty-tracking units alongside per-frame CPU uniform/staging writes | Small `2x1` format `0x2e` textures packed on shared `4 KiB` pages alongside active CPU writes | Naive re-protection causes `SIGSEGV` + `mprotect` + `fault_run` (`g_frun = 256` pages) storms (`2 * 257 = 514` released pages/frame on AoI) |
| **Indirect `QUADS` Draws** | `nvnCommandBufferDrawArraysIndirect` with `prim == 7` (`QUADS`), GPU-written `16`-byte indirect buffers | `DrawArraysIndirect` with `prim == 7` (`QUADS`), GPU-written `16`-byte indirect buffers | Vulkan lacks `QUADS` topology; converted via the shared `287`-word `quad_indirect_spv[287]` compute shader + `uint16` `0,1,2,0,2,3` index buffer |
| **Save Data Format** | Single flat `1 MiB` (`1,048,576` B) binary `svdt` (`SaveManager.FLAT_SAVE`) | Single flat `8 MiB` (`8,388,608` B) binary `DATA.BIN` committed at autosave points | No directory-per-slot tree; single fixed-size binary blob initialized at first boot and updated via state machine |

---

## 2. Cross-Venture Optimizations Implemented

### Optimization 1 — AoI (`DM_VK_EDGE_UNPIN=1`): Shared Boundary Page Unpinning & Edge Polling

- **Root Cause (Observed Live on AYN Thor):** KTGL sub-allocates small textures onto shared `4 KiB` pages adjacent to active CPU writes. Each re-protection triggers a `SIGSEGV`, and `nvn_dirty.c` `fault_run()` (`g_frun = 256`) releases up to `257` contiguous `PG_PROT` pages per shared page (`2 * 257 = 514` faults/frame), forcing a full scan of all `757` live images on every submission.
- **Implementation:**
  - [`dm/gfx/vk_android/vka_edge_unpin.h`](../dm/gfx/vk_android/vka_edge_unpin.h), [`dm/gfx/vk_android/vka_tex.c`](../dm/gfx/vk_android/vka_tex.c), [`dm/gfx/vk_android/vka.h`](../dm/gfx/vk_android/vka.h), and [`dm/gfx/tests/t_edge_unpin.c`](../dm/gfx/tests/t_edge_unpin.c).
  - Splits each tracked texture address span into partial boundary ranges `r[0]`, `r[1]` and exclusive interior pages `[ia, ie)`.
  - After `VKA_EDGE_UNPIN_AFTER` (`2`) consecutive neighbor-only skips, registers the texture in compact side-list `g_edge_hot[256]`, leaves shared boundary pages writable, keeps `[ia, ie)` write-protected via `PG_PROT`, and polls `[r0]`/`[r1]` via 64-bit hash comparison on every submit — skipping `SIGSEGV`, `mprotect`, and `full_scan`.
  - During the 2-frame warmup on straddling multi-page textures, snapshots an interior hash (`<= 1 MiB`) so `fault_run` collateral page releases are distinguished from genuine interior writes.

### Optimization 2A — AoC (`!vk_off("edgeguard")`): Pool-Aligned `64 KiB` Edge Hash Guard & Unpinning

- **Gap Addressed in AoC:** On Android/Linux, `nvn_dirty.c` protects memory in **`64 KiB` (`16`-page) units** (`g_unit`), and [`vk_res.c`](https://github.com/P1R4N351/hwaoc-android/blob/main/games/totk/android/gfx/vk_res.c) lacked sub-range edge hashing. Any small static KTGL texture sharing a `64 KiB` unit with per-frame CPU writes suffered both a fault storm and a **full Vulkan texture deswizzle + staging copy + 2 `vk_barrier_all` barriers** on every submission.
- **Implementation:**
  - [`nvn_dirty_unit_info`](https://github.com/P1R4N351/hwaoc-android/blob/main/games/totk/third_party/d2r_gfx/gfx/nvn/nvn_dirty.c) + [`vk_edge_guard.h`](https://github.com/P1R4N351/hwaoc-android/blob/main/games/totk/android/gfx/vk_edge_guard.h) + `vk_res.c` + `test_vk_ktgl_opt.c`.
  - `nvn_dirty_unit_info()` exposes the exact pool-aligned `64 KiB` unit base so boundary splitting aligns with `mprotect` granularity.
  - Snapshots head/tail sub-unit edge hashes (plus warmup interior collateral hash), unpins shared boundary units after `VK_EDGE_UNPIN_AFTER` (`2`) neighbor-only skips, and protects only exclusive full interior `[ia, ie)` units.

### Optimization 2B — AoC (`!vk_off("quadbatch")`): Pre-Pass Indirect `QUADS` Compute Batching

- **Gap Ported from AoI:** Converting GPU-pending indirect `QUADS` draws inline inside `draw_indirect` calls `vk_end_rendering_at(VK_PASS_END_COMPUTE, 0)` on each draw, splitting Adreno TBDR render passes per indirect quad draw.
- **Implementation (ported from AoI [`vka_exec.c`](../dm/gfx/vk_android/vka_exec.c) / [`vka_draw.c`](../dm/gfx/vk_android/vka_draw.c)):**
  - [`vk_quad_batch.h`](https://github.com/P1R4N351/hwaoc-android/blob/main/games/totk/android/gfx/vk_quad_batch.h) + [`vk_draw.c`](https://github.com/P1R4N351/hwaoc-android/blob/main/games/totk/android/gfx/vk_draw.c) + [`nvn_vk.c`](https://github.com/P1R4N351/hwaoc-android/blob/main/games/totk/android/gfx/nvn_vk.c).
  - `vk_qi_scan_ops()` walks upcoming opcodes (`OP_CALL` depth `16`, bounded at `64` items / `4,096` ops) before `begin_rendering_inner` opens the render pass (and on cache misses in `draw_indirect`), stopping cleanly on any buffer-write hazard (`OP_COPY_B2B`, `OP_UPDATE_UB`, `OP_DISPATCH`, `OP_SIGNAL_EVENT`, `OP_REPORT_COUNTER`, non-primitive `OP_BARRIER`) or target change (`OP_SET_RT`).
  - `quad_indirect_convert_batch()` converts up to `64` unique indirect `QUADS` buffers in one compute dispatch pass with a single `vkUpdateDescriptorSets` call and caches results keyed by `(submit_serial, vk_gpu_write_gen)`.

---

## 3. Executed Verification & Benchmark Results

| Venture | Harness / Command | Configuration & Fixture | Measured Baseline -> Optimized Result | Negative Control (Delta #27) |
|---|---|---|---|---|
| **AoI** (`hwaoi-android`) | `dm/gfx/tests/t_edge_unpin.c` on Apple Silicon (`clang -O2 -Werror`, `16 KiB` page) | `1,000` frames x `6` submissions/frame beside active neighbor CPU writes; plus `4`-page straddling texture (`r0` + `2` interior + `r1`) | Faults `1,000 -> 2` (`-99.8%`, `0` steady-state), `mprotect` `2,000 -> 3`, `full_scan` `1,000 -> 2`; `1,621.7 -> 16.2 ns/sub` cold (`100.1x`), `1,288.5 -> 8.7 ns/sub` warm (`148.1x`); `failures=0` | `T_EDGE_UNPIN_FAULT=1` (disables tail `r[1]` hash): `failures=3`, exit `1`; restored: `failures=0` |
| **AoI** (`hwaoi-android`) | Python suite + Android arm64 NDK (`CT 923`, Clang 19 / NDK 28.2) | `7` unittest modules (`59` tests) + full `libnxhost.so` build (`307` targets) | Unittests `59` OK (`skipped=2`), `[307/307] libnxhost.so` (`18M`, `0` warnings on added/edited files) | Checked before & after sort/naming fixes |
| **AoC** (`hwaoc-android`) | `games/totk/android/gfx/test_vk_ktgl_opt.c` on x86_64 Linux (`gcc -O2`, `64 KiB` dirty unit / `4 KiB` page) | Opt 2A: `1,000` frames shared `64 KiB` unit beside active neighbor write + `4`-unit straddling texture | Faults `1,000 -> 2` (`-99.8%`), false Vulkan re-uploads `1,000 -> 0` (`-100%`), `12,277.8 -> 98.4 ns/frame` isolated (`124.8x`) / `2,984.3 -> 13.2 ns/frame` warm (`226.1x`); `failures=0` | `TOTK_KTGL_OPT_FAULT=2` (ignore head hash): `failures=1`, exit `1`; restored: `failures=0` |
| **AoC** (`hwaoc-android`) | `games/totk/android/gfx/test_vk_ktgl_opt.c` (`aoc_quad_batch` suite) | Opt 2B: `8` pending indirect `QUADS` draws (`OP_CALL` + main stream) before `OP_COPY_B2B` hazard + `2` post-hazard before `OP_SET_RT` | Adreno TBDR pass splits `8 -> 0`, barriers `24 -> 3`, `vkUpdateDescriptorSets` `8 -> 1`, post-hazard batch `2`; `failures=0` | `TOTK_KTGL_OPT_FAULT=1` (omit `OP_COPY_B2B` stop): `failures=3`, exit `1`; restored: `failures=0` |
