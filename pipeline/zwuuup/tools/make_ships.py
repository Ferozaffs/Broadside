"""Generate ship concept images meant as TRELLIS.2 (image-to-3D) input, on the user's running ComfyUI.

Examples (run from the PirateGame-ArtPipeline folder):
  python -I tools/make_ships.py --models flux wan --ships carrack junk --seeds 11 22
  python -I tools/make_ships.py --models flux --ships all --count 2
  python -I tools/make_ships.py --list

Every image frames ONE whole ship out of the water (hull and keel visible), three-quarter view, plain grey
background, soft even light: that is what TRELLIS.2 reconstructs best. Files go to
assets/ships/<ship>/<ship>_<model>_s<seed>.png and each render is appended to generation_log.jsonl.
"""
import argparse, datetime, json, os, random, shutil, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from generate import api, wait_idle, render, COMFY_OUTPUT  # noqa: E402
from make_3d import ui_to_api, COMFY  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

SHIPS = {
    # historical, 15th century
    "carrack": "a 15th-century Portuguese carrack with a towering forecastle and sterncastle, three masts with square sails painted with a red cross, weathered oak planking, rope rigging",
    "caravel": "a small 15th-century caravel with two lateen-rigged masts and triangular cream sails, low sleek hull of tarred planks, a carved wooden sternpost",
    "cog": "a 14th-century Hanseatic cog, broad clinker-built hull of overlapping planks, a single mast with one large square sail striped red and white, crenellated wooden castles at bow and stern",
    "treasure_junk": "a Ming dynasty Chinese treasure junk: a broad flat-bottomed hull with a square blunt bow and a tall stern castle, five masts carrying stiff fan-shaped junk sails of red-brown matting ribbed with horizontal bamboo battens, a painted eye on the bow, red and black lacquer with a carved dragon on the stern",
    "dhow": "a 15th-century Arab dhow with a long raked hull of teak, two masts with huge lateen sails, stitched planks, a carved tall stern",
    "galley": "a 15th-century Venetian war galley: a very long, low, narrow hull with twenty long wooden oars sticking out along each side, one mast with a triangular lateen sail, a striped awning over the stern, a gilded lion of Saint Mark on the stern and a bronze ram at the bow",
    # later age of sail
    "galleon": "a 16th-century Spanish treasure galleon with four masts, square sails, a richly gilded and carved high stern with lanterns and stained-glass windows, dark varnished hull with gun ports",
    "cutter": "an 18th-century revenue cutter: a small fast boat with only ONE single mast, one huge gaff mainsail, a topsail and two jibs on a very long horizontal bowsprit, low black hull with a yellow stripe and four small cannons",
    "xebec": "a Barbary corsair xebec with three masts and lateen sails, a slim hull with a sharply overhanging bow and stern, painted green and gold patterns",
    "brigantine": "a pirate brigantine with two masts, patched black square sails, a hull of mismatched dark planks, a carved kraken figurehead with tentacles wrapping the bow",
    # light fantasy, still believable
    "ghost_carrack": "a haunted derelict carrack: sails reduced to shredded grey rags full of holes, rotting pale bleached hull with missing planks and gaping holes, broken railings, hanging chains, barnacles and dripping seaweed, an eerie pale green glow seeping from the gun ports and green lanterns on the castles",
    "coral_galleon": "a sunken galleon raised from the sea, hull overgrown with colourful coral, shells and barnacles, ragged sails dyed deep blue, a mermaid figurehead",
    "dragon_knarr": "a Norse-inspired trading knarr with a carved dragon-head prow and tail stern, round painted shields along the sides, one square sail with bold red and white stripes",
    "ember_frigate": "a black-hulled fantasy frigate with charred wood, blood-red sails, brass braziers burning on deck, iron-plated bow, a horned skull figurehead",
    "merchant_prince": "an opulent merchant carrack with a white and gold painted hull, purple silk sails embroidered with a golden sun, ornate balconies and a gilded figurehead",
}

FRAMING_FLUX = ("isolated 3D game asset render of {ship}. The complete ship is shown out of the water, the entire hull "
                "and keel visible, seen from the front-left at a 45-degree angle with the bow closest to the camera, slightly "
                "from above, centered with margin around it. Plain light "
                "grey seamless background, no water, soft even studio lighting, highly detailed, realistic materials. "
                "The ship is coherent: bowsprit, figurehead and forecastle are all at the bow nearest the camera, the sterncastle "
                "and rudder at the far end, and every sail and flag is filled by the same wind blowing from behind the ship.")
FRAMING_SEMI = ("semi-stylized 3D game asset of {ship}. Realistic historical proportions and construction, but "
                "hand-painted textures: clean painterly colours, simplified surface detail, crisp readable shapes, no grime, "
                "scratches or photographic texture. Soft flat even lighting with no cast shadows, no reflections and no "
                "specular highlights. The complete ship is shown out of the water, the entire hull and keel visible, seen "
                "from the front-left at a 45-degree angle with the bow closest to the camera, slightly from above, centered "
                "with margin around it. Plain light grey seamless background."
                " The ship is coherent: bowsprit, figurehead and forecastle are all at the bow nearest the camera, the sterncastle and rudder at the far end, and every sail and flag is filled by the same wind blowing from behind the ship.")
FRAMING_STYLIZED = ("stylized hand-painted 3D game asset of {ship}, in the style of a painterly fantasy video game prop. "
                    "Chunky simplified shapes, slightly exaggerated proportions, thick masts and sturdy rigging, clean hand-painted "
                    "textures with soft colour gradients and no photographic detail. Flat even ambient lighting with no cast "
                    "shadows, no reflections and no specular highlights. The complete ship is shown out of the water, the entire "
                    "hull and keel visible, seen from the front-left at a 45-degree angle with the bow closest to the camera, "
                    "slightly from above, centered with margin around it. Plain light grey seamless background."
                    " The ship is coherent: bowsprit, figurehead and forecastle are all at the bow nearest the camera, the sterncastle and rudder at the far end, and every sail and flag is filled by the same wind blowing from behind the ship.")
FRAMING_AMADMIR = "amadmir style, stylized illustration of " + FRAMING_FLUX.split("render of ", 1)[1]
FRAMING_WAN = ("studio product photo of a detailed model of {ship}. The complete ship is shown out of the water with the whole "
               "hull and keel visible, three-quarter view from slightly above, centered, plain light grey seamless "
               "background, soft even lighting, sharp focus, realistic materials.")


def flux(prompt, lora):
    p = json.load(open(os.path.join(ROOT, "tools", "api", "Amadmir-Flux-Game-Assets.api.json"), encoding="utf-8"))["prompt"]
    p["4"]["inputs"]["text"] = prompt
    if not lora:
        p["4"]["inputs"]["clip"] = ["2", 0]
        p["8"]["inputs"]["model"] = ["1", 0]
        del p["3"]
    p["11"]["inputs"]["filename_prefix"] = "Ships/flux"
    return p


def wan(prompt, object_info):
    ui = json.load(open(os.path.join(COMFY, "user", "default", "workflows", "Wan2.2-Text-to-Image.json"), encoding="utf-8"))
    p = ui_to_api(ui, object_info)
    p["5"]["inputs"]["text"] = prompt
    p["7"]["inputs"].update(width=1024, height=1024)
    p["11"]["inputs"]["filename_prefix"] = "Ships/wan"
    return p


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--models", nargs="+", default=["flux"], choices=["flux", "semi", "stylized", "amadmir", "wan"])
    ap.add_argument("--ships", nargs="+", default=["all"])
    ap.add_argument("--seeds", type=int, nargs="*")
    ap.add_argument("--count", type=int, default=1)
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--port", default="8188")
    a = ap.parse_args()
    if a.list:
        print("\n".join(f"{k:16} {v}" for k, v in SHIPS.items()))
        return
    base = f"http://127.0.0.1:{a.port}"
    ships = list(SHIPS) if a.ships == ["all"] else a.ships
    seeds = a.seeds or [random.randint(1, 2**31) for _ in range(a.count)]
    object_info = api(base, "/object_info") if "wan" in a.models else None

    for model in a.models:  # model-major order keeps the same model loaded between renders
        for ship in ships:
            for seed in seeds:
                desc = SHIPS[ship]
                wf = {"flux": lambda: flux(FRAMING_FLUX.format(ship=desc), False),
                      "amadmir": lambda: flux(FRAMING_AMADMIR.format(ship=desc), True),
                      "stylized": lambda: flux(FRAMING_STYLIZED.format(ship=desc), False),
                      "semi": lambda: flux(FRAMING_SEMI.format(ship=desc), False),
                      "wan": lambda: wan(FRAMING_WAN.format(ship=desc), object_info)}[model]()
                wait_idle(base)
                status, secs, images, errors = render(base, wf, seed)
                out_dir = os.path.join(ROOT, "assets", "ships", ship)
                os.makedirs(out_dir, exist_ok=True)
                files = []
                for img in images[:1]:
                    dst = os.path.join(out_dir, f"{ship}_{model}_s{seed}.png")
                    shutil.copy2(os.path.join(COMFY_OUTPUT, img["subfolder"], img["filename"]), dst)
                    files.append(os.path.relpath(dst, ROOT).replace("\\", "/"))
                entry = {"time": datetime.datetime.now().isoformat(timespec="seconds"), "kind": "ship_3d_source",
                         "model": model, "name": ship, "seed": seed, "description": desc, "status": status,
                         "seconds": round(secs), "files": files, "errors": errors}
                with open(os.path.join(ROOT, "generation_log.jsonl"), "a", encoding="utf-8") as f:
                    f.write(json.dumps(entry, ensure_ascii=False) + "\n")
                print(f"{status} {model} {ship} seed={seed} {secs:.0f}s -> {', '.join(files) or errors}", flush=True)


if __name__ == "__main__":
    main()
