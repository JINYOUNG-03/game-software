"""Reopen delivered .blend files, evaluate every pose, render representative motion."""

import bpy
import importlib.util
import json
import math
import sys
from pathlib import Path


sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("asset_author", ROOT / "tools/build_assets.py")
author = importlib.util.module_from_spec(spec)
spec.loader.exec_module(author)

report = []
manifest = json.loads((ROOT / "metadata/asset_manifest.json").read_text(encoding="utf-8"))
names = [a["id"] for a in manifest["assets"] if a["category"] in ("human", "animal")]
if "--story-only" in sys.argv:
    names = [name for name in names if name.removeprefix("human_") in author.STORY_HUMANS]
for name in names:
    bpy.ops.wm.open_mainfile(filepath=str(ROOT / "blend" / (name + ".blend")))
    rig = next(o for o in bpy.context.scene.objects if o.type == "ARMATURE")
    meshes = [o for o in rig.children if o.type == "MESH"]
    evaluated_frames = 0
    for action in bpy.data.actions:
        rig.animation_data.action = action
        for frame in range(int(action.frame_range[0]), int(action.frame_range[1]) + 1):
            bpy.context.scene.frame_set(frame)
            graph = bpy.context.evaluated_depsgraph_get()
            for obj in meshes:
                posed = obj.evaluated_get(graph)
                assert all(math.isfinite(c) for v in posed.data.vertices for c in v.co), (name, action.name, frame)
                assert all(v.co.length < 12 for v in posed.data.vertices), (name, action.name, frame, "exploding vertex")
            evaluated_frames += 1
    shots = [("run", 7), ("jump", 21)]
    if name in ("human_protagonist", "animal_tiger") or name.removeprefix("human_") in author.STORY_HUMANS:
        shots.extend([("walk", 9), ("turn_left", 12), ("attack", 16)])
    for clip, frame in shots:
        rig.animation_data.action = bpy.data.actions[clip]
        bpy.context.scene.frame_set(frame)
        author.preview("pose_" + name + "_" + clip, (0, 0, 1.05), 3.2 if name.startswith("human") else 4.0,
                       resolution=600, samples=8)
    report.append({"asset": name, "evaluated_action_frames": evaluated_frames, "status": "pass"})

# Import the actual GLB once, then verify the skinned result and sampled exported action.
bpy.ops.wm.read_factory_settings(use_empty=True)
roundtrip_name = "human_ian_broker" if "--story-only" in sys.argv else "human_protagonist"
bpy.ops.import_scene.gltf(filepath=str(ROOT / "glb" / (roundtrip_name + ".glb")))
rig = next(o for o in bpy.context.scene.objects if o.type == "ARMATURE")
assert len(bpy.data.actions) == 11
run = next(a for a in bpy.data.actions if "run" in a.name)
for track in rig.animation_data.nla_tracks:
    track.mute = True
rig.animation_data.action = run
bpy.context.scene.frame_set(7)
assert sum(o.type == "MESH" for o in bpy.context.scene.objects) >= 30
author.preview("glb_roundtrip_" + roundtrip_name + "_run", (0, 0, 1), 3.2, resolution=600, samples=8)
report.append({"asset": roundtrip_name + ".glb", "roundtrip_actions": len(bpy.data.actions), "status": "pass"})
report_name = "story_human_pose_report.json" if "--story-only" in sys.argv else "blender_pose_report.json"
(ROOT / "metadata" / report_name).write_text(json.dumps(report, indent=2), encoding="utf-8")
print("BLENDER_POSE_CHECK_PASS", len(report))
