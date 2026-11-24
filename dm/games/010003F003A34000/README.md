<!-- PEANUTS: exact-build base-game profile. -->
# Pokémon: Let's Go, Pikachu!

**Status: tested on AYN Thor with PurpleVK/Turnip T30 (driver ID 18).** The user tested the overworld and wild encounters, and the saved game was visible after an update. The system driver and a full playthrough remain unverified.

Game Freak's engine before Sword and Shield, SDK 5.4.150, NVN and Wwise. The profile is for title `010003F003A34000`, base version `v0` only; it does not register updates or the Eevee edition.

## Builds

| Module | Build ID | Role |
|---|---|---|
| `main` | `1c375692de4f4b879abe88742dc0ca60` | Game |
| `subsdk0` | `7a7ca048269ec2e6ed9a2d812a951d9f` | GLSLC, imported by main |
| `subsdk1` | `15c1c7f4f364010c95aeb5bb8adf30dc` | NintendoSDK_movie |
| `sdk` | `eee08631aff2601e0718f411cfefa13f` | Nintendo SDK |

The rewriter uses the shared build-gated SDK rules and the exact-build timing observations in `patches.py`.

## Shaders

The base RomFS has 27,818 files. A full `shader_split.py` scan of the base RomFS and main found **11,160 unique shaders**: 39 compute, 6,411 pixel and 4,710 vertex. Sources: `bin` 8,357, `main` 2,770 and `systemresource` 33. The split wrote 39 compute-answer rows and 4,442 variation pairs, with no unreadable-file warning. Its supplemental single-stage cover pairs 38 vertex shaders with pixel shader `09e1114630d6ccd4`.

The generated tables and `shader_files.tsv` come from that same split. Runtime shader coverage remains unverified: a device run must check missing-shader dumps before claiming completeness.

The full Windows Vulkan build succeeded for **11,160 of 11,160 shaders** (39 compute, 6,411 pixel, 4,710 vertex). All **11,699 SPIR-V modules**, including 539 unsigned-output variants, passed `spirv-val --target-env vulkan1.3`; the assembled library passed the builder's final check. Library stamp: inputs `124298227a59`, chain `cdcf140b1558`, Vulkan chain `c679ddfbc5db`. No unrecognized binding kinds or undecoded binding slots were reported, and no Float64 capability remains. One shader uses `Int64` and physical-storage-buffer addresses; device suitability and runtime correctness still need testing.

## Memory

The NPDM declares no system resource. `profile_guess.py` selects the standard 4 GiB reservation, 256 MiB alias and stack regions, and `app_pool 0xCD500000`. These partition values have not been measured during a boot.

The four Android-rewritten images total 68,701,896 bytes. `profile_guess.mem_used` rounds the images to 66 MiB and adds 4 MiB, so `mem_used` is explicitly **`0x4600000` (70 MiB)** instead of the default 64 MiB. This is computed from the rewrite output, not a runtime heap measurement.

## Validation

The base title ID, display version and module build IDs were read from the dump. The Android rewrite completed for `main`, `subsdk0`, `subsdk1` and `sdk` without custom cuts.

- 51 focused game-profile, profile-guess, Android profile-options, game-list inclusion, readiness and font tests passed on Windows.
- Runtime-profile generation accepted all 172 profiles, including this one (schema `0x4c8af101`, layout 12,200 bytes). The package/highest-build check passed for `v0`.
- The final Android pack build accepted the rewritten modules and the current shader-library stamp and produced a 114.5 MiB local pack. All generated game binaries remain outside the repository.
- The new note's links and the profile/note/table consistency checks passed. The full documentation check reports 40 unrelated broken links in the checkout.
- The broader Python tool suite did not complete on Windows: JVM subprocess output caused encoding errors and the launcher JVM test stalled. Native host unit tests and Metal checks were not run on this Windows machine.
- A separately signed release APK, `dev.nxnative.android.peanutspikachu` (0.1.7, version code 107, 32,041,947 bytes), compiled successfully and installed on an AYN Thor running Android 13 / API 33. The package is non-debuggable; its signature, API-33 compatibility and archive-hole checks passed. The launcher cold start returned success (253 ms). The original app was retained, and the game and prepared pack were copied into a separate library folder.
- A subsequent Thor run reached the controller-selection screen with main build `1c375692de4f4b879abe88742dc0ca60`, SDK 5.4.150 and PurpleVK 26.3.0-devel / Turnip T30 (driver ID 18). The log's nine FPS readings were 28.3 minimum, 29.9 median and 30 maximum, while waiting at that screen; these are not gameplay performance measurements. No gameplay or system-driver run has been verified.

`readiness.tsv` requires shader draw parameters, used by every vertex shader. It reports degraded readiness without shader Int64 or buffer device addresses, used by one extracted shader whose runtime importance is not known. These are static shader observations, not results from a tested phone.

## Controller selection

**Resolved:** in Docked mode the host reports player 1 as FullKey (Pro Controller). At the selection screen this game requests styles `0x5a`, which include Handheld and single Joy-Cons but exclude FullKey. The log detects the Thor's Odin Controller and assigns it to player 1, yet the game does not accept that style. Switching the existing in-game Console mode setting to Handheld exposes the Handheld npad, which the game accepts. The user confirmed the buttons worked immediately. `op_mode handheld` is now the profile default for future APK builds; the installed app's per-game Handheld choice is already saved.

## Frame-rate ceilings

**Resolved:** the user observed accelerated
gameplay after selecting 60 FPS. The affected release exported no
`fps_ceiling_only` launcher flag: Gradle compared a full TSV row (including its
key) to a one-element value list. Its generated metadata therefore left column
41 empty, and the run log showed `DM_PRESENT_HZ=120` and `DM_VSYNC_HZ=120`.
Changing the limiter from 30 to 60 then allowed roughly twice the native frame
rate and simulation speed. The export now reads the row's value column. The
rebuilt metadata contains column 41 equal to `1`.

The 30/60/90/120 options are presentation ceilings. For every option the launcher
preserves the guest's 60 Hz clock and game-owned present interval, clears native
FPS targets/patch plans and disables decoupling. Pikachu requests interval 2,
so higher ceilings alone do not promise native 60/90/120 FPS.

## Overworld 60 FPS

The exact-build observations in `patches.py` select the original G[0] duration
only for the world's step load, halve the three per-update movement offsets
during their owning function, and correct turning interpolation to
`1 - sqrt(1 - t)`. The shared duration table remains unchanged. The host adapter
in `dm/peanuts/pikachu_fps.c` snapshots the field heartbeat before each present:
overworld interval 1, native menu/battle/capture interval 2, on a 60 Hz clock.
Caps 60/90/120 all use native 60 in the world; they do not unlock native 90/120.
Game owned and cap 30 leave this adapter inactive. Old packs without its gates
also remain inactive.

Release version code 110 booted on AYN Thor / Android 13 with Turnip T30
(PurpleVK 26.3.0-devel). The live log confirmed 60 Hz guest clocks and a
five-second overworld sample of 58.9 FPS. A Rattata encounter sampled 29.9–30.0
FPS with interval 2. This validates scene selection and cadence, rather than a
complete gameplay-speed comparison. No system-driver run was made.

The reference implementation and its measurements are
[here](https://github.com/raulzrrs/letsgo-recomp/blob/47e300016768d945f0f8f130fa1250f826e1b24c/runtime/hle/fps60.c).
Its experimental all-scene counter patches are incomplete: throwing and other
per-update motion remain accelerated. This profile does not enable them.

Release test 112 reached roughly 60 FPS
in a wild encounter with `PN_LGP_SCENE60_TEST=1`, but gameplay still felt too
fast. The experiment was disabled on the Thor and its log confirmed a return
to 29.9–30.0 FPS. Leave the experiment disabled for normal play.

Build 114 traced a 299 ms flight at 30 FPS and a 267 ms flight at 60 FPS
across two different throws on the Thor. The capture controller then waits
for an event script to finish. The script advances its integer frame clock
once per update independently of projectile physics. The opt-in build 115
holds that clock on alternate 60 FPS presents for the active capture scene's
script. The script processes its events before the increment, so the hook
preserves their order and keeps the timeline near 30 ticks per second.
On the Thor, build 115 applied the timeline hook at 59.9 FPS. The post-flight
state was still active at tick 40 (668 ms), versus tick 16 (267 ms) as the
last sampled tick at 60 FPS on build 114. These are sampled lower bounds
from different throws. The ball motion and Pidgey's jump still looked too fast
in the build 115 play test. The experiment was disabled again.

Build 116 doubles the loaded frame duration for the species dodge arc and
keeps its configured height. It also corrects the vertical integration offset
of the 60 Hz ball flight so it matches the original 30 Hz path at shared sample
times in the timing test. On the Thor, the dodge hook ran near 60 FPS. Pidgey's
jump felt better but looked high, the throw still felt fast, and the wobble
looked normal. The throw traces so far used different trajectories, so their
elapsed flight times do not establish a correction factor.

Build 117 also adjusts the ball object's own pre-impact integrator and
ground-roll interpolation. Those paths use a hardcoded 1/30-second timestep
before the later capture controller's movement and event script. The
experiment halves that timestep and scales the roll's frame duration while
the active capture scene owns an event manager.
The Thor log confirmed the new timestep hook fired during a throw at 59.9 FPS.
The player reported improvement. Jump height and full-scene timing remain
unverified.

Build 118 allows the opt-in mode to select 60 Hz before entering the
overworld. On the Thor, startup reported 59.8 FPS after warm-up and reached
the Continue screen with the 60 FPS cap selected. The user reported normal
intro playback. A full playthrough at 60 FPS remains unverified.

## Remaining verification

- Test gameplay on both the system driver and Turnip; the observed controller-screen boot alone is not a playable-status claim.
- Check SDK 5 NVN object layouts and service behavior during boot; see [host bring-up](../../../docs/agents/03-bringup.md).
- Check the title screen, movement, catching controls and motion, movies, audio, saving and a second boot. Keep existing saves intact.
- Capture any shaders absent from the static tables and compare their keys with the split.
- Keep the profile out of the playable release catalog until device testing establishes its status.
