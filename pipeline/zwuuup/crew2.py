"""Crew references, second batch: crew1's arms hung only 10-20 degrees out (Flux's idea of an A-pose); here the pose
is spelled out, and the view is the pipeline's standard three-quarter one (docs/3D_PIPELINE.md: characters keep the
framing, plus an A-pose; crew1's front views fail G4). (2026-11-21, the user: a complete generated crew, the women from the sailors' parts don't look good):
full-body sailors in the game skeleton's A-pose (arms 45 degrees down, as assets/crew/sailor*.glb's rest pose), front
view, for TRELLIS.2 then Broadside's tools/rig_crew.py (skinned onto that skeleton, its idle/run/work kept).
Clothes cut close (breeches, waistcoats, short jackets: no long coats or skirts, which the leg bones would tear) and an
off-white shirt on everyone (the game tints the shirt in the team colour). Validate with --kind character."""
import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "tools"))
import make_parts  # noqa: E402

make_parts.FRAMING = (
    "semi-stylized 3D game character model of {part}. Realistic human proportions, hand-painted textures: clean "
    "painterly colours, simplified surface detail, crisp readable shapes, no grime. Full body from the top of the head "
    "to the soles of the feet, standing straight in a rigging A-pose like a 3D character turnaround sheet: both arms "
    "raised out to the sides, halfway between hanging down and a T-pose, each arm straight and held 45 degrees out "
    "from the body with a wide gap of air between the arm and the torso, the hands open, palms down, far away from the "
    "hips; legs straight and apart at shoulder width, feet flat. "
    "Seen from the front-left at a 45-degree angle, slightly from above (the pipeline's framing for every asset), "
    "symmetrical. Empty hands, no weapons, nothing held, nothing "
    "in front of the body. Soft flat even lighting with no cast shadows and no reflections. One single character, "
    "complete, centered with margin around it, standing on nothing. Plain light grey seamless background, no ground.")

make_parts.PARTS.clear()
make_parts.PARTS.update({
    "crew_f1": ("a young woman pirate deckhand in her twenties: a loose off-white linen shirt with the sleeves rolled to "
                "the elbow, a red sash tied at the waist, dark brown breeches tucked into knee-high black boots, a red "
                "bandana over long dark hair tied back in a braid"),
    "crew_f2": ("a woman pirate sailor in her thirties: an off-white linen shirt under a fitted brown leather waistcoat, "
                "a wide belt with a brass buckle, grey striped canvas trousers, low brown boots, short auburn hair under a "
                "black tricorne hat"),
    "crew_f3": ("a woman pirate gunner in her twenties: an off-white linen shirt under a dark blue sleeveless jerkin laced "
                "at the front, a leather strap across the chest, black breeches, knee-high brown boots, blonde hair in a "
                "high ponytail under a green headscarf"),
    "crew_m1": ("a young male pirate sailor in his twenties: a loose off-white linen shirt, a blue neckerchief, tan canvas "
                "trousers rolled to mid-calf, low black shoes, a red knitted cap, a short brown beard"),
    "crew_m2": ("a burly male pirate in his forties: an off-white linen shirt with the sleeves rolled up, a black leather "
                "waistcoat, a wide belt, dark grey breeches, knee-high boots, a black bandana, a thick black beard"),
    "crew_m3": ("an older male pirate boatswain in his fifties: an off-white linen shirt, a short dark green jacket open at "
                "the front, tan breeches, brown boots, a brown tricorne hat, a grey beard and a grey ponytail"),
})
sys.argv = ["make_parts.py", "--parts", *make_parts.PARTS, "--seeds", "44", "55", "66"]
make_parts.main()
print("all done", flush=True)
