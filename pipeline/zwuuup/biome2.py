"""Biome props, second batch: the palms, banana plants and charred trees of the first came with a disc of sand,
grass or ground under them (G3), the lava rocks touched the frame edge, and the charred trees' twigs were too thin for TRELLIS. First batch (2026-11-21): the islands' trees are procedural cones and palms built in code (scenery.cpp),
the Ember Reach has none and no biome has rocks. A test for two biomes - the Tropics (palms, banana plants,
boulders) and the Ember Reach (charred trees, lava rocks, obsidian spires) - before the rest. TRELLIS is good at solid
shapes and loses thin, wispy ones, so the foliage is asked for broad, thick and chunky. Validate with --kind prop."""
import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "tools"))
import make_parts  # noqa: E402

make_parts.PARTS.clear()
NOGROUND = ("; the trunk ends cleanly at its root flare, floating on nothing: no sand, no soil, no grass, no ground, "
            "no base, no shadow disc under it")
make_parts.PARTS.update({
    "tree_palm": ("a single tropical coconut palm tree: a gently curved ringed trunk and a dense crown of broad, thick, "
                  "stylized fronds drooping in a full rounded shape, a cluster of coconuts under the crown; solid chunky "
                  "leaves, not thin or wispy" + NOGROUND),
    "plant_banana": ("a tropical banana plant: a clump of short green trunks with large, broad, thick paddle-shaped "
                     "leaves arching out in a full rounded bush; solid chunky leaves, not thin or wispy" + NOGROUND),
    "rock_lava": ("a compact cluster of jagged black volcanic lava rocks with glowing orange-red molten cracks running "
                  "through them, small in the frame and centred with a wide margin on every side; no ground disc, no "
                  "pool of lava, no base under it"),
    "tree_charred": ("a dead charred tree from a volcanic island: a black burnt twisted trunk and five or six thick, "
                     "stubby broken-off branches, no twigs and no leaves, glowing orange embers in the deep cracks of the "
                     "bark; heavy chunky shapes" + NOGROUND),
})
sys.argv = ["make_parts.py", "--parts", *make_parts.PARTS, "--seeds", "52", "65", "78"]
make_parts.main()
print("all done", flush=True)
