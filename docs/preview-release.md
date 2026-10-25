# Survivor 2 VR — experimental preview 1

Standalone Meta Quest and Windows PC OpenXR adaptation of **Resident Evil Survivor 2 - Code: Veronica**, targeting **NAOMI World BHF2 Ver. E (`gunsur2`)**.

**Preview status:** simulator and automated validation only. Physical Quest/PC headset comfort, performance and Android first launch remain unvalidated. This is the initial single-player implementation.

## Downloads and setup

- **Quest:** `survivor2-code-veronica-vr-quest.apk`, arm64, package `com.vrlightgunhub.survivor2`, version `1.0.0-g24f8cf87f`, project release signer. Sideload or use its VR4ALL card, grant file access and open under Unknown Sources.
- **PC VR:** extract `survivor2-code-veronica-vr-pcvr.zip`, place your files in `roms` beside `flycast.exe`, activate an OpenXR runtime and run **Play Survivor 2.cmd**. Windows x64 and the Visual C++ x64 runtime are required.
- Supply your own complete **`gunsur2.zip`** (15 cartridge ROMs) and supported **`naomi.zip`**, unless a compatible BIOS is inside the game ZIP. Quest paths are `/sdcard/Download/gunsur2.zip` and `/sdcard/Download/naomi.zip`. No CHD or PIC is required. Japanese revisions are unsupported; renaming them does not convert the game.
- Setup checks ROM lengths and CRC metadata; this does not certify compressed payload integrity. No commercial game or BIOS is distributed.

## Included

- Stereo head tracking, level first-person body camera, tracked gun aiming and native analog movement.
- Left stick move/strafe; right stick 30° snap turn; either fresh trigger takes/fires the solo gun; A/X credit/Start; B/Y native gun button/action; left Menu pauses VR settings.
- Shared CM/Lupin settings menu, licensed Arcade/Zapper/custom GLB support, native accepted-round feedback and held-input protection across focus/tracking/play transitions.

## Known limits

- One player per app instance. Reload follows the native game. Reload, Two players and Wider view settings are disabled with their capability labels.
- No wider-FOV patch: head turns beyond native submitted geometry can expose undrawn space. No native Survivor weapon extraction; bundled licensed models are used.
- Movement currently requires both controllers tracked. Later stages, other weapons/melee/pickups, Claire, bosses, death/restart and long sessions remain unvalidated.

## Validation and corresponding source

The build record contains 301 automated checks across native/input/standalone behavior, shared settings, Android setup, PC setup and packaging. Simulator evidence covers normal World boot, native selections, cemetery combat, movement/collision, tracked rays, hand transfer, snap turns, menus and focus/tracking transitions. These checks do not establish physical headset compatibility.

Both binaries correspond exactly to commit [`24f8cf87f800ae31b1344826c4224227262602eb`](https://github.com/vr4all-standalone/survivor2-code-veronica-vr/tree/v1.0.0-preview.1); `v1.0.0-preview.1` points to that commit. `main` additionally contains publication documentation and artwork. Source and license notices are included/preserved; Flycast and mikermak's hotd2-vr are the upstream foundations. See the README and validation notes for details.

Verify downloads against `SHA256SUMS.txt`:

```text
319197db060459b175be42474814c8c00fce3606fe5db614c3c7284374a3e7ea  survivor2-code-veronica-vr-quest.apk
a26e378d9a27b5f4ea67c7bf37c35de52faa3aa0393e5d3c9d4ef2a9f339a406  survivor2-code-veronica-vr-pcvr.zip
```

Unofficial fan project, unaffiliated with Capcom or Sega.
