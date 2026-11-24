# the story cast now, taking turns with the idle run's re-rolls (comfy_guard waits for an empty queue each time)
import os
D = os.path.dirname(os.path.abspath(__file__))
p = os.path.join(D, "story_cast.py"); s = open(p, encoding="utf-8").read()
a = 'while not done("cast_idles2.log"):'
assert a in s
s = s.replace(a, 'while False:  # (was: after the cast idle run; now taking turns with it)')
open(p, "w", encoding="utf-8").write(s)
print("ok")
