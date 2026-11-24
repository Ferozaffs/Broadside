"""On ZWUUUP (2026-11-24, the user: "lantern_harbour go with hanging on side"): every idle of the Lamplighter pulsed
(dE 13-21, flash 22-47% over 8 seeds) - the glowing lamp held at his chest. New portraits: the lamp hanging from his
hand at his side, its flame turned low, and his eyes a pale sheen rather than a glow. 4 seeds, through comfy_guard."""
import os, sys
ROOT = os.path.dirname(os.path.abspath(__file__))
os.chdir(ROOT)
sys.path.insert(0, ROOT)
import comfy_guard
TEXT = ("The character is a mysterious lighthouse keeper with a weathered face half-hidden under a deep grey oilskin hood, "
        "a short grey beard and pale, watchful eyes with an odd silvery sheen. He wears a heavy grey oilskin coat with brass "
        "buttons and a coil of rope at his waist, and an old brass lamp hangs from one hand down at his side, its flame "
        "turned low. He looks watchful and secretive.")
comfy_guard.run([sys.executable, "-I", "tools/generate.py", "--style", "amadmir", "--name", "lantern_harbour", "--count", "4",
                 "--character", TEXT])
print("all done", flush=True)
