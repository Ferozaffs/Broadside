# The Campaign (design)

Solo. One seamless sea you sail for real, about 24 x 24 km, split into biome regions, with ports to put into
(port screens, not walkable towns). Money, upgrades, a fleet and named crew come from quests: treasure, pirates,
convoys, rescues, escorts, cargo, and mystery chains that lead to the big boss fights.

## The sea
- `ARENA_WORLD`: 24 km square, the world's edge a fog bank that turns you back.
- Biomes (`BiomeAt(x, z)`, noise-warped borders):
  - **Temperate Isles** (the middle): today's green islands, the home port.
  - **The Tropics** (south): white sand, palms, turquoise shallows; treasure, pirates.
  - **The Frozen North** (north): snow-capped rock, ice floes, grey sea, cold light.
  - **The Ember Reach** (east): black volcanic rock, ash, a red glow at night.
  - **The Fog Banks** (west): thick fog, dark moss, wrecks; ghost ships, the Dutchman.
- Islands in archipelago clusters with open sea between; 2-3 ports in each biome.
- The chart (Tab): the whole sea, ports, the quest markers, where you've been.
- Fast-sail (hold F when no enemy is near): the sea runs at 4x.

## Ports (sail in, press E)
- **Harbourmaster**: repairs (gold by the hull missing), the port's news.
- **Tavern**: quests (2-4 on offer, by biome and how far you've got), recruits (crew with a trait each).
- **Shipwright**: upgrades (hull, sails, guns, crew quarters), new ships (cutter -> frigate -> ship of the line ...).
- **Market**: sell cargo and plunder; buy supplies.
- **Governor**: bounties, reputation with the port's flag, letters of marque later.

## Quests
Every quest is a template with a giver, a place, a goal and a reward, rolled for the biome:
- **Treasure**: a map piece -> sail to the island marked -> drop anchor at the X (stop near it) -> chest.
- **Pirate hunt / bounty**: a named pirate, last seen near a place; sink or capture him.
- **Convoy raid**: an enemy convoy on a lane between two ports; sink or capture its cargo ships.
- **Escort**: a merchantman from here to there; pirates come for her.
- **Cargo run**: carry goods to a port before a deadline (the hold slows the ship a little).
- **Rescue**: castaways on an island (sail close, send the boat); a captive held aboard a pirate ship (cripple it,
  board it).
- **Chains**: a mystery in steps (a message in a bottle -> a wreck -> a name -> a lair), each biome's chain ending in
  its boss (the Kraken, the Leviathan, the Storm Caller, the Dutchman, the Sea Fortress / the Armada).

## Fleet and crew
- **Fleet**: up to 3 escort ships, recruited in port or captured, following you (the existing escort AI), each with
  its own hull that carries over.
- **Crew**: named officers with a trait (gunner: reload, bosun: repairs, navigator: speed, surgeon: crew losses...)
  built on the Odyssey's perks; they can be lost.
- **Upgrades** bought at the shipwright, kept between sessions.

## Saving
`campaign.sav` beside the game: the seed, gold, ship and upgrades, fleet, crew, quests, the ports seen,
where you are. Saved on putting into port and on leaving the game.

## Phases
1. **The sea**: the world and its biomes, ports and port screens (repair), the chart, fast-sail, gold, saving,
   roaming pirates to fight.
2. **Quests**: the templates above, the quest log, rewards; the first chain and boss.
3. **Fleet and crew**: recruiting, officers, upgrades, ships for sale.
4. **Biome looks**: the water, sky and fog of each region, palms / ice / ash, its own weather.
5. **Music and polish**: region music, the map's art, balancing.
