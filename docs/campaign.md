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
- The sea's own shapes (2026-11-23, the user: "we dont have any tight passages and large coves", then "a bit too
  many coves and both feel a bit unnatural"): three **coves** in the whole sea, each in a region of its own (not the
  home waters) - an oval bay in a landmass (the shore wandering, the land behind deep on one side), two headlands
  narrowing an off-centre mouth 140-230 m wide; and a **strait** in each region - two shores of their own length,
  tapering at the ends, a channel that narrows and opens (110-260 m) and curves, 1.5-2.3 km long. Big rocks
  (r 150-280 m), spaced by both neighbours so they meet. Ports are placed first, as before.
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
- **Governor**: bounties, letters of marque later (reputation with each port: built, below).

## The story (v0.24: the user's go-ahead on 2026-11-22; every region's story leads to the end)
**Who she is.** Captain of the *Sovereign*, a 74-gun ship of the line of the Admiralty of the Isles, the pride of the
squadron of **Lord Admiral Caspian Ashgrave** - her mentor, the most decorated man in the navy. Her first mate and
oldest friend is **Mara Vell**, born on Lantern Rock, granddaughter of the Lamplighter.

**What Ashgrave wants.** The Lamplighters keep an old story: the **Leviathan** sleeps at the end of the world, and
whoever rings the **Tidebell** over it commands every sea. The bell was broken in five and sunk in the five seas long
ago, and a beast lay down to sleep over each piece. Ashgrave found the pieces. His five captains - **the Five**,
the dread pirates (Gentleman Corwin, Calypso Vane, Old Rime, Cinder Kate, the Grey Widow) - rang them, and the five
beasts woke in one year. To ring the whole bell he needs a voice the sea answers: Mara's, the Lamplighters' blood.

**The prologue** (a new campaign; playable, skippable). Night at the edge of the Ember Reach, the *Sovereign* at
full strength (every upgrade, her officers, a squadron round her). A pirate line to break: the controls on a strong
ship. Then a storm out of a clear sky, black sails all round, and Ashgrave's *Dominion* hoists one too and opens fire
on his own squadron. "Forgive me, Captain. The sea needs a master." His boats take Mara; the five pieces are rung
together, and something vast rises under the *Sovereign* and drags her down. Black.
She wakes on the beach at Port Haverly. Old Tom Fennick pulled her out of the surf, with a dispatch case that washed
up beside her: Ashgrave's orders to the Five, each told to keep a piece of the bell and its beast in its own sea until
the Admiral's yard has the bell's cradle ready. The Admiralty lists the *Sovereign* as lost to pirates, with all
hands. Old Tom lends her a cutter. That's where the campaign starts today.

**Why she goes to each sea** (not just to get stronger). In every region's story:
- **one of her officers**, scattered as prizes among the Five, is there to bring home (each joins her again, under
  their own name: the officers system's five roles, one a region);
- **a piece of the Tidebell** lies under its beast: she must have it before Ashgrave does;
- **a lead** to the *Sovereign*, to Mara, to what the Admiral is building.

| Region | Story (its steps stay the same kinds of quest) | Her officer | What she learns |
|---|---|---|---|
| Temperate Isles | *The Guns of the Grey Isle*: the *Merryweather* (a brig of her own squadron that got away) ashore; the purser taken by Corwin; Corwin's powder runner; the Sea Fortress - Ashgrave's old fort, rearmed over the first piece | Jonah Stroud, bosun (among the *Merryweather*'s survivors) | Ashgrave's flag flew over the fort; Corwin keeps a bell piece in its magazine |
| The Tropics | *The Queen Beneath the Reef*: the pearl diver's map; the *Dolphin*'s crew; Calypso, the beast's keeper, feeding it ships; the Kraken Queen | Grace Fairweather, navigator (sold to Calypso, stranded with the *Dolphin*'s crew) | Mara was seen aboard a black ship, singing, and the reef sang back |
| The Frozen North | *The Coil in the Ice*: the frozen whalers; their log; Old Rime's soundings (a road through the ice for a fleet); the Serpent | Silas Gage, master gunner (put on the ice by Old Rime) | the fleet will come south through the ice to a yard in the Ember Reach |
| The Ember Reach | *The Iron Beast*: the foundry's ledger (iron for the Admiral's yard); her surgeon held aboard Kate's ship; the coal convoy; the Ironclad | Amos Whitlock, surgeon (made to tend the foundry's slaves) | the *Sovereign* was raised from the deep and is being rebuilt in iron plate as Ashgrave's flagship |
| The Fog Banks | *The Ship Without a Crew*: lanterns on a beach; the drowned captain's chest (the Widow's bargain: the *Sovereign*'s dead for the Dutchman's crew); the last man off the ship; the Flying Dutchman | Ruth Penhallow, quartermaster (the last of the *Sovereign*'s crew off the Dutchman) | Mara is the Storm Caller now, bound by the Admiral's will |

Each story's end hands her the piece and sends her to whichever seas are left. The dread pirates' hails know who she
is ("The Admiral said you'd drowned"); passengers' tales and the taverns' rumours are about the Five, the black sails,
the bell and the Admiral; the omens darken as she gathers the pieces.

**The last story** (*The Last Voyage*, once she holds all five pieces): the Lamplighter recasts the bell, and
Ashgrave comes for it.
1. *The Black Sails*: his herald carries the Armada's orders through the Fog Banks. Sink it.
2. *The Armada*, in the Ember Reach: his flagship is the iron-plated *Sovereign*. Break the line and **take her
   back**: she's hers again (the ship of the line, her name, every upgrade she had in the prologue).
3. *The Storm Caller*, in the far Fog Banks: Mara. Ring the bell over her and the Admiral's hold breaks: she's freed,
   not sunk, and sails with her.
4. *The Leviathan*: Ashgrave, beaten, wakes it anyway. At the end of the world, the *Sovereign* and Mara: ring it
   back to sleep for good. Then the sea is hers.

**Memories** (flashbacks; the user, 2026-11-23: "flesh out the relationship with Mara and the Admiral ... optional for
those that want story", then "should be like prologue small gameplay sections. Maybe we need a narrator"). Five short
played scenes from the years before the prologue (people.h kMemories: the lines; World::StartMemory/UpdateMemory/
EndMemory: the play), one more for each region's story told, always in order whatever the region; a notice when one
returns, then in port "Memories" beside Set sail (a gold dot for one not yet played; memories.cfg). Her ship, upgrades,
officers, fleet and the hour are put by and given back after, docked where she was (Esc: "Leave the memory"; sunk in
one: nothing lost). A narrator tells it (a voice of its own, across the top); Mara and the Admiral speak as the people
at sea do; her own words are text.
- I *The Grey Isle* (Greywater, dusk; a cutter): the Merryweather's master dead, two luggers astern - make the fort's
  guns; then the Admiral's study ("the sea wants a hand on it. One hand").
- II *Lantern Rock* (night; the frigate Wren): a squall knocks her about and works her hull; Mara sings it still; into
  the lee, where the Dominion lies at anchor.
- III *The Ice* (Frostholm; the Wren): five marks of the Admiral's channel out of the ice; his letter asks for Mara;
  she refuses him ("I go where you go").
- IV *The Sovereign* (Port Haverly, morning; the Sovereign, every upgrade): clear the approaches of a pirate band with
  the Dominion alongside; he asks Mara about the bell.
- V *The Last Night* (the Reach's edge, night; the squadron in company): Mara dreams of five bells - "come and find me,
  whatever I've become" - and the black flag at dawn leads into the prologue.
Tests: BS_MEMTEST (each played by a bot from Port Haverly, and everything put back; BS_MEMONLY=k, BS_MEMLOG);
BS_MEMORY=k (or "list") in port starts one, for screenshots.

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
- **The last story**, *The Last Voyage* (once all five are told, offered in every tavern; the Lamplighter of Lantern
  Rock tells it): who woke the five beasts. A bounty on the herald of the black sails (the Fog Banks), then three
  lairs in a row: the Admiral's Armada (the Ember Reach), the Storm Caller (the far Fog Banks), and at the end of the
  world the Leviathan (the Ember Reach's very edge, danger 10). Then the sea is yours.

## Progression (built)
- She starts in a **cutter**. The frigate and the ship of the line are **built**, not bought: three parts each (a
  keel, rigging and 18-pounders; frames, a mainmast and 32-pounders) won on quests, from dread pirates and in lairs,
  and the shipwright's labour. Ships she's built stay hers.
- The **fleet** is won: prizes, quests that pay a ship, a merchantman's guard ship after a rescue. Nothing for sale.

## Port reputation (built)
Each of the twelve ports thinks something of her, in five steps: **Stranger** (0), **Known** (60), **Trusted** (200),
**Friend of the port** (500), **Hero of the port** (1000).
- **Up**: its quests done (50 + a 25th of the reward, x1.5 for a story's step, at most 150), pirates sunk in its waters
  (within 4.5 km, +5), a merchantman saved near it (+35), the region's dread pirate sunk (+60 to every port there).
- **Down**: its quests failed or given up (-30), a merchantman under attack lost near it (-15).
- **What it gives**: a ship for the fleet among its rewards once it knows her, a ship's part once it trusts her (the
  road to the frigate: earn a home port); its tavern pays 5% more a step and its prices (repairs, upgrades, building,
  officers) are 5% less a step; a friend of the port gets a fourth quest, and when she's fighting in its waters its
  warships come out to help (a hero's: two), going home when it's over. Its keepers greet her warmly from Trusted on
  (people.h kWarm, two days in three, by name).
- It never changes how hard the sea is (difficulty by place only). Saved (`rep`), in the snapshot (protocol 30).
  Test: BS_REPTEST; BS_CAMPREP=n sets every port's for screenshots.

## The sea's own life (built)
- **Traffic**: traders sailing port to port (the pirates hunt them; fire on one and she fights back) and navy patrols
  (they fight pirates near them), more of both in the quieter regions.
- **At sea, every few minutes**: wreckage with sea-chests (gold, sometimes a treasure chart), a merchantman under
  attack (save her for a reward), a sea monster rising under you, or the region's **dread pirate** - a mini-boss
  ship of the line with two special shots and two mates, and a big bounty: Gentleman Corwin (chain and ricochet),
  Calypso Vane (fire and grape), Old Rime (chain and mortar), Cinder Kate (fire and mortar), the Grey Widow (homing,
  spectral).
- **And more (v0.23, after three bot careers showed the sea one long running fight: pirates within 800 m 63% of the
  time, 4 kinds of event)**: half as many pirate bands; a **rival captain** in a ship like hers with tricks of her own
  (pays 4x her bounty and rep); **smugglers** running down the lanes (sink one for her contraband); **survivors
  adrift** in a crippled boat (alongside, slowly, before they're lost); in the Fog Banks, mostly at night, **ghost
  ships** rising close by (spectral shot; a chance of a ship's part). Every keeper and sea voice has six lines, every
  dread pirate three hails; the name pools are two or three times bigger. Test: BS_SEAEVENTTEST.
- **No name again soon**: the tavern's board keeps clear of the pirates, captives, merchantmen and cargoes on the
  work she's taken lately (the last pool-less-six of each kind: 26 pirates, 18 captives, 26 merchantmen, 18 cargoes),
  and never shows one twice; what she's had is in the save (`names`). The people at sea have several faces each,
  the same ship always the same captain. Test: BS_NAMETEST (150 days of every named offer taken: none again).

## Each region's own work and hazards (v0.23, built)
Offered only in its own ports (most days), and a hazard of its own (test: BS_REGIONTEST):
- **The Temperate Isles - smugglers**: she runs, she doesn't fight; cripple her, come alongside, seize the cargo.
  **The law**: fire on a merchantman and you're wanted for five minutes - the navy hunts you (and the port thinks less of you).
- **The Tropics - reef salvage**: lie still over a wreck on a reef while the divers work; a shark comes for the blood
  in the water. **The hurricane**: a storm wandering the region on the clock - lightning on whatever's in it.
- **The Frozen North - a whaler in the ice**: push through the floes, lie alongside, take her crew off, bring them in.
  **Ice on the rigging**: at sea in the north it builds (ten minutes to the worst, -25% speed); port clears it.
- **The Ember Reach - evacuate the island**: the villagers off the beach before the mountain blows (against the
  clock), under the bombs. **Volcanic bombs**: round its islands, marked red on the water a moment before they land.
- **The Fog Banks - the lost lighthouse keeper**: only a search area; his light shows within 350 m. **Out of the
  fog**: pirates find you close, out of nowhere.
- The taverns also tell where the next untold story is (a story is offered only in its own region's ports).

## Fleet and crew (built: see TODO.md, phase 3)
- **Fleet**: up to 3 escort ships, won (prizes, quests, rescues), following you (the existing escort AI), each with
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
4. **Biome looks**: the water, sky and fog of each region, palms / ice / ash, its own weather. Done: solid ice floes
   in the north, wrecks and ghost lights in the Fog Banks, each region's squalls (blizzards, thunder squalls, ash
   storms), and the day turning with the campaign's clock (dawn, day, dusk, night).
5. **Music and polish**: region music, the map's art, balancing.
