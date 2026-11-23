# Kitbashed ships and generated 3D parts

The game's frigate (NelloF, hand-made) is the quality bar. Generated 3D (TRELLIS.2 on ZWUUUP, see the art pipeline's
`docs/3D_PIPELINE.md`) is good at solid shapes and bad at thin ones: rigging comes out as blobby nets, sails as thick
slabs, flags vanish, and the whole ship is one fused mesh (the game needs its sails apart, to sway, light and paint
them). So a ship is built from parts:

1. **Hull** (TRELLIS.2): a reference with no rig, from ZWUUUP's `tools/make_parts.py`. The wording that works for
   small and middling ships: "a museum scale model of the hull of ..., shown with its masts taken out for
   restoration" (`hulls2.py`). Galleons still come out rigged; try again or strip one in Blender. Validate
   (`tools/validate_ref.py ... --kind prop`), then `tools/make_3d.py --max-faces 60000`.
2. **Prep** (here): `blender -b --factory-startup --python tools/prep_part.py -- hull.glb hull_prepped.glb --align
   --length 30 --tris 40000 --tex 1024 [--yaw 180]`. `--align` lays the long axis fore and aft but can't tell bow
   from stern: render it, and if the bowsprit lands on the transom, prep again with `--yaw 180`.
2b. **Transom** (here, for a double-ended hull): `blender -b --factory-startup --python tools/transom.py --
   hull_prepped.glb hull_t.glb [--cut 0.06] [--fill 0.72] [--windows 4|0] [--paint r g b]`. TRELLIS often makes the
   stern a second stem, so the ship reads as having two fronts (the user, 2026-11-23): this fills out the quarters,
   cuts the stern off square (raked), caps it with a planked board (gold rail, stern windows) and hangs a rudder.
   Run it on both the hull and its `_lo` hull. The cutter: `--windows 4 --paint 0.07 0.10 0.20`; the gunboat
   (a launch): `--windows 0 --cut 0.07 --paint 0.09 0.06 0.04`.
3. **Rig** (here): `blender -b --factory-startup --python tools/rig_ship.py -- hull_prepped.glb ship.glb --type
   brig|cutter|ship [--masts 0.30 0.58] [--rig-scale 1] [--preview dir]`. It reads the deck and the beam off the
   hull, then builds masts, yards, square sails (canvas texture: seams, tabling, weathering), a gaff spanker, jibs,
   shrouds with ratlines, stays, lifts and braces; and re-planks the deck (TRELLIS smears colour over an open deck).
   Materials: `sail_canvas` (the game's sail keys), `rope_tar` (mesh `rope_rigging`), `spar_wood`, `deck_planks`.
   Deck fittings, each set down on the deck by a ray cast onto the hull: guns along both sides from
   `data/parts/prop_cannon.glb` (`--guns N` a side: brig 5, ship 7, cutter 0; cut to ~1200 triangles each),
   `--bow-gun [--bow-gun-at 0.21]` for a chaser, a wheel, a capstan and hatch gratings over a dark hold
   (`--no-fittings` leaves them off). The cannon model's muzzle points glTF -z (Blender +Y): turn it to aim.
4. **Far detail** (`ship_lo.glb`, as the frigate has): prep the hull again with `--tris 6000 --tex 512` and rig it
   with `--lite` (no ratlines, lifts or braces; coarser sails): about 6-7k triangles against 44k.
5. **Into the game**: `assets/ship_<class>/ship.glb` + `ship.cfg` (`length=41`, `keel=-1.5`, `sails=sail,canvas`).
   The keel matters: at the default -3.6 a shallow generated hull sits so deep that the sea shows through its deck.
   Release staging packs every `assets/ship*` folder.
   `prep_part.py --width W` gives a tubby hull a beam of its own; `--desat k` washes the paint toward grey.

## Built so far (2026-11-21, local, not shipped)

| Class | Hull | Rig | Notes |
|---|---|---|---|
| cutter (`assets/ship_cutter`) | ZWUUUP `assets/parts/hull2_brig/hull2_brig_semi_s33` -> `assets/3d/hull2_brig_semi_s33` | prep `--yaw 180`, transom, `--type brig --no-course` (two square sails a mast) | a blue-and-gold brig, carved scroll bow, 10 guns (57k triangles) |
| gunboat (`assets/ship_gunboat`) | `hull_cutter_semi_s33` (an open launch) | `--type cutter --masts 0.38 --rig-scale 0.9 --bow-gun --bow-gun-at 0.21` | gaff mainsail and two jibs, a bow chaser (33k) |
| ghost: the Flying Dutchman and the Fog's "Drowned" ships (`assets/ship_ghost`) | ZWUUUP `hull4_ghost_semi_s99` (hulls3's s22 failed the frame checks; hull4 asked for a small ship on a plain ground) | prep `--align --yaw 180 --length 30 --width 9 --desat 0.65`, rig `--type ship --tattered --guns 6`; cfg `keel=-4` | rotten grey-green canvas, rents and ragged feet per sail; TRELLIS made her tubby (12.5 m beam on 30 m), so `--width` narrows her (66k / lo 7.8k) |

## Figureheads (shipped in data/parts)

Eagle, lion, mermaid: `tools/prep_part.py ... --height 2.6 --tris 12000 --tex 512` -> `data/parts/figure_<name>.glb`.
Drawn by `ShipModel::Figure` at the stem anchor (`BS_FIGPOSE "y z tilt scale"`). The kraken failed in TRELLIS (a
shredded mesh): reroll it before adding a fourth.

## Biome props (2026-11-23)

The Tropics' palms and banana plants and the Ember Reach's charred trees and obsidian spires are TRELLIS models from
approved references (ZWUUUP biome2.py / biome_3d*.ps1), prepared with `tools/prep_part.py` into `data/parts/biome_*.glb`
plus a `_lo` copy (600 triangles, 128 px textures) for range:

| prop | source | prep |
|---|---|---|
| biome_palm | tree_palm_semi_s65 | `--height 11 --tris 4000 --trim-base 0.06` (the sand disc, the user's G3 waiver) |
| biome_banana | plant_banana_semi_s52 | `--height 3.6 --tris 3000` |
| biome_charred | tree_charred_semi_s52 | `--height 8 --tris 3000 --trim-base 0.05` (its ground disc) |
| biome_spire | spire_obsidian_semi_s26 | `--height 12 --tris 2000 --desat 0.45` (it lit up magenta) |

`_lo`: the same with `--tris 600 --tex 128` (pass `--tris` once: the script reads the first). Scenery::Build scatters
them over each Tropics / Ember island, standing them on the island's mesh as drawn (a ray down its land triangles:
RockHeight leaves them metres under the weathering) and on ground flat enough for each; DrawIslands draws them in full
within 320 m, the low copy to 1800 m. `BS_FLORALOG=1` prints the counts. The sandstone boulder and lava rock came out
of TRELLIS as shards at 30k faces and are being redone.
