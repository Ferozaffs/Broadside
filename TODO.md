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

## Reviewer plan, part 1 (2026-11-13)
- [x] Rounds: 8 min or the score limit (FFA 12 sinkings, Teams 20, Pirate 5 banked), 60 s / 10 s warnings, a results
      screen (winner, standings, awards), 18 s later a fresh round (scores reset, everyone respawns). No damage or
      firing during the results. BS_ROUNDLEN=<s> for short test rounds; simtest checks a round end + restart.
- [x] Gun traverse 26 -> 34 deg (tune::gunTraverse), dashed firing-arc lines on the water, "GUNS CAN'T BEAR" +
      which way to steer instead of "CARRIAGE LIMIT", "OUT OF RANGE"/"TOO CLOSE" for elevation.
- [x] Hit-confirm tick sound, "SUNK <name>" for your sinkings, "HULL CRITICAL" warning under 25%.
- [x] Pirate balance: the carrier was never sunk (stern chase: broadside guns can't fire ahead). Now 0.8x speed, no Fair
      Wind, no harbour repairs, 120 hp (+20), must hold the cove 10 s to unload; hunter bots ahead of the carrier
      cut it off between it and the cove. 3 seeds: 17 grabs, 5 carriers sunk, 10 banked (several limping in).
- [x] Feed: others' air only for 360s or >3.5 s, power-ups only yours or within 450 m, groundings once per 30 s.
- Protocol v8.

## Reviewer plan, part 2: joining (2026-11-13)
- [x] src/upnp.cpp: hosting asks the router (UPnP IGD via SSDP + SOAP, plain winsock) to forward the UDP port to this
      PC, reads its public address, removes the mapping on leave/quit. HUD: "FRIENDS JOIN <ip>", F2 copies, LAN
      address; clear messages for UPnP switched off (names the router and this PC's address), refused, CGNAT.
- [x] Tested against a fake IGD (scratchpad fake_igd.py; BS_UPNP_SSDP=127.0.0.1:19000 --upnp-map-test 27615): finds it,
      maps, reads the address, unmaps. --upnp-probe is a read-only check of the real network. BS_NOUPNP=1 disables.
- The user's own router (192.168.1.1) answers SSDP only as a WPS device: UPnP port forwarding is off there.

## Ship classes + tracers (2026-11-13)
- [x] Cutter / Frigate / Ship of the Line (sim.cpp kClasses): the same hull scaled 0.7 / 1 / 1.3, so mass, inertia,
      buoyancy, drag, hit box, rocks, collisions, wakes, fx and the model all follow. Per class: hp 55/100/160, speed
      36/30/24, turn, planing, guns 3/4/6 a side (two gun decks on the big one), reload, ball speed, damage, gun arc
      +-40/34/26 deg. Cutter has a bow chaser (third trigger side, +-29 deg off the bow).
- [x] Raking fire: a ball running along the hull (in at bow or stern) does extra damage scaled by deck length:
      +0 on a cutter, +12% frigate, +25% ship of the line. "RAKED!" on hits.
- [x] Pick the class in the menu (cards with stat bars) or with 1/2/3 while sunk or between rounds; it applies at the
      next respawn (MSG_SET_CLASS). Bots pick a mix; fleet escorts are cutters. Protocol v9.
- [x] Measured (simtest 6d): cutter airborne 24% / jumps to 45 m, frigate 20%, ship of the line 5% / 10 m.
- [x] Duels (simtest 6e, BS_ONLYDUELS=1 BS_DUELPAIR=1|2|5 BS_DUELS=n, run pairs in parallel): final 32 each -
      cutter v frigate 15-15, cutter v line 16-15, frigate v line 11-16. No class dominates; bot duels are noisy
      (+-9%) and bots don't rake on purpose, so players will separate them more. 7 tuning passes, see playtest/duel*.
- [x] Tracers: balls in flight glow like red-hot shot with a fading streak; bright at night/fog, faint at noon.
- Real models for the cutter and ship of the line still to come (scaled frigate for now).

## Playtest feedback (2026-11-13 evening)
- [x] Speed lost on landing (kept 41% on average, several to ~0): heave drag now acts in world up/down (a nose-down
      hull no longer meets its own deck area as a wall), hulls level their pitch gently in the air (airPitchLevel,
      spins untouched), the keel turns 70% of a slide into forward speed (was 50%), the underwater brake only works
      on a nose driving down. Rudder drag (turnDrag) so hard turns still bleed some way. Landings now keep ~65%;
      air time unchanged (23%, planing 1.2 -> 1.35). simtest prints "landings:"; BS_LANDLOG, BS_SLIDE, BS_AIRLEVEL,
      BS_OLDDRAG for experiments.
- [x] Calm water near land: MakeShoreDamp(rocks) -> a 256^2 wave-height map (distance to land, 30% at the coast,
      full sea 260 m out) used by OceanCPU (physics) and every shader through common.glsl (same texels, bilinear).
      Storm Hs: open 15.6 m, harbours 6-10 m. BS_SHORETEST=1 --simtest measures it.
- [x] Fog 0.0032 -> 0.0068 (ships fade at ~200-300 m); night darker (moon x0.65, exposure 2.6 -> 1.8, darker sky).
- [x] Pirate: every captain hunts the carrier (lead pursuit, ignores crates and others unless < 120 m; those ahead
      cut it off at the delivery harbour). Carrier speed 0.75x its class, capped at 21 m/s. Each treasure surfaces
      at a random spot >= 950 m from a randomly chosen delivery harbour (World::cove, in the snapshot); a treasure
      dropped near it moves the delivery to the furthest harbour. 2 seeds: 8 grabs, 5 carriers sunk, 3 banked.
- [x] Landing carry (user: "68% still feels low"): at touchdown the horizontal way swings onto the keel line
      (forwards, or tail-first for a backwards landing) keeping 90-100%, and half the leftover spin is damped so it
      sticks. A hull that comes down tail-first while still spinning keeps swinging round with the drive braking
      gently (astern -> ahead in normal sailing is unchanged). Landings keep 84% overall, 90-100% unless backwards.
- [x] The aiming trajectory hides on deck and when the guns can't bear; the firing arcs hide on deck. RMB fades
      your own ship to a ghost (drawn after the water, blended; BS_AIMZOOM=1 for screenshots).
- Protocol v10.

## v0.9 feedback (2026-11-14)
- [x] Gunboat (SC_GUNBOAT, escort only): scale 0.5, 35 hp, 2 guns a side, damage 0.55, reload 3 s. Fleet escorts
      are gunboats (cutter escorts were too strong).
- [x] Host options, sent in WELCOME: ships allowed (bitmask, World::AllowedClass, bots too), deck-only camera
      (forced first person), skills. Menu rows SHIPS ALLOWED / CAMERA / SKILLS; --classes, --deck-only, --skills.
      Compact menu layout below 860 px height; card blurbs wrap.
- [x] Night darker again: moon {0.055,0.08,0.14}, exposure 1.45, ambient {0.03,0.04,0.07}.
- [x] Ship LODs: ship_hi (366k tris, <110 m), ship (150k, <320 m), ship_lo (56k, <650 m, also shadows), then
      procedural. tools/blend_to_game_ship.py takes detail + name; the LODs share ship.glb's textures. BS_LOD pins one.
- [x] Skills (protocol v12): Shift dash (0.35 s x 55 m/s^2, 8 s cd), E grappling hook (150 m, 7 s, reel 10 m/s,
      mass-shared pull, parts at 230 m or when the hooked ship dashes, 12 s cd). Server-authoritative; prediction pulls our own end. Bot hunters
      dash and hook the carrier, the carrier dashes away; --autopilot uses both. BS_HOOKTEST=1, BS_SKILLS=1 (simtest).
      A line holds for its full 7 s: no dash break, no distance break (only the hooker's E, a sinking or round end).
- [x] Hit flash: was added before exposure, so at night one hit turned the screen white for ~0.2 s. Now a warm flare
      from the screen edges after tone mapping. Particles fade as the camera closes on them. Rope: pale hemp, wider.
- [x] Deck-only is enforced under --autopilot too.

## Maelstrom arena (2026-11-14)
- [x] Host option ARENA: Isles / Maelstrom (protocol v13, arena byte in WELCOME; --arena maelstrom). Same island ring,
      whirlpool filling the bay (600 m radius). src/maelstrom.h holds the shape and current (shaders/common.glsl
      mirrors it): funnel 55 m bowl + 20 m throat, swirl 28 m/s peak at 70 m (~1/r outside), inflow 44/(1+(r/95)^2)
      m/s (grows all the way in), fading out from 420 to 600 m. First version was 340 m: too small to fight in.
      OceanCPU::Sample adds height and current, so ships, balls and particles all feel it; the waves calm to 30% in
      the funnel via the shore-damp grid.
- [x] Physics: the current drags hulls through the existing water-relative drag. Added an inward pull of 1.15x what
      keeps a hull circling with the water (buoyancy is vertical only, so without it ships flew off the tangent), and
      keel "slide carry" only counts slip the hull makes itself (otherwise the current on the beam became thrust).
      Eye (26 m): buoyancy down to 30%, 22 hp/s; kill to the last attacker, and a hook now marks its target too.
- [x] BS_MAELTEST: drifting at 350-500 m just circles (fight there); from 220 m swallowed in ~30 s, 130 m ~13 s.
      Sailing straight out escapes from: cutter 75 m (not 55), frigate 95 m (not 75, but 75 with dashes), ship of
      the line 120 m (not 95). Sailing across on a line aimed 100-350 m from the eye: every class gets through.
- [x] Bots: route round the deep part (botRoute 235 m) instead of across it, steer out inside 215 m, never dash
      towards the eye; spawns 400-600 m, crates 130+ m, treasure 200+ m. Bot matches (BS_ARENA=1, skills, 2 seeds):
      0-2 swallowed per 6 min. Teams see fewer sinkings (the sides start on opposite rims and sail round).
- [x] Look: funnel normal in the macro shape, flow-mapped small waves, spiral foam streaks advected in polar
      coordinates, a dark throat, spray thrown round the rim, a roar in the ambience. Menu backdrop circles it.
- [x] Rock hook (any arena): E with the reticle on rock within 150 m makes the line fast there (HOOK_ROCK = 64 in
      hookTarget/hooked, the point in PlayerInput::hookPoint and Ship::hookAnchor, sent in snapshots only when set).
      All the pull is on the ship (up to 30 m/s^2, easing off as it closes at 22 m/s); server and prediction alike.
- [x] Maelstrom stacks: 3 at 180-210 m spaced round the eye + 3 at 245-295 m between them (own Lcg, the island ring
      is unchanged). Rock colours follow the funnel's waterline. Rescue test (BS_MAELTEST): from 80 m a frigate or
      ship of the line is swallowed alone, and gets out hooking the nearest stack, at once or when the swirl brings it
      in reach. Bots (and --autopilot) grapple a stack further out when inside 215 m, let go once clear, look along
      their drift as well as the bow for rocks, and turn away from a stack rather than "for the middle".
- [ ] Bots hit the stacks a lot: 35-70 rock hits per 6-min match vs 15-30 on the Isles, mostly fleet escorts.
- [x] The chase camera slides in along its line when a rock hides our ship from it, and eases back out after.
- [ ] Ideas: let it wander, or appear only in storms.

## Crew mode (2026-11-14)
- [x] Role per player (menu "You sail as: Captain / Crew", --crew; HELLO carries it). The server signs a crewman on
      aboard the captain's ship with the fewest hands (human captain wins a tie), spawning a bot captain if there is
      none. World::crew[MAX_CREW], positions in hull space; MSG_CREW_INPUT (60 Hz, client-authoritative feet, server
      clamps), MSG_CREW_INFO/LEFT, crew + holes at the end of every snapshot. Protocol v14.
- [x] Guns: GunStation() on the gangway above each gun; E mans it within crewReach (3.2 m), the gun leaves the
      captain's broadside (Ship::mannedMask), reloads on its own (gunReload) and fires on the crewman's own aim
      (PendingShot::ownAim; the client lays it with SolveAim(dir, gun)).
- [x] Holes: on ships with human crew every ball hit leaves a Leak (max 8), 0.35 hp/s each; hold F at its
      LeakStation for 2.5 s to patch it and get 50% of that hit back (EventType::Repair). Water spurts from open holes.
- [x] First person: deck map rasterised from the ship model at load (ShipModel::DeckAt/WallAt: highest up-facing
      face under 6.8 m, walls from steep faces). Steps up to 0.8 m (ladder rungs), up to 1.25 m only onto the high
      decks; railings block. Camera rides the interpolated ship, a share of its roll in the horizon.
- [x] Figures: human crewmen in team colours, and the ship's own gun crews (navy jackets, white trousers) at every
      gun nobody works, on ships near the camera.
- [x] Crew HUD: gun reload ring, range and elevation, context prompts (work / leave / fire / patch), hole markers
      on deck, "CREW · <captain>'s <class>". Crew help keys. --autopilot as crew walks to a gun (round by the
      quarterdeck ladders when it's on the other side), fires, and patches holes. BS_CREWTEST (simtest), BS_CREWLOG.
- [x] Credit: balls carry the crewman who fired them (Ball/PendingShot::crew); crewmen count hits, sinkings and
      patches (snapshot), the scoreboard lists them under their captain, and the kill feed names the gunner.
- [x] Switching without reconnecting (Esc screen): captain -> "Sign on as crew" (the ship and fleet go), crew ->
      "Change ship" (MoveCrew: the next captain's ship, friends only in teams) or "Take a ship of your own".
      MSG_SET_ROLE (C->S) / MSG_ROLE (S->C). BS_ROLETEST=1 walks a client through all three on a timer.
- [x] Overboard: when the ship goes over on her beam ends in the water (up.y < 0.3 for 0.5 s), or slams down after
      2.5 s+ in the air (25%, 10% if holding a gun). The crewman swims in world space (client-authoritative, the
      server gets the world position and blocks gun work and patching), carried by the current (the maelstrom
      too); E at the hull climbs back aboard, otherwise they haul you in after 20 s. Others see a swimmer and a feed line.
- [x] Sailors have legs and arms that swing as they walk (stride from how fast they cross the deck); swimmers paddle.
- [x] Crew --autopilot walks a breadth-first path over the deck map (same step rules), to the reachable spot
      nearest its goal: round the hatch, up the ladders, out to the forecastle.
- [x] Jobs: E at the wheel (JobStation, aft) or the pin rail by the mainmast: a crewman at the helm makes her turn
      15% quicker, one tending the sails gives 6% more speed (Ship::helmed/trimmed, SF_HELM/SF_TRIM in snapshots
      so the captain's prediction matches). Crewman::gun holds JOB_HELM / JOB_SAILS for these.
- [x] Captain's orders: Z / X / C (MSG_ORDER) -> "man the port guns / starboard guns / patch the holes" banner for
      the crew aboard; bot captains with human crew give them on their own (holes first, else the side the nearest
      enemy is on), at most every 8 s and only when it changes.
- [x] Sailor model: Mixamo character with idle / run / work animations (tools/mixamo_to_game_crew.py), GPU skinning
      in the lit shader (vertexBoneIds/Weights, boneMatrices, uSkinned); CrewModel draws every figure. Gun crews work
      or idle, humans run / work / idle by what they're doing. Falls back to the stick figures without the pack.
- [x] New ship of the line: CGTrader cgt_frigate_001 (tools/fbx_to_game_ship.py -> assets/ship_line, three levels of
      detail split under 60k vertices for raylib's 16-bit indices; tools/prep_albedo.py bakes its AO into the
      albedo). Any class can have its own model in assets/ship_<class>/ (Game::ModelOf); ship.cfg gained yscale.
      Deck walking and the captain's eye now read each model's own deck (DeckLevel / DeckAft / DeckFore, StationSpot).
- [x] Model packs encrypted: BSPAK2 = ChaCha20 under assets/pak.key (gitignored; CMake bakes it into pak_key.h).
- [ ] Waiting on formats: "detailed ship" (.mb, Maya) and "Zeven Provincien" (.skp, SketchUp) can't be read here.
- [x] Sailor: "Pirate" by Bugrimov Maksim (Fab Standard License): Unreal project BroadsideExport (Documents/Unreal
      Projects) + tools/ue_export_pack.py (full editor, -ExecutePythonScript: skeletal export needs a render scene)
      -> FBX; tools/ue4char_to_game_crew.py bakes the rig's 0.01 empty into metres, retargets the Mixamo idle / run /
      work onto the UE4 skeleton (spine/head by rotation change, limbs aimed joint to joint, hands and feet at rest),
      12k triangles, clothes as the first material (the game tints only that). Not the pack's UE sample mannequins.
- [x] All three pirate outfits (sailor, sailor_2, sailor_3: Full_01-03 with the pack's other skin and hair albedos);
      each gun station / crewman keeps one.
- [x] The camera inside rigging or sails: the lit shader dithers away anything within 0.8 m of the eye (Bayer 4x4, not
      in the shadow pass). On a cutter the scaled-down deck is still cramped for a full-size man.

## After v0.11 (2026-11-14, "check 3, 6 and 7, then playtests and a gameplay review")
- [x] Performance (GTX 1060, 1600x900, 25-32 ships): chase camera 60 fps (vsync), deck views 52-60. BS_PERFLOG prints
      fps every 5 s and [hitch] lines (per-section times, and the swap). The cost up close is overdraw, not LOD.
      CrewModel caches the last pose; bot gunners beyond 140 m aren't drawn; figures beyond 50 m share timing.
- [x] Footsteps (sfx::Step, six synthesised boots on planking): yours a stride apart, a creak on a ladder or step,
      other crewmen's within 30 m from their gait.
- [x] Escorts after hard turns: tried following the captain's wake (a trail of where he sailed) and full sail through
      the turn. Neither beat the fixed slots in simtest (slot error / distance from the captain), so both reverted.
- [x] Bots aground by harbours: a ship bound for a harbour lines up on it from the open water inside and comes in
      straight (the stacks either side of the mouth are left standing); escorts follow the captain into a harbour in
      line astern; a ship with rocks dead ahead eases to half sail, and escorts out by the wall stop sprinting to catch
      up (that carried them past the captain's turn and into the rocks behind the harbour). Over 3 seeds x 3 modes:
      rock hits 196 -> 104, by harbours 50 -> 36 (the rest: badly damaged ships scraping as they go down).
      simtest prints "rock hits (n by harbours), stuck by a harbour"; BS_ROCKLOG / BS_ESCTRACE=name for detail.
- [x] v0.8.0 release zip: README.txt carried an older copy of itself; trimmed and re-uploaded.
- [x] New trailer (playtest/make_trailer_v11.py, from the QA footage) and README GIF.
- [x] QA (playtest/qa_v11.sh: crew, captain, pirate, storm teams, maelstrom, night, deck view, two-copy network crew):
  - Crew autopilot stuck for 20-40 s: in a well in the deck it couldn't climb out of (0.87 > the 0.8 step), or
    pushing at a hole it had no path to. Now: a scramble out of anything below the main deck (up to 1.25), the
    nearest hole first and one it makes no headway on for 4 s skipped for 20 s, and a rescue (onto the nearest open
    main deck) when boxed in (fewer than 160 reachable cells) or, on autopilot, getting nowhere for 5 s.
  - Gunner's eye stood right behind the breech (the line's dark gun filled half the screen): now beside the gun,
    whichever side is clear of posts (ShipModel::WallAt).
  - The captain's order banner sat on top of the AIRBORNE pill; the holes warning under it. Moved.
  - Overboard / back-aboard feed lines piled up: once per 20 s (the big banner still shows), and 5 s to find your
    feet after climbing back aboard before a heel can throw you off again.
  - Sunk crewmen were offered the class cards (the captain chooses the ship). Hidden for crew.
  - The crew tag ("CREW · CPT. X'S SHIP OF THE LINE") overflowed its panel: tracked labels measured as plain text
    (ui::MeasureLabel).

## Deck view: steadier, smoother, clearer (2026-11-16, "improve visibility on deck without taking away from immersion")
- [x] Own ship drawn between prediction ticks (ApplyPrediction carries her on by inputAcc; Reconcile keeps that part
      in visOffset). Frames and 60 Hz ticks don't line up, so some frames got 0 steps and some 2: at 30 kn she jumped
      ~30 cm a frame, and on deck the whole sea juddered (the chase camera eases, so it showed less). Measured jitter
      (position against velocity, per-second max): median 29 cm -> 11 cm. BS_NOEXTRAP: the old way.
- [x] Roll: the captain stands upright on a heeling deck (feet on the planks, body taking 75% of the heel out), the
      eye eases after its spot (~80 ms, at most 0.5 m off) and the horizon shows a third of the heel, soft-capped at
      6 deg (tanh) and eased. Storm, frigate: tilt max 16 -> 6 deg, rate 45 -> 17 deg/s; cutter 31 -> 6 deg, 200 ->
      40 deg/s. BS_OLDDECK: the old deck view.
- [x] The captain crosses to the rail on the side they look over (after 0.3 s looking there; ~4.5 m/s), 1.1 m in
      from it so the rail stays in sight. Out of the shrouds and off the centreline masts. Share of the far view (sea
      60 m+ and sky, middle 60% of the screen) hidden by our own ship, paired over 6 runs (3 ships x clear/storm):
      old spot 28%, rail 19% (half a metre from the rail 15%, but then the rail drops out of sight). BS_NORAIL.
- [x] RMB on deck is a spyglass: our own ship within 60 m, in a 14 deg cone round the line of sight and above the
      rail, goes 80% see-through (two passes: solid parts with depth, then the faint ones without; a 4x4 dither
      decides solid or faint, so the faint ones are all equally faint and their order doesn't matter), and a soft
      vignette darkens the edges. BS_NOGLASS.
- BS_DECKLOG=1 (testing): heel / tilt / jitter (ours and the nearest other ship's) every second. BS_DECKBLOCK=1 adds,
  every 5 s, how much of the far view our own ship hides (sails / anything solid) and how much has rigging in front
  (ShipModel::RayHits) from where we stand and from other spots on the quarterdeck, how often the eye is in the sea
  and how often a sailor within 7 m stands in the middle of the view. It casts ~1000 rays twice a second and hitches
  the frame, so leave it off when measuring jitter.
- Findings: other ships (snapshot interpolation) are smooth, 3-6 cm a frame. The eye is in the sea no more than
  before (0-2% in storms). Rigging counted too, the rail still wins (hull/masts/sails + rigging, cutter 50% -> 38%,
  frigate 44% -> 33%). Closer to the rail is better on every count (0.8 m: about 5 points less again, 0.55 m: 10),
  but from closer than ~1.1 m the rail itself drops out of the bottom of the screen and she feels like she's not
  there; kept 1.1 m. Further forward on the cutter puts the eye in the mizzen shrouds (rigging in front of 63%);
  further aft is behind the spanker. A sailor within 7 m in the middle of the view: 8-21% of frames.
- Next: gun crews kneeling at the rail can still stand right in front of the captain when not zooming (figures are
  only culled within 2.2 m); the cutter's sails hide more (it shares the frigate's rig at 0.7 scale).

## More race courses, menu, maelstrom treasure (2026-11-15)
- [x] Four courses (ARENA_STRAIT .. ARENA_ZIGZAG, protocol v17): the strait's plan now sits on a spine (s along it, n
      across) built from turns: serpent 2 x 130 deg R 430, hairpin 180 deg R 420, zigzag 3 x 120 deg R 170 (half
      width 130, no stack-field bulge). Spine rotated to fit tightest, centred. Every bend gets a headland on its
      inside and a stack off its point (SF_CORNER). Wall rocks stepped by distance along the wall; any wall rock that
      pokes into another stretch of channel (inside of a bend, the hairpin's other leg) is dropped.
- [x] Progress is World::RaceS (nearest point on the channel middle): standings, "m to go", bot look-ahead, stalls.
      Bots shorten their look-ahead when the straight line to it crosses rock (round a point).
- [x] Host "Any": World::raceRotate, the next course every round (StartRound rebuilds, MatchStart.flags = arena,
      clients SetOcean on it). Menu: GAME row full width (Free-for-all was cut off), race shows a COURSE row instead
      of arena/skills/fleets; Quit moved to the bottom; compact layout below 960 px tall; Segmented shrinks long labels.
- [x] Pirate: a treasure dropped in the maelstrom's funnel (r < 200) resurfaces fresh elsewhere; any dropped treasure
      left 60 s resurfaces; fresh ones surface outside the bots' keep-out ring (they never fetched them inside it).
- Simtest: 4 courses x 2 seeds all 6/6, winners 50-68 s. BS_RACECOURSE, BS_RACESEEDS, BS_RACEMAP=dir (.ppm maps).

## Race mode: the Strait (2026-11-15)
- [x] MODE_RACE always sails ARENA_STRAIT (World::Init forces it; nothing else uses it). Protocol v16.
      A 2 km channel down z (strait::startZ -1000 .. finishZ +1000, cliffs at +-endZ 1260). StraitPlan (seed):
      meandering middle Cx(z), half width Hw(z) ~205 m; features at ~-600/-220/170/540 in random order: narrows
      (headland cliffs, ~70-110 m open, a stack in each mouth), split (an island in the fairway: ~70 m gut one side,
      ~180 m the other, a stack off each end), stack field (10 stacks), dogleg (140-190 m sideways, a stack on the
      inside of each corner); one more narrows at ~830. Lone stacks off the cliffs. No harbours. Shore calm over 90 m
      (not 260) so the sea still runs up the middle.
- [x] Finish line only (no gates): course = one RaceGate right across; chequered buoys every 24 m wherever there's
      water, chequered banner. HUD: place, "N m TO GO", clock; FINISH marker / edge arrow; standings by z.
- [x] Guns run out 5 s after the start (tune::raceGunsHeld); no damage during the countdown; grid 38 m abreast,
      62 m between rows (ships of the line landed on each other at 30/45).
- [x] Bots: World::raceLine (x, free half width, z every 10 m, from scanning the open water, smoothed); they aim
      80-160 m ahead on it, each in its own lane where it's wide; dash only with 150 m clear ahead; with rocks ahead
      they take the clear heading nearest the one they want. Helm amidships during the countdown (they used to spin).
- Simtest (BS_ONLYMODE=3, BS_STRAITLOG=1 prints the channel every 50 m): 4 seeds, 6/6 finish, winners 50-83 s,
  ~20 cripples, 30-45 hooks, 15-30 rock hits per race, longest stall 6-13 s.
- [ ] Bots never hook rocks in the strait (humans can swing round stacks/headlands; bots only hook ships ahead).
- [x] Escorts: no tags, markers or feed lines at all (filler); your own keep the green ring on the water.

## Shipwright (2026-11-16, user: "add ship editor so users can paint their boats and sails, sails should have a
## windows paint like editor so you can paint your logo. Would also be nice if it was possible to edit meshes")
- [x] Livery (src/livery.h): hull, trim and sail colours ("original" = the model's own) and a 240x160 picture for the
  sails. On the wire: palette (<= 255 colours) + runs, 1-40 KB; refused over 60 KB. Imported pictures are merged
  down to fit (fewer colours, then 2x2 / 4x4 blocks). `--simtest` with BS_LIVERYTEST=1 checks the round trip,
  the limit and damaged data.
- [x] Network: protocol 18. HELLO carries the livery; the server keeps it per peer and sends it with every
  PLAYER_INFO for that captain's ship. Escorts wear their leader's. Tested host + client on one PC (each sees both).
- [x] Drawing (ShipModel::ClassifyPaint / BuildSailCanvas, ship.frag uPaint*): the new colour keeps the texture's
  grain (scaled by its brightness against the material's average). Frigate/cutter: coloredWood* = hull,
  redWood = trim. Ship of the line (one atlas): its blues are the hull, its golds the trim (KeyWeights, the same
  test on the CPU and in the shader). ship.cfg paint_hull / paint_trim / paint_key override. Each piece of sail
  cloth gets canvas coordinates (texcoords2, attribute 5): square sails seen from astern, fore-and-aft from
  starboard; the biggest square sail is "the main sail".
- [x] Shipwright screen (src/shipyard.cpp): the ship floats alone on the menu's sea, drag to turn, wheel to zoom.
  Painter: tools, size, filled, mirror, grid, undo/redo, 28 colours + hue/sat/value, drop a PNG. Esc with unsaved
  paint asks to Save or Cancel.
- Test switches: BS_SHIPYARD=1 (2: the painter), BS_YARDCLS, BS_YARDCAM "yaw pitch dist", BS_YARDTEST=tools|png,
  BS_YARDWHERE, BS_LIVERY=file (load instead of livery.dat), BS_LIVERYALL=1 (every ship in our paint).
- [x] Parts page (user: "What you suggested with figureheads etc"): figureheads, stern pieces, a flag with the picture,
  mast sliders. Livery wire version 2 (version 1 still reads): figure, stern, flag, mastH (-20..25 %), rake (-8..20 half
  degrees), spread (-15..20 %), each checked on decode. The parts are built from tubes and squashed spheres in
  src/shipparts.cpp (no model to license); where they go is found from each model (ShipModel::BuildAnchors: stem head,
  stern top, transom). The masts' shape is a bend in ship.vert (RigShape: above the hull's top along the ship, from
  the deck map, + 1.3 m) mirrored on the CPU by ShipModel::RigPoint for the masthead lantern and the pennant. The
  pennant is now a streamer for every ship. Cosmetic only: hits, the deck map and the crew are the model's own.
- Test switches: BS_YARDPARTS="figure stern flag height rake yards" (opens the Parts page), BS_YARDFOCUS=1|2 (bow,
  stern), BS_YARDLOOK="x y z" (camera on a hull-space point), BS_ANCHORLOG=1 (magenta balls where the parts go).
- Found on the way: the ship of the line sailed backwards (assets/ship_line/ship.cfg had flip=1; her bowsprit and
  spritsail pointed aft, her stern lanterns hung off the bow). Now flip=0. The cfg is gitignored: it reaches players
  through the release staging, which copies it.
- Not done: editing the meshes freely / splicing hull and rig between the models (the line ship is one atlas without
  separate hull and rig parts; see the reply of 2026-11-16).

## Roll, realistic mode, no bot limit (2026-11-16, user: "Roll is still too much especially when hit by cannon balls tune roll and also spin back a little. We also want to introduce a realistic mode where ship behaves closer to reality except wind direction. Also remove the bot/player limit")
- [x] Roll: a ball hit keeps its whole shove but only 15% of its rolling effect (yaw 70%, pitch 50%); extra roll damping
  and righting about the keel afloat, and the same levelling in the air (a point-blank broadside threw a cutter clear
  and it kept rolling); less lean in turns (heel 0.03 -> 0.012). `BS_ROLLTEST=1 broadside --simtest` (4-gun broadside into
  the beam, then hard over): cutter 89 -> 18 deg, frigate 76 -> 9, ship of the line 18 -> 5; turn heel 31-35 -> 14-21 deg.
  Full simtest vs v0.13: jumps and duels alike, the worst roll in the jump run up.y 0.34 -> 0.94. Tuning switches:
  BS_HEEL, BS_ROLLDAMP, BS_ROLLRIGHT.
- [x] Realistic handling (host option, `--realistic`, protocol 19): World::Real(). Top speed x0.21,
  1.5 m/s astern, drive 0.6 m/s^2 / scale^2, turn rate x0.07 needing way on, slow yaw response, no planing throw, no
  breach kick, no slide carry, ball shove x0.02, recoil x0.1. Bots pace escorts / lead the treasure carrier by the real
  speeds. `BS_HANDLINGTEST=1`: cutter 14 kn, frigate 12.5, line 10; 60/95/160 m turning circles. Realistic duels
  (`BS_ONLYDUELS=1 BS_NOMODES=1 BS_REAL=1`) end in 1.5-2.5 min, no groundings. Host + autopilot client: prediction
  error avg 1 cm.
- [x] No ship / player limit (user: "I said no limit on bots as well, computers will have more performance in the future"):
  World::ships / crew are std::deques that grow (NewShipSlot / NewCrewSlot; a deque so a Ship& survives AddShip), the
  client's per-ship tables grow on first sight of an id (Game::GrowShips / GrowCrew, also pred.EnsureShips). Ship and
  crew ids are 16 bits on the wire (NO_SHIP = 0xFFFF, HOOK_ROCK = 32766, SHIP_ID_LIMIT 32000); 4095 peers (ENet's max).
  Bot stepper uncapped; 24 bot names then numbered; TAB scores squeeze/cut to fit; a big race field lines up six abreast.
  `BS_CROWDTEST=1 BS_CROWD=n`: 63 bots ~3 ms a tick, 100 ~4.6 ms, 100 captains with fleets (400 ships) ~12 ms.
  40 bots in practice: 46-61 fps on the GTX 1060. A snapshot is ~115 bytes a ship at 30 Hz, to every client.
- [x] Realistic handling in every mode and arena, with or without skills (user: "We should not limit the users").

## Voyage: co-op mode (2026-11-17, user: "build a coop mode where you have survive going from on harbor to the other while
## chased by pirates. Arena must be way way bigger.")
- [x] MODE_VOYAGE (team 0 = the convoy: every human and the host's bots, now "allies"; team 1 = pirates) on its own sea,
  ARENA_PASSAGE: a rounded oblong of coast (|x/1550|^4 + |z/4900|^4 = 1) of overlapping big islands with a backdrop of
  high land, start harbour in the south end, destination in the north, a haven halfway up one side; ~34 islands, islets
  and ~26 stack clusters inside (MakePassage in sim.cpp). ~12x the Isles' water. Built in ~140 ms.
- [x] Sea lane: Dijkstra over a 25 m grid with a clearance cost, string-pulled, Chaikin-rounded, every 10 m
  (World::route, RouteS/RouteAt). The convoy bots follow it (each in a lane, waiting for the hindmost human), the HUD
  draws it as gold dots on the water for the next 800 m.
- [x] Pirates (Ship::raider): bands of 1-3 out of sight (1050-1400 m) astern 45%, abeam 35%, ahead 20%; how many at
  once (1 + captains + 0.3 escorts) x (0.6 + 0.8 progress), bigger ships further on; +35% speed while more than ~150 m
  from their prey; give up after 15 s more than 2.7 km from every convoy ship; keep 700 m off the haven and the
  destination; go down for good (slot reused only after the clients heard it left). "PIRATES SIGHTED off the port bow".
- [x] Reserve: teamScore[0] (2 per starting captain, min 3, +1 per human joining under way); sunk with none left ->
  Ship::lost (SF_LOST), out until the next voyage. teamScore[1] = pirates sunk. Won: every human captain afloat in
  the destination's ring and nobody waiting; lost: nobody afloat or waiting. Round limit 60 min.
- [x] The voyage waits in harbour until someone is aboard (the host's client joins a moment after its server starts).
  Late joiners: in the harbour line until the convoy is 400 m out, then astern of the leader.
- [x] Big-sea plumbing: shore-damp grid per arena (1024 cells over +-5400 m for the passage; OceanCPU::DampN/Extent,
  shore texture recreated on a size change), harbour scenery generalised off the 730 m ring (Port::base), arena
  shadow box follows the camera in 1 km steps, island LOD (coarse copy past 1.4 km) + side culling + 9 km cull
  (voyage 50 -> 70-78 fps on the GTX 1060), bots look further ahead for rocks the faster they go, the camera starts
  astern of your ship on every spawn (it kept the last compass bearing and could face the bow).
- [x] Tests: BS_VOYAGETEST=1 (BS_CONVOY, BS_SEED, BS_REAL, BS_FLEETS, BS_SKILLS, BS_VOYMIN, BS_VOYMAP=chart.ppm,
  BS_ROCKLOG, BS_VOYNOSAIL), BS_VOYGRACE=s brings the first pirates sooner, BS_PIRATES=n caps them. Results: 4 bot
  captains make harbour in 6-11 min losing 2-10 ships; one alone 9-17 min; Realistic 27 min; tick ~2 ms.
- [x] Difficulty (host, menu row DIFFICULTY in place of the one-option sea; `--difficulty easy|normal|hard|deadly`;
  World::difficulty, voyage::kLevels; in MSG_WELCOME). Reserve per captain 3/2/1/0 (at least 5/3/2/0; Deadly: none,
  joiners bring none), pirates x0.6/1/1.35/1.7, band gaps x1.4/1/0.8/0.65, first band after 50/35/25/15 s, catch-up
  0.2/0.35/0.45/0.55, class mix shifted -0.2/0/+0.15/+0.3, Easy pirates' aim wanders 0.06 rad. Bot runs (one each):
  2 captains made it / made it / made it / LOST at 200 s; 4 captains made it (2 lost) / (3) / (4) / LOST at 471 s.
- [x] Sea monsters (2026-11-17, user: "add kraken should be fearsome but very rare not all games should spawn it.
  Megladon shark would also be nice"). World::monsters / Monster (sim.h), UpdateMonsters (sim.cpp), rolled when the
  convoy sails (Level::kraken / megalodon chances, where along the route), NetMonster in the snapshot, EventType::Monster
  (MEV_*), Kill a = KILLER_MONSTER + type. Kraken: stalks under its prey, rises alongside (2.5 s), holds (Ship::held:
  dragged low, speed bled away, also in the client's prediction), 2.5% of her hull a second + a 7% slam every ~4.5 s
  (any class lasts ~25 s), hp 170 + 55 per captain (x Level::monster 0.7/1/1.2/1.4); lets go after 55 s; another
  victim only if still hungry (half the time, above half health, at most 2, 3 on Hard+). Megalodon: circles at 75 m,
  fin up, charges at 21 m/s, bite 6 + 12% of her hull, dives 5 s, again; 6 bites or 220 s. Balls hit them (sphere /
  capsule; the kraken's 17 m round a body 15 m off her side, drawn 1.8x: monster::krakenSize, user: "Kraken looks too
  small"); convoy bots circle in at a cable to shoot (aiming at the kraken's far side, away from her). Art in
  monsters.cpp (procedural: mantle, eyes, arms as per-frame tubes wrapping the hull, a lofted swimming shark with fins,
  gaping jaw, teeth); sfx Roar and Bite synthesised. Tests: BS_KRAKEN=m / BS_MEGALODON=m force one at m metres,
  BS_NOMONSTERS, BS_MONPOSE (harmless, holds you forever: screenshots), BS_MONCAM="dist height deg" (a camera on it).
- [x] Raid (2026-11-17, user: "reversed mode were you are the pirates that should takedown a convoy before it reaches
  port"): MODE_RAID on the passage. Captains (and host bots) are team 1, pirates; SpawnConvoy puts (3 + 2 per pirate
  captain) x Level::raidShips merchants (Ship::merchant, team 0, AI, max 30) in the start harbour's line when someone's
  aboard; they run the voyage's convoy logic (keep within 250 m of the hindmost, fight inside 230 m) at 72% speed
  (laden). Sunk or in port (EventType::Escaped) they're gone. teamScore [0] in port, [1] sunk; won at raidNeed sunk
  (Level::raidNeed share: 50/60/70/80%), lost once too many are in port. Pirates spawn/respawn by PlaceNearConvoy:
  in wait 1.6-2.5 km up the lane, then 650-950 m from a merchant, ahead or abeam. The haven is the pirates' (repairs).
  Monsters hunt the players' side (PlayerTeam()). Snapshot carries raidTotal / raidNeed: protocol 23. Test:
  BS_RAIDTEST=1 (BS_PIRATES=n bot captains, BS_DIFF, BS_SEED, BS_RAIDSHIPS).
- [x] Boss mode (2026-11-17, user: "a boss mode where you fight a huge ship that feels a bit like bullet hell, coop"):
  MODE_BOSS on the Isles; SC_BOSS (scale 3, the line's model) "The Leviathan", Ship::boss, team 1, its own brain
  (BossThink: a 230 m circle round the bay) and guns (UpdateBoss: patterns, World::BossBall, not class guns). Hull
  (3000 + 1500 per captain) x Level::bossHp (0.55 / 1 / 1.3 / 1.6), pattern rate x Level::bossRate. Patterns: ring
  (2-3 waves at 120-320 m), spiral (2-3 arms, ranges cycling), broadside walls (3 waves from 90 m out), aimed fans,
  mortars (Ball::splash 17 m bursts, EventType::BossMortar marks the spot), plus grapeshot at anyone inside 170 m
  every 1.4 s. Phases at 2/3 and 1/3 (EventType::BossPhase); phase 3 brings 2 + captains/2 gunboats (SpawnRaider).
  Shared reserve as in the voyage. Its shot never hits itself (fired from inside the hull). Allied bots circle it at
  210-270 m, slower, and allow for their own way when aiming (boss mode only: the shot drifted ~100 m). Client: boss
  bar with phase pips, magenta tracers for its shot, red mortar rings, banners. Network: snapshots now carry only
  balls in flight, positions in 16 bits (0.2 m): ~16 bytes a ball; peaks ~220 balls in the air.
  Bot runs: 3 captains Normal lose at phase 3 after ~8 min; Easy 2 captains got it to ~5%; Hard 4 captains lose in 3.5
  min. Players who dodge should win Normal. Test: BS_BOSSTEST=1 (BS_CAPTAINS, BS_DIFF, BS_BOSSPAT=n one pattern,
  BS_BOSSHP), BS_BOSSCAM="dist height deg spin".
- [x] Menu redesign: two pages. Front: name, Host a game, Join (address + button), Shipwright, Quit, ship cards.
  Host page: the modes as cards (VERSUS: FFA, Teams, Pirate, Race / CO-OP: Voyage, Raid, Boss) and beside them only
  the chosen mode's settings, then "Host game - open to friends" and "Practice" (offline). Esc goes back.
  BS_MENUPAGE=1 opens on the host page (screenshots).
- Protocol 24.

## More bosses (2026-11-17, user: "We should add more bosses" -> "Make all of them, start from the top")
Plan, in order: Kraken Queen, Flying Dutchman, Sea Serpent, Sea Fortress, Admiral's Armada, Ironclad, Storm Caller.
Framework (done): BossKind / kBossNames (sim.h), World::bossChoice (host: --boss, the menu's picker under the co-op cards;
BOSS_RANDOM = a new one each round, SpawnBoss picks among the ones built so far), bossKind, bossHpNow/Max (the bar),
bossPos/Heading/State/T, bossParts (BossPart: hittable pieces). Bosses that aren't ships get a stowed proxy ship
(BossProxy: owns their shot, sinks ours, never in the sea or the snapshot). Balls into them: BallHitsBoss -> BossDamage
(EventType::BossHit / BossPart). Snapshot: a boss block (kind, phase, state, hull 0..1, pos, heading, t, parts) -> client
bossView; pred gets bossKind/Phase/Pos for currents. Tells: EventType::BossTell (a strike line, red on the water,
DrawTells), BossSlam, BossInk. Allied bots: BossAimFor (where to shoot), dodge tells and shells about to land.
Protocol 25.
- [x] Kraken Queen (BOSS_KRAKEN): body in the middle of the bay (3.4x a kraken), 6 arms on an 80 m ring (BossPart:
  DOWN, RISING, UP, WINDUP, SLAM, GRAB, SINKING). Arms shield her (6 up: half damage, full with 3 or fewer). Slam: rears
  back (tell 1.8 s), comes down along a 240 m line (14 dmg + shove); one at a time (two later), 8-13 s apart per arm.
  Grab (25%, within 130 m of its root): holds a ship (Ship::held, also predicted), drags her in, 1.6% hull/s, 9 s, until
  the arm is shot off (arm hull 110 + 40 per captain). Phase 2: whirlpool current round her (QueenCurrent in StepShip,
  predicted too) and ink clouds; phase 3: ink bombs (mortars) and arms regrow in 20 s not 32. Body hull 1600 + 700 per
  captain. Bot runs (3 captains, Normal): she wins at ~60% hull in phase 2 after ~7 min.
- [x] Flying Dutchman (BOSS_DUTCHMAN): a ship (SC_GHOST, scale 2, the line's model drawn see-through after the water,
  green sails and lanterns, mist). States (fd::, World::bossState): ARRIVING, PHASED (ethereal: 1.8x speed, shot and
  hulls go through it, no tag or marker for us, bots don't aim at it), MATERIALISING (2 s, 230 m astern of the captain
  it's haunting, World::bossTarget; "IT'S BEHIND YOU"), SOLID (11 s, 13 in phase 3: spectral shot - Ball::ghost, green,
  through islands - fanned broadsides, rings of slow souls, aimed fans), FADING (1.5 s). Phase 2: 1-2 drowned cutters come
  up with it each time; phase 3: cursed water (bossParts state 1, 48 m, 12 s: sail at 0.45, predicted too). Hull 1500 +
  650 per captain. A green-grey fog comes in while it's the boss (client, UpdateAtmosphere). Bot runs: phase 2 at ~58%.
- [ ] Sea Serpent  - [ ] Sea Fortress  - [ ] Admiral's Armada  - [ ] Ironclad  - [ ] Storm Caller
- Open: no compass or chart in the HUD (the lane and the harbour marker do the job); the bot convoy fights only
  what comes within 300 m.

## Playtest list (2026-11-17, user: "add friendly fire so bots play less chaotic, also red team spawn in the wrong directions.
## Respawns should be close to harbor. Realistic mode could need a little better turn rate. Escort should have less hp so a
## full salvo from a cutter kills on ... crew should have way higher fire rate like 5-10x ... climb up an be lookout.
## Custom sails should also be heavily tinted towards their team color ... a blend mode that works with black")
- [x] Friendly fire: host option (default on, `--no-friendly-fire`), World::friendlyFire. Bots hold fire with a friend within
  ~20 m x scale of the line to the target (or 60 m past it), whatever the option. Mode tests, friendly hits in 6 min:
  free-for-all 30 (of 162), teams 22 (of 123), pirate 6 (of 276).
- [x] Respawns off a harbour mouth (500-610 m out along its axis, spread +-75 m, clear of rocks and other ships), facing the
  centre of the bay. Teams: your side's harbour. Otherwise the approach furthest from enemies: a mouth, or 0.55 rad
  beside one when every mouth is taken (pirate: 5 fleets, 4 harbours; first sinking 2 s -> 15 s). BS_KILLLOG=1 in the
  mode tests prints the first kills.
- [x] Realistic turning x0.07 -> x0.12: cutter 8.4 -> 13 deg/s, frigate 5.8 -> 7.3, ship of the line 2.9 -> 3.7.
- [x] Escorts (the gunboat class) 35 -> 14 hp: a full cutter broadside (3 x 8 x 0.75 at ~0.8 speed) sinks one.
- [x] Crew guns reload x0.15 (tune::crewReload, ~7x). Crew test: the crewman fired 106 of the ship's 229 shots in 3 min.
- [x] Lookout (JOB_LOOKOUT 102): E at the port shrouds by the mainmast; perch LookoutSpot(MastTop) = 74% up, through
  ShipModel::RigPoint (follows the sliders); the server lets a lookout's position up to 60 m; Ship::lookout -> SF_LOOKOUT;
  every enemy captain's tag on for everyone aboard. BS_CREWJOB=102 starts a crewman aloft (tests).
- [x] Team tint for painted sails (teams, and the pirate): ship.frag uTeamTint, colour = side colour x (0.2 + 1.05 sqrt(luma)),
  mixed 0.8 (BS_TEAMPAINT): white -> bright red/blue, black -> deep red/blue, a picture stays in shades of it.
- Protocol 21.

## Wider Shipwright sliders (2026-11-17, user: "increase the shipwright slider so we can have more rake etc. increase by at least 50%")
- [x] livery.h ranges: height -30..+40 % (was -20..+25), rake -6..+15 deg (was -4..+10), yards -23..+30 % (was -15..+20).
  Protocol 20 (an older game refuses the new values, so the versions don't mix). All three classes checked at both ends.

## Far-ship model artifacts (2026-11-16, user: "the current ones seems to have some rendering artifacts ... at the front of the frigate")
- [x] ship_lo.glb (every ship beyond 320 m, and every ship's shadow) had its ropes squeezed to a few triangles each; the
  strands fused into flat sheets: a plank-like slab beside the bow (the anchor cables) and a grey wedge by the jib, and
  their shadows on our own ship's bow (the shadow pass draws ship_lo with ropes for ships within 280 m).
  tools/blend_to_game_ship.py now drops the rigging from the low level (distant ships never drew it); ship_lo is 34k
  triangles (was 56k). Found with BS_HIDEPART=1/2/3 (no sails / sails only / no ropes) and BS_PARTDUMP=<file>
  (per-part triangles and bounds) - neither the hull budget nor small parts were it.
- The frigate's anchors are near-black iron in the model (base colour ~2% grey): crude up close, by design.

## Rooms (2026-11-16, user: "online play just like i did in qwop-row with room names") - REVERTED
- Taken out again until our own signalling server exists (user: "revert that fix for now until we fix proper
  cloudflare"): the PeerJS cloud only took our cards disguised as a WebRTC candidate, and the two-home path was
  never tested. The code is in git (commit e04c7a2, src/rooms.cpp): STUN on the ENet socket, the punch and the
  ENet intercept carry over; only the signalling transport changes. Notes from the PeerJS version:
- [x] src/rooms.cpp: the host's server lists its room on PeerJS's cloud signalling server (wss://0.peerjs.com, as qwop-row's
  PeerJS does) as "broadside-<name>"; a joiner sends the room its card (protocol version, public address from STUN
  asked on the game's own ENet socket, LAN address), the room answers with its own, both punch UDP at each other
  ("BSPUNCH!", eaten by the ENet intercept like the STUN answers) and ENet connects. The cards ride inside a
  CANDIDATE message: the cloud server re-checks message shapes and hangs up on an OFFER without a real SDP.
  A missing room comes back as EXPIRE -> "No room called ...". Another protocol version -> says so.
- Tested on one PC: host + `--join <room>` finds the room and joins over the LAN address (the host's punch shows
  the way in). Through the router's own public address (BS_ROOMPUB=1) it doesn't get in: this router doesn't loop
  back (NAT loopback), which says nothing about two homes. Not yet tested between two homes.
- No relay (TURN): two strict routers (symmetric NAT / CGNAT on both sides) can't meet; the address + UPnP is the
  fallback. Switches: `--room NAME` (host / dedicated server), BS_NOROOM=1, BS_ROOMLOG=1 (signalling trace),
  BS_ROOMPUB=1 (join by the public address even on one network).

## Known issues / next
- Deck view (fixed 2026-11-14): the captain's eye stood in front of the quarterdeck, at a height from the physics deck;
  it now stands on the model's quarterdeck (ShipModel::DeckAt) behind the rail. Figures closer than 2.2 m to the
  camera aren't drawn.
- Escorts: ~70-90 m off their slot behind a straight-sailing captain; after a hard U-turn they take ~10-15 s to
  reform (they keep way on and swing wide). Behind a lone bot captain ~125-160 m off their slots (out by the wall they
  no longer sprint to catch up: the price of fewer groundings). In bot matches ~150-180 m from the captain.
- Crew view under water: a ship of the line landing a 4-5 s jump (power-ups) goes 6-13 m under; a cutter in a storm
  dunks every few seconds. The physics is as intended for captains, but crew spend a lot of time looking at the sea
  from below. Options: shallower landings for crewed ships, or a quicker pop-up.
- Captain's deck view on the ship of the line looking abeam: the eye is only just above its high bulwark and the gun
  crews stand on the raised carriage cells by the rail.
- Two-PC network test still never done (one-PC host + client works).

## Later / ideas
- Base top speed is unchanged (58 kn); only Fair Wind makes it faster. Raise tune::topSpeed if wanted.
- Test flags added: --test-power, --crate-cam, --freecam px py pz tx ty tz, BS_AIMTEST=1 (with --demo).
- Two-PC network test (never done for real).
- Sail furling for the imported model; muzzle positions vs the slimmer model.
- Lantern light on the water surface; thunder/sound in general (no audio yet).

If the session runs out of tokens, continue when they refresh.
