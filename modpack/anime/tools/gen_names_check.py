"""The names the game puts together itself (src/sim.cpp: OfficerName, PirateName, CaptiveName) through the pack's word
swap (swap.py's T, the game's own swap in Python): anything still a man's name or word is printed. The --lines-export
the no-men hunt read doesn't hold these (they're made at run time from tables), so they're checked here.
  python -I gen_names_check.py [path/to/src/sim.cpp]"""
import os, re, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from swap import T

src = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "..", "..", "src", "sim.cpp")
code = open(src, encoding="utf-8").read()


def table(fn, var="k"):
    m = re.search(fn + r"\(uint16_t seed\) \{(.*?)\n\}", code, re.S)
    assert m, fn
    out = {}
    for t in re.finditer(r"static const char\* const (\w+)\[\d+\] = \{(.*?)\};", m.group(1), re.S):
        out[t.group(1)] = re.findall(r'"([^"]*)"', t.group(2))
    return out


MEN = re.compile(r"\b(he|him|his|himself|man|men|boy|lad|lads|son|nephew|brother|father|uncle|husband|king|lord|sir|"
                 r"mister|gentleman|chaplain|parson|baron|Ezra|Tobias|Silas|Jonah|Amos|Caleb|Isaac|Gideon|Jack|Tom|"
                 r"Morgan|Ned|Hob|Jim|Hal|Piet|Billy|Bart|Hugo|Sam|Jonas|Rupert|Scratch)\b")
ALLOW = {"Red Morgan", "Old Scratch", "the parson's niece", "a ship's chaplain"}  # (a woman's name, the devil, and roles a woman holds in the pack)
problems = []
for fn in ("OfficerName", "PirateName", "CaptiveName"):
    for var, names in table(fn).items():
        for n in names:
            t = T(n)
            m = MEN.search(t)
            if m and n not in ALLOW:
                problems.append(f"{fn}.{var}: {n!r} -> {t!r}  ('{m.group(0)}')")
print("\n".join(problems) or "ok: no men in the generated names")
