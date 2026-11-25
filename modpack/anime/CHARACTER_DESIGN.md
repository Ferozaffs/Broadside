# Designing anime characters (waifu_gen)

How the anime pack's 120 portraits were designed, written so another agent can design new characters the same way
and render them with `tools/waifu_gen.py`. Every rule here comes from the user's own picks and redo notes.

## 1. The workflow

1. **Write the characters** into a character file (section 3). Work in sets of 3 or 4. Base each one on the
   character's role in the game: who she is, her signature prop, her colours, her voice archetype.
2. **Check:** `python -I tools/waifu_gen.py lint chars.json`. Errors block the render. Warnings are lessons from
   earlier redos: fix them unless you have a reason not to.
3. **Read the prompts:** `waifu_gen.py prompts chars.json`. They should read as one clear picture.
4. **Render:** `waifu_gen.py render chars.json --free`. Each character gets three seeds, or six for mid-thigh framing,
   at about 20 s each on a 12 GB card.
5. **Make the sheet:** `waifu_gen.py sheet chars.json`. Send it to the user and ask for a letter per character (for
   example "A c b redo").
6. **Record the picks:** `waifu_gen.py pick chars.json pirate_9=B castaway_6=E`. For a redo, see section 6.

## 2. The recipe (rated 10/10 by the user)

The tool wraps every character in a fixed style and framing:

> `granblue fantasy style. Anime fantasy character portrait, waist-up, facing the viewer, plain pale grey background,
> clean full colour illustration.` + the character

The character text always follows the same order, and the format's fields map onto it:

> The character is a **[build] [origin] [role]** woman **[age]**, **[figure]**, with **[hair]**, **[face]**. She wears
> **[outfit]**, **[legs]**, and **[prop or pose]**. **[Expression]**.

Example (pirate_9, picked):

> The character is a charming, beautiful young French pirate woman in her twenties with long wavy chestnut hair, a
> beauty mark, a single pearl earring and full glossy lips. She wears an unbuttoned lace-cuffed blouse over a fitted
> lilac brocade corset and a faded lilac coat, and stands side-on in a fencer's en garde, her rapier pointed straight at
> the viewer, her other hand raised elegantly behind her. A charming, dangerous, amused smile that enjoys the viewer's
> discomfort.

What makes her work:
- **Beautiful and alluring:**
  - full lips, always named
  - a fitted, corseted bodice or a low neckline
  - a knowing look
- **Older characters** are glamorous mature women in their forties, never old.
- **One clear concept**, a **signature prop**, a **colour of her own**, attitude, and layered clothes. These are the
  faces the user rated top tier.
- **The face carries her voice archetype** (section 5).
- **Period clothes.** The setting is the age of sail. A fantasy touch is fine, but nothing modern.

## 3. The character file

The file is JSON: `{"defaults": {...}, "characters": [{...}, ...]}`. Each character inherits the defaults.
`tools/example_characters.json` is a worked example.

| field | required | what to write |
|---|---|---|
| `id` | yes | unique, used in the file names (`<id>_<tag>_s<seed>.png`) |
| `role` | yes | her job: "pirate quartermistress", "ship's surgeon", "harbourmistress" (the tool adds "woman") |
| `age` | yes | "in her twenties", "in her thirties", "in her forties". For ageless types: "an adult woman who looks like a young woman in her early twenties" |
| `build` | | 1-3 adjectives ending in her look: "cool, beautiful", "tall, gorgeous", "tomboyish, beautiful" |
| `origin` | | "young French", "Black Caribbean", "Nordic". Say "young" here when she should read young |
| `figure` | | "slender", "toned and curvy", "sunburnt and slender" |
| `hair` | | length, colour and style in one phrase: "long straight black hair in a high ponytail tied with a red silk ribbon" |
| `face` | | eyes, marks, jewellery and lips: "a beauty mark, a single pearl earring and full glossy lips" |
| `outfit` | | from the outside in, fitted and period: "a faded lilac coat over a fitted lilac brocade corset" |
| `legs` | for mid-thigh | what's on her hips and thighs: "tight white breeches and tall polished boots hugging her curvy hips and thighs" |
| `prop` | one of these two | what she holds and where: "holds a glinting scalpel up between two fingers beside her face" |
| `pose` | | the whole body when the pose is the point: "stands with her arms folded under her chest, a cutlass tucked under one arm" |
| `expression` | | the archetype's face and mood: "shaken but indignant, cheeks flushed, glaring" |
| `framing` | | `waist-up` (default), `mid-thigh` (legs in the picture: needs `legs`, six seeds), `close` (head to waist), or your own phrase |
| `seeds` | | default `[1001, 2002, 3003]`, or `[4004 ... 9009]` for mid-thigh |
| `tag` | | default `wf`. For a redo use `r1`, `r2`... (new files beside the old ones) |
| `archetype` | | her voice type (section 5). Not part of the prompt: it's a note for the expression and the voice |
| `prompt` | | the whole character text, replacing the fields. Use it only when the fields can't say it |

Write `prop`, `pose` and `expression` as phrases that continue the sentence ("holds...", "stands...", "a sly
grin..."). The tool adds "She wears ..., and" and the full stops.

## 4. Rules (each one learned from a redo)

**Hard rules** (lint errors):
- **Every character is an adult**, in her twenties or older. "Ancient but looks young" and bratty types are written
  as grown women. Never write teen, girl, child or anything like them.
- **Mid-thigh framing needs `legs`.** Without them the model crops at the waist, and the user's note will be
  "leg issue". Outpainting the legs afterwards didn't work: re-render with the legs written in.

**Lessons** (lint warnings):

| avoid | why (the user's note) | write instead |
|---|---|---|
| nose ring, nose piercing | "nose ring looks weird", twice | earrings |
| sweater, jumper, hoodie, jeans | "too modern sweater", twice | a linen blouse, a laced jerkin, a corset, a coat |
| huge, massive, bodybuilder | "too big", "should be slimmer" | tall, athletic, strong, slender |
| buzz cut, boyish grin or face | read as a man ("too boyish") | a pixie cut with long side bangs, a soft pretty face, long eyelashes; "tomboyish" |
| old, elderly, wise old eyes | "too old" | a glamorous mature woman in her forties; or a youthful face with knowing eyes |
| crouching | "too many legs" | standing, her weight on one hip |
| peg-leg, full-body props | "too zoomed out" | something that keeps her waist-up |
| sun-kissed | darkened her skin more than intended | name the skin tone outright |
| a tool across the shoulders | warps in the idle animation | the tool gripped in a hand |
| the same pose in a whole set | "poses too similar" | a different pose for each (see below) |
| a strange outfit (chain-mail bodice, iron jaw plate) | "strange outfit" | plausible period clothes; one fantasy accent at most |
| a plain prop held low, no accent | "too boring" | the prop held up near the face, an accent colour (a red ribbon, a sash), a raised eyebrow |

**Poses.** "Raised in one fist by her shoulder" is the model's favourite. Use it for at most one character in a
set. Other poses that came out well:
- a hand on her hip, the other holding the prop
- arms folded under the chest
- the prop resting on a shoulder with a hand on it
- a rapier pointed at the viewer
- a fan or banknotes raised by her face
- a hand over one eye (chuunibyou)
- a pistol held against the cheek while blowing a kiss
- a prop clutched to the chest

## 5. Archetypes: what her face does

Each character has one voice archetype, and her expression should show it at a glance. These are the anime pack's
18 (`voices.tsv` has each voice).

| archetype | face and expression |
|---|---|
| tsundere | pouting, pink cheeks, glaring, "it's not like I care" |
| kuudere | calm, flat, half-lidded stare, almost no smile |
| dandere | shy, blushing, glancing away, a small smile |
| deredere | sweet, beaming, open affectionate smile |
| genki | huge open-mouthed grin, bursting with energy |
| onee-san | teasing, knowing big-sister smile, confident |
| ara-ara | gentle, warm, motherly smile, a soft hand to the cheek |
| yandere | sweet adoring smile that's a little unsettling, head tilted |
| sadodere | amused smirk that enjoys the viewer's discomfort |
| sukeban | cocky delinquent scowl or smirk, looking for a fight |
| bokukko | relaxed, blunt, cheeky grin (a tomboy with a pretty face) |
| ojou-sama | haughty, proud laugh behind a fan (or banknotes) |
| yamato nadeshiko | serene, polite, gentle smile, graceful |
| kamidere | regal, imperious, looking down at the viewer, godlike calm |
| chuunibyou | dramatic pose, one hand over an eye, a grandiose cackle |
| gyaru | bubbly wink, playful, laughing |
| mesugaki | smug, teasing grin, mocking (written as a grown woman) |
| loli-baba | an adult with a youthful face, ancient knowing eyes, amused |

## 6. Picks and redos

- **Sheets** have one row per character. Columns A, B, C are the seeds in order (A-F for six). The user answers with
  a letter per row, or "redo" with a note.
- **"Redo" with no note:** same prompt, new seeds. Set `tag` to `r1` and `seeds` to `[4004, 5005, 6006]`.
- **"Redo" with a note:** change only what the note names, then re-render with a new `tag`. Typical notes:
  - "leg issue": add `legs`, set framing to mid-thigh, use six seeds
  - "nose ring": swap it for earrings
  - "too boyish": soften the face
  - "too zoomed out": closer framing
  - "poses too similar": a new pose for each
- **Keep the old character** as it was. Write the redo as a new entry with the same `id` and a new `tag`, or keep a
  separate redo file, so it's clear which render was picked. `picks.json` records `seed`, `tag` and `file`.
- **Flag what you see** on the sheet when you send it (props merging, extra fingers, a costume that reads modern),
  but don't redo anything the user didn't ask for.

## 7. Setup and server rules

- **Models on the ComfyUI server** (the tool checks them before rendering):
  - `diffusion_models/krea2_turbo_fp8_scaled.safetensors`
  - `text_encoders/qwen3vl_4b_fp8_scaled.safetensors`
  - `vae/qwen_image_vae.safetensors`
  - `loras/granblue_fantasy_krea2.safetensors` (the Granblue Fantasy Style LoRA, Krea 2 version)

  The user downloads the models. Never ask for or handle their Civitai API key, and check the LoRA page's licence
  terms before releasing anything made with it.
- **Sampler settings:** 8 steps, cfg 1, euler/simple, an empty negative, 896x1152 (about the game's portrait shape).
- **The ComfyUI server is the user's.** One job at a time, queued only while its queue is empty. The tool waits; it
  never cancels anyone's job. Never start a second ComfyUI. Use `--free` to unload the models afterwards, so other
  GPU work (voices, video) gets its memory back.
- **Outputs:** `<out>/<id>_<tag>_s<seed>.png`, `render_log.jsonl` (every prompt as sent), `picks.json`, and the
  sheets. The default `<out>` is `portraits/` beside the character file.
