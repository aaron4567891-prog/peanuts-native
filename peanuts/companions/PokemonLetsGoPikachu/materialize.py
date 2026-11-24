"""Materialize the source-only Pikachu companion around a separately built module.

Agent/build helper: output and native binaries must remain outside this repository.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
from make_font import generate as generate_font
from build_ui import expand as expand_ui, add_map

SOURCE = Path(__file__).resolve().parent
REPO = SOURCE.parents[2]
TITLE = "010003F003A34000"
BUILD = "1C375692DE4F4B87"
MODULE_NAME = f"{TITLE}.so"
MODULE_REL = f"modules/android-arm64-v8a/{MODULE_NAME}"
HASH_TOKEN = "@MODULE_SHA256@"

# Deliberately bounded to the subset used here. These names/types are read by
# ParseManifestJson, ParseWidgetType and the page-action branch in the runtime's
# dm/peanuts/rt/mods/mod_manifest.cpp; this is not a substitute for runtime parsing.
WIDGET_KEYS = {
    "type", "rect", "bg", "color", "frame", "text", "text_scale", "bind_text",
    "bind", "max_bind", "max_sep", "need_bind", "hide_bind", "hide_eq",
    "wrap_width", "max_lines", "fit_text", "text_min_scale", "on_tap", "border", "pill", "src", "src_rect", "keep_min", "keep_max",
}
TEXT_FIELDS = {"lgp.status_text", "lgp.map_status_text", "lgp.bag_status_text", "module_error_message"}
INT_FIELDS = {"lgp.ready", "lgp.party_count", "lgp.map_ready", "lgp.bag_ready", "lgp.game_ready", "module_ready", "module_error"}
TEXT_FIELDS.add("lgp.bag.pouch_name")
TEXT_FIELDS.add("lgp.map.name")
INT_FIELDS.add("lgp.map_zone")
INT_FIELDS.update({"lgp.bag.count", "lgp.bag.page", "lgp.bag.pages", "lgp.bag.has_next"})
for item in range(10):
    TEXT_FIELDS.add(f"lgp.bag.item{item}.name")
    INT_FIELDS.update(f"lgp.bag.item{item}.{field}" for field in ("id", "qty", "valid"))
for slot in range(6):
    prefix = f"lgp.p{slot}."
    TEXT_FIELDS.update(prefix + field for field in ("name", "species_name"))
    TEXT_FIELDS.update(prefix + f"move{move}_name" for move in range(4))
    INT_FIELDS.update(prefix + field for field in (
        "valid", "species", "level", "hp", "hpmax", "atk", "def", "spa", "spd", "spe"))
    INT_FIELDS.update(prefix + f"move{move}" for move in range(4))


def validate_widget(widget):
    if set(widget) - WIDGET_KEYS:
        raise ValueError("widget contains a field outside the checked runtime subset")
    if widget.get("type") not in {"rect", "label", "value", "bar", "button", "image"}:
        raise ValueError("unexpected widget in the source-only party viewer")
    rect = widget.get("rect", [])
    if (len(rect) != 4 or any(type(n) is not int for n in rect)
            or min(rect) < 0 or rect[0] + rect[2] > 1240 or rect[1] + rect[3] > 1080):
        raise ValueError("widget rectangle must fit the declared logical canvas")
    for key in ("frame", "text_scale", "hide_eq", "keep_min", "keep_max", "wrap_width", "max_lines", "text_min_scale", "border"):
        if key in widget and type(widget[key]) is not int:
            raise ValueError(f"{key} must be an integer in this runtime subset")
    for key in ("fit_text", "pill"):
        if key in widget and type(widget[key]) is not bool:
            raise ValueError(f"{key} must be boolean")
    for key in ("bg", "color"):
        if key in widget and not re.fullmatch(r"#[0-9A-Fa-f]{8}", widget[key]):
            raise ValueError(f"{key} must be an ARGB color")
    for key in ("bind", "max_bind", "need_bind", "hide_bind"):
        if key in widget and widget[key] not in INT_FIELDS:
            raise ValueError(f"{key} does not name an agreed integer field")
    if "bind_text" in widget and widget["bind_text"] not in TEXT_FIELDS:
        raise ValueError("bind_text does not name an agreed text field")
    for key in ("text", "max_sep", "on_tap"):
        if key in widget and not isinstance(widget[key], str):
            raise ValueError(f"{key} must be text")
    if widget.get("type") == "image" and widget.get("src") not in {"file:art/intro.png", "file:art/map.png"}:
        raise ValueError("image source must be one of the external package's verified PNG inputs")


def read_template(name, digest):
    value = json.loads((SOURCE / name).read_text(encoding="utf-8").replace(HASH_TOKEN, digest))
    return expand_ui(value) if name == "manifest.template.json" else value


def validate_templates(package, manifest):
    if package["title_id"] != TITLE or manifest["title_id"] != TITLE:
        raise ValueError("the source templates name another title")
    if package["module"] != manifest["module"]:
        raise ValueError("package and manifest module declarations must match")
    if (not package["requires_module"] or not manifest["requires_module"]
            or package["min_runtime"] != 25 or manifest["min_runtime"] != 25):
        raise ValueError("the source package requires a native module and runtime 25")
    if manifest["module"]["build_ids"] != [BUILD]:
        raise ValueError("the source templates must gate the verified base build")
    if manifest["module"]["libraries"]["android-arm64-v8a"]["path"] != MODULE_REL:
        raise ValueError("unexpected Android module path")
    if (manifest["module"]["abi"] != 1
            or not re.fullmatch(r"[0-9a-f]{64}", manifest["module"]["libraries"]["android-arm64-v8a"]["sha256"])):
        raise ValueError("module declaration needs ABI 1 and a lowercase SHA-256")
    if (manifest.get("canvas_w"), manifest.get("canvas_h")) != (1240, 1080):
        raise ValueError("the UI is authored for a 1240 by 1080 logical canvas")
    pages = {p["id"] for p in manifest["pages"]}
    if len(pages) != len(manifest["pages"]) or "party" not in pages:
        raise ValueError("pages must have distinct IDs and include the party overview")
    for action in manifest["actions"].values():
        if (set(action) - {"kind", "page", "enabled_bind"} or action["kind"] != "page"
                or action["page"] not in pages or action.get("enabled_bind") not in (None, "lgp.game_ready")):
            raise ValueError("the party viewer accepts only local page navigation actions")
    for page in manifest["pages"]:
        for widget in page["widgets"]:
            validate_widget(widget)
            if widget.get("on_tap") and widget["on_tap"] not in manifest["actions"]:
                raise ValueError("widget refers to a missing page action")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--module", type=Path, help="separately compiled Android ARM64 .so")
    parser.add_argument("--out", type=Path, help="new package folder outside the repository")
    parser.add_argument("--check", action="store_true", help="validate the source templates only")
    parser.add_argument("--intro-art", type=Path, help="external generated PNG for the title/intro splash")
    parser.add_argument("--map-art", type=Path, help="external player-owned 1280x720 map PNG")
    parser.add_argument("--map-coordinates", type=Path, help="external verified town-map coordinate JSON")
    args = parser.parse_args()
    if args.check:
        package = read_template("package.template.json", "0" * 64)
        manifest = read_template("manifest.template.json", "0" * 64)
        validate_templates(package, manifest)
        print("Pikachu source templates: title, build gate, module parity and read-only UI valid")
        return
    if not args.module or not args.out:
        parser.error("materialization needs --module and --out (or use --check)")
    module, output = args.module.resolve(), args.out.resolve()
    if module.is_relative_to(REPO) or output.is_relative_to(REPO):
        parser.error("compiled modules and materialized packages must stay outside the repository")
    if output.exists():
        parser.error("--out must be a new folder; existing packages are never overwritten")
    with module.open("rb") as stream:
        header = stream.read(64)
    if (len(header) != 64 or header[:6] != b"\x7fELF\x02\x01"
            or struct.unpack_from("<HH", header, 16) != (3, 183)):
        parser.error("--module must be a little-endian ELF64 AArch64 shared object")
    with module.open("rb") as stream:
        digest = hashlib.file_digest(stream, "sha256").hexdigest()
    package = read_template("package.template.json", digest)
    manifest = read_template("manifest.template.json", digest)
    if bool(args.map_art) != bool(args.map_coordinates):
        parser.error("map art and map coordinates must be supplied together")
    if args.map_art:
        from PIL import Image
        map_art, map_coords = args.map_art.resolve(), args.map_coordinates.resolve()
        if map_art.is_relative_to(REPO) or map_coords.is_relative_to(REPO):
            parser.error("map inputs must remain outside the source repository")
        with Image.open(map_art) as picture:
            if picture.format != "PNG" or picture.size != (1280, 720):
                parser.error("map art must be the verified 1280x720 PNG")
            picture.verify()
        add_map(manifest, json.loads(map_coords.read_text(encoding="utf-8")))
    validate_templates(package, manifest)
    if args.intro_art:
        from PIL import Image
        art = args.intro_art.resolve()
        if art.is_relative_to(REPO):
            parser.error("intro art must remain outside the source repository")
        with Image.open(art) as picture:
            if picture.format != "PNG" or picture.width * picture.height > 16777216:
                parser.error("intro art must be a bounded PNG (at most 16 megapixels)")
            picture.verify()
    else:
        # Optional artwork never makes an existing package wait for a new reader.
        for action in manifest["actions"].values():
            action.pop("enabled_bind", None)
        for page in manifest["pages"]:
            page["widgets"] = [w for w in page["widgets"] if not (
                w.get("hide_bind") == "lgp.game_ready" and w.get("hide_eq") == 1)]
            for widget in page["widgets"]:
                if widget.get("need_bind") == "lgp.game_ready":
                    widget.pop("need_bind")
                if widget.get("hide_bind") == "lgp.game_ready":
                    widget.pop("hide_bind")
                    widget.pop("hide_eq", None)
    module_output = output / "dualscreen" / MODULE_REL
    module_output.parent.mkdir(parents=True)
    shutil.copyfile(module, module_output)
    generate_font(output / "dualscreen/fonts", REPO)
    if args.intro_art:
        art_dir = output / "dualscreen/art"
        art_dir.mkdir(exist_ok=True)
        shutil.copyfile(art, art_dir / "intro.png")
    if args.map_art:
        art_dir = output / "dualscreen/art"
        art_dir.mkdir(exist_ok=True)
        shutil.copyfile(map_art, art_dir / "map.png")
    for target, content in ((output / "package.json", package),
                            (output / "dualscreen/manifest.json", manifest),
                            (output / f"dualscreen/{BUILD}.json", {})):
        target.write_text(json.dumps(content, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"Materialized {TITLE} base {BUILD}: {output}")
    print(f"Module SHA-256: {digest}")


if __name__ == "__main__":
    main()
