<!-- PEANUTS: Peanuts' profile of Mario & Luigi: Brothership, adapted from nxnative's profile of the same game (seedds/nxnative, dm/games/01006D0017F7A000, main at 2f959ed). The game.tsv, the shader tables and bf_want.tsv are nxnative's; this note is written for Android. -->
# Mario & Luigi: Brothership (`01006D0017F7A000`)

The base game, 1.0.0 (v0), SDK 12.3.2. Acquire's game on Unreal Engine 4.27.2 (project `Vermillion`) with an IoStore container, NVN, PhysX, Wwise, and Bink movies drawn through a material. Modules `main`, `subsdk0` (the movie module) and `sdk`.

**Status on Android: not yet validated.** The profile and its shader tables are nxnative's, made on the Mac from 1.0.0, where the game plays from a new game through the first battle ([nxnative's note](https://github.com/seedds/nxnative/blob/main/dm/games/01006D0017F7A000/README.md)). Nothing here has been measured on a phone.

## What it needs

No rewriter cuts of its own and no `patches.py`. Every setting is nxnative's.

| Setting | Where | Why |
|---|---|---|
| `depth_mode zero` | [`game.tsv`](game.tsv) | the game sets near = 0 at startup |
| `fps_target 60`, `fps_max 120` | `game.tsv` | the game's device profile asks for 30 fps (`rhi.syncinterval=2`); the host writes Unreal's frame limit by name: [Unreal's frame limit and present interval, found by name](../../../docs/reference/frame-rate.md#unreals-frame-limit-and-present-interval-found-by-name) |
| FMallocBinned3 constructor call cut, the engine's region given what the alias region has room for | the rewriter's engine-wide Unreal rules (`dm/tools/ue_malloc.py`, `dm/tools/ue_region.py`) | as the other Unreal 4.27 games |
| Node area cut to 62 MB, regions aligned to 2 MB | the SDK-wide rules on `sdk` build `5bcf525f…` (`dm/tools/sdk_patches.py`) | the same `sdk` as OCTOPATH TRAVELER II |
| 3.75 GB guest block: `mem_reserve 0xF0000000`, `mem_alias 0xC8000000`, `mem_stack 0x10000000`, `app_pool 0xCD500000` | `game.tsv` | the layout of the other Unreal 4.27 games |
| `sys_resource 0x5000000` | `game.tsv` | the NPDM declares 80 MB of system resource |
| `pad_layout full` | `game.tsv` | the input config binds the D-pad and the right stick |
| `shader_groups GlobalShaderCache=global #Engine/=engine #Vermillion/=game` | `game.tsv` | the global shader cache, the engine's materials and the game's |

## Versions

- **1.0.0 (v0)** is the one build row and the version the shader tables were made from (`package v0`, `shader_versions v0`).
- **1.0.1 (v65536)** has another `main` (`bf0607ac…`; `subsdk0` and `sdk` are 1.0.0's). It is not a build row: the launcher offers "Build anyway" for it. Its shaders have not been scanned, so shaders 1.0.1 added or changed are missing from a pack built from these tables. A 1.0.1 build row wants a split of 1.0.1 and the rewriter checked on its `main`.

## Shaders

45,824 unique (13,581 vertex, 28,296 pixel, 3,814 compute, 133 geometry), inline in the cooked packages of the IoStore container: [Unreal Engine 4.27 shaders inline in IoStore export bundles](../../../docs/reference/shaders.md#unreal-engine-427-shaders-inline-in-iostore-export-bundles). nxnative's Mac build reported 22 compute shaders whose bindless image-buffer reads the translator removes (their results read 0); whether a route reaches them is not known.

The profile carries no prewarm seed list (`pso_manifest.tsv` has only its header): nxnative's list was recorded for the Mac, which Android does not read. Android seeds are built from phone runs.

## Android

- **Reported: the first launch renders the 3D scene white; the second launch on the same pack draws it.** Seen on update 1.0.1 with the earlier version of this profile, which built only 1.0.1. A fault that clears once the pipelines are cached fits [A draw skipped for its pipeline left a result the game kept](../../../docs/reference/rendering.md#a-draw-skipped-for-its-pipeline-left-a-result-the-game-kept-android), not shaders missing from the pack (those would stay missing). The fix there is a seed list, `vk_prewarm.bin`, made with `dm/tools/vk_prewarm.py seed 01006D0017F7A000` from a phone run's `vk_pipelines.keys`; this profile has none yet. No log is kept yet: to confirm, the logs of a first and a second launch on one pack.
- **Open:** a run of 1.0.0 on a phone, the version the tables are for.
- **Open:** 1.0.1 through "Build anyway", and then a scan of its shaders for a 1.0.1 build row.

## Found on this game

- **Inline shaders with the cooked packages in an IoStore container** (`uepak.py`, `shader_split.py`): [Unreal Engine 4.27 shaders inline in IoStore export bundles](../../../docs/reference/shaders.md#unreal-engine-427-shaders-inline-in-iostore-export-bundles).
- **More than one audio renderer** (`hle_audren.c`): [A game with more than one renderer](../../../docs/reference/audio.md#a-game-with-more-than-one-renderer).
