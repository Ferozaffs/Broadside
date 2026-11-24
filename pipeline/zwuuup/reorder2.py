# the harbour houses' references after the story cast, before the sea's second cast
import os
D = os.path.dirname(os.path.abspath(__file__))
p = os.path.join(D, "sea2.py"); s = open(p, encoding="utf-8").read()
a = 'open("story_cast.log", encoding="utf-8", errors="replace"))'
assert a in s
s = s.replace(a, 'open("houses1.log", encoding="utf-8", errors="replace"))', 1)
open(p, "w", encoding="utf-8").write(s)
print("ok")
