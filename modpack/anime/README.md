# The anime pack (source)

The pack's own text and the tools that made its portraits and words. The method, models and the user's recipe are in
`docs/CONTENT_PIPELINE.md` on `main` ("The anime pack"); the loader's format in `src/mods.h`.

- `words.tsv`, `text.tsv`, `people.tsv`, `voices.tsv`: the pack's text (installed as `mods/anime/`).
- `tools/anime_prompts.py`: every character's portrait prompt (Krea 2 Turbo + Granblue LoRA), written from the game
  portrait's own text (`tools/cast_texts.json`, from `cast_texts.py`) in the recipe the user rated 10/10, the face
  carrying her voice archetype. Redos are appended below the sets (later entries win).
- `tools/anime_set.py`: queue a set on ZWUUUP's feeder (`tools/anime_feed.py` there, which runs `tools/krea2_run.py`)
  and make its pick sheet; seeds A/B/C 1001/2002/3003, redos (`--tag r1`) 4004/5005/6006. `tools/pick.py` records the
  user's picks in `tools/anime_picks.json`. `tools/krea2_outpaint.py` extends a picture downward (a good upper body
  cut off at the waist).
- **Idles (the breathing rig, no video model; the user's pick after rig tests 1-6):** `tools/label_all.py` (on
  ZWUUUP: body-part labels, MediaPipe's multiclass selfie segmenter + Florence-2 for the hair; `seg_mp.py`/`seg_fl.py`
  are the same steps one at a time), `tools/anime_alpha.py` (BiRefNet cut-outs through ComfyUI), then
  `tools/rig_pack.py` (`rig_pack_loop.ps1` follows the other two): each picked portrait and its 48-frame idle, lined
  up by construction, at test 5's settings (sway 7, breath 14, chest 0.08, bust lift 8, hair 4.5x a beat late).
  `tools/rig2.py` is the test version (one picture to frames, for the side-by-side videos).
- **Voices:** `tools/lean_voices.py` -> `voices.tsv`; `tools/voice_jobs.py` / `voice_jobs_ja.py` -> the jobs for
  `directed_voices.py` on ZWUUUP (English, then Japanese with the English references); `tools/voice_fix_fem.py` is
  the onee-san / bokukko / chuunibyou redo (they came out male: plainly a woman's voice, pitch floor 200 Hz).
- **The pack:** `tools/stage_pack.py <dest>` assembles `mods/anime/` (portraits, idles, voices, voices_ja, text.tsv,
  words.tsv, people.tsv) from ZWUUUP and checks it. `tools/cast_sheet.py`: the whole cast as picked, 40 a sheet.
- `tools/waifu_gen.py` + `CHARACTER_DESIGN.md`: design and render new characters the same way, from a character
  file (`tools/example_characters.json`): lint against the rules learned from the user's picks, the prompts, the
  render on any ComfyUI (waits for an empty queue, never cancels), pick sheets, picks.
- `tools/swap.py`, `no_men.py`, `male_words.py`, `men.py`, `gen_review.py`: the words.tsv swap's Python twin (checked
  against the game's `--lines-export`), the hunt for men left anywhere in the text, and the review page.
- `tools/lean_voices.py`: the 120 voice descriptions from role + archetype ("voiced like a Japanese anime seiyuu").

Paths in the tools have the user name replaced by `USER`.
