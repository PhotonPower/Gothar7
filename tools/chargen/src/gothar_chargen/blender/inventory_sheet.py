"""Blender side of ``gothar-chargen inventory-sheet``: every item model in its inventory cell view
(data/inventory_views.toml, rule in gothar_chargen.inventory) and a review sheet like an inventory.

    blender --background --factory-startup --python inventory_sheet.py -- \\
        --root <repository> --out <folder> [--cell 192] [--columns 8] [--samples 32]

Writes ``<out>/cells/<id>.png`` (transparent) and ``<out>/inventory_views.png`` (grid on dark
cells, label per cell: model, category, which table the view came from). Cycles on the CPU, so it
does not compete with GPU runs of other sessions.
"""

from __future__ import annotations

import argparse
import math
import sys
from pathlib import Path

import bpy  # type: ignore[import-not-found]
from mathutils import Matrix, Vector  # type: ignore[import-not-found]

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from gothar_chargen.inventory import (  # noqa: E402
    ITEMS_DIR,
    SCRIPTS_ITEMS_DIR,
    VIEWS_FILE,
    cell,
    load_views,
    lua_items,
    model_categories,
    model_vertices,
)

GLTF_TO_BLENDER = Matrix(((1, 0, 0, 0), (0, 0, -1, 0), (0, 1, 0, 0), (0, 0, 0, 1)))
LIGHT_FROM = Vector((-0.55, 0.75, 0.65))  # view space: upper left, in front
CELL_COLOUR = (0.055, 0.047, 0.04)
BORDER_COLOUR = (0.24, 0.2, 0.15)
PAGE_COLOUR = (0.018, 0.016, 0.014)
TEXT_COLOUR = (0.8, 0.76, 0.68)


def _parse_args() -> argparse.Namespace:
    argv = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    parser = argparse.ArgumentParser(prog="inventory_sheet")
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--cell", type=int, default=192)
    parser.add_argument("--columns", type=int, default=8)
    parser.add_argument("--samples", type=int, default=32)
    parser.add_argument("--only", nargs="*", default=[])
    return parser.parse_args(argv)


def _cycles_cpu(scene: bpy.types.Scene, samples: int, denoise: bool) -> None:
    scene.render.engine = "CYCLES"
    scene.cycles.device = "CPU"
    scene.cycles.samples = samples
    scene.cycles.use_denoising = denoise
    scene.render.film_transparent = True
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGBA"


def _emission(name: str, colour: tuple[float, float, float]) -> bpy.types.Material:
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    nodes = mat.node_tree.nodes
    nodes.clear()
    out = nodes.new("ShaderNodeOutputMaterial")
    em = nodes.new("ShaderNodeEmission")
    em.inputs["Color"].default_value = (*colour, 1.0)
    mat.node_tree.links.new(em.outputs[0], out.inputs[0])
    return mat


def render_cells(args: argparse.Namespace) -> list[tuple[str, str, str]]:
    """Renders every model; returns (id, category, source of the view) per cell."""
    root = args.root
    views = load_views(root / VIEWS_FILE)
    models = sorted(f"items/{p.name}" for p in (root / ITEMS_DIR).glob("*.glb"))
    if args.only:
        models = [m for m in models if Path(m).stem in args.only]
    categories = model_categories(lua_items(root / SCRIPTS_ITEMS_DIR), models)

    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    _cycles_cpu(scene, args.samples, denoise=True)
    scene.render.resolution_x = scene.render.resolution_y = args.cell
    scene.view_settings.view_transform = "AgX"
    world = bpy.data.worlds.new("inventory")
    scene.world = world
    world.use_nodes = True
    background = world.node_tree.nodes["Background"]
    background.inputs["Color"].default_value = (0.55, 0.52, 0.48, 1.0)
    background.inputs["Strength"].default_value = 1.2
    sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN"))
    sun.data.energy = 4.0
    sun.data.angle = math.radians(10)
    toward = -(GLTF_TO_BLENDER.to_3x3() @ LIGHT_FROM.normalized())
    sun.rotation_euler = toward.to_track_quat("-Z", "Y").to_euler()
    scene.collection.objects.link(sun)
    camera = bpy.data.objects.new("camera", bpy.data.cameras.new("camera"))
    camera.rotation_euler = (math.radians(90), 0.0, 0.0)  # looks down view -Z = Blender +Y
    camera.data.clip_start = 0.005
    camera.data.clip_end = 50.0
    scene.collection.objects.link(camera)
    scene.camera = camera

    cells_dir = args.out / "cells"
    cells_dir.mkdir(parents=True, exist_ok=True)
    out = []
    for mesh in models:
        item = Path(mesh).stem
        own = sorted(categories[mesh])
        category = views.category_of(mesh, own[0] if own else None)
        view = views.resolve(mesh, category)
        vertices = model_vertices(root / "assets/source" / mesh)
        geometry = cell(view, vertices, views.fov)
        before = set(bpy.data.objects)
        bpy.ops.import_scene.gltf(filepath=str(root / "assets/source" / mesh))
        new = [o for o in bpy.data.objects if o not in before]
        for o in new:
            o.hide_render = not (o.type == "MESH" and o.name.endswith("_lod0"))
        place = GLTF_TO_BLENDER @ Matrix(geometry.matrix.tolist()) @ GLTF_TO_BLENDER.inverted()
        for o in new:
            if o.parent is None:
                o.matrix_world = place @ o.matrix_world
        camera.location = (0.0, -geometry.distance, 0.0)
        # the cell's image window: half size e and centre w in units of the full view (off-axis);
        # Blender's shift is in units of the (square) image width
        tan_half = geometry.half * math.tan(math.radians(geometry.fov) / 2)
        camera.data.angle = 2 * math.atan(tan_half)
        camera.data.shift_x = float(geometry.window[0]) / (2 * geometry.half)
        camera.data.shift_y = float(geometry.window[1]) / (2 * geometry.half)
        scene.render.filepath = str(cells_dir / f"{item}.png")
        bpy.ops.render.render(write_still=True)
        for o in new:
            bpy.data.objects.remove(o, do_unlink=True)
        source = "model" if mesh in views.models else ("category" if category else "default")
        out.append((item, category or "-", source))
        print(f"[chargen] cell {item} {category} {source} d={geometry.distance:.3f}")
    return out


def compose_sheet(args: argparse.Namespace, cells: list[tuple[str, str, str]]) -> Path:
    """A flat scene with an orthographic camera: cell squares, the cell images, labels."""
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    _cycles_cpu(scene, 16, denoise=False)
    scene.render.film_transparent = False
    scene.view_settings.view_transform = "Standard"
    world = bpy.data.worlds.new("page")
    scene.world = world
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs["Color"].default_value = (*PAGE_COLOUR, 1.0)

    columns = min(args.columns, len(cells))
    rows = math.ceil(len(cells) / columns)
    step_x, step_y, title = 1.08, 1.38, 0.7  # one cell = 1 unit; label space below each cell
    width, height = columns * step_x + 0.08, rows * step_y + title
    ppu = args.cell
    scene.render.resolution_x, scene.render.resolution_y = int(width * ppu), int(height * ppu)
    camera = bpy.data.objects.new("camera", bpy.data.cameras.new("camera"))
    camera.data.type = "ORTHO"
    camera.data.ortho_scale = max(width, height)
    camera.location = (width / 2, height / 2, 10.0)
    scene.collection.objects.link(camera)
    scene.camera = camera

    border, square = _emission("border", BORDER_COLOUR), _emission("cell", CELL_COLOUR)
    text = _emission("text", TEXT_COLOUR)

    def plane(x: float, y: float, z: float, size: float, mat: bpy.types.Material) -> None:
        bpy.ops.mesh.primitive_plane_add(size=size, location=(x, y, z))
        bpy.context.object.data.materials.append(mat)

    def label(x: float, y: float, body: str, size: float) -> None:
        curve = bpy.data.curves.new("label", "FONT")
        curve.body = body
        curve.size = size
        curve.align_x = "CENTER"
        obj = bpy.data.objects.new("label", curve)
        obj.location = (x, y, 0.02)
        obj.data.materials.append(text)
        scene.collection.objects.link(obj)

    curve = bpy.data.curves.new("title", "FONT")
    curve.body = "Inventar-Ansichten (data/inventory_views.toml) - Kontur zentriert, fill v2"
    curve.size = 0.26
    head = bpy.data.objects.new("title", curve)
    head.location = (0.1, height - 0.45, 0.02)
    head.data.materials.append(text)
    scene.collection.objects.link(head)

    for i, (item, category, source) in enumerate(cells):
        cx = 0.04 + (i % columns) * step_x + step_x / 2
        cy = height - title - (i // columns) * step_y - 0.54
        plane(cx, cy, 0.0, 1.0, border)
        plane(cx, cy, 0.001, 0.96, square)
        img = bpy.data.images.load(str(args.out / "cells" / f"{item}.png"))
        mat = bpy.data.materials.new(item)
        mat.use_nodes = True
        nodes, links = mat.node_tree.nodes, mat.node_tree.links
        nodes.clear()
        out = nodes.new("ShaderNodeOutputMaterial")
        tex = nodes.new("ShaderNodeTexImage")
        tex.image = img
        em = nodes.new("ShaderNodeEmission")
        clear = nodes.new("ShaderNodeBsdfTransparent")
        mix = nodes.new("ShaderNodeMixShader")
        links.new(tex.outputs["Color"], em.inputs["Color"])
        links.new(tex.outputs["Alpha"], mix.inputs[0])
        links.new(clear.outputs[0], mix.inputs[1])
        links.new(em.outputs[0], mix.inputs[2])
        links.new(mix.outputs[0], out.inputs[0])
        plane(cx, cy, 0.002, 0.96, mat)
        label(cx, cy - 0.62, item.removeprefix("it_"), 0.115)
        label(cx, cy - 0.78, f"{category} ({source})", 0.09)

    path = args.out / "inventory_views.png"
    scene.render.filepath = str(path)
    bpy.ops.render.render(write_still=True)
    return path


def main() -> None:
    args = _parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    cells = render_cells(args)
    path = compose_sheet(args, cells)
    print(f"[chargen] sheet {path} ({len(cells)} cells)")


if __name__ == "__main__":
    main()
