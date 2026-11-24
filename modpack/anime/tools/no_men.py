"""Any man left in the pack's text: every voiced line and every string in the game, through the pack (swap.T), searched
for male words and male names."""
import os, re, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import swap  # noqa: E402
MALE = set("""he him his himself man men lad lads boy boys fellow fellows fella sir sirs mister mr master masters king kings
lord lords father fathers son sons brother brothers uncle uncles nephew nephews husband husbands gentleman gentlemen
lordship sire chap chaps bloke grandfather grandson baron duke prince monk priest pastor widower bachelor
daddy papa dad brethren godfather landlord nobleman kinsman madman madmen gents gent squire abbot friar don
dutchman englishman frenchman midshipman seaman seamen fisherman fishermen helmsman watchman guardsman boatswain
davy jones scratch nick satan devil merman mermen bosun's""".split())
NAMES = set("""Tom Ned Jack Bart Gus Pete Joe Finn Hob Jim Hal Piet Billy Hugo Jonas Silas Rupert Sam Morgan Kit Barnaby
Jonah Elias Hector Ezekiel Tomas Alvaro Kofi Bjorn Olav Gunnar Corvin Corwin Ignatius Absalom Caspar Hollis Edmund
Rufus Thomas Ezra Bartholomew Tobias Peter Amos Ashgrave Rime Vex Davy""".split())
seen = {}
texts = [l.split('\t')[3] for l in open(os.path.join(HERE, 'lines.tsv'), encoding='utf-8')] + swap.literals()
for t in texts:
    if '(' in t and ')' in t and ';' in t:
        continue  # (code the regex caught)
    o = swap.T(t)
    hits = [w for w in re.findall(r"[A-Za-z]+", o) if w.lower() in MALE or w in NAMES]
    if hits:
        for w in hits:
            seen.setdefault(w if w in NAMES else w.lower(), []).append(o)
for w, ex in sorted(seen.items(), key=lambda kv: -len(kv[1])):
    uniq = list(dict.fromkeys(ex))
    print(f"{len(uniq):3} {w:12} | " + "\n                 | ".join(u[:120] for u in uniq[:4]))
