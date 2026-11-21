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

## Built so far (2026-11-21, local, not shipped)

| Class | Hull | Rig | Notes |
|---|---|---|---|
| cutter (`assets/ship_cutter`) | ZWUUUP `assets/parts/hull2_brig/hull2_brig_semi_s33` -> `assets/3d/hull2_brig_semi_s33` | `--type brig` (prep `--yaw 180`) | a blue-and-gold brig, carved scroll bow, 10 guns (57k triangles) |
| gunboat (`assets/ship_gunboat`) | `hull_cutter_semi_s33` (an open launch) | `--type cutter --masts 0.38 --rig-scale 0.9 --bow-gun --bow-gun-at 0.21` | gaff mainsail and two jibs, a bow chaser (33k) |

## Figureheads (shipped in data/parts)

Eagle, lion, mermaid: `tools/prep_part.py ... --height 2.6 --tris 12000 --tex 512` -> `data/parts/figure_<name>.glb`.
Drawn by `ShipModel::Figure` at the stem anchor (`BS_FIGPOSE "y z tilt scale"`). The kraken failed in TRELLIS (a
shredded mesh): reroll it before adding a fourth.
