"""Blender-only asset authoring. Run with Blender --background --python this_file.

All outputs are confined to the sibling haedong_lowpoly directory. No game code.
"""

import bpy
import json
import math
import random
import sys
from pathlib import Path
from mathutils import Vector, Quaternion


ROOT = Path(__file__).resolve().parents[1]
random.seed(87)
for folder in ("blend", "glb", "textures", "previews", "metadata"):
    (ROOT / folder).mkdir(parents=True, exist_ok=True)

PALETTE = {
    "skin": "B99476", "skin_warm": "A8795D", "skin_light": "C5A88A",
    "ink": "282D30", "hair": "292725", "ivory": "D0C5A7",
    "indigo": "354F66", "indigo_light": "536C7C", "hemp": "9B906E",
    "persimmon": "985C40", "jade": "607B68", "red": "774840",
    "wood": "70523A", "wood_dark": "463A30", "plaster": "C1B89C",
    "roof": "465252", "iron": "555B57", "bronze": "8E7951",
    "ceramic": "655348", "paper": "D4C9A6", "water": "617C7C",
    "pine": "405C4B", "leaf": "64754D", "leaf_light": "829161",
    "grass": "838361", "earth": "81725A", "stone": "777A70",
    "tiger": "AF824D", "fur_white": "C7BDA0", "wolf": "787D77",
    "deer": "92704E", "ox": "665147", "hoof": "3D3C35",
    "hair_gray": "777873", "ink_blue": "303F4C",
}
HUMAN_PRESETS = ("protagonist", "porter", "herbalist", "mira_engineer", "doyun_clerk",
                 "serat_inspector", "ian_broker", "haram_apprentice")
STORY_HUMANS = {
    "mira_engineer": {"name_ko": "미라 벤", "role_ko": "배수로 기술자", "skin": "skin",
                      "cloth": "persimmon", "head_x": .95, "head_y": .96,
                      "silhouette": "back bun, head wrap, split work apron, tool belt"},
    "doyun_clerk": {"name_ko": "도윤 렌", "role_ko": "부두 조합 서기", "skin": "skin_light",
                    "cloth": "indigo_light", "head_x": 1.10, "head_y": 1.05,
                    "silhouette": "gray temples, moustache, long split vest, ledger satchel"},
    "serat_inspector": {"name_ko": "세라트 경", "role_ko": "항만청 조사관", "skin": "skin_warm",
                        "cloth": "ink_blue", "head_x": 1.07, "head_y": 1.02,
                        "silhouette": "low helmet, padded shoulder vest, official badge, sealed document case"},
    "ian_broker": {"name_ko": "이안 노을", "role_ko": "야시장 중개인", "skin": "skin",
                   "cloth": "jade", "head_x": .96, "head_y": 1.10,
                   "silhouette": "gat hat, pointed beard, open long coat, coin purse"},
    "haram_apprentice": {"name_ko": "하람", "role_ko": "견습 수리공", "skin": "skin_warm",
                         "cloth": "hemp", "head_x": 1.03, "head_y": .94,
                         "silhouette": "tied headband, low ponytail, short patched apron, blueprint roll"},
}
MATS = {}
MANIFEST = {"pack": "haedong_lowpoly", "version": "1.0.0", "assets": []}
bpy.context.preferences.filepaths.file_preview_type = "NONE"
bpy.context.preferences.filepaths.save_version = 0


def linear(v):
    return v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4


def rgb(hex_value):
    return tuple(linear(int(hex_value[i:i + 2], 16) / 255) for i in (0, 2, 4))


def reset():
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    for action in list(bpy.data.actions):
        bpy.data.actions.remove(action)
    scene = bpy.context.scene
    scene.unit_settings.system = "METRIC"
    scene.unit_settings.scale_length = 1.0
    scene.render.fps = 30
    scene.world.color = (0.12, 0.12, 0.12)
    MATS.clear()


def mat(name):
    if name not in MATS:
        m = bpy.data.materials.new(name)
        m.diffuse_color = (*rgb(PALETTE.get(name, "777777")), 1)
        m.use_nodes = True
        shader = m.node_tree.nodes.get("Principled BSDF")
        shader.inputs["Base Color"].default_value = m.diffuse_color
        shader.inputs["Roughness"].default_value = 0.85
        if name in ("iron", "bronze"):
            shader.inputs["Metallic"].default_value = 0.65
            shader.inputs["Roughness"].default_value = 0.52
        texture = ROOT / "textures" / (name + "_basecolor.png")
        if texture.exists():
            tex = m.node_tree.nodes.new("ShaderNodeTexImage")
            tex.image = bpy.data.images.load(str(texture), check_existing=True)
            m.node_tree.links.new(tex.outputs["Color"], shader.inputs["Base Color"])
            rough = m.node_tree.nodes.new("ShaderNodeTexImage")
            rough.image = bpy.data.images.load(str(texture).replace("basecolor", "roughness"), check_existing=True)
            rough.image.colorspace_settings.name = "Non-Color"
            m.node_tree.links.new(rough.outputs["Color"], shader.inputs["Roughness"])
        MATS[name] = m
    return MATS[name]


def mesh(name, verts, faces, material, rig=None, weights=None):
    data = bpy.data.meshes.new(name)
    data.from_pydata(verts, [], faces)
    data.update()
    obj = bpy.data.objects.new(name, data)
    bpy.context.collection.objects.link(obj)
    obj.data.materials.append(mat(material))
    uv = data.uv_layers.new(name="UVMap")
    for p in data.polygons:
        axis = max(range(3), key=lambda i: abs(p.normal[i]))
        axes = [i for i in range(3) if i != axis]
        for li in p.loop_indices:
            co = data.vertices[data.loops[li].vertex_index].co
            uv.data[li].uv = (co[axes[0]] * 2, co[axes[1]] * 2)
    if rig:
        obj.parent = rig
        obj.modifiers.new("Skeletal deformation", "ARMATURE").object = rig
        if isinstance(weights, str):
            obj.vertex_groups.new(name=weights).add(list(range(len(verts))), 1, "REPLACE")
        else:
            groups = {}
            for index, values in enumerate(weights):
                for bone, value in values.items():
                    if value > 0:
                        if bone not in groups:
                            groups[bone] = obj.vertex_groups.new(name=bone)
                        groups[bone].add([index], value, "REPLACE")
        obj["modular"] = True
    return obj


def box(name, center, size, material, rig=None, bone=None):
    x, y, z = center
    a, b, c = [v / 2 for v in size]
    verts = [(x + sx * a, y + sy * b, z + sz * c)
             for sx, sy, sz in ((-1, -1, -1), (1, -1, -1), (1, 1, -1), (-1, 1, -1),
                               (-1, -1, 1), (1, -1, 1), (1, 1, 1), (-1, 1, 1))]
    return mesh(name, verts, [(0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4),
                              (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)], material, rig, bone)


def ellipsoid(name, center, scale, material, rig=None, bone=None, sides=10, rings=6):
    verts = [(center[0], center[1], center[2] - scale[2])]
    for j in range(1, rings):
        theta = -math.pi / 2 + math.pi * j / rings
        for i in range(sides):
            phi = math.tau * i / sides
            verts.append((center[0] + scale[0] * math.cos(theta) * math.cos(phi),
                          center[1] + scale[1] * math.cos(theta) * math.sin(phi),
                          center[2] + scale[2] * math.sin(theta)))
    verts.append((center[0], center[1], center[2] + scale[2]))
    faces = [(0, 1 + (i + 1) % sides, 1 + i) for i in range(sides)]
    for j in range(rings - 2):
        a = 1 + j * sides
        b = a + sides
        faces.extend((a + i, a + (i + 1) % sides, b + (i + 1) % sides, b + i) for i in range(sides))
    a = 1 + (rings - 2) * sides
    faces.extend((a + i, a + (i + 1) % sides, len(verts) - 1) for i in range(sides))
    return mesh(name, verts, faces, material, rig, bone)


def tube(name, centers, radii, material, rig=None, weights=None, sides=8):
    verts = []
    ws = []
    for j, center in enumerate(centers):
        tangent = Vector(centers[min(j + 1, len(centers) - 1)]) - Vector(centers[max(j - 1, 0)])
        tangent.normalize()
        u = tangent.cross(Vector((0, 1, 0)))
        if u.length < 0.1:
            u = tangent.cross(Vector((1, 0, 0)))
        u.normalize()
        v = tangent.cross(u).normalized()
        radius = radii[j]
        rx, ry = radius if isinstance(radius, (tuple, list)) else (radius, radius)
        for i in range(sides):
            phi = math.tau * i / sides
            verts.append(tuple(Vector(center) + u * math.cos(phi) * rx + v * math.sin(phi) * ry))
            if rig:
                ws.append({weights: 1} if isinstance(weights, str) else weights[j])
    faces = [tuple(reversed(range(sides)))]
    for j in range(len(centers) - 1):
        faces.extend((j * sides + i, j * sides + (i + 1) % sides,
                      (j + 1) * sides + (i + 1) % sides, (j + 1) * sides + i) for i in range(sides))
    faces.append(tuple((len(centers) - 1) * sides + i for i in range(sides)))
    return mesh(name, verts, faces, material, rig, ws if rig else None)


def bone_rig(name, specs):
    data = bpy.data.armatures.new(name + "_skeleton")
    rig = bpy.data.objects.new(name, data)
    bpy.context.collection.objects.link(rig)
    bpy.context.view_layer.objects.active = rig
    rig.select_set(True)
    bpy.ops.object.mode_set(mode="EDIT")
    for bone_name, head, tail, parent in specs:
        b = data.edit_bones.new(bone_name)
        b.head = head
        b.tail = tail
        if parent:
            b.parent = data.edit_bones[parent]
    bpy.ops.object.mode_set(mode="OBJECT")
    rig.show_in_front = True
    rig["forward"] = "-Y"
    rig["up"] = "+Z"
    rig["unit"] = "meter"
    rig["motion"] = "in_place; root Z bob/jump retained"
    return rig


def rotate(rig, name, radians, axis=(1, 0, 0)):
    p = rig.pose.bones.get(name)
    if p:
        p.rotation_mode = "QUATERNION"
        local_axis = rig.data.bones[name].matrix_local.to_3x3().inverted() @ Vector(axis)
        p.rotation_quaternion = Quaternion(local_axis, radians)


def animate(rig, animal=False, temperament=None):
    clips = [("idle", 60, True), ("turn_pose", 1, False), ("walk", 32, True),
             ("run", 24, True), ("turn_left", 24, False), ("turn_right", 24, False),
             ("attack", 30, False), ("jump", 42, False), ("jump_start", 10, False),
             ("jump_air", 24, True), ("jump_land", 12, False)]
    if not rig.animation_data:
        rig.animation_data_create()
    meta = []
    for clip, duration, loop in clips:
        action = bpy.data.actions.new(clip)
        rig.animation_data.action = action
        for frame in range(1, duration + 1):
            t = (frame - 1) / max(1, duration - 1)
            phase = math.tau * t
            for p in rig.pose.bones:
                p.rotation_mode = "QUATERNION"
                p.rotation_quaternion = (1, 0, 0, 0)
                p.location = (0, 0, 0)
                p.scale = (1, 1, 1)
            bob = 0
            if clip == "idle":
                rotate(rig, "chest", 0.012 * math.sin(phase))
                rotate(rig, "head", 0.025 * math.sin(phase), (0, 0, 1))
            elif clip in ("walk", "run"):
                speed = 0.40 if clip == "walk" else 0.79
                bob = (0.012 if clip == "walk" else 0.040) * (1 - math.cos(2 * phase))
                if animal:
                    # Walk: LH -> LF -> RH -> RF. Run: diagonal trot pairs.
                    offsets = {"hind.L": 0, "front.L": .25, "hind.R": .5, "front.R": .75}
                    if clip == "run":
                        offsets = {"hind.L": 0, "front.R": 0, "hind.R": .5, "front.L": .5}
                    for limb, offset in offsets.items():
                        wave = math.sin(phase + math.tau * offset)
                        rotate(rig, limb + ".upper", -speed * wave)
                        rotate(rig, limb + ".lower", speed * 1.15 * max(0, wave))
                        rotate(rig, limb + ".foot", -speed * .65 * max(0, wave))
                    rotate(rig, "spine", .035 * math.sin(2 * phase))
                    rotate(rig, "tail.01", .16 * math.sin(phase), (0, 0, 1))
                else:
                    for side, sign in (("L", 1), ("R", -1)):
                        wave = math.sin(phase) * sign
                        rotate(rig, "thigh." + side, -speed * wave)
                        rotate(rig, "shin." + side, speed * 1.4 * max(0, wave))
                        rotate(rig, "foot." + side, -.25 * max(0, wave))
                        rotate(rig, "upper_arm." + side, speed * .75 * wave)
                        rotate(rig, "forearm." + side, -.20 - (.50 if clip == "run" else .08) * max(0, -wave))
                    rotate(rig, "chest", .06 if clip == "run" else .015)
            elif clip in ("turn_left", "turn_right"):
                sign = 1 if clip == "turn_left" else -1
                ease = t * t * (3 - 2 * t)
                rotate(rig, "root", sign * math.pi / 2 * ease, (0, 0, 1))
                rotate(rig, "head", sign * .18 * math.sin(math.pi * t), (0, 0, 1))
                if animal:
                    for i, limb in enumerate(("front.L", "hind.R", "front.R", "hind.L")):
                        lift = max(0, math.sin(phase + i * math.pi / 2))
                        rotate(rig, limb + ".lower", .4 * lift)
                else:
                    for side, sign_leg in (("L", 1), ("R", -1)):
                        rotate(rig, "thigh." + side, .20 * sign_leg * math.sin(phase))
                        rotate(rig, "shin." + side, .35 * max(0, sign_leg * math.sin(phase)))
            elif clip == "attack":
                pulse = math.sin(math.pi * t) ** 3
                if animal:
                    if temperament == "passive":
                        rotate(rig, "neck", -.55 * pulse)
                        rotate(rig, "head", .3 * pulse)
                    else:
                        rotate(rig, "neck", .25 * pulse)
                        rotate(rig, "jaw", -.5 * pulse)
                        rotate(rig, "front.R.upper", -.85 * pulse)
                        rotate(rig, "front.R.lower", .65 * pulse)
                else:
                    windup = math.sin(math.pi * min(t / .4, 1)) if t < .4 else 0
                    strike = math.sin(math.pi * min(max((t - .3) / .5, 0), 1))
                    rotate(rig, "upper_arm.R", .65 * windup - 1.55 * strike)
                    rotate(rig, "forearm.R", -1.1 * windup - .15 * strike)
                    rotate(rig, "chest", -.12 * windup + .17 * strike, (0, 0, 1))
            elif clip.startswith("jump"):
                if clip == "jump":
                    crouch = math.sin(math.pi * t / .2) * .3 if t < .2 else max(0, math.sin(math.pi * (t - .8) / .2)) * .22 if t > .8 else 0
                    air = max(0, math.sin(math.pi * (t - .2) / .6)) if .2 <= t <= .8 else 0
                    bob = .48 * air - .10 * crouch
                elif clip == "jump_start":
                    crouch, air = .3 * math.sin(math.pi * t), t ** 3 * .6
                    bob = -.10 * math.sin(math.pi * t) + .12 * t ** 3
                elif clip == "jump_air":
                    crouch, air, bob = 0, .75 + .03 * math.sin(phase), .12
                else:
                    crouch, air = .4 * math.sin(math.pi * t), .6 * (1 - t) ** 2
                    bob = .12 * (1 - t) ** 2 - .10 * math.sin(math.pi * t)
                if animal:
                    for limb in ("front.L", "front.R", "hind.L", "hind.R"):
                        rotate(rig, limb + ".upper", -.35 * air - crouch)
                        rotate(rig, limb + ".lower", .65 * air + 2 * crouch)
                        rotate(rig, limb + ".foot", -.3 * air - crouch)
                else:
                    for side in ("L", "R"):
                        rotate(rig, "thigh." + side, -.4 * air - crouch)
                        rotate(rig, "shin." + side, .7 * air + 2 * crouch)
                        rotate(rig, "foot." + side, -.2 * air - crouch)
                        rotate(rig, "upper_arm." + side, -.6 * air)
            # Root bone is aligned to +Z; local Y is vertical.
            rig.pose.bones["root"].location.y = bob
            for p in rig.pose.bones:
                p.keyframe_insert("rotation_quaternion", frame=frame, group=p.name)
            rig.pose.bones["root"].keyframe_insert("location", frame=frame, group="root")
        action["loop"] = loop
        action["fps"] = 30
        action["in_place"] = True
        action.use_fake_user = True
        track = rig.animation_data.nla_tracks.new()
        track.name = clip
        strip = track.strips.new(clip, 1, action)
        track.mute = True
        meta.append({"name": clip, "frames": duration, "fps": 30, "loop": loop,
                     "duration_seconds": (duration - 1) / 30})
    rig.animation_data.action = bpy.data.actions.get("turn_pose")
    bpy.context.scene.frame_set(1)
    return meta


def human(preset):
    reset()
    specs = [("root", (0, 0, 0), (0, 0, .18), None),
             ("pelvis", (0, 0, .88), (0, 0, 1.01), "root"),
             ("spine", (0, 0, 1.01), (0, 0, 1.22), "pelvis"),
             ("chest", (0, 0, 1.22), (0, 0, 1.43), "spine"),
             ("neck", (0, 0, 1.43), (0, 0, 1.53), "chest"),
             ("head", (0, 0, 1.53), (0, 0, 1.75), "neck")]
    for side, s in (("L", 1), ("R", -1)):
        x = .09 * s
        specs += [("thigh." + side, (x, 0, .91), (x, -.015, .50), "pelvis"),
                  ("shin." + side, (x, -.015, .50), (x, 0, .10), "thigh." + side),
                  ("foot." + side, (x, 0, .10), (x, -.16, .06), "shin." + side),
                  ("upper_arm." + side, (.20 * s, 0, 1.40), (.31 * s, 0, 1.14), "chest"),
                  ("forearm." + side, (.31 * s, 0, 1.14), (.38 * s, -.015, .94), "upper_arm." + side),
                  ("hand." + side, (.38 * s, -.015, .94), (.40 * s, -.025, .84), "forearm." + side)]
    rig = bone_rig("human_v1", specs)
    rig["head_units"] = 7
    variant = STORY_HUMANS.get(preset)
    skin = variant["skin"] if variant else {"protagonist": "skin", "porter": "skin_warm", "herbalist": "skin_light"}[preset]
    cloth = variant["cloth"] if variant else {"protagonist": "indigo", "porter": "hemp", "herbalist": "jade"}[preset]
    tube("body_torso_base", [(0, 0, .91), (0, 0, 1.04), (0, 0, 1.23), (0, 0, 1.40)],
         [(.15, .10), (.12, .09), (.17, .11), (.19, .10)], skin, rig,
         [{"pelvis": 1}, {"spine": 1}, {"chest": 1}, {"chest": 1}])
    tube("body_neck", [(0, 0, 1.43), (0, 0, 1.55)], [.052, .048], skin, rig, "neck")
    ellipsoid("head_" + ("round" if preset == "porter" else "oval"), (0, -.002, 1.625),
              (.092 if preset == "porter" else .083, .079, .125), skin, rig, "head")
    for side, s in (("L", 1), ("R", -1)):
        ellipsoid("ear." + side, (.084 * s, 0, 1.625), (.018, .016, .031), skin, rig, "head", 8, 4)
        box("face_eye." + side, (.033 * s, -.077, 1.65), (.018, .007, .007), "ink", rig, "head")
        box("face_brow." + side, (.033 * s, -.076, 1.67), (.025, .008, .006), "hair", rig, "head")
        x = .09 * s
        tube("body_leg." + side, [(x, 0, .91), (x, -.01, .63), (x, -.015, .50), (x, -.005, .37), (x, 0, .10)],
             [.077, .062, .052, .052, .032], skin, rig,
             [{"thigh." + side: 1}, {"thigh." + side: 1}, {"thigh." + side: .5, "shin." + side: .5},
              {"shin." + side: 1}, {"shin." + side: 1}])
        tube("pants_baji." + side, [(x, 0, .91), (x, -.01, .64), (x, -.015, .50), (x, -.004, .32), (x, 0, .16)],
             [.089, .080, .067, .068, .040], "ivory" if preset != "porter" else "indigo_light", rig,
             [{"thigh." + side: 1}, {"thigh." + side: 1}, {"thigh." + side: .5, "shin." + side: .5},
              {"shin." + side: 1}, {"shin." + side: 1}])
        ellipsoid("shoes_jipsin." + side, (x, -.055, .065), (.052, .115, .055), "hemp", rig, "foot." + side)
        tube("body_arm." + side, [(.20 * s, 0, 1.40), (.28 * s, 0, 1.23), (.31 * s, 0, 1.14),
                                   (.35 * s, -.007, 1.04), (.38 * s, -.015, .94)],
             [.058, .052, .042, .038, .027], skin, rig,
             [{"upper_arm." + side: 1}, {"upper_arm." + side: 1},
              {"upper_arm." + side: .5, "forearm." + side: .5}, {"forearm." + side: 1}, {"forearm." + side: 1}])
        tube("sleeve_jeogori." + side, [(.20 * s, 0, 1.40), (.28 * s, 0, 1.23), (.31 * s, 0, 1.14),
                                         (.35 * s, -.007, 1.04), (.37 * s, -.012, .98)],
             [.084, .078, .068, .064, .047], cloth, rig,
             [{"upper_arm." + side: 1}, {"upper_arm." + side: 1},
              {"upper_arm." + side: .5, "forearm." + side: .5}, {"forearm." + side: 1}, {"forearm." + side: 1}])
        ellipsoid("sleeve_shoulder." + side, (.20 * s, 0, 1.40), (.083, .084, .082),
                  cloth, rig, "upper_arm." + side, 8, 4)
        ellipsoid("hands_mitten." + side, (.395 * s, -.020, .89), (.035, .028, .064), skin, rig, "hand." + side)
        ellipsoid("thumb." + side, (.366 * s, -.034, .906), (.017, .019, .035), skin, rig, "hand." + side, 8, 4)
    ellipsoid("face_nose", (0, -.082, 1.628), (.013, .023, .025), skin, rig, "head", 6, 4)
    box("face_mouth", (0, -.077, 1.59), (.027, .006, .005), "red", rig, "head")
    ellipsoid("hair_cap", (0, .009, 1.70), (.087, .079, .058), "hair", rig, "head")
    ellipsoid("hair_topknot", (0, .017, 1.77), (.027, .028, .037), "hair", rig, "head")
    tube("clothes_jeogori", [(0, 0, .94), (0, 0, 1.07), (0, 0, 1.23), (0, 0, 1.39)],
         [(.17, .117), (.148, .109), (.19, .123), (.20, .116)], cloth, rig,
         [{"pelvis": 1}, {"spine": 1}, {"chest": 1}, {"chest": 1}])
    ellipsoid("clothes_shoulder_yoke", (0, 0, 1.389), (.225, .127, .074), cloth, rig, "chest", 12, 4)
    # Raised crossed collar ribbons, a separate swap-ready accessory.
    tube("collar_left", [(-.065, -.115, 1.43), (.085, -.128, 1.20)], [.018, .014], "ivory", rig, "chest", 4)
    tube("collar_right", [(.065, -.115, 1.43), (-.04, -.126, 1.28)], [.018, .014], "ivory", rig, "chest", 4)
    tube("belt", [(0, 0, 1.002), (0, 0, 1.034)], [(.159, .12), (.157, .119)], "wood_dark", rig, "spine", 12)
    box("goreum_knot", (.07, -.137, 1.22), (.045, .022, .025), "persimmon", rig, "chest")
    tube("goreum_ribbon", [(.07, -.140, 1.21), (.105, -.143, 1.10)], [.013, .009], "persimmon", rig, "chest", 4)
    if preset == "protagonist":
        tube("scarf_travel", [(0, .005, 1.435), (0, .005, 1.49)], [.069, .064], "persimmon", rig, "neck")
        box("pouch", (-.19, .015, .98), (.11, .08, .14), "wood", rig, "pelvis")
    elif preset == "porter":
        box("jige_pack", (0, .22, 1.20), (.35, .20, .42), "wood", rig, "chest")
        for s in (-1, 1):
            tube("jige_rail_" + str(s), [(.16 * s, .24, .89), (.16 * s, .24, 1.55)], [.018, .018], "wood_dark", rig, "chest")
        ellipsoid("load_cloth", (0, .28, 1.48), (.22, .19, .14), "hemp", rig, "chest")
    elif preset == "herbalist":
        box("medicine_satchel", (.20, 0, .97), (.15, .11, .18), "persimmon", rig, "pelvis")
        tube("headband", [(0, .003, 1.67), (0, .003, 1.695)], [.088, .088], "ivory", rig, "head", 12)
        box("apron", (0, -.127, .92), (.24, .018, .22), "hemp", rig, "pelvis")
    if variant:
        story_human_details(preset, rig, skin, cloth)
    clips = animate(rig)
    properties = {"height_m": 1.75, "head_ratio": 7, "skeleton": "human_v1"}
    if variant:
        properties.update({"story_character": variant["name_ko"], "role": variant["role_ko"],
                           "silhouette": variant["silhouette"], "appearance_status": "art_direction_proposal_not_story_canon"})
    save_asset("human_" + preset, rig, clips, "human", properties)


def torus(name, center, radius, thickness, material, rig, bone, vertical=False):
    verts = []
    faces = []
    rx, ry = radius
    for i in range(12):
        phi = math.tau * i / 12
        for j in range(4):
            theta = math.tau * j / 4
            x = (rx + thickness * math.cos(theta)) * math.cos(phi)
            y = (ry + thickness * math.cos(theta)) * math.sin(phi)
            z = thickness * math.sin(theta)
            offset = Vector((x, z, y) if vertical else (x, y, z))
            verts.append(tuple(Vector(center) + offset))
    for i in range(12):
        for j in range(4):
            faces.append((i * 4 + j, ((i + 1) % 12) * 4 + j,
                          ((i + 1) % 12) * 4 + (j + 1) % 4, i * 4 + (j + 1) % 4))
    return mesh(name, verts, faces, material, rig, bone)


def story_human_details(preset, rig, skin, cloth):
    style = STORY_HUMANS[preset]
    # Head-local mesh shape changes leave the common bind skeleton untouched.
    for obj in list(rig.children):
        if obj.type != "MESH" or not obj.vertex_groups:
            continue
        if all(g.name == "head" for g in obj.vertex_groups):
            for v in obj.data.vertices:
                v.co.x *= style["head_x"]
                v.co.y *= style["head_y"]
            if obj.name.startswith("head_"):
                for v in obj.data.vertices:
                    z = (v.co.z - 1.50) / .25
                    if preset == "serat_inspector" and z < .45:
                        v.co.x *= 1.12
                    elif preset == "ian_broker" and z < .45:
                        v.co.x *= .82
                    elif preset == "mira_engineer" and z < .35:
                        v.co.x *= .90
        if obj.name == "hair_topknot" and preset in ("mira_engineer", "haram_apprentice", "serat_inspector"):
            bpy.data.objects.remove(obj, do_unlink=True)
    if preset == "mira_engineer":
        ellipsoid("hair_back_bun", (0, .093, 1.64), (.060, .045, .053), "hair", rig, "head", 10, 4)
        tube("headwear_work_wrap", [(0, .003, 1.685), (0, .003, 1.714)], [(.092, .083), (.091, .082)], "ivory", rig, "head", 12)
        for s in (-1, 1):
            tube("hair_side_lock_" + str(s), [(.076 * s, -.007, 1.70), (.075 * s, -.02, 1.59)], [.018, .009], "hair", rig, "head", 5)
        add_apron(rig, "work_apron", .55, "hemp", split=True)
        add_belt_tools(rig, hammer=True)
        for x, z in ((-.105, .83), (.09, .66)):
            box("apron_patch", (x, -.151, z), (.064, .012, .075), "indigo", rig, "pelvis" if z > .8 else "thigh.R" if x < 0 else "thigh.L")
        tube("measuring_plumb_line", [(.18, -.01, 1.01), (.21, -.01, .83)], [.004, .004], "hemp", rig, "pelvis", 4)
        ellipsoid("plumb_weight", (.21, -.01, .81), (.018, .018, .033), "iron", rig, "pelvis", 6, 4)
    elif preset == "doyun_clerk":
        for s in (-1, 1):
            ellipsoid("hair_gray_temple_" + str(s), (.081 * s, .015, 1.678), (.016, .055, .032), "hair_gray", rig, "head", 8, 4)
            tube("face_moustache_" + str(s), [(.004 * s, -.091, 1.604), (.035 * s, -.088, 1.59)], [.010, .004], "hair_gray", rig, "head", 5)
            box("face_age_line_" + str(s), (.045 * s, -.079, 1.638), (.018, .004, .003), "skin_warm", rig, "head")
        add_long_coat(rig, "indigo", .50, .195)
        box("ledger_satchel", (-.22, .025, .96), (.16, .11, .20), "wood_dark", rig, "pelvis")
        box("ledger_pages", (-.22, -.033, .995), (.128, .009, .13), "paper", rig, "pelvis")
        box("ledger_cover_spine", (-.285, -.042, .995), (.015, .015, .15), "indigo", rig, "pelvis")
        tube("brush_case", [(.18, .025, .94), (.18, .025, 1.13)], [.022, .022], "wood", rig, "pelvis", 6)
        tube("writing_brush", [(.18, .025, 1.10), (.18, .025, 1.19)], [.007, .005], "wood_dark", rig, "pelvis", 5)
        box("union_record_tag", (.10, -.145, 1.25), (.044, .012, .064), "paper", rig, "chest")
    elif preset == "serat_inspector":
        ellipsoid("headwear_helmet", (0, .004, 1.728), (.106, .096, .077), "iron", rig, "head", 12, 4)
        torus("headwear_helmet_rim", (0, .004, 1.706), (.112, .099), .012, "wood_dark", rig, "head")
        tube("headwear_helmet_tip", [(0, .004, 1.782), (0, .004, 1.835)], [.019, .005], "bronze", rig, "head", 6)
        for s in (-1, 1):
            tube("headwear_chin_tie_" + str(s), [(.092 * s, .008, 1.69), (.055 * s, -.052, 1.54)], [.008, .006], "wood_dark", rig, "head", 4)
            ellipsoid("clothes_padded_shoulder_" + str(s), (.202 * s, .004, 1.413), (.095, .095, .073), "indigo", rig, "upper_arm.L" if s > 0 else "upper_arm.R", 8, 4)
        tube("clothes_official_vest", [(0, .006, 1.05), (0, .006, 1.22), (0, .006, 1.39)],
             [(.165, .121), (.201, .142), (.206, .128)], "indigo", rig,
             [{"spine": 1}, {"chest": 1}, {"chest": 1}])
        for x in (-.12, -.04, .04, .12):
            for z in (1.12, 1.22, 1.32):
                box("clothes_vest_stitch", (x, -.14 if abs(x) < .1 else -.118, z), (.009, .010, .044), "hemp", rig, "chest" if z > 1.16 else "spine")
        box("official_badge", (0, -.155, 1.255), (.065, .02, .08), "bronze", rig, "chest")
        box("badge_inset", (0, -.168, 1.255), (.032, .006, .043), "ink", rig, "chest")
        box("sealed_document_case", (-.215, .025, .94), (.12, .09, .24), "wood_dark", rig, "pelvis")
        box("document_red_seal", (-.215, -.026, 1.0), (.045, .012, .075), "red", rig, "pelvis")
        tube("official_baton", [(.19, .025, .83), (.19, .025, 1.1)], [.017, .019], "wood_dark", rig, "pelvis", 6)
    elif preset == "ian_broker":
        tube("headwear_gat_crown", [(0, .008, 1.735), (0, .008, 1.79), (0, .008, 1.94)], [.094, .083, .072], "ink", rig, "head", 12)
        tube("headwear_gat_brim", [(0, .008, 1.774), (0, .008, 1.785)], [.24, .24], "ink", rig, "head", 16)
        torus("headwear_gat_edge", (0, .008, 1.779), (.24, .24), .005, "wood_dark", rig, "head")
        for s in (-1, 1):
            tube("headwear_gat_cord_" + str(s), [(.18 * s, -.015, 1.774), (.052 * s, -.065, 1.51)], [.004, .004], "hemp", rig, "head", 4)
            tube("face_moustache_" + str(s), [(.003 * s, -.098, 1.604), (.030 * s, -.094, 1.608)], [.008, .003], "hair", rig, "head", 5)
        tube("face_goatee", [(0, -.081, 1.579), (0, -.066, 1.525)], [.021, .003], "hair", rig, "head", 6)
        add_long_coat(rig, "wood_dark", .42, .205)
        ellipsoid("coin_purse", (.20, -.002, .98), (.072, .054, .085), "persimmon", rig, "pelvis", 8, 4)
        torus("purse_tie", (.20, -.002, 1.045), (.034, .026), .005, "hemp", rig, "pelvis")
        box("deal_book", (-.21, .012, .99), (.11, .07, .15), "red", rig, "pelvis")
        box("deal_book_clasp", (-.21, -.027, .99), (.03, .012, .045), "bronze", rig, "pelvis")
    elif preset == "haram_apprentice":
        tube("headwear_tied_band", [(0, .004, 1.69), (0, .004, 1.724)], [(.095, .080), (.093, .079)], "indigo", rig, "head", 12)
        tube("headwear_band_tail", [(0, .084, 1.70), (.04, .108, 1.57)], [.018, .008], "indigo", rig, "head", 4)
        tube("hair_ponytail", [(0, .075, 1.68), (0, .11, 1.57)], [.028, .012], "hair", rig, "head", 7)
        tube("hair_front_fringe", [(-.065, -.058, 1.704), (-.024, -.083, 1.678)], [.025, .009], "hair", rig, "head", 5)
        add_apron(rig, "apprentice_apron", .73, "indigo", split=False)
        box("apron_repair_patch", (.06, -.149, .83), (.07, .012, .06), "persimmon", rig, "pelvis")
        add_belt_tools(rig, hammer=False)
        tube("blueprint_roll", [(-.21, .04, .80), (-.21, .04, 1.07)], [.041, .041], "paper", rig, "pelvis", 10)
        torus("blueprint_roll_tie", (-.21, .04, .95), (.043, .043), .006, "persimmon", rig, "pelvis")
        torus("rope_loop", (.19, .02, .95), (.060, .085), .009, "hemp", rig, "pelvis", vertical=True)


def add_belt_tools(rig, hammer):
    side = .19
    box("tool_belt_pocket", (side, .015, .97), (.09, .09, .12), "wood_dark", rig, "pelvis")
    tube("tool_handle", [(side, .015, .87), (side, .015, 1.14)], [.012, .012], "wood", rig, "pelvis", 6)
    if hammer:
        box("tool_hammer_head", (side, .015, 1.14), (.105, .045, .045), "iron", rig, "pelvis")
    else:
        box("tool_chisel_blade", (side, .015, 1.155), (.023, .012, .065), "iron", rig, "pelvis")


def add_apron(rig, name, bottom, material, split):
    # Independent front panels follow thighs instead of a rigid pelvis-only skirt.
    panels = ((-.078, "R"), (.078, "L")) if split else ((0, None),)
    for x, side in panels:
        width = .145 if split else .27
        bone = "thigh." + side if side else "pelvis"
        top = 1.025
        mid = .88
        verts = [(x - width / 2, -.139, top), (x + width / 2, -.139, top),
                 (x - width / 2, -.148, mid), (x + width / 2, -.148, mid),
                 (x - width * .55, -.148, bottom), (x + width * .55, -.148, bottom)]
        weights = [{"pelvis": 1}] * 2 + [{"pelvis": .7, bone: .3} if side else {"pelvis": 1}] * 2 + [{bone: 1}] * 2
        mesh(name + ("." + side if side else ""), verts, [(0, 2, 3, 1), (2, 4, 5, 3)], material, rig, weights)
    for s in (-1, 1):
        tube(name + "_strap_" + str(s), [(.115 * s, -.108, 1.41), (.12 * s, -.137, 1.03)], [.010, .012], material, rig,
             [{"chest": 1}, {"spine": 1}], 4)


def add_long_coat(rig, material, bottom, width):
    # Open coat: two front and two rear panels, split along the leg seam.
    for side, s in (("L", 1), ("R", -1)):
        for front, y in ((True, -.143), (False, .135)):
            xs = [.045 * s, width * s]
            verts = [(xs[0], y, 1.36), (xs[1], y * .72, 1.36),
                     (xs[0], y, .92), (xs[1], y * .90, .92),
                     (xs[0], y, bottom), (xs[1] * 1.08, y, bottom)]
            weights = [{"chest": 1}] * 2 + [{"pelvis": 1}] * 2 + [{"thigh." + side: 1}] * 2
            mesh("clothes_coat_%s_%s" % (side, "front" if front else "back"), verts,
                 [(0, 2, 3, 1), (2, 4, 5, 3)], material, rig, weights)
        tube("clothes_coat_lapel_" + side, [(.045 * s, -.151, 1.36), (.045 * s, -.151, 1.02)], [.012, .012], "ivory", rig,
             [{"chest": 1}, {"spine": 1}], 4)


def animal(species):
    reset()
    settings = {
        "tiger": (1.08, 1.48, .30, "tiger", "dangerous", "carnivore"),
        "wolf": (.78, 1.15, .20, "wolf", "neutral", "carnivore"),
        "deer": (1.05, 1.10, .23, "deer", "passive", "herbivore"),
        "ox": (1.12, 1.65, .38, "ox", "passive", "herbivore"),
    }
    height, length, width, color, mood, diet = settings[species]
    front = -.32 * length
    hind = .32 * length
    hoofed = species in ("deer", "ox")
    neck_top = (0, front - (.12 if hoofed else .18), height + (.37 if species == "deer" else .17))
    head = Vector((0, neck_top[1] - .17, neck_top[2] + .04))
    specs = [("root", (0, 0, 0), (0, 0, .16), None),
             ("pelvis", (0, hind, height * .86), (0, 0, height * .94), "root"),
             ("spine", (0, 0, height * .94), (0, front, height), "pelvis"),
             ("neck", (0, front, height), neck_top, "spine"),
             ("head", neck_top, tuple(head + Vector((0, -.12, 0))), "neck"),
             ("jaw", tuple(head + Vector((0, -.02, -.07))), tuple(head + Vector((0, -.22, -.08))), "head"),
             ("tail.01", (0, hind + .10, height), (0, hind + .38, height * .83), "pelvis"),
             ("tail.02", (0, hind + .38, height * .83), (0, hind + .60, height * .54), "tail.01")]
    limb_data = {}
    for side, sign in (("L", 1), ("R", -1)):
        x = width * .72 * sign
        for end, y in (("front", front), ("hind", hind)):
            # Hind stifle points forward, hock rearward; hoof/paw under the hip.
            a = (x, y, height * .91)
            b = (x, y + (-.17 if end == "hind" else .055), height * .57)
            c = (x, y + (.12 if end == "hind" else .01), height * .24)
            d = (x, y - .035, .075)
            limb = end + "." + side
            specs += [(limb + ".upper", a, b, "pelvis" if end == "hind" else "spine"),
                      (limb + ".lower", b, c, limb + ".upper"),
                      (limb + ".foot", c, d, limb + ".lower")]
            limb_data[limb] = (a, b, c, d)
    rig = bone_rig(species + "_v1", specs)
    rig["temperament"] = mood
    rig["diet"] = diet
    body = tube("body", [(0, hind + length * .12, height * .86), (0, hind, height * .91),
                  (0, 0, height * .92), (0, front, height * .96), (0, front - length * .10, height * .95)],
         [(width * .56, height * .16), (width, height * .27), (width * 1.02, height * .28),
          (width, height * .29), (width * .55, height * .18)], color, rig,
         [{"pelvis": 1}, {"pelvis": 1}, {"pelvis": .45, "spine": .55}, {"spine": 1}, {"spine": 1}], 10)
    tube("neck", [(0, front, height * .99), neck_top], [width * .69, width * .47], color, rig, "neck")
    ellipsoid("head", tuple(head), (width * (.80 if species == "tiger" else .60), .23 if species == "tiger" else .15,
                                    .19 if species == "tiger" else .145), color, rig, "head")
    muzzle_y = head.y - (.18 if species == "tiger" else .22)
    ellipsoid("muzzle", (0, muzzle_y, head.z - .035), (width * .38, .125, .075),
              "fur_white" if species in ("tiger", "wolf") else color, rig, "head")
    ellipsoid("nose", (0, muzzle_y - .105, head.z - .025), (width * .27, .025, .033), "ink", rig, "head", 8, 4)
    ellipsoid("jaw", (0, muzzle_y + .03, head.z - .095), (width * .35, .13, .035), color, rig, "jaw")
    for side, sign in (("L", 1), ("R", -1)):
        ellipsoid("eye." + side, (width * (.64 if species == "tiger" else .53) * sign, head.y - .11, head.z + .055), (.018, .024, .016), "ink", rig, "head", 8, 4)
        if species == "tiger":
            ellipsoid("ear." + side, (width * .62 * sign, head.y + .025, head.z + .175), (.060, .033, .062), "ink", rig, "head", 8, 4)
            ellipsoid("ear_inner." + side, (width * .62 * sign, head.y - .005, head.z + .178), (.034, .009, .036), "fur_white", rig, "head", 8, 4)
            ellipsoid("muzzle_jowl." + side, (.065 * sign, muzzle_y, head.z - .035), (.075, .105, .072), "fur_white", rig, "head", 8, 4)
        else:
            tube("ear." + side, [(width * .42 * sign, head.y + .02, head.z + .10),
                              (width * .64 * sign, head.y + .04, head.z + (.27 if species != "tiger" else .21))],
             [.052, .012 if species != "tiger" else .035], color, rig, "head", 6)
        if hoofed:
            horn_base = (width * .39 * sign, head.y + .04, head.z + .1)
            if species == "deer":
                p1 = (width * .60 * sign, head.y + .10, head.z + .38)
                p2 = (width * .98 * sign, head.y + .15, head.z + .64)
                tube("antler_main." + side, [horn_base, p1, p2], [.027, .021, .005], "ivory", rig, "head", 6)
                tube("antler_tine." + side, [p1, (width * .30 * sign, head.y - .10, head.z + .57)], [.016, .004], "ivory", rig, "head", 6)
            else:
                tube("horn." + side, [horn_base, (width * .95 * sign, head.y + .05, head.z + .17),
                                       (width * 1.11 * sign, head.y + .08, head.z + .30)], [.043, .027, .005], "ivory", rig, "head", 7)
    for limb, (a, b, c, d) in limb_data.items():
        upper, lower, foot = [limb + suffix for suffix in (".upper", ".lower", ".foot")]
        near_b = tuple(Vector(a).lerp(Vector(b), .73))
        after_b = tuple(Vector(b).lerp(Vector(c), .3))
        near_c = tuple(Vector(b).lerp(Vector(c), .78))
        after_c = tuple(Vector(c).lerp(Vector(d), .28))
        radius = width * (.55 if limb.startswith("hind") else .44 if species == "tiger" else .35)
        thin = .44 if species == "tiger" else .29
        tube(limb + "_leg", [a, near_b, b, after_b, near_c, c, after_c, d],
             [radius, radius * .83, radius * .60, radius * .47, radius * .37,
              radius * max(.38, thin), radius * thin, radius * thin], color, rig,
             [{upper: 1}, {upper: 1}, {upper: .5, lower: .5}, {lower: 1}, {lower: 1},
              {lower: .5, foot: .5}, {foot: 1}, {foot: 1}])
        foot_size = .06 if hoofed else width * .34
        ellipsoid(limb + "_" + ("hoof" if hoofed else "paw"), (d[0], d[1] - .035, .055),
                  (foot_size, .09 if hoofed else .12, .055), "hoof" if hoofed else color, rig, foot, 8, 4)
        if hoofed:
            box(limb + "_cloven_seam", (d[0], d[1] - .118, .056), (.006, .006, .065), "ink", rig, foot)
        else:
            for toe in (-1, 0, 1):
                ellipsoid(limb + "_toe_" + str(toe), (d[0] + toe * foot_size * .55, d[1] - .105, .051),
                          (foot_size * .28, .04, .033), color, rig, foot, 6, 4)
    tail_points = [tuple(rig.data.bones[n].head_local) for n in ("tail.01", "tail.02")]
    tail_points.append(tuple(rig.data.bones["tail.02"].tail_local))
    if species == "deer":
        tail_points = [(0, hind + .1, height), (0, hind + .23, height * .9), (0, hind + .29, height * .84)]
    tube("tail", tail_points, [.055 if species == "wolf" else .030, .048 if species == "wolf" else .026, .012],
         "fur_white" if species == "deer" else color, rig,
         [{"tail.01": 1}, {"tail.01": .5, "tail.02": .5}, {"tail.02": 1}])
    if species == "tiger":
        # Surface ribbons follow the exact body facets; no coplanar or hidden stripes.
        for segment in range(1, 3):
            for ribbon in range(4):
                fraction = .12 + ribbon * .23
                for radial in (0, 1, 2, 3, 7, 8, 9):
                    n = 10
                    a = body.data.vertices[segment * n + radial].co
                    b = body.data.vertices[(segment + 1) * n + radial].co
                    c = body.data.vertices[segment * n + (radial + 1) % n].co
                    d = body.data.vertices[(segment + 1) * n + (radial + 1) % n].co
                    center = Vector((0, (a.y + b.y) / 2, height * .92))
                    outward = ((a + b + c + d) / 4 - center)
                    outward.y = 0
                    outward.normalize()
                    offset = outward * .004
                    taper = .08 if radial in (0, 1, 9) else .035
                    verts = [tuple(a.lerp(b, fraction) + offset), tuple(a.lerp(b, fraction + taper) + offset),
                             tuple(c.lerp(d, fraction + taper + .035) + offset), tuple(c.lerp(d, fraction + .035) + offset)]
                    bone = "pelvis" if segment == 1 else "spine"
                    mesh("stripe_%s_%s_%s" % (segment, ribbon, radial), verts, [(0, 1, 2, 3)], "ink", rig, bone)
        for i in range(3):
            for s in (-1, 1):
                box("cheek_stripe_%s_%s" % (i, s), (s * width * .57, head.y - .05 + i * .05, head.z),
                    (.009, .016, .07), "ink", rig, "head")
    clips = animate(rig, True, mood)
    save_asset("animal_" + species, rig, clips, "animal", {"temperament": mood, "diet": diet,
               "skeleton": species + "_v1", "run_gait": "diagonal_trot", "attack": "head_butt" if hoofed else "bite_and_swipe"})


def roof(name, center, width=2.4, depth=2.4, thatch=False):
    x, y, z = center
    verts = []
    # Curved eave section, ridge runs Y; front/back gables remain open for assembly.
    section = [(-width / 2, .08), (-width * .32, .17), (0, .66), (width * .32, .17), (width / 2, .08)]
    for yy in (-depth / 2, depth / 2):
        verts.extend((x + xx, y + yy, z + zz) for xx, zz in section)
    faces = [(i, i + 1, i + 6, i + 5) for i in range(4)]
    obj = mesh(name, verts, faces, "hemp" if thatch else "roof")
    solid = obj.modifiers.new("Roof thickness", "SOLIDIFY")
    solid.thickness = .07
    if not thatch:
        for i in range(13):
            yy = y - depth / 2 + depth * i / 12
            tube(name + "_tile_rib_%02d" % i,
                 [(x + xx, yy, z + zz + .025) for xx, zz in section], [.026] * 5, "roof", sides=6)
        tube(name + "_ridge", [(x, y - depth / 2 - .03, z + .70), (x, y + depth / 2 + .03, z + .70)], [.07, .07], "roof")
    return obj


def architecture():
    reset()
    # Every module has its own local origin at a useful placement anchor.
    modules = []

    def module(name, fn, snap, note):
        before = set(bpy.context.scene.objects)
        fn()
        objects = list(set(bpy.context.scene.objects) - before)
        collection = bpy.data.collections.new(name)
        bpy.context.scene.collection.children.link(collection)
        for o in objects:
            for c in list(o.users_collection):
                c.objects.unlink(o)
            collection.objects.link(o)
        modules.append((name, objects, snap, note))

    module("foundation_2m", lambda: box("stone_plinth", (0, 0, .15), (2, 2, .30), "stone"), [2, 2, .3], "bottom center")
    module("floor_2m", lambda: [box("floor_plank_%02d" % i, (-.90 + i * .2, 0, .07), (.193, 2, .14), "wood") for i in range(10)], [2, 2, .14], "bottom center")
    module("post_2_4m", lambda: tube("timber_post", [(0, 0, 0), (0, 0, 2.4)], [.13, .105], "wood", sides=8), [2, 2, 2.4], "foot center")
    module("beam_2m", lambda: box("beam", (0, 0, .11), (2.2, .20, .22), "wood_dark"), [2, 0, 0], "bottom center")
    module("wall_plaster_2m", lambda: [box("plaster", (0, 0, 1.2), (1.8, .12, 2.4), "plaster"),
                                      box("bottom_sill", (0, 0, .18), (2, .18, .12), "wood"),
                                      box("top_sill", (0, 0, 2.3), (2, .18, .12), "wood")], [2, 0, 0], "bottom center; fits between posts")

    def lattice():
        box("paper", (0, .012, 1.22), (1.78, .025, 2.20), "paper")
        for x in (-.9, -.6, -.3, 0, .3, .6, .9):
            box("lattice_v", (x, -.02, 1.22), (.035, .06, 2.26), "wood")
        for z in (.1, .48, .86, 1.24, 1.62, 2, 2.36):
            box("lattice_h", (0, -.025, z), (1.84, .06, .035), "wood")

    module("wall_paper_lattice_2m", lattice, [2, 0, 0], "bottom center")
    module("roof_giwa_2m", lambda: roof("giwa_roof", (0, 0, 0)), [2, 2, 0], "wall-top center; 0.2m eave overhang")
    module("roof_thatch_2m", lambda: roof("thatch_roof", (0, 0, 0), thatch=True), [2, 2, 0], "wall-top center")

    def gable_module():
        mesh("gable", [(-1, 0, 0), (1, 0, 0), (0, 0, .66)], [(0, 1, 2)], "plaster")
        tube("gable_trim_l", [(-1, 0, 0), (0, 0, .66)], [.05, .05], "wood")
        tube("gable_trim_r", [(1, 0, 0), (0, 0, .66)], [.05, .05], "wood")

    module("gable_2m", gable_module, [2, 0, 0], "wall-top center")
    module("fence_2m", lambda: [box("fence_rail", (0, 0, z), (2, .07, .09), "wood") for z in (.3, .75)] +
           [tube("fence_post", [(x, 0, 0), (x, 0, .95)], [.055, .045], "wood") for x in (-1, -.5, 0, .5, 1)], [2, 0, 0], "ground center")

    def table():
        box("table_top", (0, 0, .44), (.90, .60, .07), "wood")
        for x in (-.34, .34):
            for y in (-.22, .22):
                box("table_leg", (x, y, .21), (.065, .065, .42), "wood_dark")

    module("soban_table", table, None, "ground center")
    module("stool", lambda: [box("seat", (0, 0, .40), (.38, .30, .07), "wood"),
                             box("leg_l", (-.13, 0, .18), (.06, .24, .36), "wood"),
                             box("leg_r", (.13, 0, .18), (.06, .24, .36), "wood")], None, "ground center")

    def jar():
        tube("onggi_jar", [(0, 0, z) for z in (.03, .12, .28, .48, .60, .65)],
             [.14, .22, .27, .25, .16, .17], "ceramic", sides=12)
        tube("onggi_lid", [(0, 0, .65), (0, 0, .69), (0, 0, .72)], [.19, .19, .10], "ceramic", sides=12)

    module("onggi_jar", jar, None, "ground center; removable lid")
    module("rice_sack", lambda: [ellipsoid("sack", (0, 0, .28), (.23, .18, .28), "hemp"),
                                  tube("tie", [(0, 0, .49), (0, 0, .55)], [.06, .045], "wood")], None, "ground center")
    module("storage_chest", lambda: [box("chest", (0, 0, .26), (.75, .40, .48), "wood"),
                                      box("chest_lid", (0, 0, .52), (.79, .44, .07), "wood_dark"),
                                      box("latch", (0, -.211, .41), (.07, .025, .12), "bronze")], None, "ground center")
    module("medicine_drawers", lambda: [box("cabinet", (0, 0, .52), (.80, .32, 1.04), "wood_dark")] +
           [box("drawer", (x, -.175, z), (.235, .035, .21), "wood") for x in (-.26, 0, .26) for z in (.15, .39, .63, .87)] +
           [box("handle", (x, -.20, z), (.035, .025, .028), "bronze") for x in (-.26, 0, .26) for z in (.15, .39, .63, .87)], None, "ground center")
    module("paper_lantern", lambda: [tube("lantern", [(0, 0, z) for z in (.05, .12, .36, .43)], [.10, .15, .15, .10], "paper"),
                                     tube("top", [(0, 0, .43), (0, 0, .46)], [.11, .11], "wood_dark"),
                                     tube("base", [(0, 0, .02), (0, 0, .05)], [.11, .11], "wood_dark")], None, "base center")
    module("wood_bucket", lambda: [tube("bucket", [(0, 0, .02), (0, 0, .32), (0, 0, .32), (0, 0, .04)], [.12, .17, .145, .10], "wood", sides=10),
                                    tube("handle", [(-.16, 0, .30), (-.14, 0, .52), (.14, 0, .52), (.16, 0, .30)], [.016] * 4, "iron")], None, "ground center")
    module("reed_mat", lambda: box("woven_mat", (0, 0, .012), (1.8, .9, .024), "hemp"), None, "ground center")
    module("firewood", lambda: [tube("log", [(-.35, y, z), (.35, y, z)], [.065, .055], "wood") for y, z in ((-.12, .07), (0, .07), (.12, .07), (-.06, .18), (.06, .18))], None, "ground center")

    # Hinged door is a skeletal prop with an editable opening animation.
    reset_selection()
    rig = bone_rig("door_v1", [("root", (0, 0, 0), (0, 0, .1), None),
                               ("hinge", (0, 0, 0), (0, 0, 2.2), "root")])
    box("door_leaf", (.45, 0, 1.1), (.9, .065, 2.2), "wood", rig, "hinge")
    for z in (.22, 1.97):
        box("door_brace", (.45, -.047, z), (.88, .04, .09), "wood_dark", rig, "hinge")
    box("door_handle", (.77, -.06, 1.04), (.045, .04, .12), "iron", rig, "hinge")
    animate_door(rig)
    door_objs = [rig] + list(rig.children)
    export_glb("prop_door_hinged", door_objs)
    MANIFEST["assets"].append({"id": "prop_door_hinged", "category": "skeletal_prop", "bones": 2,
                              "glb": "glb/prop_door_hinged.glb", "animations": [{"name": "door_open", "frames": 30}],
                              "origin": "hinge foot"})
    modules.append(("door_hinged", door_objs, None, "hinge foot; door_open action"))
    for index, (name, objects, snap, note) in enumerate(modules):
        export_glb("module_" + name, objects)
        MANIFEST["assets"].append({"id": "module_" + name, "category": "architecture_prop", "snap_m": snap,
                                  "library_translation_m": [(index % 5) * 3.5, (index // 5) * 3.8, 0],
                                  "origin": note, "glb": "glb/module_" + name + ".glb"})
        for obj in objects:
            if obj.parent not in objects:
                obj.location.x += (index % 5) * 3.5
                obj.location.y += (index // 5) * 3.8
    save_blend("architecture_library")
    preview("architecture_library", (7, 6, .7), 22)
    # Assemble an inhabited roadside medicine shop from the same module meshes.
    prototypes = {name: [o.copy() for o in objects] for name, objects, _, _ in modules}
    reset()
    placements = [("foundation_2m", (0, 0, 0)), ("floor_2m", (0, 0, .30)),
                  ("roof_giwa_2m", (0, 0, 2.84)), ("wall_plaster_2m", (0, 1, .44)),
                  ("wall_paper_lattice_2m", (0, -1, .44)),
                  ("medicine_drawers", (0, .65, .44)), ("soban_table", (0, -.2, .44)),
                  ("onggi_jar", (1.55, .2, 0)), ("rice_sack", (1.5, -.7, 0)),
                  ("paper_lantern", (-.75, -1.22, 1.8))]
    for x in (-1, 1):
        for y in (-1, 1):
            placements.append(("post_2_4m", (x, y, .44)))
    for name, offset in placements:
        index = [m[0] for m in modules].index(name)
        old_offset = Vector(((index % 5) * 3.5, (index // 5) * 3.8, 0))
        for source in prototypes[name]:
            obj = source.copy()
            obj.data = source.data.copy()
            bpy.context.collection.objects.link(obj)
            obj.location = source.location - old_offset + Vector(offset)
    box("ground", (0, 0, -.06), (5, 4.5, .12), "earth")
    for y in (-1.02, 1.02):
        gable("shop_gable", (0, y, 2.84))
    save_asset("scene_medicine_shop", None, [], "assembled_example", {})
    preview("scene_medicine_shop", (0, 0, 1.2), 6)


def gable(name, center):
    x, y, z = center
    mesh(name, [(x - 1, y, z), (x + 1, y, z), (x, y, z + .66)], [(0, 1, 2)], "plaster")


def animate_door(rig):
    rig.animation_data_create()
    a = bpy.data.actions.new("door_open")
    rig.animation_data.action = a
    for f, angle in ((1, 0), (30, math.pi * .55)):
        rotate(rig, "hinge", angle, (0, 0, 1))
        rig.pose.bones["hinge"].keyframe_insert("rotation_quaternion", frame=f)
    a.use_fake_user = True
    bpy.context.scene.frame_set(1)


def nature():
    reset()
    assets = []

    def add(name, fn, kind):
        before = set(bpy.context.scene.objects)
        fn()
        objects = list(set(bpy.context.scene.objects) - before)
        export_glb(name, objects)
        assets.append((name, objects))
        MANIFEST["assets"].append({"id": name, "category": kind, "glb": "glb/" + name + ".glb", "origin": "ground center"})

    def pine():
        tube("pine_trunk", [(0, 0, 0), (.06, 0, 1), (.13, .02, 2.2), (.08, .03, 3.2)], [.17, .13, .09, .025], "wood")
        for i, z in enumerate((1.35, 1.9, 2.4, 2.9)):
            radius = .95 - i * .17
            tube("needle_tier", [(.1, 0, z), (.1, 0, z + .15), (.10, 0, z + .65)], [radius * .8, radius, .05], "pine", sides=9)

    def broadleaf():
        tube("tree_trunk", [(0, 0, 0), (0, .04, 1.5), (.1, 0, 2.3)], [.18, .13, .05], "wood")
        for i in range(5):
            phi = i * math.tau / 5
            point = (.6 * math.cos(phi), .6 * math.sin(phi), 2.2 + (i % 2) * .3)
            tube("branch", [(0, 0, 1.5), point], [.06, .018], "wood")
            ellipsoid("canopy", point, (.72, .65, .66), "leaf" if i % 2 else "leaf_light", sides=8, rings=4)

    def grass(reed=False):
        for i in range(14 if reed else 18):
            phi = i * 2.399
            x, y = .23 * math.cos(phi) * (i % 4) / 4, .23 * math.sin(phi) * (i % 4) / 4
            h = random.uniform(.58, 1.1) if reed else random.uniform(.18, .45)
            if reed:
                tube("reed_stem", [(x, y, 0), (x + .04, y, h)], [.009, .006], "grass", sides=4)
                tube("reed_head", [(x + .04, y, h * .78), (x + .04, y, h)], [.025, .014], "hemp", sides=5)
            else:
                verts = [(x - .025, y, 0), (x + .025, y, 0), (x + .06, y + .025, h * .6), (x + .10, y + .04, h)]
                mesh("grass_blade", verts, [(0, 1, 2), (0, 2, 3)], "grass")

    add("nature_pine", pine, "tree")
    add("nature_broadleaf", broadleaf, "tree")
    add("nature_grass_clump", lambda: grass(False), "vegetation")
    add("nature_reed_clump", lambda: grass(True), "vegetation")
    for i, size in enumerate(((.55, .45, .4), (.9, .65, .7), (.35, .28, .2))):
        add("nature_rock_%02d" % i, lambda i=i, size=size: ellipsoid("rock", (0, 0, size[2] * .65), size, "stone", sides=7 + i, rings=4), "rock")
    for name in ("earth", "grass", "stone"):
        add("terrain_" + name + "_2m", lambda name=name: box("terrain", (0, 0, -.08), (2, 2, .16), name), "terrain")
    add("terrain_slope_2m", lambda: mesh("slope", [(-1, -1, 0), (1, -1, 0), (1, 1, .5), (-1, 1, .5),
                                                        (-1, -1, -.16), (1, -1, -.16), (1, 1, -.16), (-1, 1, -.16)],
                                                        [(0, 1, 2, 3), (4, 7, 6, 5), (0, 4, 5, 1), (1, 5, 6, 2), (2, 6, 7, 3), (3, 7, 4, 0)], "earth"), "terrain")
    for i, (name, objects) in enumerate(assets):
        next(a for a in MANIFEST["assets"] if a["id"] == name)["library_translation_m"] = [(i % 4) * 3, (i // 4) * 3, 0]
        for obj in objects:
            obj.location.x += (i % 4) * 3
            obj.location.y += (i // 4) * 3
    save_blend("nature_library")
    preview("nature_library", (4, 3, 1), 16)


def textures():
    # Periodic Fourier detail: opposite tile edges match exactly. Both maps are authored here.
    n = 256
    for name in ("wood", "earth", "stone", "hemp", "grass", "roof", "plaster"):
        base = rgb(PALETTE[name])
        pixels = []
        rough = []
        for j in range(n):
            y = j / (n - 1) * math.tau
            for i in range(n):
                x = i / (n - 1) * math.tau
                detail = .05 * math.sin(x * 9 + math.sin(y * 3)) + .025 * math.cos(y * 13 - x * 7)
                if name == "wood":
                    detail += .10 * math.sin(x * 21 + 1.3 * math.sin(y * 2))
                elif name == "hemp":
                    detail += .055 * math.sin(x * 48) * math.cos(y * 48)
                elif name == "roof":
                    detail += .08 * math.cos(x * 6)
                elif name == "stone":
                    detail += .06 * math.cos(x * 3 + math.sin(y * 4))
                pixels.extend((*[max(0, min(1, c * (1 + detail))) for c in base], 1))
                r = max(.55, min(.98, .87 + detail * .4))
                rough.extend((r, r, r, 1))
        for suffix, values, space in (("basecolor", pixels, "sRGB"), ("roughness", rough, "Non-Color")):
            image = bpy.data.images.new(name + "_" + suffix, width=n, height=n)
            image.colorspace_settings.name = space
            image.pixels.foreach_set(values)
            image.filepath_raw = str(ROOT / "textures" / (name + "_" + suffix + ".png"))
            image.file_format = "PNG"
            image.save()
            bpy.data.images.remove(image)


def reset_selection():
    bpy.ops.object.select_all(action="DESELECT")


def export_glb(name, objects):
    reset_selection()
    for obj in objects:
        obj.select_set(True)
    if objects:
        bpy.context.view_layer.objects.active = objects[0]
    bpy.ops.export_scene.gltf(filepath=str(ROOT / "glb" / (name + ".glb")),
                              export_format="GLB", use_selection=True, export_yup=True,
                              export_animations=True, export_animation_mode="ACTIONS",
                              export_force_sampling=True, export_nla_strips=True,
                              export_skins=True, export_all_influences=False,
                              export_apply=True, export_extras=True)


def save_blend(name):
    for image in bpy.data.images:
        if image.source == "FILE" and not image.packed_file:
            image.pack()
    bpy.context.scene.frame_set(1)
    bpy.context.scene.render.fps = 30
    bpy.context.scene["asset_pack"] = "Haedong low-poly modular starter kit"
    bpy.context.scene["coordinate_system"] = "Blender Z-up, forward -Y; GLB Y-up, forward +Z"
    coords = [o.matrix_world @ Vector(c) for o in bpy.context.scene.objects if o.type == "MESH" for c in o.bound_box]
    if coords:
        low = Vector([min(v[i] for v in coords) for i in range(3)])
        high = Vector([max(v[i] for v in coords) for i in range(3)])
        for screen in bpy.data.screens:
            for area in screen.areas:
                if area.type == "VIEW_3D":
                    area.spaces.active.shading.color_type = "MATERIAL"
                    view = area.spaces.active.region_3d
                    view.view_location = (low + high) / 2
                    view.view_distance = max((high - low).length * 1.2, 2.5)
                    view.view_rotation = Vector((4, -6, 3.5)).to_track_quat("Z", "Y")
                    view.view_perspective = "ORTHO"
    bpy.ops.wm.save_as_mainfile(filepath=str(ROOT / "blend" / (name + ".blend")))


def save_asset(name, rig, clips, category, properties):
    objects = [o for o in bpy.context.scene.objects if o.type in ("MESH", "ARMATURE")]
    triangles = sum(sum(len(p.vertices) - 2 for p in o.data.polygons) for o in objects if o.type == "MESH")
    if rig:
        validate_rig(rig)
    export_glb(name, objects)
    if rig:
        export_parts(name, rig, [o for o in objects if o.type == "MESH"], category)
    save_blend(name)
    MANIFEST["assets"].append({"id": name, "category": category,
                              "blend": "blend/" + name + ".blend", "glb": "glb/" + name + ".glb",
                              "triangles_before_modifiers": triangles,
                              "mesh_parts": sum(o.type == "MESH" for o in objects),
                              "bones": len(rig.data.bones) if rig else 0,
                              "animations": clips, **properties})
    if rig:
        (ROOT / "metadata" / (name + "_parts.json")).write_text(json.dumps({
            "skeleton": rig.name, "parts": [{"name": o.name, "bones": [g.name for g in o.vertex_groups],
                                                "vertices": len(o.data.vertices)} for o in objects if o.type == "MESH"],
            "rest_bones": [{"name": b.name, "head": list(b.head_local), "tail": list(b.tail_local),
                            "parent": b.parent.name if b.parent else None} for b in rig.data.bones]
        }, ensure_ascii=False, indent=2), encoding="utf-8")
        preview(name, (0, 0, .9 if category == "human" else .75), 2.8 if category == "human" else 3.7)


def export_parts(name, rig, meshes, category):
    groups = {}
    for obj in meshes:
        n = obj.name
        if category == "human":
            if n.startswith("body_") or n.startswith("ear.") or n.startswith("thumb."):
                slot = "body_base"
            elif n.startswith("head_"):
                slot = "head"
            elif n.startswith("face_"):
                slot = "face"
            elif n.startswith(("hair_", "headwear_")) or n == "headband":
                slot = "hair"
            elif n.startswith(("clothes_", "sleeve_", "collar_", "goreum_")) or n == "belt":
                slot = "upper_clothing"
            elif n.startswith("pants_"):
                slot = "lower_clothing"
            elif n.startswith("hands_"):
                slot = "hands"
            elif n.startswith("shoes_"):
                slot = "shoes"
            else:
                slot = "accessories"
        else:
            if n.startswith(("head", "muzzle", "nose", "jaw", "eye.", "ear.", "ear_inner", "cheek_")):
                slot = "head"
            elif n.startswith(("antler", "horn")):
                slot = "horns"
            elif n.startswith("front."):
                slot = "front_legs"
            elif n.startswith("hind."):
                slot = "hind_legs"
            elif n.startswith("tail"):
                slot = "tail"
            else:
                slot = "body"
        groups.setdefault(slot, []).append(obj)
        obj["slot"] = slot
        obj["skeleton_id"] = rig.name
    part_root = ROOT / "glb" / "parts"
    part_root.mkdir(exist_ok=True)
    for slot, items in groups.items():
        export_glb("parts/" + name + "__" + slot, [rig] + items)
    (ROOT / "metadata" / (name + "_slots.json")).write_text(json.dumps({
        "id": name, "skeleton": rig.name,
        "slots": [{"slot": slot, "glb": "glb/parts/" + name + "__" + slot + ".glb",
                   "objects": [o.name for o in items]} for slot, items in groups.items()]
    }, ensure_ascii=False, indent=2), encoding="utf-8")


def validate_rig(rig):
    for obj in rig.children:
        if obj.type != "MESH":
            continue
        assert obj.data.uv_layers, "Missing UV: " + obj.name
        for v in obj.data.vertices:
            total = sum(g.weight for g in v.groups)
            assert abs(total - 1) < .001, "Invalid skin weights: " + obj.name
            assert len(v.groups) <= 4
            for g in v.groups:
                assert obj.vertex_groups[g.group].name in rig.data.bones
        assert all(math.isfinite(c) for v in obj.data.vertices for c in v.co)
    assert len(rig.animation_data.nla_tracks) == 11


def preview(name, target, ortho, resolution=1000, samples=24):
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    scene.cycles.samples = samples
    scene.cycles.device = "CPU"
    scene.render.resolution_x = resolution
    scene.render.resolution_y = resolution
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.render.film_transparent = False
    if scene.world is None:
        scene.world = bpy.data.worlds.new("Preview world")
    scene.world.use_nodes = True
    scene.world.node_tree.nodes["Background"].inputs["Color"].default_value = (.18, .19, .18, 1)
    scene.world.node_tree.nodes["Background"].inputs["Strength"].default_value = .55
    scene.view_settings.view_transform = "AgX"
    bpy.ops.object.camera_add(location=Vector(target) + Vector((4, -6, 3.5)))
    camera = bpy.context.object
    camera.rotation_euler = (Vector(target) - camera.location).to_track_quat("-Z", "Y").to_euler()
    camera.data.type = "ORTHO"
    camera.data.ortho_scale = ortho
    scene.camera = camera
    helpers = [camera]
    for location, power, size in (((-3, -4, 7), 550, 5), ((4, 1, 5), 400, 4)):
        bpy.ops.object.light_add(type="AREA", location=Vector(target) + Vector(location))
        lamp = bpy.context.object
        lamp.data.energy = power
        lamp.data.shape = "DISK"
        lamp.data.size = size
        lamp.rotation_euler = (Vector(target) - lamp.location).to_track_quat("-Z", "Y").to_euler()
        helpers.append(lamp)
    scene.render.filepath = str(ROOT / "previews" / (name + ".png"))
    bpy.ops.render.render(write_still=True)
    for obj in helpers:
        bpy.data.objects.remove(obj, do_unlink=True)


def main():
    if "--story-humans-only" in sys.argv:
        existing = json.loads((ROOT / "metadata/asset_manifest.json").read_text(encoding="utf-8"))
        new_ids = {"human_" + p for p in STORY_HUMANS}
        MANIFEST["assets"] = [a for a in existing["assets"] if a["id"] not in new_ids]
        for preset in STORY_HUMANS:
            human(preset)
        (ROOT / "metadata/asset_manifest.json").write_text(json.dumps(MANIFEST, ensure_ascii=False, indent=2), encoding="utf-8")
        print("HAEDONG_STORY_HUMANS_COMPLETE", len(STORY_HUMANS))
        return
    if "--humans-only" in sys.argv:
        existing = json.loads((ROOT / "metadata/asset_manifest.json").read_text(encoding="utf-8"))
        MANIFEST["assets"] = [a for a in existing["assets"] if a["category"] != "human"]
        for preset in HUMAN_PRESETS:
            human(preset)
        (ROOT / "metadata/asset_manifest.json").write_text(json.dumps(MANIFEST, ensure_ascii=False, indent=2), encoding="utf-8")
        print("HAEDONG_HUMAN_REFRESH_COMPLETE")
        return
    textures()
    for preset in HUMAN_PRESETS:
        human(preset)
    for species in ("tiger", "wolf", "deer", "ox"):
        animal(species)
    architecture()
    nature()
    (ROOT / "metadata/asset_manifest.json").write_text(json.dumps(MANIFEST, ensure_ascii=False, indent=2), encoding="utf-8")
    print("HAEDONG_ASSET_BUILD_COMPLETE", len(MANIFEST["assets"]))


if __name__ == "__main__":
    main()
