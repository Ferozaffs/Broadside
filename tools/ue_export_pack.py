# Unreal Editor Python: export a content pack's meshes, animations and textures to FBX / PNG for Broadside.
#
#   UnrealEditor.exe <project.uproject> -ExecutePythonScript="tools/ue_export_pack.py /Game/PackFolder C:/out" -unattended -nosplash
#
# (The full editor, not UnrealEditor-Cmd: exporting a skeletal mesh needs a render scene, and the commandlet has none.
# The script closes the editor when it's done.)
#
# For Fab content under a licence that allows other engines (the Fab Standard License). The exported files are
# source material for tools/*_to_game_*.py; like every model they ship only inside the game's encrypted packs.
import os
import sys

import unreal

args = [a for a in sys.argv[1:] if not a.startswith("-")]
ROOT = args[0] if args else "/Game"
OUT = args[1] if len(args) > 1 else os.path.join(unreal.Paths.project_saved_dir(), "Export")
os.makedirs(OUT, exist_ok=True)

reg = unreal.AssetRegistryHelpers.get_asset_registry()
reg.scan_paths_synchronous([ROOT], True)  # (a commandlet only knows what has been scanned)
assets = reg.get_assets_by_path(ROOT, recursive=True)
print(f"[export] {len(assets)} assets under {ROOT}")


def task(obj, filename, exporter=None, options=None):
    t = unreal.AssetExportTask()
    t.object = obj
    t.filename = filename
    t.automated = True
    t.prompt = False
    t.replace_identical = True
    if exporter:
        t.exporter = exporter
    if options:
        t.options = options
    ok = unreal.Exporter.run_asset_export_task(t)
    print(f"[export] {'ok ' if ok else 'FAILED'} {obj.get_path_name()} -> {filename}")
    return ok


fbx = unreal.FbxExportOption()
fbx.ascii = False
fbx.collision = False
fbx.level_of_detail = False
fbx.vertex_color = True
fbx.export_morph_targets = False
fbx.export_preview_mesh = True  # (animations go out with the mesh they were made for)

counts = {}
for ad in assets:
    cls = str(ad.asset_class_path.asset_name)
    if cls not in ("SkeletalMesh", "StaticMesh", "AnimSequence", "Texture2D"):
        continue
    obj = ad.get_asset()
    rel = str(ad.package_path)[len(ROOT):].strip("/")
    d = os.path.join(OUT, rel)
    os.makedirs(d, exist_ok=True)
    name = str(ad.asset_name)
    if cls == "Texture2D":
        ok = task(obj, os.path.join(d, name + ".tga"))
    else:
        ok = task(obj, os.path.join(d, name + ".fbx"), options=fbx)
    counts[cls] = counts.get(cls, 0) + (1 if ok else 0)
print(f"[export] done: {counts}")
if os.environ.get("BS_UE_KEEP_OPEN") is None:
    unreal.SystemLibrary.quit_editor()
