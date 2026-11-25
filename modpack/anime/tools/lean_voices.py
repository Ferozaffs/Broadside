"""The anime pack's voices, leaning into the Japanese persona each was given (the user, 2026-11-24: "New voices, too
lean into the japanese persona you assigned them"): who she is (her role, from the draft) + her archetype's delivery,
as an anime seiyuu would give it, tics and all. No Western accents: the persona is the voice."""
import os, re
HERE = os.path.dirname(os.path.abspath(__file__))
P = os.path.join(HERE, 'anime', 'voices.tsv')
DELIVERY = {
    'tsundere': "a bright, sharp, high voice that snaps and huffs (\"hmph!\", \"it's not like I did it for you!\"), "
                "pouting and flustered the moment she's shown kindness; the classic tsundere heroine",
    'kuudere': "a cool, soft, flat near-monotone, few words and no wasted breath, feeling only in the smallest pauses; "
               "the classic kuudere",
    'onee-san': "a mature but clearly feminine young woman's voice, a smooth, warm, alluring alto, teasing and sure of herself, big-sister charm and a knowing chuckle (\"fufu\")",
    'genki': "a loud, bouncy, high-energy voice, everything shouted with excitement, bright laughter, never still; "
             "the classic genki girl",
    'ara-ara': "a gentle, honeyed, motherly voice, slow and warm, a soft sighing \"ara ara~\" and an indulgent laugh",
    'yandere': "a sweet, airy, adoring voice that drops to a soft, chillingly calm whisper, a little giggle in the "
               "wrong places",
    'sukeban': "a rough, low, cocky delinquent girl's voice, clipped and street-tough, a scoffing laugh, looking for "
               "a fight",
    'dandere': "a small, soft, shy voice, hesitant and breathy, stammering (\"u-um...\") and trailing off",
    'bokukko': "a bright, clear young woman's tomboy voice, a girl's voice that is casual and blunt, cheerful and sure of herself",
    'ojou-sama': "a haughty, refined, high-class young lady's voice with a ringing \"ohohoho!\" laugh, theatrical "
                 "and proud",
    'sadodere': "a silky, amused, teasing voice that enjoys every bit of your discomfort, a slow smirking drawl",
    'yamato nadeshiko': "a graceful, soft, polite and elegant voice, calm and refined, the ideal gentle lady",
    'deredere': "a sweet, warm, bubbly voice, openly affectionate and cheerful, all smiles",
    'loli-baba': "a small, girlish, high voice that speaks like an ancient sage, slow and archaic, amused by the "
                 "young",
    'mesugaki': "a bratty, smug, high teasing voice, mocking and giggling, taunting you for being so weak",
    'gyaru': "a bubbly, casual, trendy party-girl voice, sing-song and flirty, laughing at everything",
    'kamidere': "a deep, regal, imperious voice of absolute certainty, slow and grand, as if the sea were already "
                "hers and she a goddess over it",
    'chuunibyou': "a young woman's high, theatrical girl's voice, declaiming every shot like a dark-magic spell, cackling and grandiose",
}
FEM = " Unmistakably a woman's voice, clearly feminine, never deep, gruff or masculine."  # (the user: onee-san, bokukko, chuunibyou came out male)
ACCENT = re.compile(r",? (?:with )?(?:a )?(?:hint of |light |strong )?(?:a )?(?:Scandinavian|Caribbean|Spanish|Ghanaian|"
                    r"Portuguese|Nigerian|Italian|Turkish|North African|Malay|French|Chinese|East Asian|Basque Spanish|"
                    r"Scottish)(?: lilt| accent)?", re.I)
rows = [l.rstrip('\n').split('\t') for l in open(P, encoding='utf-8') if l.strip() and not l.startswith('#')]
out = ['# The anime pack\'s voices: id TAB archetype TAB the voice (Qwen3-TTS VoiceDesign, then the directed lines).\n',
       '# Every one a woman, voiced like a Japanese anime seiyuu in her archetype (the user: "lean into the japanese\n',
       '# persona"); the character the game gave her kept as who she is. No Western accents: the persona is the voice.\n']
for i, a, d in rows:
    who = d.split(':')[0].strip()
    who = ACCENT.sub('', who).strip().rstrip(',')
    if i == 'narrator':
        who = 'An older woman storyteller, telling an old sea story by a fire, unhurried and intimate'
    if i == 'fleet_7':
        who = 'Captain Dong Bang, a woman in her early thirties, dry and thoughtful, with a touch of wry humour'
    out.append(f"{i}\t{a}\t{who}. Voiced like a Japanese anime seiyuu, a {a}: {DELIVERY[a]}.{FEM if a in ("onee-san", "bokukko", "chuunibyou") else ""}\n")
open(P, 'w', encoding='utf-8', newline='\n').writelines(out)
print(len(rows), 'voices')
for l in out[3:9]:
    print(l.rstrip()[:240])
left = [l for l in out if ACCENT.search(l.split('\t')[-1].split('. Voiced')[0])]
print('accents left:', len(left))
