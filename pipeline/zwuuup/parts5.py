"""Fifth batch of parts (2026-11-21): the chest without coins (parts4's coins and sand would fuse into a base), and
the kraken figurehead in the lion's wording (parts4's came out as cartoon octopuses, not carved wood)."""
import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "tools"))
import make_parts  # noqa: E402

make_parts.PARTS.update({
    "prop_chest2": ("a pirate's sea chest: a wooden chest with a domed lid bound with iron bands and rivets, a heavy iron "
                    "lock plate at the front, rope handles at the ends, the lid shut. Only the chest: nothing around it, "
                    "no coins, no ground"),
    "figure_kraken3": ("a carved wooden ship's figurehead of a kraken: the domed head of a giant octopus with heavy brows "
                       "and glaring gilded eyes, its thick tentacles carved in low relief wrapped tightly along a short "
                       "curved timber beam that would fix it to a ship's bow, painted deep purple and teal and gilded, "
                       "carved wood with visible chisel marks, in the style of a lion or eagle figurehead"),
})
sys.argv = ["make_parts.py", "--parts", "prop_chest2", "figure_kraken3", "--seeds", "44", "55", "66", "77"]
make_parts.main()
