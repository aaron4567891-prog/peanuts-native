"""Expand declarative inventory pages; ten rows share read-only snapshot fields."""
import copy

POUCHES = [("Medicine", 60), ("TM Case", 108), ("Candy Jar", 200), ("Power-Up", 150),
           ("Catching", 50), ("Battle", 150), ("Bag", 150)]


def add_map(manifest, coordinates):
    """External, player-owned map positions become exact bounded zone gates."""
    if coordinates.get("size") != [1280, 720] or len(coordinates.get("entries", [])) != 43:
        raise ValueError("expected the verified 1280x720 map with 43 coordinate entries")
    page = next(p for p in manifest["pages"] if p["id"] == "map")
    page["widgets"] = [w for w in page["widgets"] if w["type"] in {"rect", "button"}
                       or (w.get("hide_bind") == "lgp.game_ready" and w.get("hide_eq") == 1)]
    # Keep the original bottom-tab geometry. Crop the empty eastern sea with
    # runtime UVs, retaining all region points, and use the tall content area.
    crop = (30, 20, 730, 660)
    view = (146, 80, 948, 858)
    extra = [dict(type="label", rect=[44, 38, 330, 0], text="KANTO MAP",
                  text_scale=5, color="#FF4D3A27", need_bind="lgp.game_ready"),
             dict(type="label", rect=[400, 38, 610, 0], bind_text="lgp.map.name",
                  text_scale=4, color="#FF4D3A27", need_bind="lgp.map_ready"),
             dict(type="label", rect=[1030, 38, 100, 0], text="Zone", text_scale=3,
                  color="#FF7A684F", need_bind="lgp.map_ready"),
             dict(type="value", rect=[1120, 38, 80, 0], bind="lgp.map_zone",
                  text_scale=4, color="#FF4D3A27", need_bind="lgp.map_ready"),
             dict(type="image", rect=list(view), src="file:art/map.png",
                  src_rect=[30/1280, 20/720, 760/1280, 680/720],
                  need_bind="lgp.game_ready"),
             dict(type="label", rect=[44, 941, 1120, 0], bind_text="lgp.map_status_text",
                  text_scale=2, color="#FF7A684F", need_bind="lgp.map_ready",
                  wrap_width=1120, max_lines=3),
             dict(type="label", rect=[44, 941, 1120, 0], text="Waiting for a verified live location",
                  text_scale=2, color="#FF7A684F", need_bind="lgp.game_ready",
                  hide_bind="lgp.map_ready", hide_eq=1)]
    zones = set()
    for entry in coordinates["entries"]:
        first, count = entry["zone_first"], entry["zone_count"]
        x, y = entry["position"]
        if (type(first) is not int or type(count) is not int or first < 0 or count < 1
                or first + count > 48 or not (0 <= x <= 1280 and 0 <= y <= 720)):
            raise ValueError("invalid external map coordinate or zone range")
        zone_range = set(range(first, first + count))
        if zones & zone_range:
            raise ValueError("external map zone ranges overlap")
        zones |= zone_range
        if not (crop[0] <= x <= crop[0]+crop[2] and crop[1] <= y <= crop[1]+crop[3]):
            raise ValueError("map crop must retain every verified region marker")
        px = round(view[0] + (x-crop[0]) * view[2]/crop[2])
        py = round(view[1] + (y-crop[1]) * view[3]/crop[3])
        for size, color in ((26, "#FF4D3A27"), (20, "#FFFFD449"), (10, "#FFDB4E3E")):
            extra.append(dict(type="rect", rect=[px-size//2, py-size//2, size, size],
                              bg=color, frame=0, need_bind="lgp.map_ready",
                              hide_bind="lgp.map_zone", keep_min=first, keep_max=first+count-1))
    # Keep the opaque intro last on this page too.
    position = next(i for i, w in enumerate(page["widgets"])
                    if w.get("hide_bind") == "lgp.game_ready" and w.get("hide_eq") == 1)
    page["widgets"][position:position] = extra


def expand(manifest):
    source = next(p for p in manifest["pages"] if p["id"] == "bag")
    manifest["pages"].remove(source)
    manifest["actions"].pop("bag", None)
    for action in manifest["actions"].values():
        if action["page"] == "bag":
            action["page"] = "bag_0_0"
    manifest["actions"]["bag"] = {"kind": "page", "page": "bag_0_0",
                                      "enabled_bind": "lgp.game_ready"}

    def label(text, rect, scale=4, **extra):
        return dict(type="label", rect=rect, text=text, text_scale=scale,
                    color="#FF4D3A27", need_bind="lgp.game_ready", **extra)

    def button(text, rect, target, selected=False):
        manifest["actions"][target] = dict(kind="page", page=target,
                                           enabled_bind="lgp.game_ready")
        return dict(type="button", rect=rect, text=text, on_tap=target,
                    text_scale=3, bg="#FFF3C94B" if selected else "#FFFFF3CF",
                    color="#FF4D3A27", border=2, need_bind="lgp.game_ready", pill=True)

    for pouch, (title, capacity) in enumerate(POUCHES):
        for page in range((capacity + 9) // 10):
            page_id = f"bag_{pouch}_{page}"
            fixed = [copy.deepcopy(w) for w in source["widgets"] if
                     w["type"] == "rect" or (w["type"] == "button" and w["rect"][1] == 980)
                     or (w.get("hide_bind") == "lgp.game_ready" and w.get("hide_eq") == 1)]
            # Splash always draws last; content is inserted below it.
            splash = [w for w in fixed if w.get("hide_bind") == "lgp.game_ready" and w.get("hide_eq") == 1]
            fixed = [w for w in fixed if w not in splash]
            widgets = fixed + [label("BAG / " + title, [44, 40, 1000, 0], 6),
                               label("Name", [60, 185, 700, 0]),
                               label("Qty", [1000, 185, 140, 0])]
            for other, (name, _) in enumerate(POUCHES):
                widgets.append(button(name, [36 + other * 168, 110, 160, 50],
                                      f"bag_{other}_0", other == pouch))
            for row in range(10):
                y, prefix = 225 + row * 56, f"lgp.bag.item{row}."
                widgets += [dict(type="rect", rect=[40, y, 1160, 52], bg="#FFFFF3CF",
                                 frame=0, need_bind=prefix + "valid", hide_bind="lgp.game_ready", hide_eq=0),
                            dict(type="label", rect=[60, y+12, 840, 0], bind_text=prefix+"name",
                                 text_scale=4, color="#FF4D3A27", need_bind=prefix+"valid",
                                 hide_bind="lgp.game_ready", hide_eq=0, fit_text=True, text_min_scale=2),
                            dict(type="value", rect=[1000, y+12, 160, 0], bind=prefix+"qty",
                                 text_scale=4, color="#FF4D3A27", need_bind=prefix+"valid",
                                 hide_bind="lgp.game_ready", hide_eq=0)]
            widgets += [label(f"Page {page+1}", [530, 815, 180, 0]),
                        dict(type="label", rect=[60, 895, 1100, 0], bind_text="lgp.bag_status_text",
                             text_scale=3, color="#FF7A684F", need_bind="lgp.bag_ready",
                             wrap_width=1100, max_lines=2),
                        dict(type="label", rect=[60, 895, 1100, 0], text="Waiting for a stable live Bag",
                             text_scale=3, color="#FF7A684F", need_bind="lgp.game_ready",
                             hide_bind="lgp.bag_ready", hide_eq=1)]
            if page:
                widgets.append(button("Previous", [40, 805, 300, 60], f"bag_{pouch}_{page-1}"))
            if (page + 1) * 10 < capacity:
                next_button = button("Next", [900, 805, 300, 60], f"bag_{pouch}_{page+1}")
                next_button.update(need_bind="lgp.bag.has_next", hide_bind="lgp.game_ready", hide_eq=0)
                widgets.append(next_button)
            widgets += splash
            manifest["pages"].append(dict(id=page_id, widgets=widgets))
    return manifest
