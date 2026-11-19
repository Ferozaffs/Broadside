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

## Danger by place (no rubber banding)
Every point of the sea has a danger level 1-10 that depends on where it is and nothing else: the Temperate Isles 1-2,
the Tropics and the Frozen North 3-6, the Fog Banks 5-8, the Ember Reach 6-10, rising toward the edge of the chart.
Pirates, monsters, the dread pirates and the lairs' bosses there are that strong (hull, gunnery, heavy shot, special
shot), and pay that well. A fresh ship is safe at home and sunk out in the wild; the upgrades, officers and fleet are
how she earns the far seas. The HUD shows the danger against a rating of her ship. (The Odyssey keeps its scaling.)

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
- **Stories**: each region's mystery in three steps and a lair, ending in its boss (built):
  - the Temperate Isles, *The Guns of the Grey Isle*: a wrecked brig's survivors, her purser freed, a powder runner
    sunk -> the Sea Fortress.
  - the Tropics, *The Queen Beneath the Reef*: a pearl diver's map, a dragged-under crew, the beast's keeper -> the
    Kraken Queen.
  - the Frozen North, *The Coil in the Ice*: stranded whalers, their log, an ice pirate's soundings -> the Sea Serpent.
  - the Ember Reach, *The Iron Beast*: a foundry's ledger, a kidnapped engineer, a coal convoy -> the Ironclad.
  - the Fog Banks, *The Ship Without a Crew*: lanterns on an empty beach, a drowned captain's chest, the last of his
    crew -> the Flying Dutchman.
  The lair turns the sea into the boss's arena for the fight, then back to the last port.

## The sea's own life (built)
- **Traffic**: traders sailing port to port (the pirates hunt them; fire on one and she fights back) and navy patrols
  (they fight pirates near them), more of both in the quieter regions.
- **At sea, every few minutes**: wreckage with sea-chests (gold, sometimes a treasure chart), a merchantman under
  attack (save her for a reward), a sea monster rising under you, or the region's **dread pirate** - a mini-boss
  ship of the line with two special shots and two mates, and a big bounty: Gentleman Corwin (chain and ricochet),
  Calypso Vane (fire and grape), Old Rime (chain and mortar), Cinder Kate (fire and mortar), the Grey Widow (homing,
  spectral).

## Fleet and crew (built: see TODO.md, phase 3)
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
