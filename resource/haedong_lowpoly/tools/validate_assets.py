"""Offline glTF structure / animation / skin validation; outputs stay in this pack."""

import json
import math
import struct
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
REQUIRED = {"idle", "turn_pose", "walk", "run", "turn_left", "turn_right", "attack",
            "jump", "jump_start", "jump_air", "jump_land"}
COMPONENTS = {5120: ("b", 1), 5121: ("B", 1), 5122: ("h", 2), 5123: ("H", 2),
              5125: ("I", 4), 5126: ("f", 4)}
WIDTHS = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}


def load(path):
    data = path.read_bytes()
    magic, version, size = struct.unpack_from("<III", data)
    assert magic == 0x46546C67 and version == 2 and size == len(data)
    length, kind = struct.unpack_from("<II", data, 12)
    assert kind == 0x4E4F534A
    doc = json.loads(data[20:20 + length])
    start = 20 + length
    binary = b""
    if start < len(data):
        length, kind = struct.unpack_from("<II", data, start)
        assert kind == 0x004E4942
        binary = data[start + 8:start + 8 + length]
    return doc, binary


def accessor(doc, binary, index):
    a = doc["accessors"][index]
    assert "sparse" not in a
    view = doc["bufferViews"][a["bufferView"]]
    fmt, size = COMPONENTS[a["componentType"]]
    width = WIDTHS[a["type"]]
    step = view.get("byteStride", size * width)
    begin = view.get("byteOffset", 0) + a.get("byteOffset", 0)
    assert begin + (a["count"] - 1) * step + size * width <= len(binary)
    values = [struct.unpack_from("<" + fmt * width, binary, begin + i * step) for i in range(a["count"])]
    assert all(math.isfinite(v) for row in values for v in row)
    return values


def main():
    manifest = json.loads((ROOT / "metadata/asset_manifest.json").read_text(encoding="utf-8"))
    for item in manifest["assets"]:
        for key in ("glb", "blend"):
            if key in item:
                assert (ROOT / item[key]).is_file(), item[key]
    results = []
    signatures = []
    human_ids = {a["id"] for a in manifest["assets"] if a["category"] == "human"}
    for path in sorted((ROOT / "glb").rglob("*.glb")):
        doc, binary = load(path)
        for i in range(len(doc.get("accessors", []))):
            accessor(doc, binary, i)
        skins = doc.get("skins", [])
        names = {a.get("name") for a in doc.get("animations", [])}
        if path.name.startswith(("human_", "animal_")):
            assert names == REQUIRED, (path.name, names)
            assert len(skins) == 1
        mesh_nodes = [n for n in doc.get("nodes", []) if "mesh" in n]
        for node in mesh_nodes:
            for p in doc["meshes"][node["mesh"]]["primitives"]:
                assert "POSITION" in p["attributes"]
                assert "TEXCOORD_0" in p["attributes"]
                positions = accessor(doc, binary, p["attributes"]["POSITION"])
                assert len(positions) > 0
                if "indices" in p:
                    assert all(i[0] < len(positions) for i in accessor(doc, binary, p["indices"]))
                if "skin" in node:
                    joint_count = len(skins[node["skin"]]["joints"])
                    joints = accessor(doc, binary, p["attributes"]["JOINTS_0"])
                    weights = accessor(doc, binary, p["attributes"]["WEIGHTS_0"])
                    assert all(all(j < joint_count for j in row) for row in joints)
                    wa = doc["accessors"][p["attributes"]["WEIGHTS_0"]]
                    divisor = 65535 if wa["componentType"] == 5123 else 255 if wa["componentType"] == 5121 else 1
                    assert all(abs(sum(row) / divisor - 1) < .002 for row in weights)
        for animation in doc.get("animations", []):
            for sampler in animation["samplers"]:
                times = [r[0] for r in accessor(doc, binary, sampler["input"])]
                assert times == sorted(set(times))
                output = accessor(doc, binary, sampler["output"])
                assert len(output) == len(times)
            for channel in animation["channels"]:
                if channel["target"]["path"] == "rotation":
                    sampler = animation["samplers"][channel["sampler"]]
                    values = accessor(doc, binary, sampler["output"])
                    assert all(abs(sum(v * v for v in row) - 1) < .002 for row in values)
        if path.parent == ROOT / "glb" and path.stem in human_ids:
            skin = skins[0]
            inverse_binds = accessor(doc, binary, skin["inverseBindMatrices"])
            signatures.append([(doc["nodes"][j]["name"], tuple(round(v, 6) for v in bind))
                               for j, bind in zip(skin["joints"], inverse_binds)])
        results.append({"file": str(path.relative_to(ROOT)), "bytes": path.stat().st_size,
                        "meshes": len(doc.get("meshes", [])), "skins": len(skins),
                        "animations": sorted(names), "status": "pass"})
    assert len(signatures) == len(human_ids) and all(s == signatures[0] for s in signatures), "Human rest skeleton mismatch"
    report = {"status": "pass", "glb_files": len(results), "human_shared_skeletons_checked": len(signatures), "checks": ["GLB header/buffer lengths",
              "manifest paths", "finite accessors", "UV presence", "index bounds", "bone indices",
              "normalized skin weights", "11 clips per human/animal and part", "strictly increasing key times",
              "unit rotation quaternions", "identical human inverse bind matrices"],
              "limits": ["Not Khronos glTF Validator certification", "No game build/run", "No automated collision or foot-contact verification"],
              "files": results}
    (ROOT / "metadata/validation_report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print("PASS:", len(results), "GLB files; shared human skeleton verified")


if __name__ == "__main__":
    main()
