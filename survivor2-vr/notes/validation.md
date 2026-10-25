# Survivor 2 VR validation

Target: NAOMI World BHF2 Ver. E (`gunsur2`). Experimental preview candidate; see the [public release notes](../../docs/preview-release.md) for publication status.
The physical headset checklist remains open; compilation and simulator runs do not replace it.

## Automated checks

| Check | Observed result |
| --- | --- |
| Cabinet mailbox / signed axes / stale input | 23 passed |
| Native camera rays / revision guards / transitions / actor visual ownership | 57 passed |
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
`dist/build-verification.json`. This is the original local build record. The subsequent preview publishes these unchanged artifacts with the binary source tag recorded in the public release notes. No physical headset installation has been validated.

Package checks enumerate contents, enforce app/launcher/version identity, require GPL/cgltf
notices and model credits, compare the model file set between PC and Quest, and reject game
archive, memory/rip, generated game-model and private signing-material paths. The PC ZIP has a
checked portable launcher and VR/OpenGL/native-depth defaults. Local test packages may use the public Android debug signer and must remain private test builds. The published preview uses the private project release signer (CN=VR Light Gun Hub); its signing key is not included.

## Remaining physical and game coverage

- Quest and PC OpenXR headset: stereo comfort, frame pacing, tracking loss, system menu resume,
  controller bindings, hand transfer, recenter and persistent settings.
- Android setup layout, storage permission, missing-file recovery, Play and Quit-to-setup.
- Claire, other native weapons/melee/pickups, later stages/bosses, death/restart and long sessions.
- Wider view remains unavailable. Head turns beyond native submitted geometry expose undrawn space.
- Native linked-cabinet multiplayer is outside this initial one-player implementation.

The default weapon route and native collision math are measured. Unvisited game paths are
coverage limits, not a claim that each weapon and scene has been tested.

## Final independent source review

One fresh whole-branch reviewer inspected the implementation and independently compiled
the input/native/standalone checks. Two material tracking findings were reproduced and fixed:

1. Inactive zero samples during controller tracking loss could rearm held movement/buttons.
   The new tests first failed (51/53), then passed (53/53). Only a live tracked release rearms.
2. A previously acquired origin survived invalid current head orientation. The simulator now
   supports `XRTRACK head 0/1`, independently of controller poses. `xr-head-loss-red` emitted
   a cabinet trigger at 8.025 s while the head was invalid (6–10 s). After passing current
   `xrLocateViews` orientation validity to the input gate, `xr-head-loss-green` remains neutral
   through loss and held recovery; the next trigger is the fresh 17.008 s press.

Position-only fallback is preserved when orientation is still valid. Native play/briefing/death
phase changes also advance the held-control generation at VBlank; transient hurt/grab modes
stay within the same active phase. The final normal-boot route checks this path again.

The review found no other material issue in the native HLE semantics, camera/ray math,
visual ownership, setup or packaging source. Physical and unvisited-game coverage remains open.

Final smoke exposed an activity-classification edge case before delivery. Native firing
initialization is [1,1,45hex,0], followed by [1,1,a9hex,1]; idle initialization briefly
uses [1,1,41hex,0]. Stage zero alone cannot distinguish these from briefing [1,0,0,0].
The predicate now includes the measured mode1/submode1/stage0 case, with briefing and
unknown stage/submode rejection tests. All57 native checks pass. The short xr-phase-green
route fires twice without a false inactive transition; the correction was independently reviewed.

## Preview 2 native weapon update (2026-10-25)

The user reports successful headset gameplay on preview 1, followed by a request for native weapon models and a report that original character arms reappear while firing the Ingram. This is user-reported testing; it does not close the physical checklist for preview 2.

Owned-source research identified four arcade loadout rows containing eight distinct native weapon IDs: BR-93R (4), Python (6), Gold Luger (7), Ingram (8), SPAS-12 (11), AK47S (12), M79 (14) and LN-02 (18). Other inherited source assets are not advertised as additional arcade inventory.

The direct builder traverses complete native weapon object subtrees and omits only their hand-root mesh. All eight private owned-source builds match independently measured source strip/triangle counts, decoded texture CRCs and complete emitted geometry/material/texture data. Synthetic malformed-source and cache integrity/topology tests run under optimized fast-math. No proprietary fixtures or derived weapon files are tracked or packaged.

The Ingram regression was reproduced with the native state [mode1, submode1, motiona9, stage2]. That state previously toggled gameplay inactive, exposed the native visuals and neutralized held input. Exact motion-aware acceptance keeps it active while preserving unknown-state/briefing exclusions. The initial phase/state suite passes81checks, input23 and app6. The shared settings suite passes132checks. A focused review found no phase/state defect.

Preparation/selection tests cover all-eight completeness, actual presentation signals before work, skip/failure session latches, native/custom selection boundaries and actual atomic replacement behavior. Inherited model builder checks pass16/16. Focused decoder and preparation/render integration reviews found no actionable defects. Release packaging regressions were updated to Android versionCode2 and the preview2 portable README:42/42pass.

Further aiming and runtime integration evidence follows. Raw ROM sections, RAM/VRAM, models, screenshots and instrumented research binaries remain private under ignored research directories.


### Final native aiming and bank integration

All eight native equipped IDs were fired through the native equip handshake in private tests. Authoritative routes were observed for BR-93R/Python/SPAS-12 (ray caller03a5a4), Gold Luger/Ingram (04245a), AK47S (03af38), M79 (five projectile-creation completion callers) and LN-02 (039a7c). Claire-specific gun IDs were exercised through the loaded Steve actor; this does not establish a full Claire route.

The projectile hook acts once on a freshly allocated, validated P1 entity, preserving the original function epilogue and shared descriptors. Native metadata and every entity word outside the intended position/orientation/velocity fields were unchanged. M79 velocity lengths and pairwise fan separation matched within8.9e-7; subsequent snapshots showed native gravity continuing. LN-02 initial speed and orientation matched, but the entity expired before the later asynchronous capture. Cosmetic hitscan tracers remain native.

Six controlled stale/non-VR/P2 probes left112-byte shot records unchanged. The final native suite passes110/110, with23inputchecks, model/prep suites and6appchecks also passing. A clean Windows build passed. Final dynarec smoke produced M79 one accepted round/five tracked projectiles, LN-02 one/one, and Ingram72accepted rounds/72tracked rays with no inactive transition. Focused and whole-branch reviews found no actionable defect.

Fresh-cache simulator run `weapons-runtime-prepare` prepared all eight complete caches from the loaded owned cartridge. `weapons-runtime-render` used valid caches without extraction; after a fresh trigger took the tracked gun, all eight models were captured and visually inspected without native hands. Unknown equipped IDs used the licensed fallback. These model-routing pokes were separate from the actual native equip/fire tests above.

`weapons-runtime-custom` retained the Zapper override while native ID8 was equipped. `weapons-runtime-repair` rejected a deliberately corrupt Ingram cache and rebuilt only that entry. `weapons-runtime-skip` skipped preparation, kept fallback through focus loss/return and did not retry during that session. Caches and licensed research model files remain in ignored private app data.

For an external inventory cross-check, see the [arcade-version play guide](https://gamefaqs.gamespot.com/ps2/540785-resident-evil-survivor-2-code-veronica/faqs/13888) and [Namco operator manual](https://arcarc.xmission.com/PDF_Arcade_Manuals_and_Schematics/Gun%20Survivor%202.pdf). The implementation inventory and source fingerprints come from the owned World cartridge.


### Published preview 2 artifacts

Final clean source: `fbeaacfea7374e1269e123c56ec202ba44294159`, tagged `v1.0.0-preview.2`. Windows RelWithDebInfo and Quest arm64 release builds passed. The APK reports package `com.vrlightgunhub.survivor2`, versionCode2, versionName `1.0.0-gfbeaacfea` and sole launcher `Survivor2StartActivity`. Its public signing certificate matches preview1 (`a293e4f11a4c139a9b5dc5d6d84081c887f8a98d5fc62951e517551775479e61`).

Strict packaging verified both binaries name the source commit, matched all nine bundled model/credit files, checked licenses and portable launch defaults, and rejected game-data/private-signing paths. No extracted weapon caches are in either archive. GitHub asset digests and anonymous downloaded bytes matched local SHA-256 values:

```text
44650175736a69fe6c40d918f0105066196b55661cc00cd855c349ddd3d74f00  survivor2-code-veronica-vr-quest.apk
b0ef6d319aab21d930d03978898448251cd940757f10a831060ad44f449e34a7  survivor2-code-veronica-vr-pcvr.zip
```

The [public Preview2 release](https://github.com/vr4all-standalone/survivor2-code-veronica-vr/releases/tag/v1.0.0-preview.2) and corresponding source tag were verified. VR4ALL's existing Survivor card selected the live Preview2 Quest APK, including its exact digest and size. The local catalog suite passed4/4. Physical headset testing of this update remains open; no headset installation was performed in this session.
