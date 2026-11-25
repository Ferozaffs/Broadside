"""Harbour houses, second try for the Temperate and Fog ones (all three first tries stood on a patch of ground).
First: (the user, 2026-11-23: "The harbour houses should also get an upgrade"): the two warehouses on each
port's quay were boxes built in code. One house for each region, in its own style, as TRELLIS references (solid
shapes, thick and chunky); validate with --kind prop. After the story cast's portraits (story_cast.log 'all done'),
before the sea's second cast (sea2.py waits for this log)."""
import os, sys, time
ROOT = os.path.dirname(os.path.abspath(__file__))
os.chdir(ROOT)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import make_parts  # noqa: E402


def done(log):
    try:
        return any(l.startswith("all done") for l in open(log, encoding="utf-8", errors="replace"))
    except OSError:
        return False


while False:
    print("waiting for the story cast ...", flush=True)
    time.sleep(120)

ALONE = ("; the whole building on its own, its foundation ending cleanly at the bottom, standing on nothing: no street, "
         "no quay, no ground, no grass, no base, no people, no boats, no shadow under it; solid chunky shapes, no thin "
         "loose details")
make_parts.PARTS.clear()
make_parts.PARTS.update({
    "house_temperate": ("a two-storey 18th-century harbour warehouse: thick whitewashed rough-stone walls, a steep red "
                        "clay-tile roof, a wide arched wooden double door with iron straps, small square shuttered "
                        "windows, a wooden hoist beam jutting from the gable, a stone chimney" + ALONE),
    "house_fog": ("an 18th-century harbour house on a gloomy northern coast: weathered grey clapboard walls, a sagging "
                  "moss-green shingle roof, boarded-up upper window, a lantern hanging by a heavy door, a crooked brick "
                  "chimney" + ALONE),
})
sys.argv = ["make_parts.py", "--parts", *make_parts.PARTS, "--seeds", "44", "55", "66", "77"]
make_parts.main()
print("all done", flush=True)
