"""New portraits for two shipwrights, second try (2026-11-22): the first try ("grips ... in her right hand down at her
side") pulled the camera back to the knees - smaller faces, and the Amadmir ink style washed out into a painterly look
unlike the cast (the user; the pipeline itself re-rendered an old pick identically). Now the tool is held up in a fist
by the shoulder, inside the cast's thigh-up framing. First try (2026-11-22, the user): in both picks the tool lies across the shoulders with no
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
                         "belt, and holds a bearded axe upright in her raised right fist, her fingers wrapped firmly "
                         "around the haft just below the head, the axe head beside her shoulder. She smirks, fearless."),
    "saltmarsh_wright": ("The character is a ruggedly handsome young shipwright with tousled sandy-blond hair, a strong "
                         "stubbled jaw and bright blue eyes, sawdust on his tanned muscular forearms. He wears an "
                         "open-necked sweat-dampened linen shirt with the sleeves torn off, a leather jerkin left unlaced "
                         "and a canvas apron, and holds a heavy wooden mallet upright in his raised right fist, his "
                         "fingers wrapped firmly around the handle, the mallet head beside his shoulder. He gives a confident, easy grin."),
}
for cid, text in JOBS.items():
    print(f"== {cid}", flush=True)
    comfy_guard.run([sys.executable, "-I", "tools/generate.py", "--style", "amadmir", "--name", cid, "--count", "4",
                     "--character", text])
print("all done", flush=True)
