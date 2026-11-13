# Broadside

Online PvP age-of-sail ship battles with over-the-top physics: FFT ocean swell you can jump off,
360s in the air, cannonball arcs, power-up crates and an island-ringed arena.

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
| Right mouse | Zoom |
| Mouse wheel | Camera distance; roll all the way in to stand on the quarterdeck |
| V | Deck view on / off |
| M | Sound on / off |
| 1 / 2 / 3 | Next ship: cutter / frigate / ship of the line |
| Tab | Scores |
| F1 | Hide help |

Sail through the floating crates for 15-30 s power-ups: **Rapid Reload**, **Triple Shot** and **Fair Wind**.

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
off a big ship's beam and onto its ends. With Fleets, every captain's escorts are cutters.

### Game modes

The host picks the game in the menu:

- **Free-for-all**: everyone against everyone. First to 12 sinkings.
- **Teams**: red against blue. No friendly fire; each side has a home harbour. First side to 20 sinkings.
- **Pirate**: one treasure chest floats somewhere out on the bay and everyone races for it. Whoever hauls it
  aboard turns pirate (black sails, a slightly tougher hull and a faster reload, but slower, no Fair Wind and no
  repairs) and has to sail it to the pirate cove and hold the cove for 10 seconds to unload it, while everyone
  else hunts them. Sink the carrier and the treasure goes over the side for anyone to grab. First to 5 banked.

Rounds last 8 minutes or until someone reaches the score limit. Then the results: the winner, the standings and
the awards (Admiral of the Fleet, Master Gunner, Flying Dutchman, Spin Doctor, Treasure Hunter, Unsinkable, and
Landlubber for the captain who found the most rocks), and 18 seconds later a fresh round starts.

**Fleets** (on top of any mode): every captain gets three AI escort ships that sail in formation and fight
alongside. Ships you lose stay lost until you are sunk yourself (you come back with a fresh fleet) or until you
sail into a friendly harbour, which also repairs your hull.

Aiming: the cannonballs land where the reticle points (on an enemy hull, the sea or the rocks). The guns swing
about 34 degrees either side of the beam; the dashed lines on the water show how far. When the reticle is outside
them the HUD says "GUNS CAN'T BEAR" and which way to steer.

## Building

Windows with Visual Studio 2026 (C++ workload): run `build.bat`. CMake fetches raylib 5.5 and ENet 1.3.18.
The game is `build/broadside.exe`.

`broadside.exe --simtest` runs the headless physics checks and a bot match in every game mode.
Hosting from the command line: `broadside.exe --host --mode teams --fleets --bots 3`.

### Ship model

The galleon model is a third-party asset ("Age of sail Blender ship" by NelloF, Fab Standard License) and
is **not** in this repository. Without it the game falls back to a built-in procedural hull. To use your own
licensed copy, put the `.glb` and `ship.cfg` in `assets/ship/`. Release zips carry it scrambled as
`ship.pak` (`tools/pack_ship.py`) so the model is not handed out on its own.

## Credits

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
