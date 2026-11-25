"""Crew references, fourth batch (2026-11-21): the user on the first generated crew - "Crew should be more interesting
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
    "crew4_f1": ("a gorgeous pirate woman in her late twenties with sun-kissed skin, long wavy dark hair under a red "
                 "bandana and gold hoop earrings: a loose off-white off-the-shoulder blouse with billowing sleeves "
                 "under a fitted black leather corset laced at the front, a crimson sash at the hip, tight dark brown "
                 "leather breeches and tall black over-the-knee boots"),
    "crew4_f2": ("a striking redheaded pirate woman in her twenties with freckles, green eyes and a long braid over one "
                 "shoulder, a black tricorne hat with a red feather: an off-white cropped blouse knotted under the "
                 "bust showing a toned midriff, a brown leather underbust corset belt with brass buckles, fitted "
                 "striped trousers, knee-high brown boots, a dagger sheathed at the hip"),
    "crew4_f3": ("a fierce, beautiful Caribbean pirate woman in her twenties with dark skin, long black braids "
                 "gathered high, gold jewellery and painted tattoos on her arms: an off-white sleeveless top under a "
                 "fitted teal leather vest, a wide belt with a holstered pistol, fitted black breeches and knee-high "
                 "boots"),
    "crew4_m1": ("a ruggedly handsome young pirate in his twenties with tousled dark hair, stubble and a cocky grin: an "
                 "off-white linen shirt open to the waist showing a muscular chest and a gold pendant, a red sash, "
                 "leather bracers, fitted dark trousers tucked into tall boots"),
    "crew4_m2": ("a tall, powerfully muscular pirate with a shaved head, a braided black beard, a gold earring and "
                 "tattoos across his bare arms: an off-white sleeveless shirt open at the chest under a short black "
                 "leather waistcoat ending at the belt, a wide belt with a big brass buckle, dark grey breeches, "
                 "knee-high boots"),
    "crew4_m3": ("a dashing pirate quartermaster in his forties with a salt-and-pepper beard, an eyepatch and a black "
                 "tricorne hat with a white feather: an off-white shirt with a ruffled collar under a short fitted "
                 "burgundy jacket with gold buttons cut at the waist, tan breeches and tall brown boots"),
})
sys.argv = ["make_parts.py", "--parts", *make_parts.PARTS, "--seeds", "21", "42", "63"]
make_parts.main()
print("all done", flush=True)
