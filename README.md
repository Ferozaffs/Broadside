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
| Tab | Scores |
| F1 | Hide help |

Sail through the floating crates for 15-30 s power-ups: **Rapid Reload**, **Triple Shot** and **Fair Wind**.

### Game modes

The host picks the game in the menu:

- **Free-for-all**: everyone against everyone.
- **Teams**: red against blue. No friendly fire; each side has a home harbour.
- **Pirate**: one pirate (tougher, faster reload, black sails) against everyone else. Only the pirate can haul
  the floating treasure chests aboard, and has to sail them home to the pirate cove to bank them. Whoever sinks
  the pirate becomes the pirate.

**Fleets** (on top of any mode): every captain gets three AI escort ships that sail in formation and fight
alongside. Ships you lose stay lost until you are sunk yourself (you come back with a fresh fleet) or until you
sail into a friendly harbour, which also repairs your hull.

Aiming: the cannonballs land where the reticle points (on an enemy hull, the sea or the rocks). The guns only
swing about 26 degrees either side of the beam; "CARRIAGE LIMIT" means they can't reach.

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
