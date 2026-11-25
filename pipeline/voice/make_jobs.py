"""Every line the game says (lines_ctx.tsv, from broadside --lines-export) with its acting direction: the scene, from
where it's said (the fifth column), and the feeling of that line (feel.tsv, by its index in uniq.tsv; the story's
own lines have theirs written out whole). -> jobs_all.json: [[face, hash, line, direction], ...], the story first,
then the ports, then the sea."""
import json, os

H = os.path.dirname(os.path.abspath(__file__))
SCENE = {
    "hello:harbour": "Speaking face to face to a ship's captain who has just come into the harbour office.",
    "hello:tavern": "Speaking across the bar to a ship's captain who has just walked into the tavern.",
    "hello:wright": "Speaking to a ship's captain in the shipyard, looking her ship over.",
    "special:harbour": "Speaking to a ship's captain whose ship has just limped into harbour badly damaged.",
    "special:tavern": "Telling a ship's captain at the bar that there is no work today.",
    "special:wright": "Telling a ship's captain who can't pay for the work.",
    "warm": "Greeting a ship's captain the whole town now admires, as an old friend.",
    "blacksails": "Leaning over the bar, telling a ship's captain an alarming rumour, voice lowered.",
    "sea:hail": "Called across the water from a merchant ship to a passing captain.",
    "sea:navy": "Called across the water from a navy warship to a passing captain.",
    "sea:distress": "Screamed across the water from a merchant ship under pirate attack, begging for help.",
    "sea:saved": "Called across the water by a merchant captain just rescued from pirates.",
    "sea:castaway": "A castaway just taken off a deserted island, on the rescuing ship's deck.",
    "sea:captive_f": "A prisoner just freed from a captured pirate ship, to the captain who freed her.",
    "sea:captive_m": "A prisoner just freed from a captured pirate ship, to the captain who freed him.",
    "sea:struck": "Shouted across the water by a beaten pirate captain, surrendering in battle.",
    "dread": "A dread pirate captain hailing an old enemy across the water before a battle.",
    "fleet:idle": "Called across from an allied ship sailing in company on a calm sea.",
    "fleet:enemy": "Shouted across from an allied ship as enemy ships appear.",
    "fleet:hurt": "Shouted across from an allied ship that has just been badly hit in battle.",
    "fleet:kill": "Shouted across from an allied ship as an enemy ship sinks.",
    "fleet:joined": "Called across from a ship that has just joined the captain's fleet.",
    "pirate:approach": "Shouted across the water from a pirate ship closing in to attack.",
    "pirate:taunt": "Shouted across the water from a pirate ship in the middle of a cannon battle.",
    "pirate:sinking": "Shouted from a pirate ship as it sinks.",
    "rival": "A rival captain hailing across the water before a duel.",
    "smuggler": "Called across the water from a smuggling boat caught by a warship.",
    "adrift": "Shouted from a crippled boat adrift at sea, to a passing ship.",
    "adrift_thanks": "A survivor just taken off a drifting boat, on deck, to the captain.",
    "tale_how": "A rescued passenger on a quiet voyage home, telling the captain how they came to be there.",
    "tale_saw": "A rescued passenger on a quiet voyage home, telling the captain something strange they saw.",
    "story_tale": "A rescued passenger on a quiet voyage home, confiding something important to the captain.",
    "omen": "A rescued passenger on a quiet voyage home, confiding a rumour to the captain.",
}
STORY = {k: d for _, k, d in []}
test = {line: d for f, line, d in json.load(open(os.path.join(H, "..", "zw", "directed_test.json"), encoding="utf-8"))}
STORY_MORE = {
    "home_gage": "Back aboard after months as a prisoner on the ice, reporting to his captain: quiet, grim, steady, a low fire under it.",
    "home_stroud": "Reunited with his captain after thinking everyone dead: overjoyed, loud, laughing, rough and emotional.",
    "home_fair": "Reunited with her captain after months as a prisoner, handing over her secret log: proud, quick, a little emotional.",
    "home_whit": "Back aboard after being forced to work for the enemy: tired, haunted, quietly determined.",
    "home_pen": "Speaking of her drowned shipmates now crewing a ghost ship: grave, grieving, fiercely determined.",
    "sovereign": "Standing on the deck of their great warship, just recaptured: proud, relieved, gruff, almost laughing.",
    "mara_free": "Just freed after a long captivity, to her captain: tired, warm, a little shaky, then fierce and resolute.",
    "finale": "After the final victory, at the lighthouse at dawn: slow, peaceful, solemn and kind, a quiet blessing.",
}
uniq = {}
for ln in open(os.path.join(H, "uniq.tsv"), encoding="utf-8"):
    i, ctx, _, text = ln.rstrip("\n").split("\t")
    uniq[text] = int(i)
feel = {}
for ln in open(os.path.join(H, "feel.tsv"), encoding="utf-8"):
    if ln.strip():
        i, f = ln.rstrip("\n").split("\t")
        feel[int(i)] = f


def scene_of(ctx):
    for k in (ctx, ":".join(ctx.split(":")[:2]), ctx.split(":")[0]):
        if k in SCENE:
            return SCENE[k]
    raise KeyError(ctx)


jobs, missing = [], []
for ln in open(os.path.join(H, "lines_ctx.tsv"), encoding="utf-8"):
    face, h, mood, text, ctx = ln.rstrip("\n").split("\t")
    if ctx.startswith("scripted:"):
        key = ctx.split(":", 1)[1]
        d = test.get(text) or STORY_MORE.get(key)
        order = 0
    else:
        i = uniq[text]
        if i not in feel:
            missing.append((i, text))
            continue
        d = scene_of(ctx) + " " + feel[i][0].upper() + feel[i][1:] + "."
        order = 1 if ctx.split(":")[0] in ("hello", "special", "warm", "blacksails") else 2
    if not d:
        missing.append((ctx, text))
        continue
    jobs.append((order, face, h, text, d))
jobs.sort(key=lambda j: (j[0], j[1]))
json.dump([[f, h, t, d] for _, f, h, t, d in jobs], open(os.path.join(H, "jobs_all.json"), "w", encoding="utf-8"), indent=0, ensure_ascii=False)
print(len(jobs), "jobs;", len(missing), "missing", missing[:5])
