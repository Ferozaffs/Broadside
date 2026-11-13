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

## Round 4 feedback (2026-11-13, done while the user slept)
- [x] Foam: foam bias was 0.35 (Garrett's sample scene: 0.85). Now 0.8, add 0.1, threshold 0. BS_FOAM=bias,thr,add,decay overrides.
- [x] Steering quicker: turnRate 1.7 (was 1.15), slow 0.55, rudder slew 7/s. 360 at speed ~2.9 s.
- [x] Slight air control: rudder gives 1.4 rad/s^2 of yaw in the air, but can't wind a spin past 1.6 rad/s.
      Air spin above 2.6 rad/s bleeds off. sternKick 2.0 -> 1.1. Planing throw capped at 13 m/s (one seed launched 72 m).
- [x] Underwater brake: a hull with the deck >0.5 m under loses forward speed (1.6/s). Dunks avg 0.65 s, max ~1 s / 3 m.
- [x] Aim: the reticle point is now where the balls land. Client raycasts the reticle (enemy hull / sea / rock), solves the
      exact ballistic elevation (gravity + StepBall drag, low arc, ship velocity incl. roll) from the battery that faces
      it, and sends the barrel direction (AimElevation is now identity). Assist pulls the point onto an enemy near the
      reticle with lead. Test: BS_AIMSIDE=1 BS_AIMLOG=1 --demo prints the miss (0-3 m at 100-500 m within traverse).
- [x] Game modes (menu GAME: Free-for-all / Teams / Pirate, SHIPS: Solo / Fleets; --mode, --fleets):
  - Teams: join the smaller side, red vs blue, no friendly fire (balls pass through friends, rams don't hurt),
    team score = enemy ships sunk, spawn off your own harbour.
  - Fleets: 3 AI escorts per captain (60 hp, 15% faster + catch-up speed), formation behind the captain, they fight
    enemies near you. Lost escorts stay lost until you sink (fresh fleet on respawn) or reach a friendly harbour.
  - Pirate: one pirate (250 hp, 0.8x reload, black sails) vs everyone. Booty chests (5) only the pirate side can haul
    aboard; bank them at the pirate cove (harbour 0). Pirate sinks -> booty spills as chests, killer becomes pirate.
    The first human to join takes the black flag from a bot.
- [x] Harbours: 4 per map (MakeHarbours(seed)), sea stacks cleared around them; pier, lighthouse, owner flag, buoy ring.
      Friendly harbour repairs 12 hp/s and restores lost escorts. Teams: harbour 0 red, 2 blue, 1/3 neutral.
- [x] MAX_SHIPS 32, protocol v6, distant ships (>650 m) use the procedural low-poly hull, ships behind the camera skipped.
- [x] simtest: fleet station-keeping test (BS_FLEETSTRAIGHT, BS_FLEETLOG) and 6-minute bot matches per mode checking
      friendly fire, harbour refits, booty, pirate hand-over (BS_NOMODES=1 skips them).

## Round 5 feedback (2026-11-13)
- [x] Less floaty air: ships now fall at full g (shipGravityScale 0.5 -> 1.0). To keep the jumps, a hull that clears the
      water while rising gets a breach boost (+70% of its vertical speed, max +8 m/s, plus 3 m/s forward). Captains only.
      Jumps 2-4 s instead of up to 5+, 24% airborne instead of 33%. Spray burst on breach (client).
- [x] Fleets keep up: escorts never go astern (that flipped their rudder and they floundered for ~10 s after every
      captain U-turn), keep way on in big turns, stronger catch-up (+0.8x top speed from 15-75 m off), half planing
      (they skim instead of flying), wait in the slot when the captain heaves to, only peel off to fight what threatens
      the captain, and rejoin from astern after 4 s >380 m off. Straight-line slot error 135 -> ~60 m.
- [x] Pirate mode reworked: a single treasure everyone chases. The carrier turns pirate (black sails, 200 hp with the
      extra added on top, 0.8x reload, 0.9x speed) and must bank it at the cove (harbour 0); sunk -> it floats where
      they sank. New treasure 6 s after a bank, across the bay from the cove. Everyone is FFA; all harbours repair.
- [x] Lanterns: real iron lantern housings with warm glowing glass (near ships), new glow texture (hot core + soft
      halo), slight flicker, warm reflections streaking across the water at night/fog, lighthouses sweep two beams,
      the hard screen-space dots are gone (soft far halos only past ~220 m).
- Protocol v7.

## Playtest 2026-11-13 (recorded)
- [x] --autopilot: the bot brain sails our ship from what the client can see; the camera looks at the nearest enemy
      (the treasure carrier first) and the trigger goes through the normal reticle/aim-solver path. Prints a report.
- [x] --record <sec> <file.mp4> [--record-start <sec>]: frames piped to ffmpeg at a steady 30 fps (BS_FFMPEG = path).
- [x] Recorded FFA, fleets, pirate, night, fog, teams/storm and a host + joining client (40 ms fake lag) on one PC:
      all modes work, the client joined and played the same match, prediction corrections avg 0.4 m.
- [x] Escorts kept running aground: they always have a goal (their slot) so they used the harbour-approach rules
      (wall at 760 m, 35 m rock probe). Now only a captain heading for a harbour gets those; escorts may sail 20 m
      further out than their captain (600-650 m), outer slots are pulled inside, and a second 40 m rock probe.
- playtest/ (gitignored) holds the clips; playtest/make_trailer.py cuts the trailer.

## Rendering pass (2026-11-13: "the other models don't match the water's fidelity")
- [x] Sun shadows: two cascades (ships round the camera every frame, the whole arena once per match), PCF, on
      ships, islands, props and the sea (no glints / less scatter in shadow). shaders/shadow.glsl.
- [x] Lit shader: GGX sun highlight + fresnel sky reflection by roughness, sky/horizon/sea-bounce ambient, wet rock
      glossy at the waterline, sails translucent against the sun and sky-lit from both sides.
- [x] Islands: 6x denser mesh with buttresses/gullies, procedural rock + turf from a tiling noise texture (triplanar,
      world-space finite-difference bump), three-tier conifers in forest patches.
- [x] Harbours: one built mesh each (planked pier on braced pilings, stone pier head, tapered banded lighthouse with
      gallery and lamp room, tiled sheds, barrels and crates), weathering + grime line in the shader.
- [x] Deck view: the mouse wheel zooms on past 22 m onto the quarterdeck rail (V toggles), camera fixed to the hull
      with a share of its heel; near plane 0.12 m. BS_EYE "y z x" moves the eye, BS_CAMREL=1 makes --cam yaw ship-relative.
- [x] Water deeper and more saturated (scatter colour, green crest light, absorption ~1.8x, darker underwater fog).
- [x] Ratlines were solid black sheets: their texture is an RGB opacity mask; converted to alpha on load.

## Wakes (2026-11-13: "no effect from the boat crossing the water")
- [x] Wake map (Renderer::BeginWake/EndWake): a 2048^2 RGBA16F top-down map of the arena, redrawn every frame from
      each ship's track (Game::UpdateWake/DrawWakeMap). r = foam, g = churned water, b = wave ridge height.
- [x] Kelvin arms at 19.5 deg from the bow that follow the curved track, a frothy stern trail that breaks up
      within a few lengths into lighter churned water, foam where the hull meets the sea, a bow wave.
- [x] Foam patches: landings, breaches, cannonball splashes (light for skips), ships going down.
- [x] water.frag: foam broken up with three noise scales, ridges/chop bend the normal, aerated water lighter.
- Build: shaders are now copied by a target that runs on every build (shader-only edits used to be skipped).

## Sound (2026-11-13)
- [x] src/audio.cpp: every effect synthesised at startup (no sample files): cannon near/far, hull hit, rock hit,
      splash, skip, slam, breach whoosh, creak, thunder, chime, treasure, sinking; 2-6 variants each, random pitch.
- [x] Live ambience on the audio thread: rolling sea + breaking crests, gusting wind with a rigging whistle, water
      rushing along our hull (by speed, louder on deck), rain; everything muffled when the camera is under water.
- [x] 3D: distance falloff, stereo pan, speed-of-sound delay (far broadsides arrive late, and use the dull variant).
      Thunder follows lightning. M mutes; BS_VOLUME sets the level; automated runs (--shot/--record) are muted.
- [x] --sfx-export <dir> writes every sound and a 46 s sampler.wav.
- Not heard by a person yet: needs a listening pass (levels, which sounds feel synthetic).

## Known issues / next
- Escorts: ~50-70 m off their slot behind a straight-sailing captain; after a hard U-turn they take ~10-15 s to
  reform. In bot matches they average ~200 m from an erratic bot captain (fighting, chasing crates).
- Bots still run aground near harbours now and then.
- Two-PC network test still never done (one-PC host + client works).

## Later / ideas
- Base top speed is unchanged (58 kn); only Fair Wind makes it faster. Raise tune::topSpeed if wanted.
- Test flags added: --test-power, --crate-cam, --freecam px py pz tx ty tz, BS_AIMTEST=1 (with --demo).
- Two-PC network test (never done for real).
- Sail furling for the imported model; muzzle positions vs the slimmer model.
- Lighter LOD for distant ships.
- Lantern light on the water surface; thunder/sound in general (no audio yet).

If the session runs out of tokens, continue when they refresh.
