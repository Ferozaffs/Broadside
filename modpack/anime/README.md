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
- `tools/waifu_gen.py` + `CHARACTER_DESIGN.md`: design and render new characters the same way, from a character
  file (`tools/example_characters.json`): lint against the rules learned from the user's picks, the prompts, the
  render on any ComfyUI (waits for an empty queue, never cancels), pick sheets, picks.
- `tools/swap.py`, `no_men.py`, `male_words.py`, `men.py`, `gen_review.py`: the words.tsv swap's Python twin (checked
  against the game's `--lines-export`), the hunt for men left anywhere in the text, and the review page.
- `tools/lean_voices.py`: the 120 voice descriptions from role + archetype ("voiced like a Japanese anime seiyuu").

Paths in the tools have the user name replaced by `USER`.
