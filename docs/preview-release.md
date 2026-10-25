# Survivor 2 VR — experimental preview 2

Native weapon update for **Resident Evil Survivor 2 - Code: Veronica**, NAOMI World BHF2 Ver. E (`gunsur2`), on standalone Meta Quest and Windows PC OpenXR.

## Changes

- Prepare eight complete native weapon models locally from your own cartridge: BR-93R, Python, Gold Luger, Ingram, SPAS-12, AK47S, M79 and LN-02. Native character hands are omitted from the tracked models.
- Automatically match the tracked model to the weapon equipped by the game. Pickups, weapon swaps, ammo and reload remain native. Explicit custom GLB choices still override model presentation.
- Keep the Ingram firing animation in the active VR phase, preventing the original arms from briefly reappearing and held fire from being interrupted.
- Preparation can be skipped with B or Y; the licensed fallback remains available. Missing or corrupt caches are rebuilt on a new session.

## Setup

Update the existing app with `survivor2-code-veronica-vr-quest.apk` or extract `survivor2-code-veronica-vr-pcvr.zip` for PC VR. Quest package `com.vrlightgunhub.survivor2` uses versionCode2 and the same project release signer. Existing game files remain usable. PC launch: **Play Survivor 2.cmd**, with an active OpenXR runtime and the Visual C++ x64 runtime.

Supply your complete owned World `gunsur2.zip` and supported `naomi.zip` (unless a supported BIOS is embedded). No CHD or PIC is needed. No game, BIOS or extracted model data is distributed; preparation runs on your device.

Select **Gun → In-game weapons** for automatic model switching. After preparation, use B/Y for the native gun action and either fresh trigger to take/fire the solo gun. Other controls remain in the README.

## Validation and limits

The user reports successful headset gameplay on preview1. This update has separate decoder, native phase, preparation, cache and simulator checks recorded in [validation](../survivor2-vr/notes/validation.md). Physical headset testing of preview2 remains open. Native source loadout tables establish the eight supported weapon IDs; other inherited cartridge assets do not establish additional arcade inventory.

One player per app instance. Wider view remains unavailable; head turns beyond submitted geometry can reveal undrawn space. Movement requires both controllers tracked. Full character routes, natural pickup progression, later stages, melee, bosses and long sessions remain open.

Both binaries correspond to the exact source at [`v1.0.0-preview.2`](https://github.com/vr4all-standalone/survivor2-code-veronica-vr/tree/v1.0.0-preview.2). Verify downloads with the release’s `SHA256SUMS.txt`. Source and upstream notices are preserved. Unofficial fan project, unaffiliated with Capcom or Sega.
