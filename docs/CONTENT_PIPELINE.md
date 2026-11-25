# How Broadside's content was made (and how to make more)

The story, the people, their faces, idles and voices, the music and the generated 3D were all made by an agent working
with the user, on the user's own PC ("ZWUUUP", a Windows box with a 12 GB GPU running ComfyUI). This
is the method, the models, the prompts and the rules learned on the way, so the next agent can carry on in the same
voice and style. The scripts themselves are archived in `pipeline/` (see the end); the live copies are on ZWUUUP.

## 0. Working rules (all of them learned the hard way)

- **The user picks.** Everything generated goes to the user as a contact sheet (2-3 seeds per character, labelled
  A/B/C) and only the picks go in. Send sheets as they're ready, a few characters at a time, so picking runs alongside
  the rendering. Redo = new seeds (same prompt) or a rewritten prompt for what the user named; never guess more fixes.
- **ZWUUUP's ComfyUI is the user's** (port 8188, they use it for their own music and video): never start a second
  ComfyUI, queue only when its queue is empty, never cancel their jobs. Every job goes through `comfy_guard.py`
  (waits for the server and an empty queue, re-runs a job that died with the server, interrupts a hung one).
- Models and big outputs on the models drive, not the system drive. Clear the raw ComfyUI outputs of rejected runs.
- Two GPU jobs at once (Wan + TRELLIS, TTS + anything) run out of 12 GB: sequence them.
- Launch long jobs detached (`Invoke-CimMethod Win32_Process Create` from a .ps1 run over ssh), log to a file.
- Licences decide the models: the Amadmir LoRA needs credit and its images can't be sold (fine for the free game,
  THIRD_PARTY_NOTICES.md); only style models whose licence allows a game; the bought ship models only
  ship encrypted (`tools/pack_ship.py`).

## 1. The story

The campaign's story is in `docs/campaign.md` ("The story"); the lines in `src/people.h`. How it was built:

1. **Propose a whole arc first**, one paragraph, and let the user react before writing lines. The user's test was
   motivation: "what is the motivation to go to ember reach, tropics etc. That isnt just to get stronger". So every
   region's story serves the end: Lord Admiral Ashgrave betrayed her and took her ship (the Sovereign) and her first
   mate Mara; his five captains are the five dread pirates, one to a region; the Tidebell was broken into five and each
   piece lies under a region's beast; each region gives back one of her old officers (bosun, navigator, master gunner,
   surgeon, quartermaster). The last story pays all of it off (the Armada, the Sovereign retaken, Mara freed from the
   Storm Caller).
2. **Open with the loss** (the prologue: the Sovereign's last night, played, not told; then a cutter at Port Haverly).
3. **Let people carry it**: the dread pirates' hails know who she is; rescued passengers tell how they got there and
   what they saw, and an omen that darkens with each story told; the taverns say where the next story is.
4. **Backstory as optional play**: the user asked for flashbacks that "not interrupt the game flow ... an option when
   in harbor". Memories are five short scenes at sea played the prologue's way, with a narrator, chosen in port.
5. **Variety over length**: each region has its own work and hazard (smugglers and the law, reef salvage and the
   hurricane, a whaler in the ice, an island to evacuate, the lost lighthouse keeper); six lines for every keeper and
   sea voice; no name twice in a while. A bot plays whole careers (`BS_QATEST`, the career bot) to find where it gets
   repetitive.

Writing style: short, period-flavoured, spoken lines (no stage directions in them), each with a job (a greeting, a
rumour, a complaint about money); the player is always "she" / "Captain".

## 2. Character design

- Every face has **one clear concept, a signature prop, a colour of its own, an attitude, layered clothes** (no bare
  chests). The user rated these "top tier" and they're weighted 2x in the face pools. Period accuracy matters less than
  character, but no anachronisms (a modern pistol, a wristwatch, six fingers: redo).
- Plenty of women at sea (smugglers, castaways, captives, navy, merchants, rivals, pirates); a name that's a woman's
  gets a woman's face. The same ship always has the same captain; the least-seen face comes first.
- The user wanted the crew "more interesting and sexier/hotter": attractive, characterful pirates (fitted corsets,
  off-shoulder blouses, open shirts, tattoos, jewellery, feathered hats), stylized-realistic, SFW.

## 3. Portraits (the game: Flux + Amadmir)

- **Model:** Flux.1 Dev GGUF Q6_K + the "SXZ Amadmir [FLUX]" LoRA (`--style amadmir`, always). Comic-book ink linework,
  fine hatching, muted colours. `pipeline/zwuuup/tools/generate.py`, `run_cast.py`, `pipeline/zwuuup/CLAUDE.md`.
- **Prompt:** the style and framing text is fixed (never edit it per character); only the CHARACTER text changes:
  `The character is <who, age, build, face, hair>, flintlock era. <He/She> wears <layered clothes, colours>, and holds
  <signature prop> raised in <his/her> right fist by <his/her> shoulder. <Two or three words of attitude>.`
  Every character's text: `pipeline/zwuuup/cast.json` and the later lists (`cast_more2.py`, `sea2.py`, `story_cast.py`,
  `wrights*.py`, `cast_redo.py`, `cast_keep.py`).
- **Framing:** the game cuts the body at mid-thigh. Props held up by the shoulder in a fist (tools lying across the
  shoulders with no hand warp in the idle). A full-length render is cut at a fraction of its height
  (`[seed, cut]` in `tools/portrait_picks.json`).
- **Picks:** 2-4 seeds a character, a sheet, the user's choice into `tools/portrait_picks.json`, then
  `tools/import_portraits.py` (alpha cut-out, the mid-thigh cut, 820 px tall, 256 colours).

## 4. Idles (Wan 2.2)

`pipeline/zwuuup/docs/IDLE_ANIMATION.md` has the method. In short:
- Wan 2.2 first/last frame from the portrait (the same image both ends, so it loops), 48 frames at 16 fps, 576x736
  (or the portrait's own shape at the same pixel count: a wide portrait squeezed into 576x736 won't line up). Motion
  overall: a slow sway, breathing; effects that move on their own (a torch flame, pipe smoke) named in `--motion`.
- A cfg ladder 1.0 / 1.25 / 1.5 / 2.0, first pass wins: higher cfg keeps the mouth still, lower keeps the colour.
- Checks on every run: the loop seam (last->first jump vs a normal step), talking (mouth box change), colour drift
  (`colour_drift.py`: skin warming mid-loop, reject dE > 6) and local flashes (`flash_check.py`: > 9% of the figure
  brightening). Face and mouth boxes from `detect_faces.py`, by hand where it misses.
- `pick_best.py` ranks every run (the user's order: talking is acceptable, a seam is bad, extreme colour shifts are the
  worst) and writes `best_idles.json`; `idle_choice.json` pins the user's picks. **Every character gets one** (the user:
  some animated and some not is weirder): the best attempt where none passed. Re-roll, never composite.
- `tools/import_idles.py` lines the frames up with the game portrait and writes `data/idles/<id>.png` (8x6 frames).
  From v0.25 the idles ship as their own pack (`mods/idles/`), not in every release zip.

## 5. Voices (Qwen3-TTS)

- Qwen3-TTS 1.7B (Apache 2.0) on ZWUUUP in its own venv (`pipeline/voice/tts_setup.ps1`; torch from the cu130
  index). It can't share the GPU with ComfyUI: render when the queue is empty, free ComfyUI's models first.
- **Every line acted**: `broadside --lines-export` writes every line with where it's said; `make_jobs.py` gives each a
  direction = the scene (`SCENE`, from where it's said) + the feeling of that line (`feel.tsv`, written per unique
  text: only ~520 unique texts in ~2,600 lines). `directed_voices.py`: VoiceDesign per line with the character's voice
  description + the direction, 6 takes, keep the take whose speaker embedding is closest to the character's reference.
  (Cloning one reference was flat: "lack a bit of character and doesn't take context into consideration".)
- Voice descriptions (`pipeline/zwuuup/voices.json`: id -> [sex, description]). VoiceDesign skews men high: references
  are pitch-checked (men 80-150 Hz, women 160-255) and takes far above the reference dropped.
- Files: `data/voices/<id>/<hash>.ogg` (hash = `sfx::LineHash` of the line); a line with no file is babbled.

## 6. Music (YuE2)

`pipeline/music/make_music.py tracks.json` through the user's ComfyUI (YuE2): each track a style line (genre,
instruments, BPM, mood) and lyrics with `[verse]`/`[chorus]` tags (`tracks.json`). Each region its own sea music (two
takes), four battle takes, each boss its own theme; the user picks takes; normalised to -16 LUFS.

## 7. 3D (TRELLIS.2) and kitbashing

`pipeline/zwuuup/docs/3D_PIPELINE.md` and `docs/kitbash.md`. A Flux reference, `tools/validate_ref.py` (a mandatory
approval before any 3D: one object, clean margins, a plain background...), `make_3d.py` (TRELLIS.2), then
`tools/prep_part.py` in Blender (one mesh, facing +z, on y = 0, a set height, fewer triangles). TRELLIS is good at solid
shapes, bad at rigging and sails: hulls are generated bare and rigged by `tools/rig_ship.py`; crew are skinned onto the
sailors' skeleton (`tools/rig_crew.py`). A big mesh that runs out of memory in the remesh: `make_3d.py --remesh-res 512`.

## 8. The anime pack (Krea 2 + Granblue) - on the `modloader` branch

An optional pack (`mods/anime/`), the user's "waifu" experiment: every character a woman ("no men", offstage too), new
faces, new voices in English and Japanese. The pack's text, tools and prompts are in `modpack/anime/` on `modloader`.
- **Portraits:** Krea 2 Turbo (fp8) + the Granblue Fantasy Style LoRA (Krea 2 version, 1.0), 8 steps, cfg 1,
  euler/simple, 896x1152, ~21 s each. The prompt wraps the character text in `granblue fantasy style. Anime fantasy
  character portrait, waist-up, facing the viewer, plain pale grey background, clean full colour illustration.`
- **The recipe the user rated 10/10** (Dread Calypso): beautiful and alluring - full lips, a fitted corseted bodice
  with cleavage, a hand on the hip, a knowing look; older characters glamorous mature women. Each keeps her game
  portrait's concept, prop and colours; a man becomes a woman of the same role (often younger); the face carries her
  voice archetype (a tsundere pouts with pink cheeks, a kuudere stares flat and calm, an ojou-sama laughs behind a fan).
  Every character is an adult: "ancient but looks young" and "bratty" types are written as grown women.
- Sets of four characters, seeds A/B/C = 1001/2002/3003 (redos 4004/5005/6006), fed to ZWUUUP as they're written
  (`anime_feed.py`), a sheet per set to the user. A good upper body cut off at the waist: extended downward
  (`krea2_outpaint.py`: pad below, inpaint only the pad).
- **Words:** `words.tsv` whole-word swaps (he -> she, lads -> lasses, names -> women's names) plus `text.tsv` for the
  lines that need rewriting; ships keep "man" (man-o'-war, merchantman).
- **Voices:** 120 archetype voices (`voices.tsv`), each "voiced like a Japanese anime seiyuu, a <archetype>: <delivery>",
  no Western accents; Japanese renders keyed by the English line's hash.

## The archive (`pipeline/`)

Copies of the scripts and the prompt/pick files, as they stood at v0.24.0 (paths have the user name replaced by
`USER`; set them for the machine). The live pipeline is on ZWUUUP:
- `pipeline/zwuuup/` - `Documents/PirateGame-ArtPipeline` (portraits, idles, voices, 3D, anime renders; its own
  `CLAUDE.md` and `docs/`). The batch scripts (`crew*.py`, `hulls*.py`, `parts*.py`, `houses*.py`, `sea2*.py`...) are kept for their prompts;
  one-off fixes and diagnostics were left out.
- `pipeline/music/` - `Documents/PirateGame-Music`.
- `pipeline/voice/` - the voice jobs (`make_jobs.py`, `feel.tsv`) and the TTS setup.

Not archived: the user's own captain (fleet_7) is drawn from the user's likeness; its prompt stays off the repo.
