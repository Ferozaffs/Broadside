"""Crew references, fifth batch: crew4_m3 again - all three of crew4's quartermasters wore a knee-length coat
(the coat-tails would stretch with the leg bones); here a waistcoat, no coat. Fourth batch (2026-11-21): the user on the first generated crew - "Crew should be more interesting
and sexier/hotter". Attractive, characterful pirates in a stylized-realistic look (crew2's semi-stylized framing gave
cute, plain deckhands), still rig-friendly: the skeleton's A-pose spelled out, front view (the user allowed it for
rigged characters), clothes cut close (corsets, fitted breeches, short waistcoats: no long coats or skirts), and a
light, off-white garment on everyone for the game's team tint. Validate with --kind character."""
import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "tools"))
import make_parts  # noqa: E402

make_parts.FRAMING = (
    "stylized-realistic 3D game character model of {part}. Attractive adult proportions, an appealing, confident face "
    "full of character, like a hero character in a modern AAA pirate adventure game; detailed hand-painted textures "
    "with clean readable shapes. Full body from the top of the head to the soles of the feet, standing straight in a "
    "rigging A-pose like a 3D character turnaround sheet: both arms raised out to the sides, halfway between hanging "
    "down and a T-pose, each arm straight and held 45 degrees out from the body with a wide gap of air between the arm "
    "and the torso, the hands open, palms down, far away from the hips; legs straight and apart at shoulder width, "
    "feet flat. Facing the viewer, front view. Empty hands, nothing held, weapons only sheathed or holstered. Soft "
    "flat even lighting with no cast shadows and no reflections. One single character, complete, centered with margin "
    "around it, standing on nothing. Plain light grey seamless background, no ground.")

make_parts.PARTS.clear()
make_parts.PARTS.update({
    "crew4_m3": ("a dashing pirate quartermaster in his forties with a salt-and-pepper beard, an eyepatch and a black "
                 "tricorne hat with a white feather: an off-white shirt with a ruffled collar and billowing sleeves "
                 "under a fitted burgundy waistcoat with gold buttons that ends at the belt, tan breeches and tall "
                 "brown boots; no coat, no jacket, nothing hanging below the belt"),
})
sys.argv = ["make_parts.py", "--parts", *make_parts.PARTS, "--seeds", "84", "105", "126", "147"]
make_parts.main()
print("all done", flush=True)
