"""Biome props, first batch (2026-11-21): the islands' trees are procedural cones and palms built in code (scenery.cpp),
the Ember Reach has none and no biome has rocks. A test for two biomes - the Tropics (palms, banana plants,
boulders) and the Ember Reach (charred trees, lava rocks, obsidian spires) - before the rest. TRELLIS is good at solid
shapes and loses thin, wispy ones, so the foliage is asked for broad, thick and chunky. Validate with --kind prop."""
import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "tools"))
import make_parts  # noqa: E402

make_parts.PARTS.clear()
make_parts.PARTS.update({
    "tree_palm": ("a single tropical coconut palm tree: a gently curved ringed trunk and a dense crown of broad, thick, "
                  "stylized fronds drooping in a full rounded shape, a cluster of coconuts under the crown; solid chunky "
                  "leaves, not thin or wispy"),
    "plant_banana": ("a tropical banana plant: a clump of short green trunks with large, broad, thick paddle-shaped "
                     "leaves arching out in a full rounded bush; solid chunky leaves, not thin or wispy"),
    "rock_sandstone": "a cluster of three rounded, weathered tan sandstone boulders of different sizes, touching",
    "tree_charred": ("a dead charred tree from a volcanic island: a black burnt twisted trunk with a few thick broken "
                     "branches and no leaves, glowing orange embers in the deep cracks of the bark"),
    "rock_lava": "a cluster of jagged black volcanic lava rocks with glowing orange-red molten cracks running through them",
    "spire_obsidian": ("a tall jagged spire of glossy black obsidian rock, sharp faceted shards rising from a heavy "
                       "rocky base, faint purple sheen"),
})
sys.argv = ["make_parts.py", "--parts", *make_parts.PARTS, "--seeds", "13", "26", "39"]
make_parts.main()
print("all done", flush=True)
