"""merchant_7 redone too (2026-11-24, the user: "Merchant 7 b you can redo as well"; his pick had a wristwatch). The same
man, his dress named in the period's terms, nothing on his wrists but the beads. Three seeds, through comfy_guard."""
import os, sys
ROOT = os.path.dirname(os.path.abspath(__file__))
os.chdir(ROOT)
sys.path.insert(0, ROOT)
import comfy_guard

TEXT = ("The character is a towering Ottoman merchant captain of the 1720s in his fifties with a magnificent black beard shot "
        "with grey, a white turban and kind dark eyes. He wears a long saffron silk kaftan over a striped under-robe, a "
        "gold-embroidered sash wound at the waist with a curved dagger in it, and soft leather slippers, bare wrists with "
        "no jewellery, and holds a string of amber prayer beads raised in his right fist by his shoulder. Warm, dignified."
        " Early 18th-century Golden Age of Sail dress, hand-sewn natural cloth, nothing modern.")
print("== merchant_7", flush=True)
comfy_guard.run([sys.executable, "-I", "tools/generate.py", "--style", "amadmir", "--name", "merchant_7", "--count", "3",
                 "--character", TEXT])
print("all done", flush=True)
