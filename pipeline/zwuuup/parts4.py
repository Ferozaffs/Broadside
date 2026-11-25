"""Fourth batch of parts (2026-11-21): the kraken figurehead again (TRELLIS shredded the first: loose curling
tentacles), a treasure chest (the wrecks' sea-chests), a single barrel."""
import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "tools"))
import make_parts  # noqa: E402

make_parts.PARTS.update({
    "figure_kraken2": ("a carved wooden ship's figurehead of a kraken, carved from one solid block of oak: a great domed "
                       "octopus head with heavy-lidded glaring eyes, its thick tentacles wrapped tightly round and pressed "
                       "flat against a curved timber beam, nothing loose or thin, painted deep purple and teal with gilded "
                       "suckers"),
    "prop_chest": ("a pirate's treasure chest: a wooden chest with a domed lid bound with iron bands and rivets, a heavy "
                   "lock at the front, the lid shut, a few gold coins on the ground-level base around it"),
    "prop_barrel": "a single sturdy wooden barrel bound with three iron hoops, standing upright, a cork bung in its side",
})
sys.argv = ["make_parts.py", "--parts", "figure_kraken2", "prop_chest", "prop_barrel", "--seeds", "11", "22", "33"]
make_parts.main()
