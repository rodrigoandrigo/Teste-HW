"""Offline glTF -> bounded, self-contained UWP scene. Requires numpy and Pillow.

Only the SDK Sponza scene's static triangles/base-color materials are supported.
Unsupported geometry fails rather than silently substituting a synthetic scene.
Texture resolution is capped at 512 for Xbox AppContainer memory budgets;
full mip chains are included and geometry is not decimated.
"""
import argparse
import json
import struct
from pathlib import Path
from urllib.parse import unquote
import numpy as np
from PIL import Image


def convert(source, destination):
    doc = json.loads(source.read_text(encoding="utf-8"))
    buffers = [(source.parent / unquote(b["uri"])).read_bytes() for b in doc["buffers"]]
    types = {5120: "i1", 5121: "u1", 5122: "<i2", 5123: "<u2", 5125: "<u4", 5126: "<f4"}
    sizes = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}

    def accessor(index):
        a = doc["accessors"][index]
        if "sparse" in a:
            raise ValueError("Sparse accessors not supported")
        view = doc["bufferViews"][a["bufferView"]]
        dt = np.dtype(types[a["componentType"]])
        n = sizes[a["type"]]
        offset = view.get("byteOffset", 0) + a.get("byteOffset", 0)
        data = np.ndarray((a["count"], n), dt, buffers[view["buffer"]], offset,
                          (view.get("byteStride", n * dt.itemsize), dt.itemsize)).copy()
        if a.get("normalized", False) and dt.kind in "iu":
            data = data.astype(np.float32) / np.iinfo(dt).max
            if dt.kind == "i":
                data = np.maximum(data, -1)
        return data

    def transform(node):
        if "matrix" in node:
            return np.array(node["matrix"]).reshape(4, 4).T
        x, y, z, w = node.get("rotation", [0, 0, 0, 1])
        r = np.array([[1-2*(y*y+z*z), 2*(x*y-z*w), 2*(x*z+y*w)],
                      [2*(x*y+z*w), 1-2*(x*x+z*z), 2*(y*z-x*w)],
                      [2*(x*z-y*w), 2*(y*z+x*w), 1-2*(x*x+y*y)]])
        m = np.eye(4)
        m[:3, :3] = r @ np.diag(node.get("scale", [1, 1, 1]))
        m[:3, 3] = node.get("translation", [0, 0, 0])
        return m

    vertices, indices, draws, cameras = [], [], [], []
    vertex_count, index_count = 0, 0

    def visit(index, parent):
        nonlocal vertex_count, index_count
        node = doc["nodes"][index]
        world = parent @ transform(node)
        if "skin" in node:
            raise ValueError("Skinned scene not supported")
        if "camera" in node:
            cameras.append((node.get("name", ""), world))
        if "mesh" in node:
            for primitive in doc["meshes"][node["mesh"]]["primitives"]:
                if primitive.get("mode", 4) != 4 or "targets" in primitive:
                    raise ValueError("Only static triangle primitives supported")
                attrs = primitive["attributes"]
                pos = accessor(attrs["POSITION"]).astype(np.float64)
                normal = accessor(attrs["NORMAL"]).astype(np.float64)
                uv = accessor(attrs["TEXCOORD_0"]) if "TEXCOORD_0" in attrs else np.zeros((len(pos), 2))
                pos = (world @ np.column_stack((pos, np.ones(len(pos)))).T).T[:, :3]
                normal = (np.linalg.inv(world[:3, :3]).T @ normal.T).T
                normal /= np.maximum(np.linalg.norm(normal, axis=1)[:, None], 1e-8)
                idx = accessor(primitive["indices"]).ravel() if "indices" in primitive else np.arange(len(pos))
                if len(idx) % 3 or np.max(idx) >= len(pos):
                    raise ValueError("Invalid triangle indices")
                if np.linalg.det(world[:3, :3]) < 0:
                    idx = idx.reshape(-1, 3)[:, [0, 2, 1]].ravel()
                vertices.append(np.column_stack((pos, normal, uv)).astype("<f4"))
                indices.append((idx.astype("<u4") + vertex_count).astype("<u4"))
                draws.append((index_count, len(idx), primitive.get("material", len(doc.get("materials", [])))))
                vertex_count += len(pos)
                index_count += len(idx)
        for child in node.get("children", []):
            visit(child, world)

    for root in doc["scenes"][doc.get("scene", 0)]["nodes"]:
        visit(root, np.eye(4))
    if not draws:
        raise ValueError("Empty scene")
    verts = np.concatenate(vertices)
    camera = next((c for name, c in cameras if name == "PhysCamera003"), cameras[0][1] if cameras else None)
    if camera is None:
        raise ValueError("SDK Sponza camera missing")
    camera_data = np.concatenate((camera[:3, 3], -camera[:3, 2], camera[:3, 1])).astype("<f4")

    # Material textures are deduplicated. A white texture covers untextured materials.
    texture_ids, textures = {}, [Image.new("RGBA", (1, 1), (255, 255, 255, 255))]
    materials = []
    for mat in doc.get("materials", []) + [{}]:
        pbr = mat.get("pbrMetallicRoughness", {})
        factor = pbr.get("baseColorFactor", [1, 1, 1, 1])
        tex = pbr.get("baseColorTexture")
        texid = 0
        if tex:
            if tex.get("texCoord", 0) != 0 or tex.get("extensions"):
                raise ValueError("Unsupported base color UV transform")
            image_id = doc["textures"][tex["index"]]["source"]
            if image_id not in texture_ids:
                image_path = source.parent / unquote(doc["images"][image_id]["uri"])
                image = Image.open(image_path).convert("RGBA")
                image.thumbnail((512, 512), Image.Resampling.LANCZOS)
                texture_ids[image_id] = len(textures)
                textures.append(image)
            texid = texture_ids[image_id]
        mode = mat.get("alphaMode", "OPAQUE")
        cutoff = mat.get("alphaCutoff", .5) if mode == "MASK" else .1 if mode == "BLEND" else -1
        materials.append(struct.pack("<4ffI", *factor, cutoff, texid))
    destination.parent.mkdir(parents=True, exist_ok=True)
    with destination.open("wb") as out:
        out.write(struct.pack("<4s5I", b"SPZ1", vertex_count, index_count, len(draws), len(materials), len(textures)))
        out.write(camera_data.tobytes())
        out.write(verts.tobytes())
        out.write(np.concatenate(indices).tobytes())
        for draw in draws:
            out.write(struct.pack("<3I", *draw))
        out.write(b"".join(materials))
        for image in textures:
            mips = [image]
            while mips[-1].width > 1 or mips[-1].height > 1:
                prev = mips[-1]
                mips.append(prev.resize((max(1, prev.width // 2), max(1, prev.height // 2)), Image.Resampling.BOX))
            out.write(struct.pack("<3I", image.width, image.height, len(mips)))
            for mip in mips:
                out.write(mip.tobytes())
    print(f"Sponza: {vertex_count} vertices, {index_count//3} triangles, {len(draws)} draws, {len(textures)} textures")
    print(f"Bounds={verts[:, :3].min(axis=0)} .. {verts[:, :3].max(axis=0)}")
    print(f"Camera={camera_data.tolist()}; bytes={destination.stat().st_size}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    convert(args.source, args.destination)
