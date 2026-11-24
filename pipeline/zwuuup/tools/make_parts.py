"""Reference images for kitbash PARTS (TRELLIS.2 input): bare hulls with no rig, figureheads, deck and world props.

The idea (2026-11-21): TRELLIS is good at solid shapes and bad at thin ones (rigging, sail cloth, flags). So make
the solid parts here, and build masts, sails and rigging in the game / Blender, where they stay crisp and the game
can sway and paint the sails.

  python -I tools/make_parts.py --list
  python -I tools/make_parts.py --parts hull_brig figure_mermaid --seeds 11 22 33
  python -I tools/make_parts.py --parts all --count 3

Files: assets/parts/<part>/<part>_semi_s<seed>.png, logged to generation_log.jsonl (kind: part_3d_source).
Validate each one (tools/validate_ref.py, --kind prop) before tools/make_3d.py.
"""
import argparse, datetime, json, os, random, shutil, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from generate import wait_idle, render, COMFY_OUTPUT  # noqa: E402
from make_ships import flux  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

HULL = ("the bare wooden hull of {what}, with NO masts, NO sails, NO rigging and NO flags at all: only short round "
        "mast stumps cut off just above the deck. The deck is visible with its railings, hatches, a ship's wheel and "
        "cannons at the rails")
PARTS = {
    # hulls (the rig is built separately)
    "hull_cutter": HULL.format(what="a small 18th-century cutter about 20 metres long, a low sleek hull painted black "
                                    "with a yellow stripe along the gunwale, a long bowsprit stump, a tiller at the stern"),
    "hull_brig": HULL.format(what="an 18th-century two-masted brig about 30 metres long, a hull painted dark blue with a "
                                  "single row of gun ports with red insides, ochre-yellow wales, a carved bow with a "
                                  "bowsprit stump and a small square stern with windows"),
    "hull_galleon": HULL.format(what="a 17th-century galleon about 45 metres long with a tall forecastle and a towering "
                                     "carved sterncastle with gilded balconies and stern windows, two rows of gun ports, a "
                                     "hull painted deep red and black with gold trim"),
    "hull_ghost": HULL.format(what="a derelict haunted frigate: rotting pale grey timbers, gaping holes in the planking, "
                                   "broken railings, barnacles and hanging seaweed, an eerie green glow from the gun ports"),
    # figureheads (mount on the bow; the Shipwright's parts)
    "figure_mermaid": "a carved wooden ship's figurehead of a mermaid with flowing hair, her arms crossed over her chest, "
                      "her tail curling beneath her, painted in sea green and pale skin tones with gilded scales, mounted "
                      "on a short curved timber beam that would fix it to a ship's bow",
    "figure_lion": "a carved wooden ship's figurehead of a roaring crowned lion, its front paws raised, painted red and "
                   "gilded, mounted on a short curved timber beam that would fix it to a ship's bow",
    "figure_eagle": "a carved wooden ship's figurehead of a fierce eagle with its wings swept back along the sides, "
                    "painted white and brown with a gilded beak and talons, mounted on a short curved timber beam that "
                    "would fix it to a ship's bow",
    "figure_kraken": "a carved wooden ship's figurehead of a kraken's head with thick tentacles curling round a timber "
                     "beam, dark purple and teal paint with gilded suckers and glaring yellow eyes",
    # props (deck and harbour)
    "prop_cannon": "a single 18th-century naval cannon on a wooden truck carriage with four small wheels, black iron "
                   "barrel, ropes and a rammer leaning against it",
    "prop_cargo": "a neat stack of cargo on a dock: three wooden barrels, two crates with rope handles and a bulging "
                  "canvas sack, on a small wooden pallet",
    "prop_anchor": "a large iron ship's anchor with a wooden stock and a coil of heavy rope at its ring, standing upright",
}

FRAMING = ("semi-stylized 3D game asset of {part}. Realistic historical proportions and construction, but hand-painted "
           "textures: clean painterly colours, simplified surface detail, crisp readable shapes, no grime, scratches or "
           "photographic texture. Soft flat even lighting with no cast shadows, no reflections and no specular "
           "highlights. One single object, complete, nothing cut off, seen from the front-left at a 45-degree angle, "
           "slightly from above, centered with margin around it, standing on nothing. Plain light grey seamless "
           "background, no water, no ground, no people.")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--parts", nargs="+", default=["all"])
    ap.add_argument("--seeds", type=int, nargs="*")
    ap.add_argument("--count", type=int, default=1)
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--port", default="8188")
    a = ap.parse_args()
    if a.list:
        print("\n".join(f"{k:16} {v}" for k, v in PARTS.items()))
        return
    base = f"http://127.0.0.1:{a.port}"
    parts = list(PARTS) if a.parts == ["all"] else a.parts
    seeds = a.seeds or [random.randint(1, 2**31) for _ in range(a.count)]
    for part in parts:
        for seed in seeds:
            desc = PARTS[part]
            wf = flux(FRAMING.format(part=desc), False)
            wf["11"]["inputs"]["filename_prefix"] = "Parts/semi"
            wait_idle(base)
            status, secs, images, errors = render(base, wf, seed)
            out_dir = os.path.join(ROOT, "assets", "parts", part)
            os.makedirs(out_dir, exist_ok=True)
            files = []
            for img in images[:1]:
                dst = os.path.join(out_dir, f"{part}_semi_s{seed}.png")
                shutil.copy2(os.path.join(COMFY_OUTPUT, img["subfolder"], img["filename"]), dst)
                files.append(os.path.relpath(dst, ROOT).replace("\\", "/"))
            entry = {"time": datetime.datetime.now().isoformat(timespec="seconds"), "kind": "part_3d_source",
                     "model": "semi", "name": part, "seed": seed, "description": desc, "status": status,
                     "seconds": round(secs), "files": files, "errors": errors}
            with open(os.path.join(ROOT, "generation_log.jsonl"), "a", encoding="utf-8") as f:
                f.write(json.dumps(entry, ensure_ascii=False) + "\n")
            print(f"{status} {part} seed={seed} {secs:.0f}s -> {', '.join(files) or errors}", flush=True)


if __name__ == "__main__":
    main()
