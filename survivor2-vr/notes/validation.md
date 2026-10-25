# Survivor 2 VR validation

Target: NAOMI World BHF2 Ver. E (`gunsur2`). Development build, unreleased.
The physical headset checklist remains open; compilation and simulator runs do not replace it.

## Automated checks

| Check | Observed result |
| --- | --- |
| Cabinet mailbox / signed axes / stale input | 23 passed |
| Native camera rays / revision guards / transitions / actor visual ownership | 47 passed |
| Standalone loaded title + cartridge set | 6 passed |
| Shared production settings / input pulses / held state | 132 passed |
| Android cartridge / BIOS metadata / setup status (JUnit) | 31 passed |
| PC cartridge / BIOS metadata | 9 synthetic cases + existing World set passed |

The setup fixtures contain synthetic ZIP headers and no copyrighted ROM payloads. Their readiness
result deliberately validates metadata only. A full 7-Zip integrity check of the user's unchanged
World archive also passed during research, followed by actual game boots.

The native tests were developed with failing checks before implementation. The focus-resume
integration needed a separate simulator regression: a held trigger leaked after focus returned
even though the pure gate tests passed. `xr-focus-held-red` reproduced cabinet trigger at 16.049 s
following focus return at 16 s. After resetting the gate generation on neutralization,
`xr-focus-held-green` emits no trigger until the fresh press at 20.024 s.

## Gameplay evidence

All raw RAM/VRAM, renders, save states and logs remain private under ignored `build-research`.

- Normal World boot traverses title, selection and briefing into cemetery combat.
- Matched native collision experiments redirect a selected zombie hit using the controller ray
  while the native camera remains fixed. Cosmetic visual-angle changes alone were insufficient.
- Native twin-pistol ammunition changes 20 → 18 → 16; the native accepted-round helper, rather
  than trigger events alone, drives recoil/flash/haptics.
- Separate head and controller poses produce independent native rays. Either fresh trigger can
  transfer the solo gun. Left-stick locomotion retains native walls/collision; right-stick snap
  produces a single 30° yaw step and requires stick recentering before another step.
- The body view stays at player position plus 16.5 native units and stays level while strafing;
  snap changes its yaw. Native auto-target pitch is suppressed only in active VR play.
- The common 13-row paused menu is captured in the eye render. Reload, Two players and Wider
  view display Survivor's capabilities and cannot enable unsupported behavior.
- Focus/menu transitions, invalid tracking and stale input have explicit neutral paths.

`xr-attachments-green` eye captures confirm that native P1 body and hand-attached weapon
visuals are hidden while enemies remain visible, before/after movement and in the paused
menu. Tests also reject NPC, P2, projectile, unknown-component and cinematic/non-VR hiding.
This evidence covers the sampled character and weapon, not every model in the game.

## Build and package record

Windows MSVC/OpenXR and Quest arm64 builds have succeeded during development. Final package
commit, identity, signer and SHA-256 results are recorded beside the local artifacts in
`dist/build-verification.json`. No headset installation or public upload is part of this task.

Package checks enumerate contents, enforce app/launcher/version identity, require GPL/cgltf
notices and model credits, compare the model file set between PC and Quest, and reject game
archive, memory/rip, generated game-model and private signing-material paths. The PC ZIP has a
checked portable launcher and VR/OpenGL/native-depth defaults. Local packages may use the
public Android debug signer and must remain test builds.

## Remaining physical and game coverage

- Quest and PC OpenXR headset: stereo comfort, frame pacing, tracking loss, system menu resume,
  controller bindings, hand transfer, recenter and persistent settings.
- Android setup layout, storage permission, missing-file recovery, Play and Quit-to-setup.
- Claire, other native weapons/melee/pickups, later stages/bosses, death/restart and long sessions.
- Wider view remains unavailable. Head turns beyond native submitted geometry expose undrawn space.
- Native linked-cabinet multiplayer is outside this initial one-player implementation.

The default weapon route and native collision math are measured. Unvisited game paths are
coverage limits, not a claim that each weapon and scene has been tested.
