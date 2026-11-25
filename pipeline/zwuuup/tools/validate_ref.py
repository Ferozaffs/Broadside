"""Validate a 2D reference image before it may be turned into a 3D model (mandatory for EVERY asset type).

  python -I tools/validate_ref.py check   <image> --kind ship        # automatic checks + the checklist to review
  python -I tools/validate_ref.py approve <image> --kind ship --confirm G1 G2 G3 G4 G5 G6 G7 S1 S2 S3 S4 --notes "..."
  python -I tools/validate_ref.py reject  <image> --kind ship --fail S1 S2 --notes "bowsprit left, figurehead right"
  python -I tools/validate_ref.py status  <image>

Step 1 runs the automatic checks (plain background, object fully inside the frame with margin, sensible size,
resolution, sharpness). Step 2 is a visual review: open the image at full size and judge every checklist item
(general G* items plus the ones for the asset kind). `approve` only works when the automatic checks pass and
EVERY checklist id is confirmed explicitly. Verdicts are appended to assets/ref_validation.jsonl, keyed by the
file's SHA-256, so editing an image invalidates its approval. tools/make_3d.py refuses images without an approval.
"""
import argparse, datetime, hashlib, json, os, sys

import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOG = os.path.join(ROOT, "assets", "ref_validation.jsonl")

GENERAL = {
    "G1": "Single subject: exactly one object, nothing else in frame (no second object, no people unless it is a character).",
    "G2": "Complete: the whole object is visible with margin; nothing cut off by the frame or hidden behind something else.",
    "G3": "Clean isolation: plain background; no ground, water, stand, base or puddle attached to the object; no text, logos or watermarks.",
    "G4": "View: three-quarter view that shows at least two sides and a bit of the top; no extreme perspective distortion.",
    "G5": "Coherence: every part is connected and structurally plausible; nothing floating, melted, duplicated or impossible; all parts agree on one front/back and up/down.",
    "G6": "Lighting: soft even light; no strong cast shadows, coloured light spill, glare or reflections that would bake into the texture.",
    "G7": "Brief: matches what was asked (type, era, theme, style) closely enough to use in the game.",
}
KINDS = {
    "ship": {
        "S1": "Heading: the bow is unambiguous - bowsprit, figurehead and forecastle are at the SAME end; sterncastle and rudder at the other.",
        "S2": "Wind: all sails and flags agree on ONE wind direction that makes sense for that heading (square sails bellied the same way, flags streaming the same way).",
        "S3": "Rig: masts upright and in a line along the hull centre; yards cross the masts; rigging runs between masts and hull, not into empty space; mast count fits the ship type.",
        "S4": "Hull: left/right symmetric, continuous keel line, gun ports or oars evenly spaced along a consistent deck line.",
    },
    "character": {
        "C1": "Anatomy: correct number of heads, arms, legs, hands and fingers; joints bend plausibly.",
        "C2": "Pose: neutral standing pose (A-pose or relaxed), limbs separated from the body, nothing crossing in front of torso or face.",
        "C3": "Attachments: held items and clothing clearly held/attached, not merged into the body; paired accessories symmetric.",
        "C4": "Full body: head to feet in frame, feet on one invisible floor plane.",
    },
    "creature": {
        "K1": "Anatomy: consistent counts of limbs, wings, tails and eyes; left/right symmetric.",
        "K2": "Pose: neutral pose with limbs separated; whole tail and wings in frame.",
    },
    "prop": {
        "P1": "Function: it is clear what the object is and its parts connect the way a real one would (hinges, handles, lids, straps).",
        "P2": "Symmetry and perspective: symmetric where the real object is; all parts share one perspective.",
    },
    "building": {
        "B1": "Structure: walls, roofs and supports line up; windows and doors sit on consistent floor levels; flat base.",
        "B2": "Complete: the whole building including its base or foundation is in frame.",
    },
    "other": {},
}


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def auto_checks(path):
    im = Image.open(path)
    w, h = im.size
    res = {"size": [w, h]}
    rgba = np.asarray(im.convert("RGBA")).astype(np.float32)
    alpha = rgba[..., 3]
    if (alpha < 250).mean() > 0.05:  # image already has a transparent background
        mask = alpha > 128
        res["background"] = "transparent"
        bg_ok = True
    else:
        # studio backgrounds are smooth gradients (vignette, floor falloff): fit a quadratic surface to the
        # border strip and measure everything against that instead of one flat colour
        rgb = rgba[..., :3]
        b = max(4, int(min(w, h) * 0.03))
        yy, xx = np.mgrid[0:h, 0:w]
        strip = np.zeros((h, w), bool)
        strip[:b], strip[-b:], strip[:, :b], strip[:, -b:] = True, True, True, True
        def basis(x, y):
            x, y = x / w - 0.5, y / h - 0.5
            return np.stack([np.ones_like(x), x, y, x * x, y * y, x * y], -1)
        A = basis(xx[strip].astype(np.float32), yy[strip].astype(np.float32))
        target = rgb[strip]
        keep = np.ones(len(A), bool)
        for _ in range(2):  # refit without object pixels that reach into the border (flags, bowsprits, rigging)
            coef, *_ = np.linalg.lstsq(A[keep], target[keep], rcond=None)
            r = np.abs(target - A @ coef).sum(1)
            keep = r < max(30.0, np.percentile(r, 80))
        bg = basis(xx.astype(np.float32), yy.astype(np.float32)) @ coef
        resid = np.abs(rgb - bg).sum(2)
        bg_spread = float(np.percentile(resid[strip], 80))
        res["background"] = {"rgb_center": [round(float(v)) for v in bg[h // 2, w // 2]], "border_spread_p80": round(bg_spread, 1)}
        bg_ok = bg_spread < 30
        mask = resid > max(60.0, bg_spread * 2)
        # in the floor zone, soft contact shadows and the bright floor/backdrop seam are just a darker or lighter
        # copy of the background (same hue); background removal drops them, so they must not count as object.
        # (Limited to the lower frame so white or grey sails higher up still count.)
        ratio = rgb / np.maximum(bg, 1)
        lum = ratio.mean(2)
        shadow = mask & (yy > h * 0.6) & (lum > 0.3) & (lum < 1.3) & (ratio.std(2) / np.maximum(lum, 1e-3) < 0.06)
        res["shadow_fraction"] = round(float(shadow.mean()), 3)
        mask &= ~shadow
    ys, xs = np.nonzero(mask)
    if len(xs) == 0:
        return dict(res, passed=False, problems=["no object found"])
    x0, x1, y0, y1 = xs.min(), xs.max(), ys.min(), ys.max()
    edge = max(2, int(min(w, h) * 0.01))
    touching = {side: int(n) for side, n in {"left": mask[:, :edge].sum(), "right": mask[:, -edge:].sum(),
                                              "top": mask[:edge].sum(), "bottom": mask[-edge:].sum()}.items() if n > 0.002 * w * edge}
    res["bbox_fraction"] = round(float((x1 - x0) * (y1 - y0) / (w * h)), 3)
    res["margins_px"] = {"left": int(x0), "right": int(w - 1 - x1), "top": int(y0), "bottom": int(h - 1 - y1)}
    gray = np.asarray(im.convert("L").resize((512, 512))).astype(np.float32)
    lap = gray[1:-1, 1:-1] * 4 - gray[:-2, 1:-1] - gray[2:, 1:-1] - gray[1:-1, :-2] - gray[1:-1, 2:]
    res["sharpness"] = round(float(lap.var()), 1)
    problems = []
    if min(w, h) < 768:
        problems.append(f"resolution {w}x{h} below 768 px")
    if not bg_ok:
        problems.append("background is not plain (busy or uneven border)")
    if touching:
        problems.append(f"object touches the frame edge: {touching}")
    if not 0.12 <= res["bbox_fraction"] <= 0.92:
        problems.append(f"object fills {res['bbox_fraction']:.0%} of the frame (want 12-92%)")
    if res["sharpness"] < 20:
        problems.append("image looks blurry")
    res["problems"], res["passed"] = problems, not problems
    return res


def checklist(kind):
    return {**GENERAL, **KINDS[kind]}


def records(path=None, digest=None):
    if not os.path.exists(LOG):
        return []
    out = []
    for line in open(LOG, encoding="utf-8"):
        r = json.loads(line)
        if digest is None or r["sha256"] == digest:
            out.append(r)
    return out


def require_approved(path):
    """Used by make_3d.py: returns the approving record or exits with instructions."""
    digest = sha256(path)
    recs = records(digest=digest)
    if not recs or recs[-1]["verdict"] != "approve":
        state = recs[-1]["verdict"] + "ed: " + recs[-1]["notes"] if recs else "never validated"
        sys.exit(f"Reference image not approved ({state}).\nValidate it first:\n"
                 f"  python -I tools/validate_ref.py check \"{path}\" --kind <ship|character|creature|prop|building|other>")
    return recs[-1]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("action", choices=["check", "approve", "reject", "status"])
    ap.add_argument("image")
    ap.add_argument("--kind", choices=list(KINDS), default="other")
    ap.add_argument("--confirm", nargs="*", default=[], help="approve: every checklist id, explicitly")
    ap.add_argument("--fail", nargs="*", default=[], help="reject: the checklist ids that failed")
    ap.add_argument("--notes", default="")
    ap.add_argument("--reviewer", default="claude")
    a = ap.parse_args()
    path = os.path.abspath(a.image)
    rel = os.path.relpath(path, ROOT).replace("\\", "/")

    if a.action == "status":
        recs = records(digest=sha256(path))
        print(json.dumps(recs[-1], indent=1) if recs else "never validated")
        return

    auto = auto_checks(path)
    items = checklist(a.kind)
    if a.action == "check":
        print(f"AUTOMATIC CHECKS: {'PASS' if auto['passed'] else 'FAIL'}")
        print(json.dumps({k: v for k, v in auto.items() if k not in ('passed',)}, indent=1))
        print(f"\nVISUAL CHECKLIST ({a.kind}) - open the image at full size and judge each item:")
        for k, v in items.items():
            print(f"  {k}  {v}")
        return

    if a.action == "approve":
        if not auto["passed"]:
            sys.exit(f"Cannot approve: automatic checks failed: {auto['problems']}")
        missing = [k for k in items if k not in a.confirm]
        if missing:
            sys.exit(f"Cannot approve: confirm every checklist item explicitly; missing {missing}")
        if not a.notes.strip():
            sys.exit("Cannot approve: --notes must say what you checked (e.g. which end is the bow, wind direction).")
    if a.action == "reject" and not (a.fail or a.notes.strip()):
        sys.exit("Reject needs --fail <ids> and/or --notes explaining the problem.")

    rec = {"time": datetime.datetime.now().isoformat(timespec="seconds"), "file": rel, "sha256": sha256(path),
           "kind": a.kind, "verdict": a.action, "reviewer": a.reviewer, "confirmed": a.confirm, "failed": a.fail,
           "notes": a.notes, "auto": auto}
    os.makedirs(os.path.dirname(LOG), exist_ok=True)
    with open(LOG, "a", encoding="utf-8") as f:
        f.write(json.dumps(rec, ensure_ascii=False) + "\n")
    print(f"{'approved' if a.action == 'approve' else 'rejected'} {rel}")


if __name__ == "__main__":
    main()
