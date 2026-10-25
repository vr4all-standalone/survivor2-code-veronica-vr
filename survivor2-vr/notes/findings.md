# Survivor 2 World research

Target: World BHF2 Ver. E, `gunsur2`, title ` BIOHAZARD  GUN SURVIVOR2`.
Reference engine: Lupin source commit `9b8d7c85a0fa461cb24231a7fff9c1cc6f9139e5`.
Local raw captures remain under `build-research/vrtest`; they are not source or release assets.

## Verified media and boot

The existing ZIP passed 7-Zip integrity testing. Its 15 game ROM lengths and CRCs match
`naomi_roms.cpp`. Program `bhf2vere.2f` is 8 MiB, CRC 756e0de6; remaining chips are 16 MiB.
CRC lookup supports the supplied alternate names for ma8/ma9. No CHD or PIC is required.
The original archive was not modified. A separate NAOMI BIOS and the game's embedded BIOS
were inspected. The generic research executable boots attract, character/level selection,
briefing and playable cemetery combat. VR normal-boot runs use A then native action B.

## Cabinet adapter

FCA has one JVS player. Button 0 is action/change weapon, button 1 trigger. Native enter,
coin, select up/down, service and test retain their own JVS bits. ADC full axes are yaw 0,
pitch 1, roll 2; the native input descriptor inverts roll. A coherent mailbox supplies all
axes/buttons and neutralizes stale packets after 250 ms while retaining ownership.

`survivor_input_test.cpp`: 23 checks including parsing/overflow, endpoints/inversion,
deadzone, invalid floats, stale ownership and reset. Source is not CM/Lupin calibration.

## Native view and shot evidence

All addresses below are World main RAM offsets. Profile/hook signatures are explicit
16-bit instruction checks in `survivor_native_math.h`; active cartridge metadata must
also name gunsur2. Unknown code prevents installation and native integration.

- Pixel focal: 457abc, screen descriptor width/height/centre follows. Setter 23749c begins
  E00C D504 F348 F53A. Measured focal varies by scene (including 718.9297 and 381.4189).
- World camera initialized at 111f66 (D515, literal 111fbc = 8c42eec0). Current camera
  view pointer is 2e1e64. Its inverse matches the active P1 eye near the actor plus eye height.
  Matrix uses native right/down/forward coordinates. XR right/up/back becomes x,-y,-z.
- P1 actor 3d5ce0; context pointer 19531c -> 2b4c9c; current actor 195324.
  Position +16/+20/+24; yaw +32. A +5461 yaw poke visibly turns left ~30 degrees while
  retaining position. Right snap therefore subtracts 5461. Native analog movement keeps
  collisions: negative roll moved left across the camera basis; positive roll met the wall.
- Common collision function 11e902 begins 2FE6. Four known callers: returns 03a5a4,
  03af38, 04245a, 042d72. Input begins origin XYZ, direction/range XYZ, muzzle XYZ;
  weapon metadata and damage/range rules remain native. Caller 04245a uses 3d69ac.
- Accepted-ammunition helper 11e816 begins D11A, literal 11e880 = 8c19531c. It decrements
  a selected u16 magazine at actor+5d4 using the game's own slot table. Initial twin
  pistols consume two rounds (20 -> 18). This helper supplies accepted-round feedback.
- Active P1 mode bytes observed [1,1,65,1]; hurt mode [4,0,0,1]; briefing [1,0,0,0].
  Restricting activity to +13 incorrectly suppressed snap turns during hurt. Active mode
  1 or 4 with +15==1 or 3 (grab) is now regression-tested. Other modes remain unclassified.

Matched clean combat states: idle stayed at 20; trigger reached 18. Bounded interpreter
traces identified 04858c firing/visual creation and the later 11e902 collision query.
Changing only a spawned object's angle fields did not redirect hits. Rotating its visual
matrix was also insufficient evidence. The actual ray experiment changed the native
selected damage target from one zombie to another with the camera fixed. Capture names:
clean-shot-trace, ray-angle-pair, ray-matrix-pair, native-ray-pair. Native hit accumulator
fields include actor+420 and flags+564. These are research evidence, not product writes.

Product implementation replaces the guarded first instruction of the collision/helper
with the emulator's existing HLE opcode and reproduces the original instruction before
continuing. Controller origin/direction are converted through the current validated rigid
native view, keeping native range. NPC/current-actor and caller checks isolate P1.

## Rendering and measured visual ownership

3D clips at W=1. Preserve genuine walls/floors through farOutBelowW=.999. HUD depths
measured around 1.01..1.11 (timer, health, minimap); flat title/briefing use deeper layers.
The exact actor visual calls are 026690, 0266ea, 026776 and 0267d0, each JSR r3 with
MOV r14,r4 in the delay slot. Callbacks 0268e0/026962/026a08/026a8a submit model nodes;
the preceding native routines already update the bones. The HLE handler emulates both
instructions and PR, skipping only the P1 body and its held-weapon attachments in active VR.
All other owners and unknown draw callbacks run normally. Full instruction guards include
the delay slots. A texture-address foreground heuristic was removed.

The separate left/right gun objects were measured at pool locations 74ba00/74bee0, with
owner +2e4 = P1 (8c3d5ce0), hand-bone +2e0 = 9/13, component flags low word 0081 and
kind low word 04ba (upper bits carry weapon ID). P2 objects have the corresponding P2
owner and the same hand bones. Product code uses these ownership/type/bone fields, never
the measured pool locations or texture names. Unattached projectiles, NPCs and P2 visuals
are retained. `xr-attachments-green` eye captures show no native duplicate arms/body,
while zombies, walls/floors, HUD, the tracked gun and the common menu remain visible.

Native view generation at 111fc4 also had to be guarded: native auto-targeting pitches the
camera down/up during movement. The active VR hook sets the view parameters at 2e1dc8
to P1 position +16.5 eye height, zero pitch/roll/offsets and -P1 yaw. Original view generation
then runs. `xr-body-camera` RAM confirms a level eye through strafe and 30-degree snap.
Nonplaying/cinematic and non-VR views keep the original route.

Changing the angle setter delay slot 233fe0 from SHAR to NOP did not change an already
loaded camera in a matched save-state trial. No wider-view patch is enabled. The common
menu keeps its row but marks it unavailable until drawing/culling is proven together.

The proven collision-ray path covers the sampled twin pistols. Other projectile/melee
paths, later weapons, Claire, later levels and sustained headset performance are not yet
completion evidence. Do not describe the whole game as finished on the strength of builds.

## Current build/tests

Windows MSVC/OpenXR builds pass. Native math/control suite has 47 checks (RED/GREEN
observed), input suite 23. Standalone setup has 31 synthetic ZIP/JVM tests; no game data
in fixtures. Quest arm64 build succeeds after an Android lexer issue was corrected.
APK inspected as com.vrlightgunhub.survivor2, code 1, 1.0.0-g<source>, Survivor launcher.

Normal-boot VR simulator runs show accepted native rounds, tracked rays, matching haptics,
shared 13-row paused menus, focus transitions and right snap. Private eye captures exist.
Normal-boot VR validation was used for the main route. For later visual ownership trials,
the existing Dreamcast.AutoLoadState setting successfully loaded the private clean combat
state before VR startup. The interpreter tracer, temporary draw diagnostics and STATE
protocol have all been removed from product source; private traces remain under build-research. Raw RIP capture is available in the PC
Survivor simulator for verification; no captured data belongs in distributions.

## Focus integration regression

The pure control gate tests passed but the early unfocused return did not sample it. In
xr-focus-held-red a held trigger leaked immediately after focus returned at 16 seconds.
neutral() now advances the adapter generation, resetting/swallowing held controls on resume.
xr-focus-held-green has no trigger until the fresh 20.024-second press. Menu and tracking
neutralization share the same reset path. These are measured integration results.

The native GUN BUTTON is routed as action (B/Y). Presses are observed in cabinet logs;
the sampled route did not establish an inventory cycle or knife attack, so those remain
coverage limits. No claim of every-weapon runtime validation is made.
