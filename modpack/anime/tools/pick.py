"""Record picks: python -I pick.py id=A id=C ... (A 1001, B 2002, C 3003)."""
import json, sys
import os
p = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'anime_picks.json')
d = json.load(open(p, encoding='utf-8'))
for a in sys.argv[1:]:
    cid, l = a.split('=')
    l, _, tag = l.partition(':')  # (id=B:r1 - a redo's render)
    seeds = (1001, 2002, 3003) if not tag else (4004, 5005, 6006)
    d[cid] = {"seed": seeds["ABC".index(l.upper())], "dir": "anime_cast", "tag": tag or "wf"}
json.dump(d, open(p, 'w', encoding='utf-8'), indent=1)
print(len(d) - 1, 'picked')
