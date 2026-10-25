# Survivor 2 native weapons

The user tested preview.1 successfully and requests all in-game weapons plus a fix for native arms appearing while the Ingram fires. Add automatic tracked model presentation for the eight distinct weapon IDs in the World arcade loadout tables: 4 BR-93R, 6 Python, 7 Gold Luger, 8 Ingram, 11 SPAS-12, 12 AK47S, 14 M79, 18 LN-02 launcher. Gameplay remains native, including pickups, swapping, ammunition, reload and projectile behavior. No cheat or new inventory is exposed. Other inherited source assets do not establish arcade availability.

Models are prepared locally from an owned, signature-matched gunsur2 cartridge. Read complete native weapon subtrees, omit only the native hand root, retain source textures and geometry, normalize facing and calibrate the grip. No proprietary source/output asset enters source control or release archives. Build bounded caches with source/schema/integrity validation shared by preparation and rendering, atomic replacements, early skip and safe licensed fallback. Native model selection follows the validated P1 equipped ID; a user-selected GLB retains precedence.

Fix the reproduced Ingram firing stage [1,1,a9,2] being misclassified inactive. Preserve briefing/cutscene/unknown phase exclusions, P2/NPC visibility, stale-pose safety, focus release and non-VR behavior. Confirm weapon firing routes with isolated owned-ROM tests; extend aim only at verified P1 shot creation sites and preserve native spread/projectile updates.

Deliver regression tests, Windows and Quest builds, source documentation and a new preview release through the previously authorized GitHub/VR4ALL workflow. Report headset testing as user reported; this session verifies simulated XR and builds.
