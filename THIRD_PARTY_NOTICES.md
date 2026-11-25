# Third-party notices

## GarrettGunnell/Water (MIT)

The FFT ocean simulation and water shading are based on https://github.com/GarrettGunnell/Water.

```
MIT License

Copyright (c) 2023 Garrett Gunnell

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## raylib (zlib/libpng license)

Copyright (c) 2013-2024 Ramon Santamaria (@raysan5). https://github.com/raysan5/raylib

## ENet (MIT)

Copyright (c) 2002-2024 Lee Salzman. https://github.com/lsalzman/enet

## Barlow Semi Condensed font (SIL Open Font License 1.1)

Copyright 2017 The Barlow Project Authors (https://github.com/jpt/barlow). Licence text: `data/fonts/OFL.txt`.

## Ship and character models

None of these are in this repository. Release builds carry them only inside the game, encrypted (ChaCha20, with a
key that is not published; see tools/pack_ship.py), as their licences require.

- **Frigate**: "Age of sail Blender ship" by NelloF, used under the Fab Standard License.
- **Ship of the line**: the "cgt_frigate_001" frigate from CGTrader (free model), used under the CGTrader Royalty
  Free License, which allows it in a game as long as it is protected from extraction. Converted for the game
  (reduced, textures baked to albedo, stretched to fit the hull).
- **Sailors**: "Pirate" by Bugrimov Maksim, used under the Fab Standard License (exported from Unreal with
  tools/ue_export_pack.py, reduced, textures baked to albedo). Only the pirate's own meshes and textures are used;
  the Unreal sample mannequins and animations that come with the pack are not. The women of the crew are made from
  the same pack's parts (tools/make_female_crew.py: reshaped, with hair and textures derived from the pack's).
  Since v0.24 the crew on deck are generated figures (see the 3D parts below) skinned onto the pack's skeleton
  (tools/rig_crew.py).
- **Sailor animations**: Breathing Idle, Running and Working On Device from Adobe Mixamo (https://www.mixamo.com),
  used royalty free under Adobe's terms for Mixamo content, retargeted onto the pirate
  (tools/ue4char_to_game_crew.py).

## Campaign character portraits (data/portraits), the logo and the icon (data/logo.png, data/icon.png)

Generated locally with FLUX.1 [dev] (Black Forest Labs) and the **SXZ Amadmir [FLUX]** style LoRA by
**sadxzero** (https://civitai.com/models/733609), cut out with BiRefNet (MIT). The LoRA's terms require this
credit, and they don't allow the generated images to be sold: the portraits, the logo and the icon are for this free game only.

## Carved figureheads and other 3D parts (data/parts)

Made locally: a reference image with FLUX.1 [dev] (Black Forest Labs; no LoRA), turned into a 3D model with TRELLIS.2
(Microsoft, MIT licence) in ComfyUI, then cut down for the game with Blender (tools/prep_part.py). The FLUX.1 [dev]
licence lets its outputs be used for any purpose other than training competing models.

The cutter, the escorts' gunboat and the ghost ship (the Flying Dutchman) are made the same way: a generated hull
rigged with masts, yards and sails in Blender (tools/transom.py, tools/rig_ship.py; docs/kitbash.md), and the
Tropics' and Ember Reach's palms, plants, charred trees and spires too.

## Idle animations of the portraits (the idles pack, mods/idles)

Made locally from the portraits above with Wan 2.2 I2V (Alibaba, Apache 2.0) and the lightx2v step-distillation
LoRAs (Apache 2.0) in ComfyUI, the first and last frame the portrait itself, cut out with BiRefNet (MIT). They are
the portraits in motion, so the Amadmir LoRA's terms above apply to them too: for this free game only.

## The anime pack (mods/anime)

An optional pack, shipped as its own download.
- **Portraits:** generated locally with Krea 2 Turbo (Krea; the Krea 2 Community License, whose terms leave the
  outputs to the user who made them) and the **Granblue Fantasy Style** LoRA (Krea2Turbo version) by **Azriaelis**
  (https://civitai.com/models/2743862), whose terms allow the images it makes to be used; the LoRA itself isn't
  shipped. Cut out with BiRefNet (MIT). A fan-made style, in a free game.
- **Idles:** the portraits themselves, moved by a procedural warp (the pack's `tools/rig_pack.py`: breathing, a sway,
  hair and cloth following). The body parts that place the warp were found with MediaPipe's multiclass selfie
  segmenter (Google, Apache 2.0) and Florence-2 (Microsoft, MIT).
- **Voices:** English and Japanese, rendered locally with Qwen3-TTS (Qwen, Alibaba; Apache 2.0) as the game's own
  are, from written voice descriptions. No real person's voice was cloned.

## Voices (data/voices)

Every spoken line was rendered locally with Qwen3-TTS (Qwen, Alibaba; Apache 2.0): VoiceDesign for each character's
voice from a written description, acted line by line with a direction for where it's said. No real person's voice
was cloned.

## Music (data/music)

Any tracks shipped in data/music were generated locally with YuE2 (multimodal-art-projection, https://github.com/multimodal-art-projection/YuE),
whose terms allow personal creators to use and share its outputs.
