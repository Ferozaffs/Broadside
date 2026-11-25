"""New portraits for two shipwrights (2026-11-22, the user): in both picks the tool lies across the shoulders with no
hand on it ("she doesnt even hold it") and Wan warps it; the user isn't a fan of Frostholm Wright's second portrait.
The cast text unchanged except the tool, now gripped in a hand at the side (on the shoulder, Flux drops the hand)."""
import os, subprocess, sys
ROOT = os.path.dirname(os.path.abspath(__file__))
os.chdir(ROOT)
sys.path.insert(0, ROOT)
import comfy_guard

JOBS = {
    "frostholm_wright": ("The character is a beautiful, muscular young Nordic shipwright woman with shaved sides, a long "
                         "platinum-blonde braided topknot, ice-blue eyes and a few frost-reddened freckles. She wears a "
                         "sleeveless fur-trimmed leather tunic that shows her strong arms, wrist wraps and a thick tool "
                         "belt, and grips a bearded axe in her right hand down at her side, her fingers wrapped firmly "
                         "around the haft. She smirks, fearless."),
    "saltmarsh_wright": ("The character is a ruggedly handsome young shipwright with tousled sandy-blond hair, a strong "
                         "stubbled jaw and bright blue eyes, sawdust on his tanned muscular forearms. He wears an "
                         "open-necked sweat-dampened linen shirt with the sleeves torn off, a leather jerkin left unlaced "
                         "and a canvas apron, and grips a heavy wooden mallet in his right hand down at his side, his "
                         "fingers wrapped firmly around the handle. He gives a confident, easy grin."),
}
for cid, text in JOBS.items():
    print(f"== {cid}", flush=True)
    comfy_guard.run([sys.executable, "-I", "tools/generate.py", "--style", "amadmir", "--name", cid, "--count", "4",
                     "--character", text])
print("all done", flush=True)
