// The campaign's people: who the captain meets in port and at sea, and what they say. Their portraits are
// data/portraits/<id>.png (drawn when there; the talk works without them).
#pragma once
#include "sim.h"
#include "audio.h"
#include <cstring>

namespace people {

struct Person {
    const char* id;
    const char* name;
    const char* title;
    const char* hello[3];  // greetings (one a day)
    const char* special;   // the harbour: her hull's in a bad way; the tavern: nothing today; the shipwright: no money
};

enum Shop { HARBOUR = 0, TAVERN = 1, WRIGHT = 2, SHOPS };

// each port's harbourmaster, tavern keeper and shipwright (campaign::kPortNames order)
inline const Person kPort[campaign::kPorts][SHOPS] = {
    {  // Port Haverly
     {"haverly_harbour", "Agatha Pruitt", "Harbourmaster of Port Haverly",
      {"Name, ship, and whether you mean to pay your dues this time. Go on, I've a ledger to fill.",
       "Port Haverly welcomes careful captains. The other sort we bury at the parish's expense.",
       "The tide's with you, the wind's with you, and the harbour fees are with me. Lovely day for it."},
      "Good heavens, look at her. Half your planking's in the sea. I'll send for the carpenters. And the bill."},
     {"haverly_tavern", "Barnaby Coyle", "Keeper of the Anchor",
      {"Come in, come in! Mind the dog. There's work on the board and ale in the barrel, and I'm not fussy which you take first.",
       "Back again? The Anchor's never short of three things: ale, rumours, and fools willing to chase them.",
       "Sit, sit. A fellow came in last night talking of guns on an island no chart shows. Paid in Spanish silver, too."},
      "Board's bare today, Captain. Even the rumours have gone home. Try tomorrow."},
     {"haverly_wright", "Old Tom Fennick", "Shipwright of Port Haverly",
      {"Hmph. Built her sister, forty year back. Better timber then.",
       "Iron on her sides, copper on her bottom, and a bit of sense in her captain. I can sell you two of the three.",
       "Bring me gold and I'll make her fly. Bring me excuses and I'll make tea."},
      "Light purse, heavy hopes. Come back when the first matches the second."}},
    {  // Saltmarsh
     {"saltmarsh_harbour", "Silas Reed", "Harbour Clerk of Saltmarsh",
      {"Oh! Captain. Yes. Welcome to Saltmarsh. Please don't fire anything. The last captain fired something.",
       "Your berth is number four. Or three. One of the dry ones. I'll, er, check the chart.",
       "There are pirates about, they say. I say nothing. Saying things gets you noticed."},
      "Is she meant to lean like that? I'm no sailor, but I don't think she's meant to lean like that."},
     {"saltmarsh_tavern", "Molly Brine", "The Drowned Duck",
      {"Welcome to the Duck, love! Wipe your boots, grab a stool, and tell me something I haven't heard.",
       "You look like trouble. Good. Trouble pays its tab.",
       "The fishermen say there's lights out on the marsh again. I say there's gin in the fishermen."},
      "No work today, sweetheart, unless you fancy scrubbing tankards. No? Didn't think so."},
     {"saltmarsh_wright", "Jonah Pike", "Shipwright of Saltmarsh",
      {"Fine lines on her. Shame about the paint. And the hull. And the rigging.",
       "Give me a week and she'll outrun anything in the Isles. Give me gold and I'll do it in a day.",
       "My father built ships, and his father. I'm the first one who gets paid on time."},
      "Can't work for promises, Captain. Timber costs coin."}},
    {  // Greywater
     {"greywater_harbour", "Captain Elias Thorne", "Harbourmaster of Greywater",
      {"Twenty-two years in the navy, and now I count barrels. State your business.",
       "Greywater keeps a clean harbour. Keep your guns run in and your crew sober.",
       "I lost this eye off the Grey Isle. Some say there are still guns on it. Some say a great deal."},
      "She's been knocked about, Captain. Get her patched before you take her out again. That's not advice."},
     {"greywater_tavern", "The Widow Hask", "The Grey Gull",
      {"Sit down. Everyone who comes through that door wants something. Let's find out what you want.",
       "My husband sailed out of here twenty years ago. I keep the lamp lit. Habit, mostly.",
       "There's coin to be made, if you're not particular how. Most captains aren't."},
      "Nothing for you today. Nothing honest, anyway."},
     {"greywater_wright", "Hector Dunmore", "Shipwright of Greywater",
      {"Och, what've ye done to her? Never mind. Never tell a shipwright. It only makes us weep.",
       "Oak from the north, pitch from the south, and my hands in between. Best hulls on the Isles.",
       "Ye want her faster or ye want her stronger? Cannae have both. Well, ye can, for a price."},
      "Gold first, Captain. My mallet doesnae swing for nothing."}},
    {  // Coral Bay
     {"coral_harbour", "Marisol Vega", "Harbourmistress of Coral Bay",
      {"Bienvenido to Coral Bay, Captain. Warm water, fair winds, and a harbour fee that's almost friendly.",
       "Watch the reef going out. It's eaten bigger ships than yours, and it's never full.",
       "The fishermen say something sings under the reef at night. Pay them no mind. Mostly."},
      "Madre de Dios, your poor ship! Come, we'll have the carpenters on her at once."},
     {"coral_tavern", "Big Ezekiel", "The Sunken Bell",
      {"Ha! Another captain with salt in the beard. Sit down, the rum's cold and the stories are hot.",
       "Work? Plenty of work. Pirates to sink, cargo to run, and fools to rescue from their own bad luck.",
       "I had a ship once. Then I had a reef. Now I have a tavern. The tavern doesn't sink."},
      "Board's empty, my friend. Drink, rest, come back when the sun is up again."},
     {"coral_wright", "Tomas Arroyo", "Shipwright of Coral Bay",
      {"She's a good ship. Not as good as she'll be when I'm done, but good.",
       "The worm down here eats a bare hull like bread. Copper, Captain. Trust me.",
       "I can make her quick enough to outrun a squall. Gold first. I'm quick at that too."},
      "Pockets empty? Mine too. That's why I charge."}},
    {  // Isla Dorada
     {"dorada_harbour", "Don Alvaro de Ruiz", "Harbour Governor of Isla Dorada",
      {"You may approach. Do not touch the railings. They were imported.",
       "Isla Dorada is a civilised port, Captain. Here we rob you with paperwork, not pistols.",
       "The governor sends his regards. The governor is me. I send my regards."},
      "Your vessel is dripping on my quay. Have it repaired, or have it removed."},
     {"dorada_tavern", "Lucia Mendez", "La Perla",
      {"Ah, a new face. In La Perla we have music, wine, and work for anyone brave enough to ask.",
       "You have the look of someone who's been shot at. It suits you.",
       "Every captain in the Tropics drinks here sooner or later. Even the ones who hang."},
      "No work tonight, Capitan. Only music. Stay a while."},
     {"dorada_wright", "Mama Odile", "Shipwright of Isla Dorada",
      {"Child, what have you done to this poor ship? Come, show Mama where it hurts.",
       "Forty years I've built hulls on this island. Not one sank that I didn't mean to.",
       "Good wood remembers, Captain. Treat her kind and she'll bring you home."},
      "Your purse is thinner than your hull, baby. Come back richer."}},
    {  // Palmhaven
     {"palmhaven_harbour", "Jack Sorrel", "Harbourmaster of Palmhaven",
      {"Welcome to Palmhaven, mate. No questions asked, no answers given. Fee's two silver, or one if you're pretty.",
       "I used to sail under the black flag. Now I just charge it mooring.",
       "Pieces of eight! That's the bird. Ignore him, he owes me money."},
      "Blimey, she's been in the wars. Get her patched before she goes down at my pier. Paperwork's a nightmare."},
     {"palmhaven_tavern", "Rosa Calder", "The Rum Barrel",
      {"Two rules in the Rum Barrel. Pay your tab, and don't touch my knives.",
       "You want work? I've got work. It's dangerous, it's dirty, and it pays. Pick two.",
       "The last captain who cheated me is still out there. In pieces. Various places."},
      "Nothing today. Drink or get out."},
     {"palmhaven_wright", "Kofi Mensah", "Shipwright of Palmhaven",
      {"Her bones are strong. I can feel it. The rest, we can fix.",
       "In Palmhaven nobody asks where a ship came from. I only ask what she needs.",
       "I built the fastest sloop in these waters. A pirate stole her. I took it as a compliment."},
      "Without gold, Captain, I am only a man with an axe."}},
    {  // Frostholm
     {"frostholm_harbour", "Ingrid Halvorsen", "Harbourmistress of Frostholm",
      {"Frostholm. Tie up fast, the ice moves at night. Fees are posted. They are not negotiable.",
       "You came through the floes? Then you are either skilled or lucky. Here we need both.",
       "Winter is coming. It is always coming, here."},
      "Ice did this? Then you sailed too fast. We repair. You learn."},
     {"frostholm_tavern", "Bjorn Ulfsson", "The Frozen Kettle",
      {"HA! Southerner! Come, drink, before the cold drinks you!",
       "Work? There is always work in the north. Most of it tries to kill you. Good work!",
       "My grandfather fought a sea serpent with his bare hands. My grandmother says it was a big eel. Grandmother lies."},
      "No more jobs today, friend. Only mead. Mead is also a job."},
     {"frostholm_wright", "Sigrid Axelsdottir", "Shipwright of Frostholm",
      {"Southern ship. Thin planks. Pretty. Useless in the ice.",
       "I can give her an iron skin. The floes will bounce off her. Mostly.",
       "My axe built half the ships in this harbour. The other half it took apart."},
      "No gold, no axe."}},
    {  // Iceward
     {"iceward_harbour", "Pastor Olav Lind", "Keeper of Iceward Harbour",
      {"God keep you, Captain. Iceward is a quiet port. We pray it stays that way.",
       "The ice took three ships this winter. I said words over each. I would rather not say them over yours.",
       "Something moves under the ice, out past the point. The children hear it singing. I tell them it is the wind."},
      "Your ship is wounded, my son. Let our people mend her, before the sea takes her."},
     {"iceward_tavern", "Hilde", "The Whalebone",
      {"Come in out of the cold, dear. Stew's hot, the fire's lit, and there's work if you want it.",
       "Eat, eat. You southern captains are all bones.",
       "My son sails with the whalers. If you see a red sail out there, tell him to write to his mother."},
      "No work today, dear. Have some stew instead."},
     {"iceward_wright", "Gunnar Holt", "Shipwright of Iceward",
      {"Hm. Bring her in.",
       "Whalebone pegs. Never rot. Never fail.",
       "Ice is honest. It shows you where it will hit. Men don't."},
      "Gold. Then talk."}},
    {  // Cinderport
     {"cinderport_harbour", "Magistrate Corvin Ash", "Magistrate of Cinderport",
      {"Cinderport. Taxed, regulated, and on fire only occasionally. Your papers.",
       "The mountain smokes, the foundries smoke, and the Magistrate does not tolerate disorder.",
       "Something iron came out of the smoke last month and sank two of the governor's ships. We do not speak of it."},
      "Your vessel is a disgrace to the harbour. The repair yard will see you. Promptly."},
     {"cinderport_tavern", "Scarlet Juno", "The Smoking Kettle",
      {"Welcome to the Kettle! Everything here's hot: the drink, the food, and the company.",
       "Got this scar from a powder keg. Got the keg's money, too.",
       "Work's dangerous out here. Fire, pirates, the mountain. Pays double, though."},
      "Nothing on the board today, sugar. Even the pirates are taking a day off."},
     {"cinderport_wright", "Forgemaster Dace", "Shipwright and Smith of Cinderport",
      {"Wood burns. Iron doesn't. Remember that out here.",
       "I forge the plates and hang them myself. No ship of mine has burned yet.",
       "Bring me gold and I'll make her a fortress. Bring me nothing and I'll make you leave."},
      "Iron costs. Come back with more."}},
    {  // Ashfall
     {"ashfall_harbour", "Ma Grubb", "Harbourmistress of Ashfall",
      {"Ashfall. Mind the ash, mind your purse, and mind me.",
       "Seen a lot of captains come through. Seen fewer go out.",
       "Fee's what I say it is. Today I say it's fair. Tomorrow, who knows."},
      "Heh. Somebody's been in a scrap. Patch her up, dearie, it's cheaper than a funeral."},
     {"ashfall_tavern", "Ignatius Pell", "The Last Ember",
      {"Welcome to the Last Ember. The name is a warning, not a promise.",
       "Sit anywhere. The ash gets in everything, so drink fast.",
       "There's always someone who wants something burned, or someone who wants something saved from burning."},
      "Nothing tonight. Even ash needs to settle."},
     {"ashfall_wright", "Sparks", "Shipwright of Ashfall",
      {"Ooh, what a beauty! Can I take her apart? I'll put her back. Mostly in the same order.",
       "I've got ideas, Captain. Big ideas. Hot shot, iron plate, a little extra powder in the right places.",
       "Don't mind the scorch marks. That was a test. It went great."},
      "I'd do it for free if I could. I can't. Gold, please!"}},
    {  // Mournwick
     {"mournwick_harbour", "Absalom Gray", "Harbourmaster of Mournwick",
      {"Welcome to Mournwick, Captain. We have rooms, a chapel, and a very reasonable undertaker.",
       "The fog came in three hundred years ago. It has not left. Neither, I find, do most of our visitors.",
       "If you see a ship with no crew out there, do not hail her. Simply note the time, and sail on."},
      "Dear me. She looks quite departed. Our yard will see to her before she is formally mourned."},
     {"mournwick_tavern", "Mother Whisper", "The Hanged Lantern",
      {"Come in, come in. The lantern knew you'd come. It always knows.",
       "Work, is it? There's always work in the fog. The fog makes work.",
       "I saw your face in the tea leaves last week. You were laughing. Or screaming. Hard to tell with tea."},
      "Nothing today, dearie. The dead are quiet today."},
     {"mournwick_wright", "Ned Coffin", "Shipwright and Coffin-Maker",
      {"Boats or coffins, it's all the same joinery. One floats a bit better.",
       "I measured you already. Habit. Don't worry about it.",
       "I'll do good work on her, Captain. I'd hate to see you back here in my other line of trade."},
      "No coin? Then no boat. And no coffin either, I'm afraid."}},
    {  // Lantern Rock
     {"lantern_harbour", "The Lamplighter", "Keeper of Lantern Rock",
      {"Lantern Rock. The light has burned three hundred years. Don't ask me how.",
       "Keep the lamp on your bow coming in. The other lights out there aren't mine.",
       "You'll see them on the beaches. The old wrecks. Something still walks the shore round them at night."},
      "The fog's been at her, I see. Leave her with us a while."},
     {"lantern_tavern", "Evangeline Marsh", "The Pale Lady",
      {"Welcome to the Pale Lady, Captain. Wine? It's very old. Older than me. Probably.",
       "Everyone here is waiting for someone who isn't coming back. You'll fit right in.",
       "There's work, if you're not afraid of the fog. Or of what sails in it."},
      "Nothing for you tonight, Captain. Stay and drink with me. I hate drinking alone with them."},
     {"lantern_wright", "Brother Caspar", "Shipwright of Lantern Rock",
      {"Peace be with you, Captain. And pitch. Peace and pitch, that's my trade.",
       "Each plank I fit, I bless. The ones on the ghost ships out there were never blessed.",
       "Gold for the abbey, and a sound hull for you. The Lord likes a fair trade."},
      "I'd give my labour for free, but the timber merchants are not so holy."}},
};

// each tavern keeper, once the last story is on offer: the black sails, as the port has heard of them
inline const char* const kBlackSails[campaign::kPorts] = {
    "Black sails, Captain. Fellow swore he saw a whole fleet of them off the Fog Banks. Paid for his drinks in a hurry and left.",
    "Have you heard? Black sails, dozens of them. Even the fishermen have stopped making jokes about it.",
    "My husband's ship had black sails at the end. I'm told there are a great many more now. Sit down. Listen.",
    "The sea's gone quiet, my friend. Too quiet. Every captain who comes in talks about black sails.",
    "They say an armada is gathering. Black sails, every pirate who ever lived. Even the music is sad tonight.",
    "Black sails? I know a few of the old crews who flew them. They're all keeping very quiet. That frightens me more.",
    "Five beasts, woken in one year. And now black sails. Drink first, Captain. Then listen.",
    "Black sails in the fog, they say! The whalers won't go out. Have some mead, friend: you'll need it!",
    "The whalers saw them: black sails, going east, a long line of them. Eat something, dear. You'll want your strength.",
    "Black sails off the Reach. The foundries have been working night and day, and nobody will say for whom.",
    "Something called the beasts up, Captain. Something's calling the black sails now. The fog's been listening.",
    "The Lamplighter came down from the rock last night. He never comes down. He asked for you, Captain.",
};

// at sea
struct SeaVoice { const char* id; const char* name; const char* title; const char* lines[3]; };
enum SeaTalk { ST_HAIL = 0, ST_NAVY, ST_DISTRESS, ST_SAVED, ST_CASTAWAY, ST_CAPTIVE_F, ST_CAPTIVE_M, ST_STRUCK, ST_COUNT };
inline const SeaVoice kSea[ST_COUNT] = {
    {"sea_merchant", "Captain Hollis Grant", "Master of a merchantman",
     {"Fair winds, Captain! Nothing in our hold worth the trouble, I promise you.",
      "Good to see an honest sail. Keep those pirates off our stern, would you?",
      "Bound for port with a full hold. Sail with us a while, if you like the company."}},
    {"sea_navy", "Commodore Ada Sterling", "The Navy's patrol",
     {"The Navy patrols these waters, Captain. See that your business stays lawful.",
      "Any sign of pirates, signal us. We hang them, you collect the bounty. Everyone's happy.",
      "Carry on, Captain. Smartly, now."}},
    {"sea_merchant", "Captain Hollis Grant", "Master of a merchantman, under attack",
     {"Pirates! For the love of God, Captain, they're on us! Help!",
      "Help! They're coming alongside! Any ship that hears us, help!",
      "We can't outrun them! Captain, if you've guns, use them!"}},
    {"sea_merchant", "Captain Hollis Grant", "Master of a merchantman",
     {"Bless you, Captain! Take this, and our thanks. We'll drink your health in every port.",
      "We'd be at the bottom without you. Here, a little something from the hold.",
      "I've a wife in port who'll light a candle for you. Thank you, Captain."}},
    {"sea_castaway", "A Castaway", "Taken off the beach",
     {"A ship! A real ship! I thought I'd die talking to the crabs!",
      "Water. Have you water? And bread? And anything that isn't coconut?",
      "Two years on that rock, Captain. Two years. Take me anywhere with a bed."}},
    {"sea_captive", "Lady Arabella Wren", "Freed from the pirates",
     {"About time, Captain. Now get me home, and keep that cutthroat in irons.",
      "I knew someone would come. I didn't know it would take this long.",
      "They said nobody would come for me. I'd very much like to see their faces."}},
    {"sea_captive_m", "The Prisoner", "Freed from the pirates",
     {"Thank God. I thought they'd hang me from the yard. Get me home, Captain.",
      "You came for me? I owe you my life, Captain. Truly.",
      "They meant to sell me. I'd have fetched a poor price, too. Thank you."}},
    {"sea_pirate", "The Pirate Captain", "Struck her colours",
     {"Quarter! We strike! Take the ship, take the gold, just leave us breathing!",
      "Enough! The ship's yours. Do what you like with her, but let us live.",
      "Hold your fire, damn you! We strike! We strike!"}},
};

// each region's dread pirate, when she finds the captain
struct DreadVoice { const char* id; const char* line; };
inline const DreadVoice kDread[campaign::BIOMES] = {
    {"dread_corwin", "Ah, a guest. How tiresome. I shall have to ruin a perfectly good coat with you."},
    {"dread_calypso", "These are my waters, sweetness. Everything in them belongs to me. Soon that includes you."},
    {"dread_rime", "The cold takes everything in the end. Today it takes you."},
    {"dread_kate", "Smell that? That's your ship burning. Not yet. Soon."},
    {"dread_widow", "I buried every captain who ever sailed against me. I have room for one more."},
};

// Their voices: no words, a hum or an ah or a laugh as each line comes up (sfx::Utter). Pitch (Hz), formant scale,
// breath, rasp, and their manner: 0 plain, 1 cheerful (laughs), 2 gruff (hmphs), 3 eerie (long low hums)
struct Voice { const char* id; float pitch, formant, breath, rasp; int manner; };
inline const Voice kVoices[] = {
    {"haverly_harbour", 190, 1.12f, 0.15f, 0.10f, 2}, {"haverly_tavern", 95, 0.92f, 0.10f, 0.20f, 1}, {"haverly_wright", 120, 0.98f, 0.25f, 0.35f, 2},
    {"saltmarsh_harbour", 150, 1.05f, 0.20f, 0.00f, 0}, {"saltmarsh_tavern", 230, 1.18f, 0.12f, 0.00f, 1}, {"saltmarsh_wright", 115, 1.00f, 0.08f, 0.05f, 1},
    {"greywater_harbour", 100, 0.95f, 0.10f, 0.25f, 2}, {"greywater_tavern", 175, 1.10f, 0.30f, 0.10f, 3}, {"greywater_wright", 105, 0.93f, 0.10f, 0.30f, 2},
    {"coral_harbour", 210, 1.15f, 0.12f, 0.00f, 1}, {"coral_tavern", 85, 0.90f, 0.08f, 0.15f, 1}, {"coral_wright", 130, 1.02f, 0.10f, 0.05f, 1},
    {"dorada_harbour", 125, 1.00f, 0.15f, 0.05f, 2}, {"dorada_tavern", 220, 1.17f, 0.28f, 0.00f, 0}, {"dorada_wright", 185, 1.10f, 0.20f, 0.25f, 1},
    {"palmhaven_harbour", 115, 0.97f, 0.20f, 0.40f, 1}, {"palmhaven_tavern", 195, 1.12f, 0.15f, 0.10f, 2}, {"palmhaven_wright", 95, 0.93f, 0.08f, 0.05f, 0},
    {"frostholm_harbour", 200, 1.12f, 0.12f, 0.00f, 0}, {"frostholm_tavern", 80, 0.88f, 0.10f, 0.35f, 1}, {"frostholm_wright", 205, 1.13f, 0.10f, 0.05f, 2},
    {"iceward_harbour", 110, 0.96f, 0.30f, 0.30f, 0}, {"iceward_tavern", 205, 1.12f, 0.12f, 0.10f, 1}, {"iceward_wright", 90, 0.92f, 0.15f, 0.40f, 2},
    {"cinderport_harbour", 115, 0.98f, 0.20f, 0.15f, 2}, {"cinderport_tavern", 210, 1.15f, 0.18f, 0.08f, 1}, {"cinderport_wright", 85, 0.90f, 0.10f, 0.30f, 2},
    {"ashfall_harbour", 170, 1.06f, 0.20f, 0.45f, 2}, {"ashfall_tavern", 120, 1.00f, 0.30f, 0.05f, 3}, {"ashfall_wright", 250, 1.22f, 0.08f, 0.00f, 1},
    {"mournwick_harbour", 100, 0.95f, 0.25f, 0.05f, 3}, {"mournwick_tavern", 175, 1.08f, 0.50f, 0.30f, 3}, {"mournwick_wright", 105, 0.96f, 0.20f, 0.05f, 3},
    {"lantern_harbour", 95, 0.94f, 0.35f, 0.20f, 3}, {"lantern_tavern", 200, 1.14f, 0.35f, 0.00f, 3}, {"lantern_wright", 110, 0.96f, 0.12f, 0.05f, 1},
    {"sea_merchant", 120, 0.98f, 0.12f, 0.10f, 0}, {"sea_navy", 190, 1.10f, 0.08f, 0.00f, 0}, {"sea_castaway", 120, 0.98f, 0.35f, 0.35f, 0},
    {"sea_captive", 225, 1.17f, 0.15f, 0.00f, 0}, {"sea_captive_m", 135, 1.03f, 0.15f, 0.05f, 0}, {"sea_pirate", 105, 0.95f, 0.15f, 0.35f, 2},
    {"dread_corwin", 120, 1.00f, 0.15f, 0.00f, 1}, {"dread_calypso", 195, 1.13f, 0.20f, 0.05f, 1}, {"dread_rime", 80, 0.90f, 0.40f, 0.45f, 3},
    {"dread_kate", 215, 1.15f, 0.12f, 0.20f, 1}, {"dread_widow", 185, 1.10f, 0.55f, 0.05f, 3},
    {"fleet_1", 220, 1.17f, 0.10f, 0.00f, 1}, {"fleet_2", 105, 0.95f, 0.20f, 0.40f, 2}, {"fleet_3", 100, 0.94f, 0.08f, 0.05f, 0},
    {"fleet_4", 200, 1.13f, 0.15f, 0.00f, 0}, {"fleet_5", 90, 0.92f, 0.10f, 0.30f, 1}, {"fleet_6", 125, 1.00f, 0.20f, 0.20f, 0},
    {"pirate_1", 95, 0.93f, 0.15f, 0.40f, 2}, {"pirate_2", 215, 1.15f, 0.20f, 0.05f, 1}, {"pirate_3", 80, 0.88f, 0.10f, 0.45f, 2},
    {"pirate_4", 130, 1.00f, 0.40f, 0.10f, 3}, {"pirate_5", 140, 1.03f, 0.10f, 0.00f, 1}, {"pirate_6", 100, 0.95f, 0.30f, 0.20f, 3},
};

// the sound they make for a line: a question gets an eh?, a laugh a heh-heh, an exclamation an oh! (or a laugh, from
// the cheerful); otherwise their manner: a hmph from the gruff, a long hum from the eerie, an ah or a mm-hm
inline void Voiced(const char* id, const char* line, uint32_t salt) {
    const Voice* v = nullptr;
    for (const Voice& x : kVoices)
        if (!strcmp(x.id, id)) v = &x;
    if (!v || !line) return;
    const uint32_t h = salt * 2654435761u + (uint32_t)strlen(line) * 40503u;
    const bool ask = strchr(line, '?') != nullptr, bang = strchr(line, '!') != nullptr;
    const bool laugh = !strncmp(line, "Ha", 2) || !strncmp(line, "HA", 2) || strstr(line, "Heh") || strstr(line, "Blimey");
    int k;
    if (laugh) k = sfx::UT_HEH;
    else if (v->manner == 3) k = (h >> 3) % 3 ? sfx::UT_HUM : sfx::UT_AH;
    else if (ask) k = sfx::UT_EH;
    else if (bang) k = v->manner == 1 && (h >> 5) % 2 ? sfx::UT_HEH : sfx::UT_OH;
    else if (v->manner == 2) k = (h >> 4) % 2 ? sfx::UT_HMPH : sfx::UT_HMM;
    else if (v->manner == 1) k = (h >> 4) % 3 == 0 ? sfx::UT_HEH : sfx::UT_AH;
    else k = (h >> 4) % 2 ? sfx::UT_AH : sfx::UT_HMM;
    sfx::VoiceParams p;
    p.pitch = v->pitch;
    p.formant = v->formant;
    p.breath = v->breath;
    p.rasp = v->rasp;
    sfx::Utter(p, k, 0.75f, h);
}

// What a port's keepers say once it thinks well of her (Trusted, a friend of the port, its hero: campaign::RepTier
// 2-4), two days in three; "%s" is her name
inline const char* const kWarm[SHOPS][3][3] = {
    {  // the harbour
     {"Your usual berth's free, Captain. I kept it free, in fact. Had words with a Dutchman over it.",
      "Ah, %s. Sign here, and here. I've waived the lighthouse fee. Don't tell anyone.",
      "Good to see you back in one piece, Captain. More or less one piece."},
     {"%s! The harbour's yours. Well, not legally. But in spirit.",
      "There she is. The town's been talking about you, Captain. Good things, mostly.",
      "Berth's ready, carpenters are waiting, and the fees are... let's say a friendly rate."},
     {"Make way for %s! Ha, I've always wanted to shout that.",
      "They've named a pier after you, Captain. A small one. It still counts.",
      "Half the harbour turned out when your sails came in. Wave, at least. They brought flags."},
    },
    {  // the tavern
     {"%s! Your table's free. I chased a fellow off it, but it's free.",
      "There's work for you, Captain. Good work. I saved the best of it for you.",
      "The regulars want to buy you a drink. I said I'd ask. They're paying."},
     {"%s! First round's on the house. The second's on them over there.",
      "Sit, sit! Half the board's got your name on it already.",
      "They sing a song about you now. It rhymes 'cannon' with 'Shannon'. Nobody knows who Shannon is."},
     {"Everybody up! It's %s! ...Fine, they're too drunk to stand. In spirit, then.",
      "Your money's no good here, Captain. Well. It's good. I just feel bad taking it.",
      "I've hung your portrait over the bar. It's not a good likeness. The nose is all wrong."},
    },
    {  // the shipwright
     {"Bring her in, Captain. I'll do right by her, and by you.",
      "I know your ship's every creak by now, Captain. Friend's price, today.",
      "Ah, %s. I'd know that hull anywhere. Mostly by the holes."},
     {"For you, Captain? The good timber. I keep it under the floor.",
      "%s! I've been saving some ideas for her. And a discount.",
      "She's a fine ship, Captain. You're a finer customer. Pays on time, mostly."},
     {"%s! The yard stops for you. Lads, down tools! ...Not literally. Pick them back up.",
      "Anything she needs, Captain. I'd build you a ship from scratch if you asked. And paid. A little.",
      "There's a model of your ship in my window now. Children press their noses on the glass."},
    },
};

// Barks: a word now and then from her fleet's captains and from the pirates come to fight her (a small portrait, one line)
constexpr int kFleetFaces = 6, kPirateFaces = 6;
inline const char* FleetFace(int seed) { static const char* const k[kFleetFaces] = {"fleet_1", "fleet_2", "fleet_3", "fleet_4", "fleet_5", "fleet_6"}; return k[(unsigned)seed % kFleetFaces]; }
inline const char* PirateFace(int seed) { static const char* const k[kPirateFaces] = {"pirate_1", "pirate_2", "pirate_3", "pirate_4", "pirate_5", "pirate_6"}; return k[(unsigned)seed % kPirateFaces]; }
enum FleetBark { FB_IDLE = 0, FB_ENEMY, FB_HURT, FB_KILL, FB_JOINED, FB_COUNT };
inline const char* const kFleetLines[FB_COUNT][6] = {
    {"Fair wind today, Captain. Long may it last.", "The men are in good spirits. Mostly about the rum.", "Keeping station off your quarter, Captain.",
     "Quiet sea. I don't trust a quiet sea.", "Whenever you want a fight, we're with you.", "Lookout says nothing on the horizon. Nothing yet."},
    {"Sail ho! Pirates, Captain!", "Enemy in sight! Guns ready!", "Black flag on the horizon, Captain!",
     "Here they come! Stand by to fire!", "Pirates closing! We're with you!", "Beat to quarters! Enemy off the bow!"},
    {"We're taking water, Captain!", "She's hurt bad! Can't take much more!", "Holed below the waterline!",
     "Our rigging's in tatters!", "We're in trouble here, Captain!", "Another hit like that and we're done!"},
    {"Got her! She's going down!", "Ha! That's one less pirate!", "A fine shot, Captain!",
     "She's sinking! Huzzah!", "Look at her burn! Well done!", "That'll teach them to fly black!"},
    {"Reporting for duty, Captain. Where you sail, we sail.", "Glad to be sailing with you, Captain.", "At your service. Just point us at the enemy.",
     "We'll keep up, Captain, don't you worry.", "Signal when you need us. We'll be there.", "An honour to sail in your fleet, Captain."},
};
enum PirateBark { PB_APPROACH = 0, PB_TAUNT, PB_SINKING, PB_COUNT };
inline const char* const kPirateLines[PB_COUNT][6] = {
    {"Strike your colours and we'll let you live! Probably!", "Fresh meat on the water, lads!", "That's a pretty ship. She'll be mine by sundown.",
     "Run if you like. We'll catch you.", "Your gold or your life! Both, actually!", "Well, well. Somebody's lost."},
    {"Is that all you've got?", "My grandmother shoots better than you!", "Ha! You missed!",
     "Keep firing, I need the practice!", "Your hull's leaking, Captain!", "Board her, lads! Board her!"},
    {"Curse you! Curse you to the depths!", "Abandon ship! Abandon ship!", "This isn't over! I'll be back!",
     "Davy Jones, here I come...", "My ship! My beautiful ship!", "You'll pay for this!"},
};

// is a captive (campaign::CaptiveName, a story's) a woman? (for the portrait)
inline bool CaptiveIsWoman(const std::string& c) {
    for (const char* w : {"daughter", "wife", "sister", "niece", "heiress", "lady", "widow", "girl", "queen", "mother"})
        if (c.find(w) != std::string::npos) return true;
    return false;
}

}  // namespace people
