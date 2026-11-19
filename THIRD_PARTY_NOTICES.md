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

- **Frigate and cutter**: "Age of sail Blender ship" by NelloF, used under the Fab Standard License.
- **Ship of the line**: the "cgt_frigate_001" frigate from CGTrader (free model), used under the CGTrader Royalty
  Free License, which allows it in a game as long as it is protected from extraction. Converted for the game
  (reduced, textures baked to albedo, stretched to fit the hull).
- **Sailors**: "Pirate" by Bugrimov Maksim, used under the Fab Standard License (exported from Unreal with
  tools/ue_export_pack.py, reduced, textures baked to albedo). Only the pirate's own meshes and textures are used;
  the Unreal sample mannequins and animations that come with the pack are not. The women of the crew are made from
  the same pack's parts (tools/make_female_crew.py: reshaped, with hair and textures derived from the pack's).
- **Sailor animations**: Breathing Idle, Running and Working On Device from Adobe Mixamo (https://www.mixamo.com),
  used royalty free under Adobe's terms for Mixamo content, retargeted onto the pirate
  (tools/ue4char_to_game_crew.py).

## Campaign character portraits (data/portraits)

Generated locally with FLUX.1 [dev] (Black Forest Labs) and the **SXZ Amadmir [FLUX]** style LoRA by
**sadxzero** (https://civitai.com/models/733609), cut out with BiRefNet (MIT). The LoRA's terms require this
credit, and they don't allow the generated images to be sold: the portraits are for this free game only.

## Music (data/music)

Any tracks shipped in data/music were generated locally with YuE2 (multimodal-art-projection, https://github.com/multimodal-art-projection/YuE),
whose terms allow personal creators to use and share its outputs.
