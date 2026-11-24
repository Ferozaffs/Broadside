# story cast first (Mara, Ashgrave, the officers), the sea's second cast after it (the user asked after Mara, 2026-11-23)
import os
D = os.path.dirname(os.path.abspath(__file__))
p = os.path.join(D, "story_cast.py"); s = open(p, encoding="utf-8").read()
a = 'while not done("sea2.log"):\n    print("waiting for the sea\'s second cast ...", flush=True)'
assert a in s, "story"
s = s.replace(a, 'while not done("cast_idles2.log"):\n    print("waiting for the cast idle run ...", flush=True)')
open(p, "w", encoding="utf-8").write(s)
p = os.path.join(D, "sea2.py"); s = open(p, encoding="utf-8").read()
a = 'open("cast_idles2.log", encoding="utf-8", errors="replace"))'
assert a in s, "sea2"
s = s.replace(a, 'open("story_cast.log", encoding="utf-8", errors="replace"))', 1)
open(p, "w", encoding="utf-8").write(s)
print("ok")
