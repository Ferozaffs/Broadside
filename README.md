# Broadside

Online PvP age-of-sail ship battles with over-the-top physics: FFT ocean swell you can jump off,
360s in the air, cannonball arcs, power-up crates and an island-ringed arena.

Built with [raylib](https://www.raylib.com/) and [ENet](http://enet.bespin.org/). The ocean is based on
[GarrettGunnell/Water](https://github.com/GarrettGunnell/Water).

## Playing

Download the latest zip from [Releases](../../releases), unzip it anywhere and run `broadside.exe`
(Windows, needs an OpenGL 4.3 GPU).

- **Practice**: play against bots.
- **Host**: others join with your IP. The host must allow `broadside.exe` through Windows Firewall, and
  for play over the internet forward **UDP port 27615** on the router.
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
| Tab | Scores |
| F1 | Hide help |

Sail through the floating crates for 15-30 s power-ups: **Rapid Reload**, **Triple Shot** and **Fair Wind**.

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
