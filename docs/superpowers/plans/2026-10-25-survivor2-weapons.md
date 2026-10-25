# Survivor 2 native weapons implementation

Spec: docs/superpowers/specs/2026-10-25-survivor2-weapons.md
Base: ef867734f5b55f35204496e52e06e138e80a4cc9

## Global constraints
- Eight runtime loadout IDs: 4,6,7,8,11,12,14,18. No new native inventory/cheats.
- Owned-ROM extraction only; ship no proprietary model/texture/ROM/capture data.
- Validate bounds, source identity and cache integrity; shared preparation/render acceptance; atomic replacement; skip/failure keeps fallback.
- Native equipped state controls automatic model; explicit custom GLB overrides it; other games unchanged.
- Preserve native spread/projectile behavior and NPC/P2/non-VR behavior. Only validated P1 shot creation receives fresh tracked aim.
- Keep phase/focus/session safety and existing user files/keys intact.

## Task 1: standalone model builder and cache format
Create survivor_model.h/.cpp, standalone model-test runner and synthetic malformed-input tests. Use bounded reader and measured source descriptor table. Mirror privately proven native MDL/TPVR decoder, hand-root exclusion, transformed canonical facing and source-fist grip. API: SurvivorGunReader; survivorGuns() descriptor array; survivorGun(int nativeId); buildSurvivorGun(reader,nativeId,out,error); validSurvivorGunCache(bytes,nativeId,error); loadSurvivorGunCache(path,nativeId). Registry contains id/name/file and measured source metadata. Cache HND1 v2 plus integrity/source footer is permitted. Register source in CMake. Independently validate actual owned-source outputs without committing fixtures.

## Task 2: native phase and equipped state
Regression first for measured Ingram stage2, narrow predicate fix with motion byte. Add nativeWeapon field default0 to GunState and publish validated P1+0x3e4 ID without changing ammo selection. Parent separately researches firing/aim sites; integrate any evidence-based additions before final review. Test native/input safety and isolated actual firing cases.

## Task 3: preparation and rendering integration
Use Task1 API. Add SurvivorGuns Source kind, direct bounded cartridge extraction, presented-frame gate/skip latch and all-eight completeness check. Reuse robust atomic save semantics. Preload model bank; reserve native-kind range separate from existing GLB indices. Select equipped model only for automatic native setting and valid state; fallback safely for unknown/missing/skipped models. Refresh/unload/GL reset handles bank. Menu calls automatic option In-game weapons. Tests cover prep lifecycle and choice boundaries.

## Task 4: verify, document and publish
All task review findings resolved, whole branch review, Windows/native/input/model/prep checks and simulated-XR weapon/focus tests. Quest release-signed build increments versionCode. Package without private assets, inspect artifact contents/hash/signature. Commit authorized source and publish new preview tag without changing old preview.2? Determine next unused preview tag from GitHub. Update release docs/VR4ALL metadata as necessary. No headset installation.
