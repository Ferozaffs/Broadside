"""Crew references (2026-11-21, the user: a complete generated crew, the women from the sailors' parts don't look good):
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
    "to the soles of the feet, standing straight in a neutral A-pose: both arms held straight, down and out at 45 "
    "degrees away from the body, hands open with the fingers together, legs straight and slightly apart, feet flat. "
    "Facing the viewer directly, front view at eye level, symmetrical. Empty hands, no weapons, nothing held, nothing "
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
sys.argv = ["make_parts.py", "--parts", *make_parts.PARTS, "--seeds", "11", "22", "33"]
make_parts.main()
print("all done", flush=True)
