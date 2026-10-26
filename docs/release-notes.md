# Survivor 2 - Code: Veronica VR 1.0.3

Version 1.0.3 is a regular release for standalone Meta Quest and Windows PC OpenXR, using the same version-number format as the other VR4ALL ports.

## Included

- Eight automatic native weapon models: BR-93R, Python, Gold Luger, Ingram, SPAS-12, AK47S, M79 and LN-02 linear launcher. Models are prepared locally from your own cartridge.
- The Ingram arms fix and tracked launcher aiming, preserving native spread, launch speed and ongoing physics.
- Purple VR4ALL menus, preparation/completion screens, pointer rays and hit dots, plus the three-second VR launch splash.
- Native controls, game progression, pickups, ammunition, saved settings and custom GLB overrides.

## Update and install

Install `survivor2-code-veronica-vr-quest.apk` over the existing app or update through VR4ALL. The package remains `com.vrlightgunhub.survivor2`, with the same release signer and versionCode **4**. The app version is `1.0.3-g<source>` so the build remains traceable to its source commit.

For PC VR, extract `survivor2-code-veronica-vr-pcvr.zip`, keep your existing game files/saves/settings, activate your OpenXR runtime and run **Play Survivor 2.cmd**.

Supply your complete owned NAOMI World BHF2 Ver. E `gunsur2.zip` and supported `naomi.zip` (unless a supported BIOS is embedded). No CHD or PIC is needed. No commercial game, BIOS or generated weapon data is included.

Select **Gun → In-game weapons** for automatic weapon models. B/Y skips preparation for that session.

## Verification and scope

This update changes release naming and version metadata. Gameplay and the purple VR4ALL interface are retained from the previous release. Build, package and source-provenance checks apply to both downloads; detailed gameplay evidence and remaining headset/route coverage are in [validation](../survivor2-vr/notes/validation.md).

One player per app instance. Wider view remains unavailable, and movement requires both controllers tracked. Later stages, full character routes and long sessions retain their existing validation limits.

Both binaries correspond to [`v1.0.3`](https://github.com/vr4all-standalone/survivor2-code-veronica-vr/tree/v1.0.3). Verify downloads against `SHA256SUMS.txt` attached to the release. Upstream source and license notices are preserved. Unofficial fan project, unaffiliated with Capcom or Sega.
