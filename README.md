# Broadside

Online PvP age-of-sail ship battles with over-the-top physics: FFT ocean swell you can jump off,
360s in the air, cannonball arcs, power-up crates and an island-ringed arena.

[![Broadside trailer: the maelstrom, storms and broadsides, the kraken and the megalodon, Voyage and Raid](docs/trailer_teaser.gif)](https://github.com/Ferozaffs/Broadside/releases/download/v0.17.0/Broadside_v0.17.0_trailer.mp4)

**[Watch the full trailer (1:48)](https://github.com/Ferozaffs/Broadside/releases/download/v0.17.0/Broadside_v0.17.0_trailer.mp4)**

Built with [raylib](https://www.raylib.com/) and [ENet](http://enet.bespin.org/). The ocean is based on
[GarrettGunnell/Water](https://github.com/GarrettGunnell/Water).

## Playing

Download the latest zip from [Releases](../../releases), unzip it anywhere and run `broadside.exe`
(Windows, needs an OpenGL 4.3 GPU).

- **Practice**: play against bots.
- **Host**: the game asks your router to open **UDP port 27615** (UPnP) and shows the address friends should
  type at the top left (**F2** copies it). If the router has UPnP switched off, the game says so: switch it on in
  the router's settings, or forward UDP 27615 to your PC by hand. Allow `broadside.exe` through Windows Firewall
  when asked. Friends on the same network use the "same network" address.
- **Join**: type the host's IP address.

Everyone must run the same version.

| Key | Action |
| --- | --- |
| W / S | Ahead / astern |
| A / D | Rudder |
| Mouse | Aim (cannons follow the camera) |
| Left mouse | Fire broadside |
| Right mouse | Zoom (your own ship turns see-through; on deck it's a spyglass that looks past your own sails) |
| Mouse wheel | Camera distance; roll all the way in to stand on the quarterdeck (you cross to the rail you look over) |
| V | Deck view on / off |
| M | Sound on / off |
| 1 / 2 / 3 | Next ship: cutter / frigate / ship of the line |
| Shift | Dash: a hard shove ahead, also in the air (skills on, 8 s cooldown) |
| E | Grappling hook on the ship under the reticle (150 m), E again to cut it (skills on, 12 s cooldown) |
| Tab | Scores |
| F1 | Hide help |

Sail through the floating crates for 15-30 s power-ups: **Rapid Reload**, **Triple Shot** and **Fair Wind**.

### Shipwright: paint your ship
**Shipwright** in the menu: pick colours for the hull, the trim and the sails, and paint a picture for the sails
in a little Paint-style editor (pencil, brush, eraser, fill, line, box, oval, text, colour picker, mirror, undo; right
mouse erases). Drop a PNG on the window to put a logo on the canvas. The picture goes on the main sail, every
square sail or every sail. Your ship floats beside the editor with the paint on as you work. Everyone you sail
with sees your paint (your escorts wear it too); in Teams the sails still take your side's colour. It's saved in
`livery.dat` next to the game.

The **Parts** page dresses her up: a figurehead (sea serpent, gilded scroll, narwhal horn or skull), something on the
stern (a great lantern that glows at night, a gilded sun, or a skull and crossbones), a flag at the stern that flies your
picture, and sliders for the masts' height and rake and how wide the yards spread. The camera swings round to the bow or
the stern as you pick. All of it is for show: the ship sails and takes hits the same.

### Ships

Pick your ship in the menu; while you wait to respawn (or between rounds) press **1 / 2 / 3** to sail something
else next time.

| | Cutter | Frigate | Ship of the Line |
| --- | --- | --- | --- |
| Hull | 55 | 100 | 160 |
| Top speed | 36 m/s | 30 m/s | 24 m/s |
| Guns a side | 3 light, quick reload, swing 40 deg | 4, swing 34 deg | 6 heavy on two decks, slow reload, swing 26 deg |
| Special | bow chaser: one gun that fires forward | the all-rounder | lands like a hammer |

A cutter flies off every swell; a ship of the line barely leaves the water but out-guns a frigate. **Raking fire**:
a shot that runs along a ship's length (in at the bow or stern) does extra damage, more the longer the deck - so get
off a big ship's beam and onto its ends.

With Fleets, every captain's escorts are **gunboats**: half a frigate's length, 35 hull, two light guns a side.
They are escorts only, not a ship you can pick.

### Game modes

The host picks the game in the menu:

- **Free-for-all**: everyone against everyone. First to 12 sinkings.
- **Teams**: red against blue; each side has a home harbour. First side to 20 sinkings.
- **Pirate**: one treasure chest surfaces somewhere out on the bay and everyone races for it. Whoever hauls it
  aboard turns pirate (black sails, a slightly tougher hull and a faster reload, but much slower, no Fair Wind and
  no repairs) and has to sail it to the harbour flying the black flag - always far away, and a different one for
  every treasure - and hold it for 10 seconds to unload, while everyone else hunts them. Sink the carrier and the
  treasure goes over the side for anyone to grab. First to 5 banked.
- **Race**: a 2 km sprint down a channel between cliffs, on one of four courses: the **Strait** (straight, with a
  dogleg), the **Serpent** (an S of two long sweeping bends), the **Hairpin** (a U-turn round a headland: you finish
  alongside where you started) and the **Zigzag** (three sharp corners, narrower). The host picks one, or "Any" for
  a different course every round. First across the finish line at the far end wins; the rest get 20 seconds. Everyone starts abreast for a 3-2-1 countdown. On the way:
  narrows where the cliffs close in to under 100 m and the whole pack arrives at once, an island in the fairway
  with a wide way round and a narrow gut down the other side, a field of sea stacks to thread, and a dogleg. Dash
  and the grappling hook are always on and come back quicker: hook a ship ahead and let it tow you up, or throw a
  line round a stack or a headland and swing yourself through a corner (every bend has a headland on its inside
  and a tall stack off its point for exactly that). Guns run out 5 seconds after the start.
  Sunk means crippled: you're patched up on the spot but sail at 40% for 4 seconds (and can't be crippled again
  meanwhile). Every match gets a new layout: the order of the narrows, island, stacks and dogleg changes.
- **Voyage** (co-op): everyone sails together as one convoy from a harbour at one end of **the Passage** to a safe
  harbour at the other, while pirates hunt it all the way. The Passage is its own sea, about 9 km long and 3 km wide
  (some twelve times the Isles): high coast all round, an archipelago of islands, islets and sea stacks in between,
  and a haven with shipwrights halfway up one side. A dotted sea lane on the water shows a way through, and the safe
  harbour is marked all the way. The menu's bot count becomes your **allies**: bot captains who sail in formation
  either side of the human ships, at their pace, firing at whatever their guns bear on without turning aside to
  fight. The pirates (black sails) turn up out of the haze in
  bands, mostly astern or abeam, sometimes lying in wait ahead; "PIRATES SIGHTED" tells you where. There are more of
  them, and bigger ships, the further you get, and they sail faster while they're out of range. They keep clear of
  the haven and the safe harbour (the forts' guns). Nobody respawns: a convoy ship whose hull is gone is
  **disabled**, dead in the water, and a friend who stays alongside her for 10 seconds brings her back with a quarter
  of her hull (the bots go back for you too). Every human captain afloat in the safe harbour (and nobody disabled)
  wins it; every convoy ship disabled at once loses it. Takes about 6-10 minutes in Arcade, half an hour in
  Realistic. Works with fleets, skills, crew, friendly fire and both handlings.
  The host picks a **difficulty**:

  | | Pirates | First pirates |
  |---|---|---|
  | **Easy** | 60% as many, less speed to catch up, smaller ships, their gunners miss more | after 50 s |
  | **Normal** | as above | after 35 s |
  | **Hard** | 35% more, faster to catch up, bigger ships, bands come sooner | after 25 s |
  | **Deadly** | 70% more, faster still, mostly frigates and ships of the line | after 15 s |

  **Sea monsters.** Not every voyage meets them (the harder the voyage, the likelier):
  - **The kraken** (rare: 8% of voyages on Easy, 15% Normal, 22% Hard, 35% Deadly). The sea goes dark under a convoy
    ship; then it rises alongside, wraps her in its arms and drags her low in the water, nearly stopped, crushing her
    hull and smashing its free arms down on her deck. Left alone she goes down in about 25 seconds. Shoot it off her:
    its body lies right alongside, so her own broadside on that side bears on it point-blank, and the rest of the
    convoy can come round and pour shot into it (careful: a miss can hit her). It has more hull than a ship of the line
    and grows tougher with every captain. If it sinks her and is still hungry it goes for another. It lets go after a
    while, and it never takes a ship in harbour.
  - **The megalodon** (25% / 35% / 45% / 55%): a fin cuts the water and circles a ship, then the charge, the jaws open,
    and a bite that tears a chunk out of her hull and throws her about. Then it dives, comes round and does it again,
    half a dozen times before it has had its fill. Shoot it when the fin is up.

  Slaying one counts as a kill. They roar, bite, ink and bleed; a banner and the feed tell everyone where they are.
- **Raid** (co-op): the voyage the other way round. Everyone sails as a **pirate** (black sails; the menu's bot count
  becomes your fellow pirates) and an AI **convoy** of merchantmen sails the Passage for the far port, down the sea
  lane, laden and slow, in formation (three columns, at the pace of its slowest ship), never turning aside to fight
  but firing at whatever its guns bear on. It's **four merchantmen for every pirate captain**, and you must sink
  **every one**: if a single ship makes port, the raid has failed. The difficulty sets how stout they are (hulls x1.3
  on Easy, x1.6 Normal, x2 Hard, x2.4 Deadly) and how many are ships of the line. You start
  lying in wait a couple of kilometres up the lane and come back from a sinking within a kilometre of the convoy. The
  haven is your cove (repairs). The sea monsters come for the pirates here.
- **Boss** (co-op): everyone (and the bots, your allies) against one huge enemy, bullet-hell style. The host picks
  which under the co-op cards (or Random: a new one every round). Each has three phases as its hull goes (the bar at
  the top of the screen), and **red** on the water always marks where something is about to land. Nobody respawns
  and the harbours do no repairs: a ship whose hull is gone is **disabled**, dead in the water, and a friend who
  stays alongside her for 10 seconds brings her back with a quarter of her hull (the bots come to help too; each time
  the same ship goes down in a round it takes half as long again). If every ship is disabled at once, the fight is
  lost. The difficulty sets its hull and how fast it attacks; its hull also grows with the crew it meets (tuned for
  three captains: a solo captain faces about a third of it, six about twice and more), and most bosses attack a little
  faster for a big crew and slower for a solo one.
  - **The Leviathan**: a man-o'-war three times the size of a ship of the line, under black sails. It fills the sea
    with magenta shot in patterns: rings, turning spirals, marching walls, aimed fans and marked mortars, with
    grapeshot up close. Later it fires faster and its gunboats come out.
  - **The Kraken Queen**: a colossal kraken in the middle of the bay with six arms that rear back and slam down along
    marked lines, or grab a ship and drag her in until the arm is shot off. Her arms shield her. Later come a whirlpool,
    ink clouds and ink bombs.
  - **The Flying Dutchman**: a ghost ship that you can't hit while it's phased out. It turns up astern of one of you
    ("IT'S BEHIND YOU"), fires spectral shot that passes through islands, and fades again. Later drowned cutters rise
    with it and the water is cursed.
  - **The Sea Serpent**: circles you spitting venom, dives, and breaches in a great arc onto a marked ring. Later it
    coils round a ship; shoot the coil to free her.
  - **The Sea Fortress**: a fortress on its own island in the middle of the bay. Six towers, each with its own fire,
    shield the keep. A searchlight sweeps the water, and where it settles the mortars follow.
  - **The Admiral's Armada**: a line of battle that fires marked volleys together, shielding the Admiral's gold-sailed
    flagship. A fresh line sails in at each phase, and later fire ships come.
  - **The Ironclad**: an armoured steamship that your shot glances off, except at her stern and paddle-boxes. She picks
    one of you and rams down a marked lane; get out of it. Later a deck gun sweeps anyone close.
  - **The Storm Caller**: brings a storm with it. Lightning strikes where a ring marks the water (turn or shorten sail),
    and waterspouts wander the bay, pulling ships in, spinning them and lifting them. Later rogue waves roll right
    across the bay, throwing everything they pass.
- **Odyssey** (co-op, roguelike): a run of rounds, each harder than the last. Odd rounds are **sea battles** in the
  bay (the pirate fleet comes round the islands in waves; sink every ship of it), even rounds are **legs of the
  Passage** (get the convoy from the start harbour to the haven, then from the haven to the far port), and **every
  fifth round is a boss**, the eight in an order of the run's own, each tougher than the last. After every round you
  win, each captain picks **one of three upgrades** (1 / 2 / 3, or click), kept for the whole run. Revives work as in
  Boss mode.
  - **Steady ones** (stack up to five): Stout Timbers (+20% hull), Drilled Gunners (reload 10% faster), Heavy Shot
    (+15% damage), Clean Hull (+6% speed), Iron Sheathing (12% less damage), Ship's Surgeon (bring friends round
    faster, come back stronger), Carpenters (mend your hull when nobody's shooting at you).
  - **Ones that change how you fight**: Hot Shot (your hits set ships ablaze), Grenado Shells (your shot bursts: near
    misses hurt too), Chain Shot (hits tear their rigging, they sail slower), Double-Shotted (a chance a broadside fires
    twice), Prize Crew (every kill: hull back, guns reloaded), Lucky Charm (survive a killing blow on 1 hull, a moment
    untouchable, once a round), Desperado (below 40% hull: much more damage, faster reload), Lightning Rod (your hits
    may call down lightning), Spectral Powder (your shot goes through islands and friends), Ram Prow (rams: triple
    damage, none to you), Powder Kegs (disabled? you blow up and take them with you), Quartermaster (crate power-ups
    last twice as long, and one to start every round).
  - **Combos**, when you hold both halves: **Firestorm** (Hot Shot + Grenado: bursts set everything ablaze), **Storm
    Surge** (Lightning Rod + Chain Shot: struck ships are chained, chained ships struck twice as often), **Devil's
    Luck** (Lucky Charm + Desperado: every kill gives a spent charm back), **Plunderer** (Prize Crew + Double-Shotted:
    after every kill your next broadside fires twice), **Juggernaut** (Ram Prow + Iron Sheathing: your rams mend your
    hull), **Kamikaze** (Ram Prow + Powder Kegs: ramming sets off a keg and you live), **Will-o'-the-Wisp** (Spectral
    Powder + Hot Shot: fire leaps from ship to ship), **Arsenal** (Quartermaster + Double-Shotted: two power-ups to
    start every round). An upgrade that would complete a combo turns up more often, and its card says so. Lose a round and the run
  is over: the results show how far you got (and the best this session), and a new run starts from round 1. Every
  round brings more pirates, bigger ships, stouter hulls and sharper gunners; bosses get more hull and attack faster.
  The host's difficulty sets where it starts.

**Small crews** (Boss, Voyage and the Odyssey with fewer than three captains, bots included): when every ship is
disabled at once, a crew can **rally** and get her going again on their own, twice a round with one captain and once
with two. The boss bar or the voyage banner shows how many are left.

Rounds last 8 minutes or until someone reaches the score limit. Then the results: the winner, the standings and
the awards (Admiral of the Fleet, Master Gunner, Flying Dutchman, Spin Doctor, Treasure Hunter, Unsinkable, and
Landlubber for the captain who found the most rocks), and 18 seconds later a fresh round starts.

**Host options** (menu, below the game settings):

- **Bots**: as many as you like (- / + steps by one up to 10, then by five). There's no ship or player limit: the
  game grows to fit, and your PC (and the host's upload) is what decides how many is too many. With fleets each
  captain brings three escorts.
- **Handling**: Arcade (over the top: 60-knot jumps off the swells), or Realistic: an age-of-sail ship. Top speeds of
  about 15 kn (cutter), 12 kn (frigate) and 10 kn (ship of the line), several seconds to gather way, a turning circle of
  60-160 m, no turning at all without way on, no jumps, and a ball hit or your own broadside barely moves the hull.
  The wind is the one thing left arcade: you can sail any course, no tacking. It works in every mode and arena and
  with skills on or off. (Mind the Maelstrom: its current runs faster than a real ship sails.)
- **Friendly fire**: on (the default), your balls hurt your own side and your own escorts too; off, they pass through
  friends. Bots hold their fire when a friend is in the way either way.
- **Ships allowed**: which of cutter / frigate / ship of the line players may pick. Bots keep to them too.
- **Camera**: Free, or Deck to make everyone play from the quarterdeck (first person).
- **Skills**: Dash (Shift) and grappling hook (E). A hooked line reels the two ships together for 7 seconds; the
  pull is shared by weight, so a cutter that hooks a ship of the line is the one that gets dragged. Once it's on, the
  line holds until its time is up (the ship holding it can let go early with E). Bots hunting a treasure carrier use both.
  The hook also bites on rock: put the reticle on a cliff or a sea stack within 150 m ("E HOOK ROCK") and the line
  hauls your own ship in towards it.
- **Arena**: Isles (the open bay inside the island ring), or Maelstrom: a whirlpool that fills the bay, 1.2 km
  across, falling away into a funnel 75 m deep. The outer half is a carousel: the water carries everyone round and
  you can fight in it or sail straight across it. Further in, the pull gets stronger the deeper you go, until it
  runs faster than you can sail: a ship of the line can't get out from inside ~100 m, a frigate ~80 m, a cutter
  ~60 m (a dash buys a little more). The eye pulls a hull under and grinds it up. Six sea stacks stand round the deep
  part, 180-300 m out: with skills on, grapple one and haul yourself out from where sailing alone can't (the swirl
  carries you past one sooner or later). Mind them, too: the current will happily smash you into one. Whoever last
  hit a ship, or had a line on it, gets the kill when the maelstrom takes it.

**Crew** (pick "You sail as: Crew" in the menu, or `--crew`): instead of a ship of your own you sign on aboard
someone else's, as a sailor on deck in first person. You join the captain who needs hands most, a human or a bot,
and if nobody is sailing a bot takes the helm for you, so it works alone too.

- **W A S D** walk the deck (Shift runs), up the quarterdeck ladders and along the gangways; the deck carries you.
- **E** at a gun works it: you aim it yourself (it's laid on whatever is under the reticle) and **left mouse**
  fires it. A gun you're working has its own reload, about seven times quicker than a broadside's. Guns nobody works are manned by the ship's own gun crew and
  fire with the captain's broadside as before.
- **F** (hold) at a shot hole patches it. On a ship with human crew every hit leaves a hole that slowly lets water
  in (0.35 hull a second each) until someone patches it, which wins back half of what that hit cost.
- Hold on: when she goes over on her beam ends, or slams down from a big jump, you can go over the side. Swim back
  (the current carries you) and press **E** at the hull to climb aboard, or wait and they'll haul you in.
- **E** at the wheel takes the helm (she turns 15% quicker with a helmsman), at the pin rail by the mainmast it
  tends the sails (6% more speed), and at the port shrouds by the mainmast it sends you up to the **lookout**, high
  on the mast (it moves with the Shipwright's mast sliders): while you're aloft every enemy is called out, so
  everyone aboard sees their names and hulls whether or not they're under the reticle. Walk or press E to climb down.
- The captain gives orders with **Z** (man the port guns), **X** (starboard guns) and **C** (patch the holes); bot
  captains give them too.
- **Esc** lets you change ship, take a ship of your own, or (as a captain) sign on as crew, without leaving the game.
  The scoreboard lists the crew under their captain with their own hits, sinkings and patches.

The ship of the line has its own model now, and the sailors on deck are animated pirates in three outfits.

**Fleets** (on top of any mode): every captain gets three AI escort ships that sail in formation and fight
alongside. Ships you lose stay lost until you are sunk yourself (you come back with a fresh fleet) or until you
sail into a friendly harbour, which also repairs your hull.

**Harbours** repair your hull while you lie in one, then not again for 2 minutes (the hull card counts it down, and
the harbour marker shows it too). Sailing out before they're done starts the 2 minutes as well. In Boss mode there are
no harbour repairs.

Aiming: the cannonballs land where the reticle points (on an enemy hull, the sea or the rocks). The guns swing
about 34 degrees either side of the beam; the dashed lines on the water show how far. When the reticle is outside
them the HUD says "GUNS CAN'T BEAR" and which way to steer.

## Building

Windows with Visual Studio 2026 (C++ workload): run `build.bat`. CMake fetches raylib 5.5 and ENet 1.3.18.
The game is `build/broadside.exe`.

`broadside.exe --simtest` runs the headless physics checks and a bot match in every game mode.
Hosting from the command line: `broadside.exe --host --mode teams --fleets --bots 3`
(also `--mode voyage`, `--mode raid` or `--mode boss`, `--difficulty easy|normal|hard|deadly`, `--classes cutter,frigate`, `--deck-only`, `--skills`, `--realistic`, `--no-friendly-fire`, `--arena maelstrom`, for a race `--arena any|strait|serpent|hairpin|zigzag`, `--time night`, `--weather storm`).
Join as crew: `broadside.exe --join <address> --crew`, or practise as crew with `--practice --bots 3 --crew`.

### Ship model

The galleon model is a third-party asset ("Age of sail Blender ship" by NelloF, Fab Standard License) and
is **not** in this repository. Without it the game falls back to a built-in procedural hull. To use your own
licensed copy, put the `.glb` and `ship.cfg` in `assets/ship/`. Release zips carry it scrambled as
`ship.pak` (`tools/pack_ship.py`) so the model is not handed out on its own.

Levels of detail come from `tools/blend_to_game_ship.py` (Blender, `-- out_dir hull_length detail name`):
`ship_hi.glb` (detail 4, ~370k triangles, your own ship close up and on deck), `ship.glb` (detail 1, ~150k) and
`ship_lo.glb` (detail 0.25, ~56k, distant ships and shadows). The extra levels are optional and share the
standard model's textures.

## Credits

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
