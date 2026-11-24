"""Every male word in the game's text (string literals with a space in them), how often, and one example."""
import re, collections, sys
SRC = r'C:\Users\USER\repos\Broadside-modloader\src'
WORDS = set("""he him his himself man men lad lads boy boys fellow fellows sir sirs mister mr master masters king kings lord
lords father fathers son sons brother brothers uncle uncles nephew nephews husband husbands gentleman gentlemen lordship
sire chap chaps bloke fella grandfather grandson baron duke prince monk priest pastor widower bachelor seaman seamen
fisherman fishermen helmsman watchman guardsman hisself daddy papa dad brethren godfather landlord nobleman kinsman
madman madmen grandad granddad lads boyo sonny laddie bucko gents gent mate's lordling squire abbot friar don senor
señor herr monsieur signor dutchman englishman frenchman spaniard dons gentlemanly manly manhood mankind""".split())
cnt = collections.Counter()
ex = collections.defaultdict(list)
for f in ['people.h', 'main.cpp', 'sim.cpp']:
    s = open(SRC + '\\' + f, encoding='utf-8').read()
    for lit in re.findall(r'"((?:[^"\\]|\\.)*)"', s):
        if ' ' not in lit:
            continue
        for w in re.findall(r"[A-Za-z]+", lit):
            if w.lower() in WORDS:
                cnt[w.lower()] += 1
                if len(ex[w.lower()]) < int(sys.argv[1] if len(sys.argv) > 1 else 1):
                    ex[w.lower()].append(lit[:110])
for w, c in cnt.most_common():
    print(f"{c:4} {w:11} | " + "\n                 | ".join(ex[w]))
