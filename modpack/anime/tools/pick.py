"""Record picks: python -I pick.py id=A id=C ... (A 1001, B 2002, C 3003)."""
import json, sys
import os
p = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'anime_picks.json')
d = json.load(open(p, encoding='utf-8'))
for a in sys.argv[1:]:
    cid, l = a.split('=')
    l, _, tag = l.partition(':')  # (id=B:r1 - a redo's render)
    if l.isdigit():  # (id=7007:r2 - a seed given outright)
        seed = int(l)
    else:
        seeds = (1001, 2002, 3003) if not tag else (4004, 5005, 6006) if tag == "r1" else None
        assert seeds, "for this redo give the seed: id=<seed>:<tag>"
        seed = seeds["ABC".index(l.upper())]
    d[cid] = {"seed": seed, "dir": "anime_cast", "tag": tag or "wf"}
json.dump(d, open(p, 'w', encoding='utf-8'), indent=1)
print(len(d) - 1, 'picked')
