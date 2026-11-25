# 3D asset pipeline: image → validated reference → TRELLIS.2 model

Guide for agents (and people) who need 3D models for the pirate game. It covers every step, the commands, the
mandatory checks and the pitfalls already hit. Read the hard rules first; then follow the steps in order.
Last verified: 2026-11-20 on the user's machine.

---

## 0. Hard rules

1. **One shared ComfyUI server.** The user runs ComfyUI on `http://127.0.0.1:8188` (portable install under
   `C:\Users\USER\Documents\ComfyUI_windows_portable_nvidia\ComfyUI_windows_portable`, started with
   `run_nvidia_gpu.bat --lowvram`). Never start a second server, never restart or kill it, never cancel the
   user's jobs. The scripts wait for an empty queue before submitting; keep it that way.
2. **Validate every reference image before making a 3D model. No exceptions, any asset type.** `tools/make_3d.py`
   refuses images without an approval record (step 3). Do not work around it; if a check is wrong, fix the check.
3. **No stock, watermarked or third-party images in shipped assets.** They are allowed only as labelled tests.
4. **Disk:** new model downloads go on the models drive (`extra_model_paths.yaml`). Generated assets live in
   this folder (`assets/`).
5. **Logs are append-only:** `generation_log.jsonl` (every 2D render) and `assets/ref_validation.jsonl` (every
   validation verdict). Never rewrite them.
6. Run all commands from `C:\Users\USER\Documents\PirateGame-ArtPipeline` with `python -I ...` (system Python
   3.10 with PIL and numpy) unless a step says otherwise.

## 1. Overview

```
 brief ──► 2D reference ──► automatic checks ──► visual checklist ──► approve / reject ──► TRELLIS.2 ──► Blender preview ──► review 3D
           (make_ships.py)   (validate_ref.py check)   (you, at full size)   (validate_ref.py)    (make_3d.py)   (blender_views.py)
                ▲                                                                   │ reject
                └──────────────────────── new seed / better prompt ◄────────────────┘
```

| Step | Tool | Time on a 12 GB card |
|---|---|---|
| 2D reference (Flux.1 Dev Q6_K, 1024²) | `tools/make_ships.py` | 80–110 s per image (first one +40 s; 300–600 s if Wan was loaded just before) |
| Validation | `tools/validate_ref.py` | 1 s automatic + your review |
| Image → 3D | `tools/make_3d.py` | 2–3 min, about 5 GB VRAM peak |
| Preview renders | `tools/blender_views.py` + `tools/tile_views.py` | about 15 s |

## 2. Make the 2D reference

TRELLIS.2 reconstructs exactly what it sees, so the reference decides the result. A good reference is:
**one whole object, three-quarter view (front-left 45°, slightly from above), plain light-grey background, soft even
light, no water, ground or stand, nothing cut off, and physically coherent.**

### Ships (ready-made)
```
python -I tools/make_ships.py --list                                   # the 15 designs and their descriptions
python -I tools/make_ships.py --models semi --ships galleon carrack --seeds 777 1001 2002
python -I tools/make_ships.py --models semi --ships all --count 2      # random seeds
```
Output is `assets/ships/<ship>/<ship>_<model>_s<seed>.png`, and each render is logged. Render 3–4 seeds per ship and
expect to reject some.

`--models` picks the source look (all use Flux.1 Dev without a LoRA unless noted):

| Key | Look | Use |
|---|---|---|
| `semi` | realistic construction, hand-painted textures, flat light | **default for game models** (see §6) |
| `flux` | photoreal studio render | very detailed; textures carry baked grime/lighting ("3D-scan" look) |
| `stylized` | chunky cartoon game prop | clean meshes but mobile-game look; exaggerates figureheads |
| `amadmir` | the game's 2D comic-ink LoRA | not for 3D: flat cel shading and a painted water strip |
| `wan` | Wan 2.2 | not for 3D: it always adds sea and reflections |

Add a ship by adding a line to `SHIPS` in `tools/make_ships.py` (describe the hull, masts, sails, colours and one or
two distinctive features). Keep fantasy elements believable. The framing text already asks for coherence
(bowsprit, figurehead and forecastle at the bow nearest the camera, all sails and flags filled by one wind from
behind), but Flux still gets it wrong sometimes. That's what step 3 is for.

### Other asset kinds (characters, creatures, props, buildings)
There's no ready-made generator yet. Copy the pattern from `make_ships.py` (`flux()` builds the API prompt from
`tools/api/Amadmir-Flux-Game-Assets.api.json` without the LoRA) and keep the same framing rules. For characters
and creatures, add **neutral A-pose, limbs apart from the body, full body head to feet**. Props go on no stand
and with no hands holding them.

## 3. Validate the reference (mandatory)

```
python -I tools/validate_ref.py check   <image> --kind <ship|character|creature|prop|building|other>
python -I tools/validate_ref.py approve <image> --kind ship --confirm G1 G2 G3 G4 G5 G6 G7 S1 S2 S3 S4 --notes "<what you checked>"
python -I tools/validate_ref.py reject  <image> --kind ship --fail S1 S2 --notes "<what is wrong>"
python -I tools/validate_ref.py status  <image>
```

**3a. Automatic checks** (`check`) fit the studio gradient and floor seam, ignore soft floor shadows, and then fail
the image on any of these:
- the background isn't plain (water, scenery, busy texture)
- the object touches the frame edge (something is cropped)
- the object fills less than 12% or more than 92% of the frame
- the resolution is under 768 px
- the image is blurry

`approve` is impossible while an automatic check fails. The checks catch framing and background problems only.
They can't see whether the object makes sense.

**3b. Visual checklist.** Open the image at full size (the Read tool shows images) and judge every item `check`
prints. General items apply to all kinds:

| Id | Item |
|---|---|
| G1 | Single subject, nothing else in frame |
| G2 | Complete, with margin, nothing hidden or cut off |
| G3 | Clean isolation: no ground, water, stand or puddle; no text, logos or watermarks |
| G4 | Three-quarter view showing two sides and a bit of top |
| G5 | Coherent: parts connected and plausible; nothing floating, melted or duplicated; one front/back and up/down |
| G6 | Soft even light; no strong shadows, glare or reflections to bake in |
| G7 | Matches the brief (type, era, theme, style) |

Kind-specific items:
- **Ship:**
  - S1 the bow is unambiguous (bowsprit, figurehead and forecastle at the same end)
  - S2 all sails and flags agree on one wind that fits the heading
  - S3 masts are upright and in line, rigging runs between mast and hull, the mast count fits the type
  - S4 the hull is symmetric, with gun ports or oars in an even line
- **Character:** C1 anatomy counts, C2 neutral pose with limbs apart, C3 held items and clothing not merged into the
  body, C4 full body in frame.
- **Creature:** K1 consistent limb and wing counts, symmetric; K2 neutral pose, whole tail and wings visible.
- **Prop:** P1 its function is readable and parts connect like the real thing; P2 symmetry and one perspective.
- **Building:** B1 walls, roofs and floors line up, flat base; B2 complete, including the foundation.

How to review:
- Judge each item on purpose; don't skim.
- For ships, find the bow first (bowsprit and figurehead), then check that every sail belly and flag agrees with it.
- `approve` requires listing **every** id with `--confirm` and notes that say what you saw (for example "bow at
  right, sails bellied forward, flags stream forward"). Edit the image and the approval is gone (it's keyed by the
  file's SHA-256).

Real rejections so far (see `assets/ref_validation.jsonl`):
- `brigantine_stylized_s777`: the bowsprit is at one end and the sea-serpent figurehead at the other, so the ship
  has no clear bow (S1, S2, G5). The user caught this one after it had already gone through TRELLIS.
- `ghost_carrack_flux_s2002`: the mast is cropped at the top and the hull sits in a water puddle (G2, G3).
- The pngtree stock galleon: watermarks and a stock licence (G3, G7).

## 4. Image → 3D model

```
python -I tools/make_3d.py --image assets/ships/galleon/galleon_semi_s1001.png --name galleon_semi_s1001 --max-faces 60000
```
- The script exits unless the image is approved. It converts the UI workflow `TRELLIS2-Image-to-3D-12GB`
  (ComfyUI workflow menu) to API format through the server's `/object_info`, copies the image into ComfyUI's
  `input/`, waits for an idle queue and runs the job. It copies the `.glb` and bake previews to `assets/3d/<name>/`.
- **Always pass `--max-faces 60000`** (30–80k for game use). 60k looks almost the same as the raw 700k because
  the baked normal map keeps the detail, and the GLB drops from about 50 MB to about 15 MB. Textures are 2048².
- Workflow settings: TRELLIS.2 structure (not Pixal3D), shape upsample at 1024 (the node's minimum; 1536 is too
  heavy), BiRefNet background removal on, seeds fixed.
- `--convert-only` writes the API JSON without running anything (no validation needed).

## 5. Check the 3D result

```
"C:/Program Files/Blender Foundation/Blender 3.3/blender.exe" -b --factory-startup --python tools/blender_views.py -- <model.glb> <out_dir>
"C:/Users/USER/Documents/ComfyUI_windows_portable_nvidia/ComfyUI_windows_portable/python_embeded/python.exe" -I tools/tile_views.py <sheet.jpg> "label=<out_dir>" ...
```
`blender_views.py` renders four views (Eevee, soft studio light) and prints the triangle count. `tile_views.py`
stacks several models into one comparison sheet. Look for:
- holes or melted thin parts (rigging, sail edges)
- floating blobs
- a back side that TRELLIS invented badly
- texture seams
- lighting baked into the texture

`tools/render_glb_views.py` is an older point-sample preview. Its white speckles are sampling gaps, so don't judge
models with it.

Blender 3.3 (and 2.92) is installed. No Blender MCP is set up; headless scripts cover previews and batch work.
Set up MCP only if someone needs to edit interactively in the user's open Blender.

## 6. Results and recommendations so far

Comparison of the same brigantine through the whole pipeline: `reference/brigantine_AB_realistic_vs_stylized.jpg`
(Blender renders, 4 views each). The model bake-off sheets are `reference/ships_flux_fleet_s777.jpg` and
`reference/ships_flux_rerolls.jpg`.

| Row | Source | Triangles / GLB | Verdict |
|---|---|---|---|
| A | `flux` realistic | 700k / 49 MB | detailed; photo grime baked into the texture ("scan" look) |
| B | `flux` realistic | 60k / 16 MB | looks the same as A, so **60k is the default** |
| C | `stylized` | 60k / 15 MB | smooth and clean, but cartoony. **The reference was invalid** (bowsprit at one end, figurehead at the other); found only after the model was built, which is why the validation gate exists |
| D | `semi`, seed 3003 | 57k / 17 MB | **best so far**: coherent brigantine rig, clean hull, bow and figurehead agree, less baked grime than A/B |

Recommendations:
- Use `--models semi`. Render 3–4 seeds per asset and expect to validate about 1 in 4 (in the brigantine test, 1 of 4
  passed: one cropped, one near side view, one with an incoherent wind).
- Use `--max-faces 60000`.
- **Avoid near-black sails or cloth.** C and D both lost the fold detail on black sails and the thin flags vanished.
  Ask for dark grey, weathered or patched canvas instead.
- Thin parts (flags, single ropes, pennants) are the first to disappear. Make them bigger in the prompt or add them
  in Blender.
- For a production asset, plan a Blender clean-up pass (separate the rigging into alpha cards, fix the sails, rebake).
  TRELLIS output is a strong starting point, not a finished asset.

## 7. Installed models and paths

| Purpose | File | Folder (on K: unless noted) |
|---|---|---|
| TRELLIS.2 diffusion | `trellis_2_int8_convrot.safetensors` | `diffusion_models/` |
| Pixal3D diffusion (needed by the template even when unused) | `pixal3d_int8_convrot.safetensors` | `diffusion_models/` |
| Shape / texture VAE | `trellis_2_shape_vae_bf16`, `trellis_2_texture_vae_bf16` | `vae/` |
| DINOv3 image encoder | `dino_v3_L_naf_fp32.safetensors` | `clip_vision/` |
| MoGe geometry | `moge_2_vitl_normal_fp16.safetensors` | `geometry_estimation/` |
| BiRefNet background removal | `birefnet.safetensors` | `ComfyUI\models\background_removal\` (C:) |
| Flux.1 Dev Q6_K + encoders + VAE | see `CLAUDE.md` §3 | |

`geometry_estimation` is mapped in `ComfyUI\extra_model_paths.yaml`. Until the user restarts ComfyUI, MoGe is reached
through a junction `ComfyUI\models\geometry_estimation\K → K:\...\geometry_estimation`, so it is listed as
`K\moge_...`. `make_3d.py` handles both names.

## 8. Pitfalls already hit

- **ComfyUI silently skips invalid outputs.** If some nodes fail validation it runs only the outputs that pass and
  reports `success`. The first TRELLIS run "succeeded" in 5 s with two previews and no model, because the
  template's MoGe node lacked the newer `refine_steps` input. `make_3d.py` now fills missing inputs from their
  defaults and aborts on any `node_errors`.
- The UI-to-API converter (`ui_to_api` in `make_3d.py`) handles `control_after_generate` slots, dynamic combos
  (`name.sub` keys) and pass-through preview nodes. Re-check it if ComfyUI is updated.
- **Flux is very slow right after Wan 2.2** (it took 665 s instead of 90 s) because of RAM pressure. Group renders
  by model; `make_ships.py` already does this.
- Blender resolves relative output paths oddly (renders once landed in `C:\assets`). `blender_views.py` uses
  absolute paths now.
- Studio backgrounds have gradients, soft floor shadows and a bright floor seam. A naive background check flags
  them as "object touches the edge". `validate_ref.py` models all three.
- BiRefNet can delete props held away from the object (CLAUDE.md §6). Check the 3D model for missing parts.
- Flux limitations: no true Chinese junk sails, no single-mast cutters (it keeps adding masts), and Viking-looking
  hulls for galleys. Reroll or rephrase, and validate.

## 9. File layout

```
tools/make_ships.py        2D ship references (Flux), logs to generation_log.jsonl
tools/validate_ref.py      mandatory reference validation, logs to assets/ref_validation.jsonl
tools/make_3d.py           TRELLIS.2 via the user's ComfyUI (enforces validation)
tools/blender_views.py     Blender headless 4-view renders of a GLB
tools/tile_views.py        comparison sheets from blender_views output
assets/ships/<ship>/       2D references
assets/3d/<name>/          .glb, bake previews, API JSON, blender/view_*.png
reference/                 contact sheets and A/B comparisons
```
