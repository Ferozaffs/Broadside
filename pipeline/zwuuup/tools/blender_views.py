"""Render a GLB from four angles with Blender (headless), for judging TRELLIS.2 output.

  "C:/Program Files/Blender Foundation/Blender 3.3/blender.exe" -b --factory-startup --python tools/blender_views.py -- <model.glb> <out_dir> [--engine EEVEE|WORKBENCH]

Writes <out_dir>/view_0..3.png (600x600) and prints the triangle count. Tile several models into one
comparison sheet with tools/tile_views.py.
"""
import bpy, math, sys, os
from mathutils import Vector

argv = sys.argv[sys.argv.index("--") + 1:]
glb, out_dir = os.path.abspath(argv[0]), os.path.abspath(argv[1])
engine = argv[argv.index("--engine") + 1] if "--engine" in argv else "BLENDER_EEVEE"
os.makedirs(out_dir, exist_ok=True)

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=glb)
meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
tris = sum(sum(len(p.vertices) - 2 for p in o.data.polygons) for o in meshes)
print("TRIANGLES", tris)

pts = [o.matrix_world @ Vector(c) for o in meshes for c in o.bound_box]
lo = Vector((min(p.x for p in pts), min(p.y for p in pts), min(p.z for p in pts)))
hi = Vector((max(p.x for p in pts), max(p.y for p in pts), max(p.z for p in pts)))
center, radius = (lo + hi) / 2, (hi - lo).length / 2

scene = bpy.context.scene
scene.render.engine = "BLENDER_WORKBENCH" if engine.upper().startswith("WORK") else "BLENDER_EEVEE"
scene.render.resolution_x = scene.render.resolution_y = 600
scene.view_settings.view_transform = "Standard"
if scene.render.engine == "BLENDER_WORKBENCH":
    scene.display.shading.light = "STUDIO"
    scene.display.shading.color_type = "TEXTURE"
else:
    scene.eevee.taa_render_samples = 32
    scene.eevee.use_gtao = True
world = bpy.data.worlds.new("w"); scene.world = world
world.use_nodes = True
world.node_tree.nodes["Background"].inputs[0].default_value = (0.82, 0.82, 0.84, 1)
world.node_tree.nodes["Background"].inputs[1].default_value = 0.9
sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN"))
sun.data.energy = 3.0
sun.rotation_euler = (math.radians(50), 0, math.radians(30))
scene.collection.objects.link(sun)

cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
cam.data.lens = 50
scene.collection.objects.link(cam); scene.camera = cam
dist = radius / math.tan(cam.data.angle / 2) * 0.72
for i, (yaw, pitch) in enumerate([(-90, 8), (-40, 22), (40, 22), (180, 15)]):
    y, p = math.radians(yaw), math.radians(pitch)
    cam.location = center + Vector((math.cos(p) * math.cos(y), math.cos(p) * math.sin(y), math.sin(p))) * dist
    cam.rotation_euler = (center - cam.location).to_track_quat("-Z", "Y").to_euler()
    scene.render.filepath = os.path.join(out_dir, f"view_{i}.png")
    bpy.ops.render.render(write_still=True)
print("DONE", out_dir)
