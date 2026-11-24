"""Reproduction test (2026-11-22): the new shipwright portraits look unlike the cast (the user). Re-render Frostholm
Wright's original pick - seed 803926554, its original text - under a test name: the same image means the pipeline is
unchanged (and the prompt change did it); a different one means something changed (ComfyUI was restarted 11-21)."""
import os, sys
ROOT = os.path.dirname(os.path.abspath(__file__))
os.chdir(ROOT)
sys.path.insert(0, ROOT)
import comfy_guard

TEXT = ("The character is a beautiful, muscular young Nordic shipwright woman with shaved sides, a long platinum-blonde "
        "braided topknot, ice-blue eyes and a few frost-reddened freckles. She wears a sleeveless fur-trimmed leather "
        "tunic that shows her strong arms, wrist wraps and a thick tool belt, and holds a bearded axe resting against "
        "her shoulder. She smirks, fearless.")
comfy_guard.run([sys.executable, "-I", "tools/generate.py", "--style", "amadmir", "--name", "test_repro_fw",
                 "--seeds", "803926554", "--character", TEXT])
print("all done", flush=True)
