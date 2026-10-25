# Survivor 2 VR Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans to implement this plan inline, task by task.

**Goal:** Build the approved standalone Naomi Survivor 2 VR app with free movement and tracked aiming.

**Architecture:** Isolate a clone of the finished Lupin engine; add a guarded Survivor camera/state profile and a game-specific analog input and firing adapter. Reuse shared OpenXR rendering, setup and menus while adapting controls to this one-player cabinet.

**Tech Stack:** C++17, Flycast, OpenGL, OpenXR, Android/Gradle, Windows/MSVC, PowerShell and Python research tooling.

**Spec:** `docs/superpowers/specs/2026-10-25-survivor-2-vr-design.md` in the VR4ALL workspace; copy into the new source tree.

## Global Constraints

- Naomi World BHF2 Ver. E, `gunsur2`, boot title ` BIOHAZARD  GUN SURVIVOR2`.
- Source `E:\___flattovrdev\survivor2-vr`, branch `codex/survivor2-vr`; no edits to sibling projects.
- App `com.vrlightgunhub.survivor2`, version `1.0.0` / `1`; one player per instance initially.
- Every RAM constant/patch requires measured evidence and revision guards. No borrowed addresses, calibration or weapon state.
- No publication, headset installation, game data packaging or new multiplayer.
- Do not claim completion unless decoupled tracked aim, free movement, Quest/PC builds and simulator checks pass; physical headset testing is recorded separately.

## Review Focus

- Neutral, invalid or lost tracking must not leave motion, actions or a trigger held.
- Mode/menu/focus transitions must swallow held controls until release.
- Unknown revisions and invalid memory must disable guarded integration safely.
- The real accepted shot direction must follow the controller independently of camera and head motion.
- The one-player cabinet must not inherit false two-player or offscreen-reload controls.

### Task 1: Isolated baseline and measured research

**Files:** new clone, private `build-research`, `survivor2-vr/notes/findings.md`, this spec/plan and private progress ledger.
**Interfaces:** Produces the observed boot path, verified camera/focal/state metadata and the firing/control adaptation approach consumed by Task 2. No unproven numeric constants enter product code.

- [ ] Verify independent source/dependencies and run baseline native controls tests.
- [ ] Boot an unmodified generic research player using the user's existing World set, with private config/saves/UDP and owned-process cleanup.
- [ ] Capture gameplay render passes and main RAM; identify camera projection and HUD, analog movement/turn routing, actions and firing path using controlled changes.
- [ ] Prove the minimum tracked-aim path independently of ordinary centered camera firing. Record evidence and exact revision checks.
- [ ] Commit research metadata and scope any remaining technical obstacles honestly.

### Task 2: Guarded camera, movement and tracked firing

**Files:** `core/rend/vr_reproject.cpp/.h`, `core/rend/vr/xr_host.cpp/.h`, new focused Survivor adapter/header and native tests, `core/hw/maple/maple_jvs.cpp` only if input routing requires it.
**Interfaces:** Consumes Task 1's measured data; produces guarded per-frame integration and native input functions with explicit neutral/reset semantics. Define exact signatures alongside the first tests after research resolves the firing path.

- [ ] Write native behavior tests before implementing the adapter: neutral/deadzones/clamping, tracking/focus/menu reset, transition swallowing, revision guard rejection and coordinate conversion at multiple head/controller poses. Run and observe expected failures.
- [ ] Implement the smallest guarded adapter and profile satisfying those tests; rerun to green.
- [ ] Run controlled playable simulator routes for movement, aiming, action/weapon change and accepted rounds. Verify camera, controller and hit behavior separately, then together.
- [ ] Validate HUD/geometry/cutscenes from captured passes; enable only measured capabilities such as wider view.
- [ ] Commit tested behavior and numeric findings.

### Task 3: Standalone identity, setup and capability-aware menus

**Files:** `core/standalone_game.h`, `CMakeLists.txt`, Android manifest/setup Java/build files, `core/rend/vr/xr_menu.cpp/.h`, `xr_labels.cpp`, `xr_hands.cpp`, Survivor build/test scripts.
**Interfaces:** Consumes Task 2's capabilities; produces the separate app and controls without unsupported 2P/reload rows.

- [ ] Add failing identity/revision/setup and menu/input-transition tests using existing native and Java harness patterns.
- [ ] Implement supported World ZIP/BIOS validation, dedicated app identity/data roots, accurate control labels and shared menu behavior adapted to Survivor.
- [ ] Run native tests and simulator setup/menu/pause/focus/persistence scenarios; inspect actual rendered results.
- [ ] Commit the standalone integration.

### Task 4: Build, package and review

**Files:** Survivor package/build scripts, `README.md`, `survivor2-vr/notes/validation.md`, private `dist/build-verification.json` and APK/PC ZIP.
**Interfaces:** Consumes tested source and final controls; produces local reviewed artifacts and a validation report.

- [ ] Build Windows and Android with independent caches. Inspect APK package/version and runtime identity.
- [ ] Package licensed software assets and launcher; enumerate archive entries to prove game data, private dumps and signing secrets are absent.
- [ ] Run final meaningful native/shared regressions and simulator gameplay/menu checks. Record source commits, checks and output hashes.
- [ ] Request one fresh whole-branch review; resolve material findings with regression tests, then rerun affected verification.
- [ ] Deliver local artifact links and explicit limits. Do not upload or install.

## Execution decisions

The user approved the full Naomi design and build on 2026-10-25. Execute inline without another design/plan approval. Research-dependent addresses and interfaces are deliberately resolved by measured Task 1 evidence; if genuine decoupled aiming cannot be established, retain an accurately labelled research milestone and report the obstacle rather than claim a finished port.
