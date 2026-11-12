# Broadside TODO

Requested by the user on 2026-11-11/12 (work through these in order, autonomously):

## In progress
- [x] Remove ballast (blow/flood). Jumps come from speed over the waves.
- [x] Ships much faster forward, astern and turning (hold W/S, arcade speed controller).
- [x] Camera pivots well above the ship (World of Warships style), so the reticle is not on the hull.
- [x] Lower ship gravity for more hang time; 360s come from yaw momentum at takeoff (no air control).
- [x] Verified in-game (camera, jumps, 360 callouts, HUD/help updated). Repackage dist/ at the end.

## Next
1. [x] Time of day: noon, sunset, night (menu selector when hosting, synced in MSG_WELCOME; src/atmosphere.h).
2. [x] Weather: clear, storm (rain, lightning, bigger sea), fog (also selectable and synced).
3. [x] Good-looking cannon smoke, explosions on hits (src/fx.cpp).
4. [x] Visible damage that tracks hull HP, and fire on board when close to death (ship.frag + ShipDamageFx; test with --test-damage 15).
5. [x] Lanterns: glow sprites, light on the hull, and fog-piercing screen halos.
6. [x] Name tags only for ships inside/near the reticle.

## Round 3 feedback (2026-11-12)
- [x] Faster acceleration (0->25 m/s in ~1.7 s, was 4.2).
- [x] Cannons swing slightly left/right with the camera to assist aiming (horizontal aim assist + parallax/lead fix; gold brackets on the reticle).
- [x] Ships stay submerged too long: fixed the 'submarine' bug (drive kept a nose-down hull diving); sails give no drive under water, stronger pop-up + levelling. Dunks avg ~1 s.
- [x] Floating crates with timed power-ups (15-30 s): Rapid Reload (x0.4), Triple Shot (12 balls), Fair Wind (x1.4 speed). 7 crates, respawn 10 s.
- [x] Arena edge: ring of islands + sea stacks from the match seed (src/scenery.cpp), solid for ships/balls/camera, surf foam.

## Later / ideas
- Base top speed is unchanged (58 kn); only Fair Wind makes it faster. Raise tune::topSpeed if wanted.
- Test flags added: --test-power, --crate-cam, --freecam px py pz tx ty tz, BS_AIMTEST=1 (with --demo).
- Two-PC network test (never done for real).
- Sail furling for the imported model; muzzle positions vs the slimmer model.
- Lighter LOD for distant ships.
- Lantern light on the water surface; thunder/sound in general (no audio yet).

If the session runs out of tokens, continue when they refresh.
