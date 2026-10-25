# Resident Evil Survivor 2 – Code: Veronica VR

Date: 2026-10-25 (America/Vancouver).
Status: approved by the user on 2026-10-25; implementation authorized.

## Intent

Create another standalone Quest and PC VR game in the user's series of Naomi conversions, following Confidential Mission VR and Lupin the Third VR. The user confirmed **Survivor 2**, rather than the original Code: Veronica survival-horror game, and asked whether PS2 or Naomi is preferable.

My recommendation is **Naomi**: reuse the existing Flycast/OpenXR engine, Quest toolchain, setup screen, menus, controller models and testing infrastructure. A PS2 version would require a different platform integration. Platform choice remains subject to the user's response to this design.

This is a more substantial adaptation than the previous rail shooters. The arcade cabinet uses an analog mounted gun for moving and turning, with a centered firing direction. Its native inputs are not the lightgun screen coordinates used by CM and Lupin. True tracked gun aiming requires research into this game's firing/camera behavior.

## Source and isolation

- Use the finished Lupin 1.0.1 source as the initial reference: `E:\___flattovrdev\lupin-vr`, branch `codex/lupin-vr`, observed commit `9b8d7c85a0fa461cb24231a7fff9c1cc6f9139e5`. It carries forward CM's latest menu and hand-control fixes.
- Consult the finished Confidential Mission source at `E:\___flattovrdev\hotd2-vr`, observed branch `naomi-gun-vr`, commit `959f79d65e19577fe67573f29c5045520da909d0`, when necessary. Record exact reference commits again at implementation start.
- My proposed new source directory is `E:\___flattovrdev\survivor2-vr`, branch `codex/survivor2-vr`, with its own build, test, save, model, signing and runtime directories. If filesystem approval is unavailable, use a dedicated directory in the permitted workspace and report that location.
- Never edit the CM, Lupin, Ninja Assault or Death Crimson source trees, stop their processes, or reuse their writable build outputs. Test processes must have a Survivor-specific name, owned-PID cleanup and a private UDP port.
- Keep VR4ALL installer changes outside this initial port. The installer checkout already has unrelated changes and other active work.

## Target and identity

My implementation choices:

- C++17 Flycast/OpenGL/OpenXR; native Android setup screen; Gradle Quest build; Windows PC VR build.
- Initial supported revision: Naomi **World BHF2 Ver. E**, MAME set `gunsur2`. Exact runtime boot title is ` BIOHAZARD  GUN SURVIVOR2`, including the leading space.
- The user's existing `E:\___flattovrdev\Downloads\gunsur2.zip` passes 7-Zip's full integrity test. All 15 game ROMs match the World BHF2 Ver. E definition by size and CRC. Two entries use alternate filenames (`bhf1ma8.4d` and `bhf1ma9.4e`); Flycast's cartridge loader searches by CRC before filename. No CHD is listed for this ROM-board game. This validates the available research input, not boot or VR compatibility. Do not rename or alter the original.
- Required BIOS: the user's `naomi.zip`, plus any dependent firmware the actual boot requires and the upstream definition confirms. Do not invent a CHD/PIC requirement or declare optional firmware necessary without evidence.
- Japanese `gunsur2j`, other revisions and the PS2 version are outside initial validation. Shared boot titles alone do not establish revision support.
- Display name **Survivor 2 – Code: Veronica VR**; Android application ID `com.vrlightgunhub.survivor2`; initial version `1.0.0` / versionCode `1`.
- Proposed software artifacts: `survivor2-code-veronica-vr-quest.apk`, `survivor2-code-veronica-vr-pcvr.zip`, with a prepared local PC launcher and validation report.
- Quest 3 is the primary target. Retain inherited Quest 3S/2/Pro and PC OpenXR compatibility, with tested platforms stated explicitly.
- Preserve Flycast, mikermak and prior contributors' attribution and licenses. No game, BIOS, ROM-derived model, raw dump or private signing material in distributable artifacts.

## Behavior to build

### Rendering and game integration

1. Real stereo scene reconstruction with tracked head rotation and translation. Measure Survivor 2's own projection, viewport/focal data, world scale, near plane, HUD depth and screen transitions from multiple attract and playable scenes.
2. Use revision guards for every game-memory read used as state and every patch. Never copy CM/Lupin RAM addresses, texture IDs, calibration, player-state or weapon-state assumptions.
3. Preserve menus, timer, health, items, text, cinematics, transitions and effects on an adjustable panel. Hide only measured native weapon/reticle geometry that duplicates the tracked presentation; retain gameplay feedback.
4. Offer wider view only after its drawing, culling and shot-direction behavior are measured together. If not yet validated, expose it as unavailable with an accurate reason instead of applying another game's patch.
5. Keep a research report with evidence for each numeric constant, original instruction bytes, patch guards, relevant revision checks and known coverage limits. Private captures remain outside tracked/distributed source.

### Movement and tracked aiming

1. Preserve the game's free-roaming rules and collisions. Proposed default: left stick controls forward/backward movement and strafing; right stick provides snap turning. Recenter has an explicit non-conflicting binding. The builder determines dead zones, speed, turn increment and handedness details from controlled trials, and documents the final controls before packaging.
2. Separate looking, locomotion and gun aiming. The controller must aim the shot, rather than merely rotate the entire game view to a centered crosshair. Determine the game's view transform and firing-ray path, with an evidence-backed implementation before claiming tracked free aim.
3. Preserve original action/pickup, weapon cycling, trigger, credits/start, continue and cabinet menu inputs. A gun-side button is an action/weapon-change input in this game; do not label or repurpose it as CM's reload without verification.
4. Guns may switch hands by either trigger in solo play, carrying forward the user's recent preference where it fits Survivor 2. Movement/action mappings must remain consistent across hand switching, menu closure, lost tracking and focus changes. Swallow held inputs on transitions so menu selections cannot fire, move or act in the game.
5. Recoil, muzzle effects and haptics must follow actual accepted rounds and supported weapon behavior. Empty pulls, pickups and mode changes must not pretend to be shots. Start with licensed Arcade/Zapper and user GLB models; extract a Survivor-specific gun from the user's game only if its source and complete geometry can be demonstrated. Do not carry over CM's pistol/rifle or Lupin's gun.
6. Initial gameplay target is **one player per app instance**. Upstream models this cabinet as one JVS player and handles two players through linked machines. Do not expose CM/Lupin's two-hands-as-P1/P2 option or promise that it works. Existing linked-cabinet behavior may be preserved; new network multiplayer and its headset validation are a separate project.

### Menus and setup

1. Carry over the paused VR menu, pointer selection from either hand, stick navigation/repeat, live gun preview, saved settings, labelled How to play, comfort controls, haptics levels and timed second-selection Quit confirmation.
2. Adapt rows to genuine Survivor capabilities. Remove the two-player hand-assignment row from this single-player view. Replace rail-shooter reload choices with actual Survivor controls; add turn settings where needed. Keep familiar ordering for shared rows, including Resume, Recenter, Gun, Gun angle, Laser, Haptics, World size, Panel distance, Wider view, How to play and Quit.
3. Pause both simulation and audio while keeping headset rendering and head motion live. Test rapid open/close, focus loss, recenter, resume and held controls.
4. A separate setup activity recognizes the supported game ZIP and BIOS, explains missing/wrong revisions, and enables Play only when required structural checks pass. Canonical initial Quest inputs are `Download/gunsur2.zip` and `Download/naomi.zip`, subject to verified dependency requirements. Use app-specific settings, saves and generated-model caches.
5. PC packaging includes the same checked setup/launch behavior and a launcher with accurate game paths. Preserve the user's originals; research/normalization copies stay private.

## Research milestone and completion gates

The first implementation milestone is a measured boot and playable control/camera prototype in an isolated source tree. Its purpose is to prove whether the centered firing model can be adapted to tracked aiming while preserving free movement. That milestone is not a completed VR port.

Before calling the port complete:

- Identify the actual set and dependencies by archive inspection and successful boot; verify solo start, character selection, playable movement, firing, action, weapon changes and continue.
- Demonstrate decoupled head motion and controller aiming at several targets and head poses, with positive game hit/shot evidence. Demonstrate neutral sticks do not move, pause does not advance gameplay, and tracking/focus loss clears controls.
- Verify stereo/parallax, scale, HUD/text, near-plane geometry, cutscenes and transitions in both a static scene and moving gameplay; wider view requires separate causal checks.
- Add meaningful native tests for input routing, mode transitions, revision guards and any new adapters. Run the existing shared menu/control regressions from the new source tree.
- Build Windows and Quest, inspect APK identity/version and packaging contents, and run simulator tests for setup, menus, pause/focus, persistence, locomotion and tracked aiming. Verify APK/ZIP hashes and record exact source commits and test results.
- State physical headset testing, sustained performance, later stages and unvalidated devices honestly. A successful build or simulator run is not physical play testing.
- If native free aim cannot be established, report the measured obstacle and preserve the research. Do not substitute a flat display or an aim-steers-the-camera prototype and call it the requested finished port.

## Boundaries

Local development, builds, private research using the user's existing files, documentation and software packaging are in scope after design approval. Publishing a repository/release, uploading game content, installing to a headset, sending messages to other chats, or adding multiplayer are not included in this design. Obtain authorization for external publication or headset installation when there is a concrete tested result to review.

## Evidence consulted

- Local CM and Lupin source/readmes, plus Lupin's measured findings and test harness documentation.
- Flycast `core/hw/naomi/naomi_roms.cpp`: `gunsur2` World BHF2 Ver. E ROM-board definition and Japanese alternate revision.
- Flycast `core/hw/naomi/naomi_roms_input.h`: gun button, trigger, select, enter, pitch, roll and yaw inputs. [Upstream source](https://github.com/flyinghead/flycast/blob/master/core/hw/naomi/naomi_roms_input.h).
- Flycast `core/hw/maple/maple_jvs.cpp`: one-player `jvs_namco_fca` setup, distinct from ordinary lightgun input. [Upstream source](https://github.com/flyinghead/flycast/blob/master/core/hw/maple/maple_jvs.cpp).
- Flycast `core/network/naomi_network.cpp`: Survivor 2 cabinet CPU/link configuration.
- Namco *Gun Survivor 2 Operators Manual*, part 90500129, issue 1, How to Play (page 29) and I/O Switch Test (page 48): mounted-gun movement/turning/action and analog axes. [Manual transcription, page 29](https://www.manualslib.es/manual/390595/Namco-Gun-Survivor-2.html?page=29).

## Approval

The user selected **Approve Naomi design and build** on 2026-10-25, approving this scope including free-roaming movement and single-player first.

The brainstorming builder check returned one unresolved question: approval of Naomi rather than PS2. No additional planning-critical questions were found.
