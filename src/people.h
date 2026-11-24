// The campaign's people: who the captain meets in port and at sea, and what they say. Their portraits are
// data/portraits/<id>.png (drawn when there; the talk works without them).
#pragma once
#include "sim.h"
#include "audio.h"
#include "mods.h"
#include <cstring>
#include <cctype>

namespace people {

inline int strncasecmp_bs(const char* a, const char* b, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        const int x = tolower((unsigned char)a[i]), y = tolower((unsigned char)b[i]);
        if (x != y || !x) return x - y;
    }
    return 0;
}

struct Person {
    const char* id;
    const char* name;
    const char* title;
    const char* hello[6];  // greetings (one a day)
    const char* special;   // the harbour: her hull's in a bad way; the tavern: nothing today; the shipwright: no money
};

enum Shop { HARBOUR = 0, TAVERN = 1, WRIGHT = 2, SHOPS };

// each port's harbourmaster, tavern keeper and shipwright (campaign::kPortNames order)
inline const Person kPort[campaign::kPorts][SHOPS] = {
    {  // Port Haverly
     {"haverly_harbour", "Agatha Pruitt", "Harbourmaster of Port Haverly",
      {"Name, ship, and whether you mean to pay your dues this time. Go on, I've a ledger to fill.",
       "Port Haverly welcomes careful captains. The other sort we bury at the parish's expense.",
       "The tide's with you, the wind's with you, and the harbour fees are with me. Lovely day for it.",
       "Sign the book, Captain. Neatly. The last fellow signed with an anchor and I still haven't forgiven him.",
       "Mind the buoy on your way out. It's new. The old one was hit by a captain very like you.",
       "Haverly's quiet this season. I'd like it to stay that way. You understand me."},
      "Good heavens, look at her. Half your planking's in the sea. I'll send for the carpenters. And the bill."},
     {"haverly_tavern", "Barnaby Coyle", "Keeper of the Anchor",
      {"Come in, come in! Mind the dog. There's work on the board and ale in the barrel, and I'm not fussy which you take first.",
       "Back again? The Anchor's never short of three things: ale, rumours, and fools willing to chase them.",
       "Sit, sit. A fellow came in last night talking of guns on an island no chart shows. Paid in Spanish silver, too.",
       "The pie's mutton. Probably. Don't ask what the dog had for supper.",
       "Heard a sailor swear he saw a sea serpent off the Grey Isle. Then he ordered another, so who knows.",
       "If you're hiring, half my regulars can sail and the other half can at least shout."},
      "Board's bare today, Captain. Even the rumours have gone home. Try tomorrow."},
     {"haverly_wright", "Old Tom Fennick", "Shipwright of Port Haverly",
      {"Hmph. Built her sister, forty year back. Better timber then.",
       "Iron on her sides, copper on her bottom, and a bit of sense in her captain. I can sell you two of the three.",
       "Bring me gold and I'll make her fly. Bring me excuses and I'll make tea.",
       "Oak's oak. Doesn't care who you are. Treat her right and she'll carry you home.",
       "Seen a hundred captains come through that door. The careful ones come back.",
       "Mind the shavings. I'm not sweeping for anyone, not even the Admiralty."},
      "Light purse, heavy hopes. Come back when the first matches the second."}},
    {  // Saltmarsh
     {"saltmarsh_harbour", "Silas Reed", "Harbour Clerk of Saltmarsh",
      {"Oh! Captain. Yes. Welcome to Saltmarsh. Please don't fire anything. The last captain fired something.",
       "Your berth is number four. Or three. One of the dry ones. I'll, er, check the chart.",
       "There are pirates about, they say. I say nothing. Saying things gets you noticed.",
       "I've written your arrival in the book. In pencil. In case you leave suddenly.",
       "Would you, um, mind not mooring next to the fishing boats? They complain about the guns.",
       "The tide tables say high water at noon. The tide tables have been wrong before. Often."},
      "Is she meant to lean like that? I'm no sailor, but I don't think she's meant to lean like that."},
     {"saltmarsh_tavern", "Molly Brine", "The Drowned Duck",
      {"Welcome to the Duck, love! Wipe your boots, grab a stool, and tell me something I haven't heard.",
       "You look like trouble. Good. Trouble pays its tab.",
       "The fishermen say there's lights out on the marsh again. I say there's gin in the fishermen.",
       "Gin, ale, or the house special? The house special is gin in a nicer cup.",
       "Had a navy lieutenant in here crying into his rum last week. Pirates took his ship and his wig.",
       "Sit near the fire, love. The marsh damp gets into your bones and won't pay rent."},
      "No work today, sweetheart, unless you fancy scrubbing tankards. No? Didn't think so."},
     {"saltmarsh_wright", "Jonah Pike", "Shipwright of Saltmarsh",
      {"Fine lines on her. Shame about the paint. And the hull. And the rigging.",
       "Give me a week and she'll outrun anything in the Isles. Give me gold and I'll do it in a day.",
       "My father built ships, and his father. I'm the first one who gets paid on time.",
       "She creaks on the port side. You hear that? That's money, that is. My money.",
       "Pitch, oakum, a bit of swearing. That's all shipbuilding is, really.",
       "Seen worse. Not often, mind. But I've seen worse."},
      "Can't work for promises, Captain. Timber costs coin."}},
    {  // Greywater
     {"greywater_harbour", "Captain Elias Thorne", "Harbourmaster of Greywater",
      {"Twenty-two years in the navy, and now I count barrels. State your business.",
       "Greywater keeps a clean harbour. Keep your guns run in and your crew sober.",
       "I lost this eye off the Grey Isle. Some say there are still guns on it. Some say a great deal.",
       "Your crew. Sober. Your guns. Quiet. Your stay. Short. We'll get along.",
       "There's a storm in the west. There's always a storm in the west. Plan accordingly.",
       "I don't like pirates, Captain. I don't much like privateers either. Prove me wrong."},
      "She's been knocked about, Captain. Get her patched before you take her out again. That's not advice."},
     {"greywater_tavern", "The Widow Hask", "The Grey Gull",
      {"Sit down. Everyone who comes through that door wants something. Let's find out what you want.",
       "My husband sailed out of here twenty years ago. I keep the lamp lit. Habit, mostly.",
       "There's coin to be made, if you're not particular how. Most captains aren't.",
       "My husband drowned off the point twenty years ago. I keep his chair empty. Sit elsewhere.",
       "The grey comes in at dusk. Folk stay indoors. You'll want to, too.",
       "Drink up and listen. The sea's been talking lately, and none of it's good."},
      "Nothing for you today. Nothing honest, anyway."},
     {"greywater_wright", "Hector Dunmore", "Shipwright of Greywater",
      {"Och, what've ye done to her? Never mind. Never tell a shipwright. It only makes us weep.",
       "Oak from the north, pitch from the south, and my hands in between. Best hulls on the Isles.",
       "Ye want her faster or ye want her stronger? Cannae have both. Well, ye can, for a price.",
       "Built for the grey water, my hulls. Heavy, honest, and ugly as sin.",
       "You want pretty, go south. You want to survive, stay here and pay me.",
       "Hmm. Who patched this? A blind man with a spoon?"},
      "Gold first, Captain. My mallet doesnae swing for nothing."}},
    {  // Coral Bay
     {"coral_harbour", "Marisol Vega", "Harbourmistress of Coral Bay",
      {"Bienvenido to Coral Bay, Captain. Warm water, fair winds, and a harbour fee that's almost friendly.",
       "Watch the reef going out. It's eaten bigger ships than yours, and it's never full.",
       "The fishermen say something sings under the reef at night. Pay them no mind. Mostly.",
       "Welcome to Coral Bay, Captain! Watch the reef by the second marker. And the first. And the third.",
       "The water's so clear here you can count the wrecks on the bottom. Try not to add one.",
       "We get pirates, hurricanes and tax collectors. The tax collectors are the worst."},
      "Madre de Dios, your poor ship! Come, we'll have the carpenters on her at once."},
     {"coral_tavern", "Big Ezekiel", "The Sunken Bell",
      {"Ha! Another captain with salt in the beard. Sit down, the rum's cold and the stories are hot.",
       "Work? Plenty of work. Pirates to sink, cargo to run, and fools to rescue from their own bad luck.",
       "I had a ship once. Then I had a reef. Now I have a tavern. The tavern doesn't sink.",
       "Ezekiel's rum: guaranteed to take the barnacles off your hull and your soul.",
       "Big fella like me hears a lot of whispers. Big fella like me also forgets them, for a price.",
       "Sit, friend! The band starts at sundown. They're terrible. Everybody loves them."},
      "Board's empty, my friend. Drink, rest, come back when the sun is up again."},
     {"coral_wright", "Tomas Arroyo", "Shipwright of Coral Bay",
      {"She's a good ship. Not as good as she'll be when I'm done, but good.",
       "The worm down here eats a bare hull like bread. Copper, Captain. Trust me.",
       "I can make her quick enough to outrun a squall. Gold first. I'm quick at that too.",
       "Coral eats wood, Captain. The sea eats everything. I just slow it down.",
       "Copper bottom, that's the secret. The reef bites, but copper bites back.",
       "My grandfather built boats for the turtle hunters. I build them for madmen. Like you."},
      "Pockets empty? Mine too. That's why I charge."}},
    {  // Isla Dorada
     {"dorada_harbour", "Don Alvaro de Ruiz", "Harbour Governor of Isla Dorada",
      {"You may approach. Do not touch the railings. They were imported.",
       "Isla Dorada is a civilised port, Captain. Here we rob you with paperwork, not pistols.",
       "The governor sends his regards. The governor is me. I send my regards.",
       "Isla Dorada thanks you for your visit. Isla Dorada will also thank you for your harbour fee.",
       "You will find our port civilised, Captain. Unlike some ports. I name no names. Palmhaven.",
       "Gold in the hills, gold in the sea, gold in my ledger. Everything here shines."},
      "Your vessel is dripping on my quay. Have it repaired, or have it removed."},
     {"dorada_tavern", "Lucia Mendez", "La Perla",
      {"Ah, a new face. In La Perla we have music, wine, and work for anyone brave enough to ask.",
       "You have the look of someone who's been shot at. It suits you.",
       "Every captain in the Tropics drinks here sooner or later. Even the ones who hang.",
       "Sit, Captain. I'll play you something. Sad songs are free; happy songs cost a drink.",
       "La Perla has seen kings and cutthroats. They drink the same. They tip differently.",
       "Stay for the evening. The sunsets here could make a pirate weep. I've seen it."},
      "No work tonight, Capitan. Only music. Stay a while."},
     {"dorada_wright", "Mama Odile", "Shipwright of Isla Dorada",
      {"Child, what have you done to this poor ship? Come, show Mama where it hurts.",
       "Forty years I've built hulls on this island. Not one sank that I didn't mean to.",
       "Good wood remembers, Captain. Treat her kind and she'll bring you home.",
       "Mama Odile sees everything, child. Including that crack in your mainmast.",
       "The sea gives, the sea takes. Mama makes sure it takes a little less from you.",
       "Sit. Tea first, then timber. We do things properly here."},
      "Your purse is thinner than your hull, baby. Come back richer."}},
    {  // Palmhaven
     {"palmhaven_harbour", "Jack Sorrel", "Harbourmaster of Palmhaven",
      {"Welcome to Palmhaven, mate. No questions asked, no answers given. Fee's two silver, or one if you're pretty.",
       "I used to sail under the black flag. Now I just charge it mooring.",
       "Pieces of eight! That's the bird. Ignore him, he owes me money.",
       "Palmhaven: no questions asked, no answers given. Fees in coin, not stories.",
       "Moor where you like. Just not where the smugglers like. They get touchy.",
       "Nice ship. Shame if someone noticed her. I won't. For a price."},
      "Blimey, she's been in the wars. Get her patched before she goes down at my pier. Paperwork's a nightmare."},
     {"palmhaven_tavern", "Rosa Calder", "The Rum Barrel",
      {"Two rules in the Rum Barrel. Pay your tab, and don't touch my knives.",
       "You want work? I've got work. It's dangerous, it's dirty, and it pays. Pick two.",
       "The last captain who cheated me is still out there. In pieces. Various places.",
       "Rum, Captain? It's what the Barrel's for. Well, that and the fighting.",
       "Somebody's always selling a map here. Most of them are drawn on the back of a bill.",
       "Keep your purse close and your friends closer. Not too close. They'll take the purse."},
      "Nothing today. Drink or get out."},
     {"palmhaven_wright", "Kofi Mensah", "Shipwright of Palmhaven",
      {"Her bones are strong. I can feel it. The rest, we can fix.",
       "In Palmhaven nobody asks where a ship came from. I only ask what she needs.",
       "I built the fastest sloop in these waters. A pirate stole her. I took it as a compliment.",
       "Palm wood's soft, Captain. That's why I import oak. That's why I'm expensive.",
       "I fixed a smuggler's sloop last week. Fastest thing on the water now. Don't tell the navy.",
       "Give her a week out of the water and she'll thank you. Give me the gold and I'll thank you."},
      "Without gold, Captain, I am only a man with an axe."}},
    {  // Frostholm
     {"frostholm_harbour", "Ingrid Halvorsen", "Harbourmistress of Frostholm",
      {"Frostholm. Tie up fast, the ice moves at night. Fees are posted. They are not negotiable.",
       "You came through the floes? Then you are either skilled or lucky. Here we need both.",
       "Winter is coming. It is always coming, here.",
       "The ice moves at night, Captain. Moor fast and keep a watch.",
       "Frostholm takes care of its own. Behave, and you'll be one of them.",
       "Snow on the decks is weight. Sweep it, or the sea will sweep you."},
      "Ice did this? Then you sailed too fast. We repair. You learn."},
     {"frostholm_tavern", "Bjorn Ulfsson", "The Frozen Kettle",
      {"HA! Southerner! Come, drink, before the cold drinks you!",
       "Work? There is always work in the north. Most of it tries to kill you. Good work!",
       "My grandfather fought a sea serpent with his bare hands. My grandmother says it was a big eel. Grandmother lies.",
       "Ha! Another one! Sit, sit, the stew is hot and the stories are hotter!",
       "My grandmother wrestled a bear. My mother wrestled two. I just run the Kettle.",
       "Drink! The cold can't find you if you're warm on the inside!"},
      "No more jobs today, friend. Only mead. Mead is also a job."},
     {"frostholm_wright", "Sigrid Axelsdottir", "Shipwright of Frostholm",
      {"Southern ship. Thin planks. Pretty. Useless in the ice.",
       "I can give her an iron skin. The floes will bounce off her. Mostly.",
       "My axe built half the ships in this harbour. The other half it took apart.",
       "Ice cuts like a knife, Captain. Your hull needs armour, not paint.",
       "I build them to break the ice, not to look pretty. They do both, mind.",
       "Stand back from the forge. Unless you're cold. Then stand closer."},
      "No gold, no axe."}},
    {  // Iceward
     {"iceward_harbour", "Pastor Olav Lind", "Keeper of Iceward Harbour",
      {"God keep you, Captain. Iceward is a quiet port. We pray it stays that way.",
       "The ice took three ships this winter. I said words over each. I would rather not say them over yours.",
       "Something moves under the ice, out past the point. The children hear it singing. I tell them it is the wind.",
       "Bless you for coming, Captain. Few ships make it this far north.",
       "We pray for the whalers every morning. Lately we pray harder.",
       "Mind the ice in the channel. The Lord made it beautiful and very hard."},
      "Your ship is wounded, my son. Let our people mend her, before the sea takes her."},
     {"iceward_tavern", "Hilde", "The Whalebone",
      {"Come in out of the cold, dear. Stew's hot, the fire's lit, and there's work if you want it.",
       "Eat, eat. You southern captains are all bones.",
       "My son sails with the whalers. If you see a red sail out there, tell him to write to his mother.",
       "Eat something, dear. You look half frozen. Everyone does, up here.",
       "The whalers bring me bones and stories. The bones are more reliable.",
       "Sit by the fire. I'll find you a blanket. And soup. And maybe gossip."},
      "No work today, dear. Have some stew instead."},
     {"iceward_wright", "Gunnar Holt", "Shipwright of Iceward",
      {"Hm. Bring her in.",
       "Whalebone pegs. Never rot. Never fail.",
       "Ice is honest. It shows you where it will hit. Men don't.",
       "Hm. Ice damage. Seen it a hundred times. Fixed it ninety-nine.",
       "Whalebone and oak. Best ribs a ship can have.",
       "Sit down, wait, don't talk. I work better quiet."},
      "Gold. Then talk."}},
    {  // Cinderport
     {"cinderport_harbour", "Magistrate Corvin Ash", "Magistrate of Cinderport",
      {"Cinderport. Taxed, regulated, and on fire only occasionally. Your papers.",
       "The mountain smokes, the foundries smoke, and the Magistrate does not tolerate disorder.",
       "Something iron came out of the smoke last month and sank two of the governor's ships. We do not speak of it.",
       "Cinderport has rules, Captain. Break them and the foundry needs fuel.",
       "Your papers. Your cargo. Your intentions. In that order.",
       "The smoke never clears here. Neither do my suspicions."},
      "Your vessel is a disgrace to the harbour. The repair yard will see you. Promptly."},
     {"cinderport_tavern", "Scarlet Juno", "The Smoking Kettle",
      {"Welcome to the Kettle! Everything here's hot: the drink, the food, and the company.",
       "Got this scar from a powder keg. Got the keg's money, too.",
       "Work's dangerous out here. Fire, pirates, the mountain. Pays double, though.",
       "Sit down, sailor. Drinks are hot, the company's hotter, and the bill's the hottest of all.",
       "The foundry men drink like the forges. I keep the barrels full and the questions empty.",
       "You've got the look of someone who wants trouble. Lucky you. I sell it by the glass."},
      "Nothing on the board today, sugar. Even the pirates are taking a day off."},
     {"cinderport_wright", "Forgemaster Dace", "Shipwright and Smith of Cinderport",
      {"Wood burns. Iron doesn't. Remember that out here.",
       "I forge the plates and hang them myself. No ship of mine has burned yet.",
       "Bring me gold and I'll make her a fortress. Bring me nothing and I'll make you leave.",
       "Iron, Captain. Wood's for burning. Iron's for ships.",
       "I can plate her hull so thick the cannonballs bounce. You'll sink slower, too.",
       "Hammer's hot. Speak quick, or speak later."},
      "Iron costs. Come back with more."}},
    {  // Ashfall
     {"ashfall_harbour", "Ma Grubb", "Harbourmistress of Ashfall",
      {"Ashfall. Mind the ash, mind your purse, and mind me.",
       "Seen a lot of captains come through. Seen fewer go out.",
       "Fee's what I say it is. Today I say it's fair. Tomorrow, who knows.",
       "Ashfall, dearie. Mind the ash, mind the lava, mind your manners.",
       "The mountain grumbled this morning. It grumbles most mornings. One day it'll do more.",
       "Fees are fees, ash or no ash. Pay up and stay warm."},
      "Heh. Somebody's been in a scrap. Patch her up, dearie, it's cheaper than a funeral."},
     {"ashfall_tavern", "Ignatius Pell", "The Last Ember",
      {"Welcome to the Last Ember. The name is a warning, not a promise.",
       "Sit anywhere. The ash gets in everything, so drink fast.",
       "There's always someone who wants something burned, or someone who wants something saved from burning.",
       "Another moth to the flame. Sit. The ember burns for all.",
       "Every captain who drinks here thinks he'll be the one to sail the Reach. Some even are.",
       "Fire and sea, Captain. Both want you dead. I want you paying."},
      "Nothing tonight. Even ash needs to settle."},
     {"ashfall_wright", "Sparks", "Shipwright of Ashfall",
      {"Ooh, what a beauty! Can I take her apart? I'll put her back. Mostly in the same order.",
       "I've got ideas, Captain. Big ideas. Hot shot, iron plate, a little extra powder in the right places.",
       "Don't mind the scorch marks. That was a test. It went great.",
       "Ooh, look at all these holes! I can fix that. I can fix anything. Mostly.",
       "I've got an idea for a steam pump. It only exploded twice. Want it?",
       "Hand me that wrench. No, the other wrench. No, the bigger other wrench."},
      "I'd do it for free if I could. I can't. Gold, please!"}},
    {  // Mournwick
     {"mournwick_harbour", "Absalom Gray", "Harbourmaster of Mournwick",
      {"Welcome to Mournwick, Captain. We have rooms, a chapel, and a very reasonable undertaker.",
       "The fog came in three hundred years ago. It has not left. Neither, I find, do most of our visitors.",
       "If you see a ship with no crew out there, do not hail her. Simply note the time, and sail on.",
       "Mournwick welcomes the living. The dead are already here.",
       "Moor quietly, Captain. The fog carries sound, and some things listen.",
       "Every ship that leaves Mournwick, I count. Not every ship comes back to be counted."},
      "Dear me. She looks quite departed. Our yard will see to her before she is formally mourned."},
     {"mournwick_tavern", "Mother Whisper", "The Hanged Lantern",
      {"Come in, come in. The lantern knew you'd come. It always knows.",
       "Work, is it? There's always work in the fog. The fog makes work.",
       "I saw your face in the tea leaves last week. You were laughing. Or screaming. Hard to tell with tea.",
       "Sit, child. The fog tells me you've come far. The fog tells me a lot of things.",
       "Light a candle for the drowned. They'll remember you kindly.",
       "The raven says you'll live a long time. The raven lies sometimes. Drink."},
      "Nothing today, dearie. The dead are quiet today."},
     {"mournwick_wright", "Ned Coffin", "Shipwright and Coffin-Maker",
      {"Boats or coffins, it's all the same joinery. One floats a bit better.",
       "I measured you already. Habit. Don't worry about it.",
       "I'll do good work on her, Captain. I'd hate to see you back here in my other line of trade.",
       "Ships and coffins, Captain. Same wood, same nails. Different customers.",
       "I measure every captain who comes in. Habit. Don't take it personally.",
       "She'll float. For now. Everything floats, for now."},
      "No coin? Then no boat. And no coffin either, I'm afraid."}},
    {  // Lantern Rock
     {"lantern_harbour", "The Lamplighter", "Keeper of Lantern Rock",
      {"Lantern Rock. The light has burned three hundred years. Don't ask me how.",
       "Keep the lamp on your bow coming in. The other lights out there aren't mine.",
       "You'll see them on the beaches. The old wrecks. Something still walks the shore round them at night.",
       "The light stays lit, Captain. As long as I breathe, it stays lit.",
       "I've watched this fog for forty years. It's never been this thick.",
       "Keep the lamp in your sight and the rocks out of it."},
      "The fog's been at her, I see. Leave her with us a while."},
     {"lantern_tavern", "Evangeline Marsh", "The Pale Lady",
      {"Welcome to the Pale Lady, Captain. Wine? It's very old. Older than me. Probably.",
       "Everyone here is waiting for someone who isn't coming back. You'll fit right in.",
       "There's work, if you're not afraid of the fog. Or of what sails in it.",
       "Welcome to the Pale Lady. Mind the fog on the way in. It follows people.",
       "Pour you something dark? We only serve dark things here.",
       "The ships out there aren't all real, you know. Some just haven't noticed they're gone."},
      "Nothing for you tonight, Captain. Stay and drink with me. I hate drinking alone with them."},
     {"lantern_wright", "Brother Caspar", "Shipwright of Lantern Rock",
      {"Peace be with you, Captain. And pitch. Peace and pitch, that's my trade.",
       "Each plank I fit, I bless. The ones on the ghost ships out there were never blessed.",
       "Gold for the abbey, and a sound hull for you. The Lord likes a fair trade.",
       "Peace be with you, Captain. And with your hull, which needs it.",
       "Prayer and pitch. I don't know which keeps them afloat, so I use both.",
       "The brothers built ships here before the light was lit. We'll build them after."},
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
struct SeaVoice { const char* id; const char* name; const char* title; const char* lines[6]; };
enum SeaTalk { ST_HAIL = 0, ST_NAVY, ST_DISTRESS, ST_SAVED, ST_CASTAWAY, ST_CAPTIVE_F, ST_CAPTIVE_M, ST_STRUCK, ST_COUNT };
inline const SeaVoice kSea[ST_COUNT] = {
    {"sea_merchant", "Captain Hollis Grant", "Master of a merchantman",
     {"Fair winds, Captain! Nothing in our hold worth the trouble, I promise you.",
      "Good to see an honest sail. Keep those pirates off our stern, would you?",
      "Bound for port with a full hold. Sail with us a while, if you like the company.",
      "Watch yourself to the south, Captain. We heard guns there at dawn.",
      "Carrying cloth and pepper. Nothing a pirate would bother with. Right?",
      "If you see the navy, tell them the Grant family paid its taxes. Mostly."}},
    {"sea_navy", "Commodore Ada Sterling", "The Navy's patrol",
     {"The Navy patrols these waters, Captain. See that your business stays lawful.",
      "Any sign of pirates, signal us. We hang them, you collect the bounty. Everyone's happy.",
      "Carry on, Captain. Smartly, now.",
      "Your papers are in order, I trust. We'll assume so. Don't make us check.",
      "There's a price on every pirate in these waters, Captain. Collect it.",
      "We've lost three patrols this month. If you see black sails, run or fight. Don't parley."}},
    {"sea_merchant", "Captain Hollis Grant", "Master of a merchantman, under attack",
     {"Pirates! For the love of God, Captain, they're on us! Help!",
      "Help! They're coming alongside! Any ship that hears us, help!",
      "We can't outrun them! Captain, if you've guns, use them!",
      "They've shot away our mainmast! We're dead in the water!",
      "We have women and children aboard! For mercy's sake, Captain!",
      "They want the cargo! Let them have it, just get them off us!"}},
    {"sea_merchant", "Captain Hollis Grant", "Master of a merchantman",
     {"Bless you, Captain! Take this, and our thanks. We'll drink your health in every port.",
      "We'd be at the bottom without you. Here, a little something from the hold.",
      "I've a wife in port who'll light a candle for you. Thank you, Captain.",
      "Rum for your crew, Captain, and my undying gratitude. Mostly the rum.",
      "We'll tell every port from here to the Reach what you did today.",
      "If you're ever in need, ask for the Grants. We remember our friends."}},
    {"sea_castaway", "A Castaway", "Taken off the beach",
     {"A ship! A real ship! I thought I'd die talking to the crabs!",
      "Water. Have you water? And bread? And anything that isn't coconut?",
      "Two years on that rock, Captain. Two years. Take me anywhere with a bed.",
      "Is it still the same year? Please tell me it's still the same year.",
      "I carved my name on every tree on that island. Every one. Never again.",
      "A hammock, Captain. Just a hammock. I'll sleep for a week."}},
    {"sea_captive", "Lady Arabella Wren", "Freed from the pirates",
     {"About time, Captain. Now get me home, and keep that cutthroat in irons.",
      "I knew someone would come. I didn't know it would take this long.",
      "They said nobody would come for me. I'd very much like to see their faces.",
      "Do you know what they fed me? Fish. Every day. Raw fish.",
      "My father will reward you. My father will also have words with the navy.",
      "I kept their captain's knife. I meant to use it. Get me home first."}},
    {"sea_captive_m", "The Prisoner", "Freed from the pirates",
     {"Thank God. I thought they'd hang me from the yard. Get me home, Captain.",
      "You came for me? I owe you my life, Captain. Truly.",
      "They meant to sell me. I'd have fetched a poor price, too. Thank you.",
      "My family thought me dead. Wait until they see me.",
      "I counted the days by the rats. There were a lot of rats.",
      "If you ever need a man with nothing to lose, Captain, you know where I'll be."}},
    {"sea_pirate", "The Pirate Captain", "Struck her colours",
     {"Quarter! We strike! Take the ship, take the gold, just leave us breathing!",
      "Enough! The ship's yours. Do what you like with her, but let us live.",
      "Hold your fire, damn you! We strike! We strike!",
      "Take her! Take her! She was never a lucky ship anyway!",
      "Mercy, Captain! We've got families! Well, some of us!",
      "We surrender! Just don't tell the others we gave up this easily!"}},
};

// each region's dread pirate, when she finds the captain
struct DreadVoice { const char* id; const char* line; const char* more[2]; };  // (her hail: line, or one of more)
inline const DreadVoice kDread[campaign::BIOMES] = {
    {"dread_corwin", "Captain of the Sovereign, as I live. The Admiral said you'd drowned. How very untidy of you.", {"Do try to sink gracefully this time. One has standards.", "Ashgrave sends his regards. I'm to deliver them with the guns."}},
    {"dread_calypso", "The drowned captain, back from the deep. The Queen will be so pleased, sweetness. She hates to waste a meal.", {"Your Mara sang so prettily on the Admiral's deck. The reef sang back.", "Every wave out here sings my name. Listen."}},
    {"dread_rime", "Ashgrave's ghost. The cold takes everything in the end, captain. It forgot you once. Not today.", {"Your gunner lasted nine days on the ice. You won't see one.", "I have sailed the ice for fifty winters. The Admiral pays better than the cold."}},
    {"dread_kate", "Oh, it's YOU. The one that wouldn't burn. Let's try again, shall we?", {"Burn, burn, burn! Ha!", "Your ship looks lovely in iron, captain. I'll tell her you said hello."}},
    {"dread_widow", "I buried every captain who sailed against me. I buried your crew too. I have room for one more.", {"The Sovereign's dead sail with the Dutchman now. They miss you.", "The fog keeps my secrets. Soon it will keep yours."}},
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
    {"merchant_1", 105, 0.94f, 0.15f, 0.30f, 1}, {"merchant_2", 200, 1.13f, 0.12f, 0.05f, 0}, {"merchant_3", 125, 1.00f, 0.10f, 0.05f, 0},
    {"merchant_4", 210, 1.14f, 0.10f, 0.00f, 1}, {"navy_1", 110, 0.95f, 0.05f, 0.05f, 0}, {"navy_2", 225, 1.17f, 0.08f, 0.00f, 0},
    {"navy_3", 85, 0.90f, 0.20f, 0.45f, 2}, {"castaway_1", 215, 1.15f, 0.30f, 0.10f, 1}, {"castaway_2", 90, 0.92f, 0.35f, 0.30f, 2},
    {"castaway_3", 135, 1.02f, 0.20f, 0.10f, 1}, {"captive_f_1", 230, 1.18f, 0.10f, 0.00f, 0}, {"captive_f_2", 195, 1.12f, 0.15f, 0.00f, 2},
    {"captive_f_3", 205, 1.12f, 0.12f, 0.15f, 1}, {"captive_m_1", 140, 1.03f, 0.10f, 0.00f, 0}, {"captive_m_2", 85, 0.90f, 0.10f, 0.40f, 2},
    {"rival_1", 215, 1.15f, 0.12f, 0.05f, 1}, {"rival_2", 120, 0.98f, 0.10f, 0.15f, 1}, {"rival_3", 185, 1.10f, 0.15f, 0.10f, 0},
    {"rival_4", 80, 0.88f, 0.15f, 0.45f, 1}, {"rival_5", 225, 1.16f, 0.12f, 0.05f, 1}, {"rival_6", 105, 0.94f, 0.10f, 0.10f, 0},
    {"smuggler_1", 130, 1.00f, 0.25f, 0.15f, 1}, {"smuggler_2", 220, 1.16f, 0.12f, 0.00f, 1}, {"smuggler_3", 175, 1.08f, 0.30f, 0.30f, 2},
    {"smuggler_4", 190, 1.10f, 0.15f, 0.10f, 0}, {"smuggler_5", 205, 1.12f, 0.20f, 0.10f, 1}, {"castaway_4", 180, 1.08f, 0.30f, 0.20f, 2},
    {"castaway_5", 245, 1.20f, 0.25f, 0.00f, 0}, {"captive_f_4", 185, 1.10f, 0.10f, 0.05f, 2}, {"captive_f_5", 215, 1.15f, 0.10f, 0.00f, 0},
    {"navy_4", 195, 1.11f, 0.08f, 0.05f, 0}, {"merchant_5", 175, 1.07f, 0.25f, 0.25f, 1},
    {"fleet_1", 220, 1.17f, 0.10f, 0.00f, 1}, {"fleet_2", 105, 0.95f, 0.20f, 0.40f, 2}, {"fleet_3", 100, 0.94f, 0.08f, 0.05f, 0},
    {"fleet_4", 200, 1.13f, 0.15f, 0.00f, 0}, {"fleet_5", 90, 0.92f, 0.10f, 0.30f, 1}, {"fleet_6", 125, 1.00f, 0.20f, 0.20f, 0},
    {"fleet_7", 115, 0.97f, 0.12f, 0.10f, 0},
    {"narrator", 170, 1.07f, 0.22f, 0.12f, 0},  // (the memories' telling)  // (the user himself, 2026-11-23: one of her own captains)
    {"pirate_1", 95, 0.93f, 0.15f, 0.40f, 2}, {"pirate_2", 215, 1.15f, 0.20f, 0.05f, 1}, {"pirate_3", 80, 0.88f, 0.10f, 0.45f, 2},
    {"pirate_4", 130, 1.00f, 0.40f, 0.10f, 3}, {"pirate_5", 140, 1.03f, 0.10f, 0.00f, 1}, {"pirate_6", 100, 0.95f, 0.30f, 0.20f, 3},
    // the sea's second cast (2026-11-22)
    {"pirate_7", 200, 1.13f, 0.15f, 0.10f, 2}, {"pirate_8", 80, 0.88f, 0.10f, 0.45f, 1}, {"pirate_9", 135, 1.02f, 0.12f, 0.00f, 1},
    {"pirate_10", 190, 1.11f, 0.10f, 0.15f, 1}, {"pirate_11", 95, 0.93f, 0.20f, 0.50f, 1}, {"pirate_12", 215, 1.15f, 0.25f, 0.05f, 3},
    {"castaway_6", 150, 1.05f, 0.25f, 0.00f, 0}, {"castaway_7", 170, 1.06f, 0.20f, 0.25f, 2}, {"castaway_8", 100, 0.94f, 0.25f, 0.20f, 1},
    {"castaway_9", 220, 1.16f, 0.25f, 0.00f, 0}, {"captive_f_6", 225, 1.17f, 0.10f, 0.05f, 2}, {"captive_f_7", 190, 1.11f, 0.12f, 0.00f, 0},
    {"captive_m_3", 145, 1.04f, 0.12f, 0.00f, 0}, {"captive_m_4", 95, 0.93f, 0.15f, 0.35f, 2}, {"merchant_6", 215, 1.15f, 0.15f, 0.00f, 1},
    {"merchant_7", 90, 0.92f, 0.12f, 0.10f, 0}, {"navy_5", 120, 0.98f, 0.08f, 0.05f, 0}, {"navy_6", 180, 1.08f, 0.08f, 0.05f, 2},
    {"smuggler_6", 125, 1.00f, 0.15f, 0.15f, 1}, {"smuggler_7", 210, 1.14f, 0.20f, 0.00f, 1},
    // the story's people
    {"story_mara", 205, 1.13f, 0.12f, 0.00f, 0}, {"story_mara_storm", 190, 1.10f, 0.45f, 0.05f, 3}, {"story_ashgrave", 105, 0.95f, 0.08f, 0.05f, 0},
    {"officer_stroud", 85, 0.90f, 0.10f, 0.35f, 1}, {"officer_fairweather", 215, 1.15f, 0.08f, 0.00f, 0}, {"officer_gage", 95, 0.93f, 0.15f, 0.30f, 2},
    {"officer_whitlock", 130, 1.01f, 0.20f, 0.00f, 0}, {"officer_penhallow", 175, 1.07f, 0.12f, 0.20f, 2},
};

// How they say a line (sfx::Babble: the line's own syllables, without words): their manner's mood - plain, cheerful,
// gruff, eerie - unless the line itself is begging (scared), cursing or burning (angry) or trailing off (weary)
inline bool HasWord(const char* line, const char* w) {
    const size_t n = strlen(w);
    for (const char* s = line; *s; ++s)
        if (!strncasecmp_bs(s, w, n) && (s == line || !isalpha((unsigned char)s[-1])) && !isalpha((unsigned char)s[n])) return true;
    return false;
}
inline int MoodOf(const Voice& v, const char* line) {
    static const char* const scared[] = {"please", "mercy", "help", "beg", "pity", "save", "strike", "surrender", "don't shoot", "spare"};
    static const char* const angry[] = {"curse", "damn", "burn", "die", "kill", "pay for this", "board her", "destroy", "blood", "sink you", "fool"};
    const bool bang = strchr(line, '!') != nullptr;
    const size_t len = strlen(line);
    if (v.manner != 3)
        for (const char* w : scared)
            if (HasWord(line, w)) return sfx::MOOD_SCARED;
    if (bang && v.manner != 1)
        for (const char* w : angry)
            if (HasWord(line, w)) return sfx::MOOD_ANGRY;
    if (len > 3 && !strcmp(line + len - 3, "...") && v.manner != 1) return sfx::MOOD_WEARY;
    static const int k[] = {sfx::MOOD_PLAIN, sfx::MOOD_CHEERFUL, sfx::MOOD_GRUFF, sfx::MOOD_EERIE};
    return k[v.manner & 3];
}
inline const Voice* VoiceOf(const char* id) {
    for (const Voice& x : kVoices)
        if (!strcmp(x.id, id)) return &x;
    return nullptr;
}
inline sfx::VoiceParams ParamsOf(const Voice& v) {
    sfx::VoiceParams p;
    p.pitch = v.pitch;
    p.formant = v.formant;
    p.breath = v.breath;
    p.rasp = v.rasp;
    return p;
}
inline bool GameWomanFace(const std::string& id);  // (below, with the faces)
inline bool WomanFace(const std::string& id);
// (line: the game's own words - a mod's recordings are filed under them too, whatever it shows: mods::VoicePath)
inline void Voiced(const char* id, const char* line, uint32_t salt, int prio = 1) {
    if (!line) return;
    const bool gameWoman = GameWomanFace(id);
    const std::string path = mods::VoicePath(id, sfx::LineHash(line), gameWoman ? 1 : 2);
    if (!path.empty() && sfx::PlayVoice(path.c_str(), 0.85f, prio)) return;  // (spoken, where there's a recording)
    if (getenv("BS_VOICELOG")) printf("[voice] no recording: %s %08x %s\n", id, sfx::LineHash(line), line);
    Voice v{};
    if (const Voice* g = VoiceOf(id)) v = *g;
    else if (!mods::Sex(id)) return;
    else v = {id, 120.0f, 1.0f, 0.1f, 0.1f, 0};
    mods::Babble mb;
    if (mods::BabbleOf(id, mb)) v = {id, mb.pitch, mb.formant, mb.breath, mb.rasp, mb.manner};
    else if (WomanFace(id) && !gameWoman) {  // (a man the mod made a woman, and gave no voice of her own)
        v.pitch = v.pitch * 1.8f > 200.0f ? v.pitch * 1.8f : 200.0f;
        v.formant = 1.15f;
        v.rasp *= 0.4f;
    }
    const uint32_t h = salt * 2654435761u + (uint32_t)strlen(line) * 40503u;
    sfx::Babble(ParamsOf(v), line, MoodOf(v, line), 0.75f, h, prio);
}

// ---- Memories: five scenes from the years before the prologue - how she came to the Admiral, how she met Mara, what
// he wanted with her - played, as the prologue is (World::StartMemory: a ship of then, the place, the hour, who was
// there). One for each story told (in order, whichever region's it was), in port under "Memories", to play or not.
// Each line on a stage of the memory (the server says when: EV_MEMORY) and so many seconds into it. Who: the narrator
// (a voice, no face), she herself ("You", no voice), Mara, the Admiral; MEM_GOAL is what she's to do (in the feed).
enum { MEM_TELL = 0, MEM_YOU, MEM_MARA, MEM_ASHGRAVE, MEM_GOAL };
struct MemoryLine { int who, stage; float at; const char* text; const char* how; };  // (how: the voice's direction)
struct Memory { const char* title; const char* when; const char* you; const char* mara; MemoryLine lines[16]; };
inline const char* MemoryFace(int who) { return who == MEM_MARA ? "story_mara" : who == MEM_ASHGRAVE ? "story_ashgrave" : who == MEM_TELL ? "narrator" : nullptr; }
inline const Memory kMemories[5] = {
    {"The Grey Isle", "Eleven years ago", "Midshipman", "", {
        {MEM_TELL, 1, 1.0f, "Eleven years ago. A midshipman aboard the brig Merryweather, her master dead at the wheel, two pirate luggers on her heels.", "Opening an old story, low and unhurried, a little grim."},
        {MEM_GOAL, 1, 9.0f, "Make for the Admiral's fort on the Grey Isle: its guns will cover you", nullptr},
        {MEM_TELL, 2, 0.5f, "The fort's guns spoke once, and the luggers remembered somewhere else to be. That evening, she was sent for.", "Dry amusement, then a turn of the page."},
        {MEM_ASHGRAVE, 2, 8.0f, "You're the one who took the Merryweather's helm when her master fell. Without orders.", "A senior admiral, interested, testing a young officer in his study. Measured, dry."},
        {MEM_YOU, 2, 16.0f, "She was going onto the rocks, sir. There wasn't time for orders.", nullptr},
        {MEM_ASHGRAVE, 2, 21.0f, "There never is. That is what the orders are for.", "Amused, gently correcting, a teacher's patience."},
        {MEM_ASHGRAVE, 2, 27.0f, "Out there every captain sails for herself, and the sea drowns them one by one. It wants a hand on it. One hand.", "Quiet conviction, looking out of a window at the sea. Almost tender, a little cold."},
        {MEM_YOU, 2, 37.0f, "Yours, sir?", nullptr},
        {MEM_ASHGRAVE, 2, 41.0f, "Someone's. I would rather it were a hand I trust. Dine with me tonight, Midshipman.", "Warm now, charming, the mentor choosing a protegee."},
        {MEM_TELL, 2, 48.0f, "She dined with him that night, and every week after. He taught her the stars and the guns, and the patience of a man who plans in decades.", "Fond, with a shadow under it."},
        {-1, 0, 0.0f, nullptr, nullptr}}},
    {"Lantern Rock", "Eight years ago", "Lieutenant of the Wren", "Master's mate of the Wren", {
        {MEM_TELL, 1, 1.0f, "Eight years ago. The frigate Wren, her first lieutenancy, caught off Lantern Rock in a squall that would not end.", "Telling an old story, the storm in it, low."},
        {MEM_GOAL, 1, 8.0f, "Hold her together and bring the Wren into the lee of Lantern Rock", nullptr},
        {MEM_MARA, 1, 9.0f, "Lieutenant! You'll want a line on you if you mean to stand there all night!", "Shouting over a storm on a ship's deck, young, cheeky, good-humoured."},
        {MEM_YOU, 1, 16.0f, "You're the new master's mate. Vell.", nullptr},
        {MEM_MARA, 1, 21.0f, "Mara. My gran keeps the light up there. She'd skin me if I let a lieutenant go over the side in her waters.", "Shouting over the wind, grinning, friendly, a little proud."},
        {MEM_TELL, 2, 0.5f, "Then Mara did something no one aboard ever spoke of afterwards. She sang, low, an old Lamplighter's tune, and the wind fell away like a dog called to heel.", "Hushed, wondering, the strangest part of the story."},
        {MEM_YOU, 2, 12.0f, "How did you do that?", nullptr},
        {MEM_MARA, 2, 16.0f, "Do what? It was blowing itself out. It's only an old song.", "Suddenly quiet and evasive, embarrassed, brushing it off."},
        {MEM_MARA, 2, 23.0f, "Don't tell anyone. Please.", "A whisper, frightened, pleading, very sincere."},
        {MEM_TELL, 3, 0.5f, "She never did. But the Admiral's flagship lay at anchor in the lee of the Rock that night, and someone aboard her was listening.", "Ominous, quiet, letting it land."},
        {-1, 0, 0.0f, nullptr, nullptr}}},
    {"The Ice", "Six years ago", "Lieutenant of the Wren", "Master's mate of the Wren", {
        {MEM_TELL, 1, 1.0f, "Six years ago. The Wren, frozen fast in the northern ice for nineteen days, the biscuit gone green.", "Cold, weary, a hard winter remembered."},
        {MEM_MARA, 1, 9.0f, "Twenty men down with the scurvy. If the ice doesn't open by Sunday we start on the boots.", "Cold, tired, grim gallows humour between old friends."},
        {MEM_YOU, 1, 16.0f, "It'll open.", nullptr},
        {MEM_MARA, 1, 19.0f, "You always say that. You're usually right. It's infuriating.", "A tired fond laugh, teasing her friend."},
        {MEM_TELL, 1, 25.0f, "On the twentieth day a sledge came over the ice: charts of a channel no one had ever sounded, under the Admiral's seal. And a letter.", "Turning a corner in the story, a little wary."},
        {MEM_GOAL, 1, 26.0f, "Follow the Admiral's channel out of the ice, mark by mark", nullptr},
        {MEM_ASHGRAVE, 2, 0.5f, "Bring her home by this road, Lieutenant. And send me Miss Vell. I have a ship with need of her particular gifts.", "Reading out his own letter, formal, courteous, with a hidden hunger."},
        {MEM_MARA, 2, 10.0f, "I'm not going. Tell him the master's mate of the Wren is needed on the Wren.", "Firm, stubborn, a little scared, refusing."},
        {MEM_YOU, 2, 17.0f, "He's the Admiral, Mara.", nullptr},
        {MEM_MARA, 2, 21.0f, "And you're my captain. Or you will be. I go where you go. That's the end of it.", "Warm, loyal, decided, final."},
        {MEM_TELL, 3, 0.5f, "She wrote that Miss Vell could not be spared. It was the only time she ever refused him, and he never spoke of it again.", "Quiet, weighted, a little sad."},
        {-1, 0, 0.0f, nullptr, nullptr}}},
    {"The Sovereign", "Three years ago", "Captain of the Sovereign", "First mate of the Sovereign", {
        {MEM_TELL, 1, 1.0f, "Three years ago. The Sovereign's first day under her colours, seventy-four guns newly blacked, and a pirate band in the approaches to Port Haverly.", "Proud, the story's high point, a smile in it."},
        {MEM_ASHGRAVE, 1, 10.0f, "The finest ship in the Isles, Captain, and I give her to the finest captain I ever made. Show me.", "Proud and commanding, across the water from his own flagship."},
        {MEM_GOAL, 1, 11.0f, "Clear the approaches to Port Haverly", nullptr},
        {MEM_ASHGRAVE, 2, 1.0f, "Bring her home. Always bring her home. And Miss Vell is your first mate, I see.", "Fatherly, then turning, noticing someone, interested."},
        {MEM_MARA, 2, 9.0f, "My lord.", "Polite, guarded, a curtsey of a greeting."},
        {MEM_ASHGRAVE, 2, 12.0f, "Your grandmother's light still burns on the Rock, I'm told. Does she still tell the old stories? The bell under the sea?", "Light and charming, but probing, too interested."},
        {MEM_MARA, 2, 22.0f, "Gran tells a great many stories, my lord. Most of them are about drink.", "Deflecting with a joke, a careful smile."},
        {MEM_ASHGRAVE, 2, 29.0f, "Ha! Keep her close, Captain. Some people are worth more than any ship.", "A hearty laugh, then something colder under it."},
        {MEM_MARA, 2, 37.0f, "He knows about the song. I don't know how, but he knows.", "Quiet, uneasy, after he has gone, to her friend alone."},
        {MEM_YOU, 2, 44.0f, "He's our friend, Mara.", nullptr},
        {MEM_MARA, 2, 48.0f, "I know. I know he is.", "Wanting to believe it, not quite managing."},
        {-1, 0, 0.0f, nullptr, nullptr}}},
    {"The Last Night", "The night before the Ember Reach", "Captain of the Sovereign", "First mate of the Sovereign", {
        {MEM_TELL, 1, 1.0f, "The night before the Ember Reach. The squadron's lanterns strung across the dark, and the Dominion's ahead.", "Hushed, the last calm before the end of the story."},
        {MEM_GOAL, 1, 7.0f, "Keep company with the squadron to the rendezvous", nullptr},
        {MEM_MARA, 1, 9.0f, "I keep dreaming of a bell. Under the water. Every night now. Five of them, ringing out of time.", "Late at night, unsettled, haunted, speaking softly."},
        {MEM_YOU, 1, 18.0f, "It's the Reach. Everyone dreams badly near the fire mountains.", nullptr},
        {MEM_MARA, 1, 23.0f, "Do they dream what the Admiral dreams? He asked me today, out of nowhere, if I ever hear bells at sea.", "Uneasy, half a laugh, frightened underneath."},
        {MEM_MARA, 1, 33.0f, "Promise me something. If anything happens, anything, you come and find me. Wherever I am. Whatever I've become.", "Very earnest, close, emotional, a plea between friends."},
        {MEM_YOU, 1, 44.0f, "Nothing's going to happen. But yes. I promise.", nullptr},
        {MEM_MARA, 1, 49.0f, "Good. Then I'll stop worrying. Liar. I won't. But it helps.", "Relieved, a small teary laugh, affectionate."},
        {MEM_TELL, 2, 0.5f, "At dawn the Dominion hoisted a black flag. And she had made a promise she meant to keep.", "The story's end, slow and certain, quietly resolute."},
        {-1, 0, 0.0f, nullptr, nullptr}}},
};

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

// Barks: a word now and then from her fleet's captains and from the pirates come to fight her (a small portrait, one line).
// Every face once: the game picks whichever she's seen least lately (Game::PickFace), and only those imported
// (data/portraits/<id>.png), so a new face in a pool turns up as soon as its portrait does.
inline const char* const kFleetPool[] = {"fleet_1", "fleet_2", "fleet_3", "fleet_4", "fleet_5", "fleet_6", "fleet_7"};
inline const char* const kPiratePool[] = {"pirate_1", "pirate_2", "pirate_3", "pirate_4", "pirate_5", "pirate_6",
                                         "pirate_7", "pirate_8", "pirate_9", "pirate_10", "pirate_11", "pirate_12"};
inline const char* FleetFace(int seed) {  // (her own fleet: few, and each keeps its captain for good)
    return kFleetPool[(unsigned)seed % (sizeof kFleetPool / sizeof kFleetPool[0])];
}
// who's speaking for one of her fleet: the ship ("the Swallow"), or, for a captain with a name of his own, him and his
// ship (fleet_7 is the user himself, 2026-11-23: "His name is Dong Bang")
inline std::string FleetCaptain(int seed, const char* ship) {
    if (!strcmp(FleetFace(seed), "fleet_7")) return std::string("Captain Dong Bang, the ") + ship;
    return std::string("the ") + ship;
}
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

// The people at sea, more than one face each: whoever speaks for a merchantman, the navy, castaways and captives gets
// one of these (by the ship's name, so the same ship keeps the same captain); name nullptr keeps the speaker's own
// Who's a woman among the faces (from ZWUUUP's voices.json, the voice each was given), and whether a name is a woman's
// or a man's: a named pirate, rival or smuggler gets a face to go with the name (the user, 2026-11-23: "male get male
// names and female get female names?" - "Dread Ned" had come out with a woman's face)
inline const char* const kWomenFaces[] = {
    "ashfall_harbour", "ashfall_wright", "captive_f_1", "captive_f_2", "captive_f_3", "captive_f_4", "captive_f_5",
    "captive_f_6", "captive_f_7", "castaway_1", "castaway_4", "castaway_5", "castaway_7", "castaway_9",
    "cinderport_tavern", "coral_harbour", "dorada_tavern", "dorada_wright", "dread_calypso", "dread_kate",
    "dread_widow", "fleet_1", "fleet_4", "frostholm_harbour", "frostholm_wright", "greywater_tavern",
    "haverly_harbour", "iceward_tavern", "lantern_tavern", "merchant_2", "merchant_4", "merchant_5", "merchant_6",
    "mournwick_tavern", "navy_2", "navy_4", "navy_6", "officer_fairweather", "officer_penhallow", "palmhaven_tavern",
    "pirate_10", "pirate_12", "pirate_2", "pirate_7", "rival_1", "rival_3", "rival_5", "saltmarsh_tavern",
    "sea_captive", "sea_navy", "smuggler_2", "smuggler_3", "smuggler_4", "smuggler_5", "smuggler_7", "story_mara",
};
inline bool GameWomanFace(const std::string& id) {  // (as the game has them)
    for (const char* w : kWomenFaces)
        if (id == w) return true;
    return false;
}
inline bool WomanFace(const std::string& id) {  // (as they are now: a mod can make a man a woman, mods::Sex)
    if (const int s = mods::Sex(id)) return s == 1;
    return GameWomanFace(id);
}
inline int NameSex(const std::string& name) {  // 1 a woman's name, 2 a man's, 0 can't tell (Cpt. Scurvy, Old Salt)
    static const char* const kF[] = {"Meg", "Bess", "Kate", "Sue", "Anne", "Stella", "Mistress", "Nell", "Lady", "Molly", "Mary", "Sal",
                                     "Wen", "Nan", "Rose", "Tilly", "Mother", "Polly", "Jean", "Queen", "Widow", "Madam"};
    static const char* const kM[] = {"Ned", "Pete", "Jack", "Bart", "Gus", "Tom", "Joe", "Finn", "Hob", "Jim", "Hal", "Piet", "Billy",
                                     "Hugo", "Jonas", "Scratch", "Silas", "Rupert", "Sam", "Morgan", "Kit", "King", "Lord", "Sir"};
    std::string w;
    auto word = [&](const char* const* list, size_t n) {
        for (size_t i = 0; i < n; ++i)
            if (w == list[i]) return true;
        return false;
    };
    for (size_t i = 0; i <= name.size(); ++i) {
        const char c = i < name.size() ? name[i] : ' ';
        if (c == ' ' || c == '-' || c == '.' || c == '\'') {
            if (word(kF, sizeof kF / sizeof kF[0])) return 1;
            if (word(kM, sizeof kM / sizeof kM[0])) return 2;
            w.clear();
        } else w += c;
    }
    return 0;
}

struct FacePick { const char* id; const char* name; };
inline const FacePick* FacePool(const std::string& base, int& n) {
    // (every face once: the game picks the one she's seen least lately, Game::PickFace; those not imported are skipped)
    static const FacePick merchant[] = {{"sea_merchant", nullptr}, {"merchant_1", nullptr}, {"merchant_2", nullptr}, {"merchant_3", nullptr},
                                        {"merchant_4", nullptr}, {"merchant_5", nullptr}, {"merchant_6", nullptr}, {"merchant_7", nullptr}};
    static const FacePick navy[] = {{"sea_navy", nullptr}, {"navy_1", "Captain Edmund Hale"}, {"navy_2", "Lieutenant Clara Vance"},
                                    {"navy_3", "Admiral Sir Rufus Blackwood"}, {"navy_4", "Commander Isobel Thorne"},
                                    {"navy_5", "Captain Thomas Reyne of the Marines"}, {"navy_6", "Captain Honoria Steele"}};
    static const FacePick castaway[] = {{"sea_castaway", "Ezra Holt"}, {"castaway_1", "Nell Harrow"}, {"castaway_2", "Old Bartholomew"},
                                        {"castaway_3", "Kip Tobias"}, {"castaway_4", "Rosa Quill"}, {"castaway_5", "Amara Sol"},
                                        {"castaway_6", "Midshipman Peter Ash"}, {"castaway_7", "Greta Holm"}, {"castaway_8", "Cookie Marchetti"},
                                        {"castaway_9", "Fen Ashby"}};
    static const FacePick woman[] = {{"sea_captive", nullptr}, {"captive_f_1", nullptr}, {"captive_f_2", nullptr}, {"captive_f_3", nullptr},
                                     {"captive_f_4", nullptr}, {"captive_f_5", nullptr}, {"captive_f_6", nullptr}, {"captive_f_7", nullptr}};
    static const FacePick man[] = {{"sea_captive_m", nullptr}, {"captive_m_1", nullptr}, {"captive_m_2", nullptr}, {"captive_m_3", nullptr},
                                   {"captive_m_4", nullptr}};
    static const FacePick rival[] = {{"rival_1", nullptr}, {"rival_2", nullptr}, {"rival_3", nullptr}, {"rival_4", nullptr}, {"rival_5", nullptr},
                                     {"rival_6", nullptr}};
    static const FacePick pirate[] = {{"sea_pirate", nullptr}, {"pirate_1", nullptr}, {"pirate_2", nullptr}, {"pirate_3", nullptr},
                                      {"pirate_4", nullptr}, {"pirate_5", nullptr}, {"pirate_6", nullptr}, {"pirate_7", nullptr},
                                      {"pirate_8", nullptr}, {"pirate_9", nullptr}, {"pirate_10", nullptr}, {"pirate_11", nullptr},
                                      {"pirate_12", nullptr}};  // (a quest's named pirate: a face to go with the name)
    static const FacePick smuggler[] = {{"smuggler_1", nullptr}, {"smuggler_2", nullptr}, {"smuggler_3", nullptr}, {"smuggler_4", nullptr},
                                        {"smuggler_5", nullptr}, {"smuggler_6", nullptr}, {"smuggler_7", nullptr}};
    struct P { const char* base; const FacePick* f; int n; };
#define BS_POOL(b, a) {b, a, (int)(sizeof a / sizeof a[0])}
    static const P k[] = {BS_POOL("sea_merchant", merchant), BS_POOL("sea_navy", navy), BS_POOL("sea_castaway", castaway), BS_POOL("sea_captive", woman),
                          BS_POOL("sea_captive_m", man), BS_POOL("rival", rival), BS_POOL("smuggler", smuggler),
                          BS_POOL("sea_pirate", pirate)};
#undef BS_POOL
    for (const P& p : k)
        if (base == p.base) { n = p.n; return p.f; }
    n = 0;
    return nullptr;
}
inline const char* const kSmugglerLines[3] = {
    "Nothing in the hold but fish, Captain! Smelly, honest fish!",
    "Customs? Never heard of them. Good day to you!",
    "You didn't see us. We didn't see you. Everyone's happy.",
};

// The sea's other people (campaign::SeaEvent): a rival captain's hail, survivors adrift calling, and their thanks
inline const char* const kRivalLines[6] = {
    "So you're the one they're singing about. Let's see if the songs are true.",
    "I've been chasing your sails for a week, Captain. There's only room for one of us out here.",
    "Every tavern from here to the Reach knows your name. By tonight they'll know mine.",
    "No hard feelings, Captain. It's just business. And pride. Mostly pride.",
    "Your ship's as fine as they say. She'll look better with my flag on her.",
    "I'll make you a wager: the loser buys the drinks. In the next life.",
};
inline const char* const kAdriftLines[3] = {
    "Ahoy! Ahoy the ship! For pity's sake, come alongside!",
    "We've no mast, no water, and no hope! Please, Captain!",
    "Our ship went down three days back. Take us off, we beg you!",
};
inline const char* const kAdriftThanks[3] = {
    "God bless you, Captain! We'd given up. Here, it's all we saved.",
    "Water... thank you. We'll tell every port what you did.",
    "Three days we drifted. I'll never complain about a hammock again.",
};

// ---- Passengers: on the way home, out of the fighting, the castaways and the freed captives talk (Game::UpdatePassengers):
// how they came to be there, what they saw, and an omen of the last story that grows with the stories she's told.
// Three lines each, a minute or so apart.
struct Tale { const char* how; const char* saw; };
inline const Tale kCastawayTales[campaign::BIOMES][4] = {
    {  // the Temperate Isles: Gentleman Corwin, the guns on the Grey Isle
     {"We were a packet out of Saltmarsh. Gentleman Corwin took her, very politely, and put us ashore with a bow and no water.",
      "He kept the ship's bell. Says he collects them. A whole cabin of bells, from every ship he's taken."},
     {"Our brig ran past the Grey Isle at night. Something on the island opened fire. Not pirates. Guns, big ones, in the rock.",
      "There's no fort on any chart of the Grey Isle. But I saw the flashes. Dozens of them. Somebody built a fortress out there."},
     {"A storm put us on the rocks. Eight of us got ashore. Three of us lasted.",
      "Ships passed the island every day. None of them stopped. They all looked like they were in a hurry to be somewhere else."},
     {"I was a powder monkey on a navy sloop. We went to have a look at the Grey Isle. The sloop didn't come back from looking.",
      "Somebody's buying powder by the ton in the Isles, Captain. Not the navy. The navy's short."}},
    {  // the Tropics: Calypso Vane, the thing under the reef
     {"We were pearl divers. One morning the reef went dark under the boat, like a cloud over the sun. Then the boat went under.",
      "It had arms, Captain. Arms like mainmasts. And it sang. I still hear it when the water's still."},
     {"Calypso Vane's crew took our sloop. She said the sea wanted a gift, and we were it.",
      "She talks to the water, that one. And I swear something talked back."},
     {"A hurricane. Out of a clear sky, in an hour. I hung on to a hen coop for two days.",
      "The old fishermen say the storms never came so fast before this year. Not before the reef started singing."},
     {"I jumped ship. The cook was trying to kill me. It's a long story, and I was right.",
      "Every morning the tide brought in dead fish. Thousands. All of them swimming away from something, I reckon."}},
    {  // the Frozen North: Old Rime, the coil in the ice
     {"We were whalers. The ice closed round us in a night. In the morning there was a coil of something in it, longer than the ship.",
      "It moved, Captain. Under the ice. You could see it pass like a shadow under a window."},
     {"Old Rime found us in a frost fog. He took our furs, our food and our boats.",
      "He wasn't after the cargo. He wanted our soundings, our charts of the ice. Why would a pirate want charts of the ice?"},
     {"Our mast went over in a blizzard. Ice on the rigging three inches thick. She just rolled.",
      "The ice comes further south every year, the whalers say. As if something's pushing it."},
     {"I was put off a ship for card sharping. On an iceberg. People take cards very seriously up here.",
      "Something big swam under my berg one night. The whole thing rocked like a cradle. I didn't sleep much after that."}},
    {  // the Ember Reach: Cinder Kate, the iron beast
     {"I worked the foundry at Cinderport. Then a ship came with no sails at all, smoking like a chimney, and burned our boats.",
      "It was iron, Captain. All of it. A ship of iron, and it floated. I've cast iron my whole life and I don't understand it."},
     {"Cinder Kate set our ship alight for the fun of it. She sang while it burned.",
      "She's working for someone. Kate never cared about coal before, and now she takes every collier in the Reach."},
     {"The mountain blew while we were fishing under it. The sea boiled and the sky rained stones.",
      "Every mountain in the Reach has woken this year. All at once. The old folk say the sea's angry at something."},
     {"Our captain bought cheap coal from a man with no name. It blew up in the hold.",
      "Someone is buying all the coal in the Reach. Shiploads of it. Feeding something big."}},
    {  // the Fog Banks: the Grey Widow, the ship without a crew
     {"A ship came out of the fog with every lantern lit and not a soul on deck. She rammed us, and sailed on.",
      "Her sails were rotten through, Captain. You could see the moon through the holes. And she sailed into the wind."},
     {"The Grey Widow sank us. She stood on her rail and read out our names, as if she was writing them down.",
      "She said her husband was waiting for us all, down below. I think she believes it."},
     {"We followed a light in the fog. We thought it was a lighthouse. It led us onto the rocks.",
      "The lights move out there. They walk over the water. I counted twelve one night, in a line, going west."},
     {"Our crew vanished. One night they were there; in the morning, only me and the cat. The cat won't talk about it.",
      "I found their boots on deck. All lined up, neat as a parade. Not a man in any of them."}},
};
inline const Tale kCaptiveTales[campaign::BIOMES][3] = {
    {{"They took me for the ransom. Gentleman Corwin apologised for the ropes. Then he tied them tighter.",
      "His men talked of a buyer for powder on the Grey Isle. Paying in Spanish silver."},
     {"Pirates took the packet, and me with it, because I can read a chart.",
      "They had me copy a chart of the Isles. Every navy anchorage marked. Someone wants to know where the navy is."},
     {"They took me from the harbour itself. Rowed in at night, bold as you like.",
      "Their captain kept talking of the Admiral. Not ours. He said the word like a prayer."}},
    {{"Calypso Vane took me off a merchantman. She wanted someone to sing to her. I can't sing.",
      "She feeds the reef, Captain. Whole crews. She says the Queen is hungry, and when she's fed she'll rise."},
     {"I came to study the reef. Now the reef's studying me, from what I've seen.",
      "They took a diver's map from us. They wanted to know where the reef sings loudest."},
     {"They kept me in a cage on deck. A good view of the sunsets, I'll give them that.",
      "A ship with black sails came alongside one night. The captain took his hat off to her. Pirates don't take their hats off."}},
    {{"They took me for my charts. The ice charts were all they wanted.",
      "Old Rime's mate said they're charting a road through the ice. For a fleet."},
     {"My ship was fast in the floes. They walked across the ice to take us.",
      "They burned whale oil day and night to keep something warm in the hold. I never saw what. It hissed."},
     {"They raided Iceward for a doctor. I'm the doctor.",
      "I stitched up a man who'd been bitten. The bite was as wide as a barrel. They said the serpent did it, and laughed."}},
    {{"They wanted someone who understands steam. I made the mistake of saying I did.",
      "They're building more than one iron ship, Captain. I saw the plans. A whole fleet."},
     {"They took me off a collier. The coal they kept. Me, they nearly didn't.",
      "The coal goes east, past the last islands, where nobody sails. They call it the Admiral's yard."},
     {"Cinder Kate took me to keep her books. Pirates keep books, it turns out. Terrible ones.",
      "Her ledger had one buyer for everything. No name. Just a mark: a black sail."}},
    {{"They took me from Mournwick. They were afraid of the fog, all of them. Pirates, afraid.",
      "They talked of a herald. A ship with black sails that goes ahead of something, and calls the beasts up from the deep."},
     {"I'm a chaplain. They wanted their ship blessed. I said I would if they let me go. I'm still deciding.",
      "They sail with the dead now, they said. The Dutchman's crew. Something has promised them a way out of the fog."},
     {"They took me from a lighthouse. They put out the light, and took me so nobody would light it again.",
      "Someone wants the lighthouses dark. All of them. So that something can come through the fog unseen."}},
};
// a story's own passengers (a step of the region's story): what they know of its mystery, in place of what they saw
inline const char* const kStoryTale[campaign::BIOMES] = {
    "That fortress on the Grey Isle flies the Admiral's own flag, Captain. I saw it with my own eyes, the night the guns opened up.",
    "The Queen, the divers call it. Calypso feeds it ships, and the Admiral pays her for every one. Something of his is down there with it.",
    "Old Rime was charting the ice for somebody's fleet. He made us sound the channels for him. A road south, he called it.",
    "There's a yard past the last of the Reach, Captain. Iron plate going up on a hull that came out of the sea. A big one. Yours, maybe.",
    "The Widow made a bargain for your drowned, Captain. They crew the Dutchman now. I heard them singing your ship's old songs.",
};
// the last story's omens, darker the more she's told: none yet; one or two; three or four; all five
inline const char* const kOmens[4][4] = {
    {"Five beasts woke in five seas the night you went down, Captain. That's no chance. Someone rang for them.",
     "They say the Admiral's five captains each took something away from that night. Something they guard with their lives.",
     "My gran used to say the deep sleeps until someone calls it. Somebody's calling, I think.",
     "The navy says the Sovereign went down to pirates, with all hands. You don't look like all hands, Captain."},
    {"They say you've taken a piece of something from one of the beasts. Good. The Admiral's people are asking after it in every port.",
     "One of the Five is down. The other four have doubled their watches. They know who's coming.",
     "A bell, they say. Broken in five. I don't know what a bell has to do with monsters, Captain, but the sailors won't stop talking about it.",
     "They talk of you in every port now. The Admiral's spies listen to that talk. Be careful."},
    {"Black sails, Captain. I saw them from the beach. A long line of them, heading east for the Admiral's yard, not a lantern lit.",
     "The pirates are gathering. All of them, even the ones who hate each other. They're flying Ashgrave's flag now.",
     "They say the Admiral has a woman aboard who calls up storms. They say she was your first mate.",
     "The black sails were there every time a beast rose. Watching. Like a shepherd with his flock."},
    {"With the Five broken, the black sails have come out of hiding. They're coming for the bell, Captain. I'd bet my life on it. I'd rather not.",
     "There's a woman with them who calls up storms. I saw her. She stood on the deck and the sky came down to listen. She looked so sad.",
     "Something bigger than all five still sleeps at the end of the world. The beasts were only its dreams. The Admiral wants to wake it.",
     "The Lamplighter on Lantern Rock knows what the bell is for. He's always known. Ask him, Captain, before the light goes out."},
};
// line `n` (0..2) of a passenger's talk: a castaway (captive false) or a captive, from region `biome`, the quest's seed;
// storyStep: the quest is a step of the region's story; told: how many of the five stories she's finished
inline const char* PassengerLine(bool captive, int biome, uint16_t seed, int n, bool storyStep, int told) {
    biome = (biome % campaign::BIOMES + campaign::BIOMES) % campaign::BIOMES;
    const Tale& t = captive ? kCaptiveTales[biome][seed % 3] : kCastawayTales[biome][seed % 4];
    if (n == 0) return t.how;
    if (n == 1) return storyStep ? kStoryTale[biome] : t.saw;
    const int stage = told <= 0 ? 0 : told <= 2 ? 1 : told <= 4 ? 2 : 3;
    return kOmens[stage][(seed / 7 + (unsigned)biome) % 4];
}

// is a captive (campaign::CaptiveName, a story's) a woman? (for the portrait)
inline bool CaptiveIsWoman(const std::string& c) {
    for (const char* w : {"daughter", "wife", "sister", "niece", "heiress", "lady", "widow", "girl", "queen", "mother", "Ruth", "Grace"})
        if (c.find(w) != std::string::npos) return true;
    return false;
}

}  // namespace people
