"""The sea's second cast, four redone (2026-11-24, the user: "dashes should be regenerated looks too modern"):
castaway_7 (the oilskin read as a parka), castaway_8 (a modern chef), captive_f_7 (a denim jacket), smuggler_6 (pea coat
and knitted cap). Same people, their clothes named in the period's own terms. Three seeds each, through comfy_guard
(shares the GPU with the idle ladders: waits for an empty queue)."""
import os, sys
ROOT = os.path.dirname(os.path.abspath(__file__))
os.chdir(ROOT)
sys.path.insert(0, ROOT)
import comfy_guard

ERA = " Early 18th-century Golden Age of Sail dress, hand-sewn natural cloth, nothing modern."
REDO = [
 ("castaway_7", "The character is a castaway whaler woman of the 1720s in her forties, broad-shouldered and weathered, with cropped iron-grey hair and frost-scarred cheeks. She wears a coarse wool shirt, a sleeveless leather jerkin laced at the front, a tattered grey wool cloak pinned at one shoulder, canvas petticoat breeches and a rope belt, and holds a broken wooden harpoon shaft with its iron head raised in her right fist by her shoulder. Stubborn, unbowed."),
 ("castaway_8", "The character is a castaway ship's cook of the 1720s in his fifties, round-faced and bald with a huge black moustache. He wears a full-sleeved linen shirt gone grey and torn, a knotted red neckerchief, wide knee-length canvas sailor's slops and a stained leather apron tied with cord, and holds a dented iron ladle raised in his right fist by his shoulder like a weapon. Exhausted but laughing."),
 ("captive_f_7", "The character is a Chinese navigator captive of the 1720s in her thirties with black hair in a low knot, sharp features and ink-stained fingers. She wears a long dark blue silk robe with a high collar and cloth frog fastenings across the chest, wide sleeves, loose silk trousers and a jade pendant, and holds a rolled paper sea chart tied with red cord raised in her right fist by her shoulder. Calm, calculating, quietly amused."),
 ("smuggler_6", "The character is a roguish Cornish smuggler of the 1720s in his thirties with curly black hair to his collar, a gap-toothed grin and a gold tooth. He wears a loose linen shirt, a red neckerchief, a long brown wool frock coat with big cuffs and brass buttons, a battered black tricorn hat, breeches and stockings, and holds a small wooden brandy cask raised on his right shoulder in his fist. Cheeky, untrustworthy, likeable."),
]
for cid, text in REDO:
    print(f"== {cid}", flush=True)
    comfy_guard.run([sys.executable, "-I", "tools/generate.py", "--style", "amadmir", "--name", cid, "--count", "3",
                     "--character", text + ERA])
print("all done", flush=True)
