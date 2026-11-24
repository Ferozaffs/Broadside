# Looping idle animations for character portraits (Wan 2.2)

How to turn a finished dialogue portrait into a short, **seamlessly looping** idle animation (breathing, cloth,
hair, flames) with a transparent background. For agents and people. Last verified: 2026-11-20.
The hard rules in `docs/3D_PIPELINE.md` §0 apply here too (one shared ComfyUI on 8188, never restart it or cancel
the user's jobs, append-only logs, downloads go to K:).

## 1. How it works

```
portrait.png ──► Wan 2.2 I2V 14B (first frame = last frame = portrait) ──► 49 frames ──► drop last frame
      │                    + lightx2v 4-step LoRAs, 576×736, 16 fps                     │
      └─ grey-background original (NOT the _alpha PNG)                                  ▼
                                       BiRefNet per frame ──► circular temporal median of alpha ──► loop check
                                                                                                    │
                     frames/*.png (RGBA) · sprite sheet 8×6 · <name>_alpha.webp · <name>_preview.webp
```

- **The loop is built in.** `WanFirstLastFrameToVideo` gets the portrait as both the start and the end image, so the
  clip begins and ends on the original art. The last frame (a near-copy of frame 0) is dropped, so playing frames
  0..47 on repeat has no jump. **Every idle animation must loop. Never make one without first/last-frame
  conditioning.**
- **Transparency:** BiRefNet runs on every frame. Smoke, loose hair and flames are ambiguous to it and can flip
  between "keep" and "cut" from one frame to the next. That makes the cut-out flicker and pop at the loop point
  (seen on dread_kate: the smoke was missing in frame 0 only). A circular 5-frame temporal median of the alpha
  channel fixes this without breaking the loop.

## 2. Run it

```
python -I tools/make_idle.py --image assets/characters/<char>/<char>_amadmir_s<seed>.png --name <char>_s<seed> \
    --motion "<character-specific motion: what sways, flickers or drifts>" [--seed 42]
```
- `--name` must be `<character folder>_s<portrait seed>`. Output goes to `assets/characters/<char>/idle/<name>/`, and the
  run is logged in `generation_log.jsonl` (`kind: idle_animation`).
- It takes about 5 min per clip (49 frames at 576×736; the first run loads the 14B models). It waits for an idle
  queue.
- `--reprocess` (same arguments) redoes frames, alpha smoothing, the sprite sheet and the loop check from ComfyUI's
  `output/Idle/<name>/` without generating again.

**Motion prompt.** A fixed base prompt asks for subtle breathing, gently swaying cloth and hair, the same pose and
expression, no talking and a static camera. `--motion` adds the character's details. Name only things that should
move (hair, coat hem, lace, flame, smoke) and say the face is frozen. Example that worked (dread_kate): *"The flame of
the torch in her raised hand flickers and dances, sparks drift upward, her long black hair with ember-orange tips
and her open coat sway gently."*

## 2b. Motion style and the face check (user feedback, 2026-11-21)

- **Motion must be OVERALL, not detailed (the user, 2026-11-21).** The whole figure moves gently as one: a slow sway,
  shoulders softly rising and falling, and clothes and hair moving with the body as whole shapes. No separate small
  motions: hair strand by strand, fingers fidgeting, lips, blinks. Movement is welcome, just not detailed movement.
- **Reference clip: `dread_kate_s154330924` (v1).** The user called it good: median step about 4, the face stays
  put, the coat and hair move as masses, and the torch flame adds life.
- `--motion` is only for effects that move on their own (a torch flame, pipe smoke). **Don't list body, hair or cloth
  details.** Every listed detail makes Wan animate it separately. That's what went wrong with dread_corwin: his
  text named hair tips, lace and the sword grip, and Wan whipped the hair and made him talk.
- **Talking fix: CFG 2.0 (now the default).** With the lightx2v 4-step LoRA the original setup sampled at CFG 1.0, and
  at CFG 1.0 ComfyUI skips the negative prompt entirely (`comfy/samplers.py`, `uncond_ = None`). So "talking, lip
  movement, mouth opening and closing" in the negative did nothing, and Wan's habit of making a camera-facing person
  talk went unchecked.

  Tested on dread_corwin, seed 7, 2026-11-21:

  | Run | Change | Mouth-area change from frame 0 |
  |---|---|---|
  | v3 | prompt mentions the mouth | 35–42 (talks) |
  | v4 | no face words in the prompt | 31–41 (still talks) |
  | v5 | **CFG 2.0** | **15–22 (grin holds; the rest is overall sway)** |

  Cost: 331 s against 281 s. Keep the positive prompt free of face words anyway. They didn't help, and the code
  comment explains why.
- **CFG 2 side effect: a brightness pulse on dark portraits (not solved yet).** Brightness swing across the loop at
  CFG 1 → CFG 2: dread_kate 7 → 27, greywater_tavern 2 → 41, dread_corwin (bright image) 3 → 4. A per-frame colour
  match to the portrait was tried and **looked worse to the user, so it was removed**. The loop check only reports
  `brightness_range_info`.

  greywater_tavern, seed 42, every CFG tested:

  | CFG | Brightness swing | `talk_p90` | Verdict |
  |---|---|---|---|
  | 1.0 | 2.0 | 29.9 | quiet |
  | **1.2** | **2.3** | **25.2** | **best: quiet, no pulse** |
  | 1.5 | 11.9 | 16.4 | quiet |
  | 2.0 | 41.4 | 11.5 | quiet |

  At CFG 1.5, dread_kate's swing stayed at 28; her mid-loop brightening may be firelight. The best CFG is per
  character: closed-mouth portraits need little or none, and dread_corwin's half-open grin needed 2.0 (1.2 and 1.5
  untested for him). That's why `--cfg-ladder` exists. It's built but not yet tested end to end, and the default is
  still 2.0.
- **Transparency seam fade.** When background removal disagrees over several frames at the start of the loop
  (dread_kate at CFG 2 lost the smoke in frames 0–3 only), the 5-frame median can't fix it. If the alpha seam is
  still above 2×, the alpha of the last 8 frames is faded toward frame 0's, so the smoke thins out before the loop
  instead of popping. dread_kate's alpha seam went from 6.65× to 0.71×.
- **Tested on all three characters (2026-11-21):** at CFG 2 nobody talks (by eye: dread_kate holds her laugh,
  dread_corwin his grin, greywater_tavern her closed lips), and all three pass the loop check with the seam fade.
  Video: `reference/idle/cfg2_all_three.mp4`.
- **Don't patch faces.** A "face lock" that pasted the original face into every frame was tried and removed. In
  motion the static face slides like a mask over the moving neck and hair, and the user found it "a lot worse".
  Fix a bad clip by re-rolling (another seed, calmer settings), never by compositing.
- **There is no automatic face check.** `--face-box CX CY RX RY` only reports `face_drift_info`. Calibration on 2026-11-21
  showed that neither raw nor motion-aligned face difference separates good from bad. The user-approved dread_kate v1
  scored 23.8 aligned (her laughing head tilts), and the talking dread_corwin clips scored 26.5–31.1. So talking and
  face changes have to be judged by eye, in motion.
- **Always judge an idle in motion** (`tools/idle_preview.py` → MP4), not from still frames. Problems like the mask
  effect only show when it plays.

## 2c. Recommended run: the CFG ladder with the talking check (2026-11-21)

```
python -I tools/face_grid.py assets/characters/<char>/<char>_amadmir_s<seed>.png      # read off the boxes
python -I tools/make_idle.py --image <portrait.png> --name <char>_s<seed> --motion "<flame/smoke only, or omit>"     --face-box CX CY RX RY --mouth-box X0 Y0 X1 Y1 --cfg-ladder 1.0 1.25 1.5 2.0
```
- The ladder renders at CFG 1.0 first and steps up only when the clip fails any check. Lower CFG keeps the
  colours stable. Higher CFG activates the negative prompt, which stops talking but brightens dark portraits.
  Each character ends up at the lowest CFG that works. Attempts are saved as `<name>_cfg<value>`, and the log entry
  (`kind: idle_ladder`) names the selected one. If none passes, try another `--seed`.
- **Talking check** (`talk_p90`, needs both boxes): each frame's head is aligned to frame 0, then the mouth box is
  compared using normalized correlation, which ignores brightness changes. It's the 90th percentile over frames;
  above 50 means talking. Calibrated on 11 clips you judged across 3 characters: talking clips scored 74–79 and quiet
  ones 6–33. An earlier raw-difference version couldn't separate them, because brightness pulses and firelight
  counted as mouth change.

| Character | `--face-box` | `--mouth-box` |
|---|---|---|
| dread_corwin s2086810490 | 0.495 0.165 0.085 0.085 | 0.476 0.205 0.545 0.235 |
| dread_kate s154330924 | 0.54 0.10 0.08 0.07 | 0.50 0.105 0.59 0.15 |
| greywater_tavern s1546026290 | 0.52 0.21 0.08 0.075 | 0.505 0.245 0.58 0.27 |

## 3. Loop check (automatic, printed and saved in `idle_info.json`)

| Metric | Fails when | Catches |
|---|---|---|
| `seam_ratio` | last→first jump > 2× the median frame step | visible loop seam |
| `median_step` | < 0.15 | no motion at all |
| `max_step` | > 6× median | a sudden jump inside the clip |
| `max_drift_from_art` | > `--max-drift` (default 28; mean abs diff from the portrait, 0–255) | big pose change or morphing (lively idles sit at about 18–25) |
| `face_drift_info` (only with `--face-box`) | never fails, information only | (unreliable for talking; judge by eye) |
| `alpha.seam_ratio` | > 2× | the transparent version pops at the loop |
| `alpha.max_step` | > 4× median | background-removal flicker |
| `brightness_range_info` | never fails, information only | exposure pulse (cfg side effect) |
| `talk_p90` (needs `--face-box` and `--mouth-box`) | > 50 | talking or mouth movement |

`alpha_raw` shows the same numbers before smoothing, for diagnosis.

**Then review it yourself.** Open a strip of frames (for example 0, 8, 16, 24, 32, 40, 47) on a coloured background
next to the original and check:
- the face and expression stay the same
- nothing new appears
- linework stays crisp enough
- the cut-out keeps props and hair

A FAIL means re-run with another `--seed` and/or a stricter `--motion` text. Don't ship it.

## 4. Results so far

| Clip | Result |
|---|---|
| `dread_kate_s154330924` (seed 42) | **PASS.** Flame flickers, smoke drifts, hair and coat sway; face and laugh hold; ink style survives. Needed alpha smoothing (raw alpha seam 3.3×, fixed to 1.3×). |
| `dread_corwin_s2086810490` (seed 42) | **FAIL, drift 19.7.** Mid-loop the head turns toward the camera and the smirk relaxes. The check caught it correctly. |
| `dread_corwin_s2086810490` (seed 7, "face is frozen" prompt) | passes the check, but the mouth still relaxes and talks mid-loop, which the whole-image drift misses |
| `dread_kate_s154330924_v2` (lively prompt) | PASS: more life (median step 6.4 against 4.0), seam 1.06×, drift 24.5 |
| `dread_corwin_s2086810490_v2` (lively prompt) | **FAIL**: talks and the hair is wild (user); face drift 56.5. A face-lock paste fixed the numbers but looked worse in motion (`reference/idle/corwin_lock_before_after.mp4`), so it was removed |

**Known limits:**
- Output is 576×736, softer than the 896×1152 portraits (ink lines slightly thicker). Higher resolution (for
  example 720×928) costs about 1.6× the time and VRAM and hasn't been tested yet. There's no upscaler installed.
- Wan sometimes "acts" (head turns, expression changes). The drift check catches it; expect to reroll about 1 in 2.
- LTX-2 is installed too, but it's less faithful to the source image. Not used.

## 5. Preview video for the user

```
python -I tools/idle_preview.py assets/characters/<char>/idle/<name> [more folders ...] --labels "..." --out reference/idle/<sheet>.mp4
```
This composites the frames on a dark background, loops them 4 times (about 12 s, H.264) and writes
`<name>_preview.mp4` into each folder. With several folders and `--out` it also makes one side-by-side video. MP4 has
no transparency, so it's for review only.

## 6. Using the output in the game

- `frames/<name>_000..047.png`: RGBA frames. Play them at 16 fps on loop.
- `<name>_spritesheet_8x6.png`: the same 48 frames in a grid of 8 columns and 6 rows, left to right, top to bottom.
- `<name>_alpha.webp`: animated, transparent, loops forever. Good for quick previews or engines that read WebP.
- `<name>_preview.webp`: the same on the generated grey background, for reviewing.
