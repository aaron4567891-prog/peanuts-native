# Pokemon: Let's Go, Pikachu! live party companion

Source-only, read-only companion for title `010003F003A34000`, base build
`1C375692DE4F4B87`, companion runtime 25. This is a new UI made from built-in
shapes and the repository's licensed Noto Sans font. It contains no game art, game code, resolved game data or native
binary. The Brilliant Diamond companion is the interaction reference; its
Unity-specific reader, assets and write actions are not reused.

## Source and package lifecycle

`package.template.json` and `manifest.template.json` are intentionally not
installable: their module digest is the literal `@MODULE_SHA256@`. A separately
compiled Android ARM64 native reader belongs outside the repository.
`materialize.py` validates the source contracts, copies that module into a new
external package folder, replaces the digest in both declarations and writes the
per-build data file. It refuses repository-local outputs or existing destinations.
The source check (`--check`) needs no module and writes nothing.

Materialization requires Python packages `Pillow` and `fontTools`. `make_font.py`
reads the existing `dm/third_party/fonts/NotoSans-Regular-subset.ttf`, generates
an alpha PNG atlas and MFNT-compatible binary metrics in the external package's
`dualscreen/fonts/`, and copies its SIL OFL license there. No generated font
binary is committed. The runtime's `file:` asset provider and MFNT parser load
these files without a native font extension or engine changes. Latin names,
available punctuation and gender signs are supported; missing glyphs use `?`.
The generated header uses capital height so existing text scales retain their
authored size. Font metrics are binary, not an invented JSON schema.

The materialized package has `package.json`, `dualscreen/manifest.json`,
`dualscreen/1C375692DE4F4B87.json` and
`dualscreen/modules/android-arm64-v8a/010003F003A34000.so`. No game-specific
resolved addresses are part of these declarative templates.

An isolated Android runtime test accepted the party, map and guide pages and the production
module, rendered waiting and synthetic six-slot states, followed a touch to the
details page, and reflected an HP mutation. The generated Noto atlas was also
loaded through the actual runtime and visually checked at 1240x1080. Synthetic
fixtures are external test artifacts and contain no live player data.

The cream content panel and persistent colorful Party, Bag, Map and Guide tabs use a
Pikachu palette inspired by the observed Brilliant Diamond companion layout.
All three tabs navigate locally; party detail pages retain member selection above
the tab bar. No unsupported Pokedex control is displayed. The Bag and Map pages
currently reports that data is unavailable; it contains no map art or guessed
location. Future native integration may publish integer `lgp.map_ready` (1 only
for verified map information) and text `lgp.map_status_text`. When ready is 1,
that text replaces the static pending notice. No map reader is implied by the
declarative page alone. Guide provides usage and the existing live reader status.
The Bag page follows the same availability contract with integer `lgp.bag_ready`
and text `lgp.bag_status_text`; it shows an honest pending notice until a verified
inventory reader is connected. No quantities or item names are invented.

## Optional title-screen artwork

Pass `--intro-art /external/generated-image.png` when materializing to copy the
user-supplied or generated artwork into the external package only. No raster art
is committed. Artwork sources and any generation prompt stay with the external
build artifacts. The current intro layout preserves the supplied title logo's
830:400 aspect ratio within a cream and gold screen.

The native reader publishes `lgp.game_ready` on every sample: initially 0, then 1
once gameplay state is verified independently of party count. Readiness remains
latched through battles and menus within that game context, and resets when the
context is destroyed or changes. Missing initial state stays on the splash.
All pages carry the opaque image overlay until ready becomes 1, and local page
actions are disabled during it. The image has no input action and does not inject
or intercept game controls. This gate must not use a nonempty party as its only
criterion, because gameplay may have an empty party. Without `--intro-art`, the
materializer strips the optional splash and its readiness gates.

## Inventory paging

`build_ui.py` expands 87 inventory pages named `bag_<pouch>_<zero-based-page>`.
The native module reads the host's `__page` text to select a pouch and ten-item
window; local navigation never writes guest memory. The shared snapshot has
integer `lgp.bag.count`, `lgp.bag.page`, `lgp.bag.pages`, `lgp.bag.has_next`, text
`lgp.bag.pouch_name`, and ten `lgp.bag.item0` through `item9` rows with text
`.name` and integers `.id`, `.qty`, `.valid`. The Next button requires has_next.
Pouch capacities are 60, 108, 200, 150, 50, 150, 150; no items are synthesized.
Names must come from the player's game or explicit numeric fallback. The native
module republishes the selected window each sample. `--check` expands and
validates these pages before packaging.

## Player-owned map inputs

Pass both `--map-art /external/pikachu-kanto-map.png` and
`--map-coordinates /external/townmap-coordinates.json` to materialize the map.
The generator validates the 1280x720 image and 43 bounded, nonoverlapping zone
ranges. Runtime normalized `src_rect` crops the unused eastern sea to source
rectangle (30,20)-(760,680), retaining every verified region point. The land view
fills 948x858 above the unchanged bottom tabs, about 44% larger in each dimension
than the original map view. Marker coordinates use the same crop transform.
A marker requires `lgp.map_ready` and uses
`hide_bind: lgp.map_zone` with inclusive `keep_min` / `keep_max` matching that
region's actual zone range. The reader publishes `lgp.map.name`, the numeric
`lgp.map_zone`, and `lgp.map_status_text`. No coordinates or map PNG are committed;
they remain external user-owned build inputs. Until the live reader is ready,
the map has no location marker and displays an explicit waiting notice.

The native module must independently gate its title/build, use ABI 1 with hash
`0x8d3f5b1e6a70c429`, and require no write or guest-call capability. Its base
`supports_build`, `create`, `destroy`, `sample` and `tick` callbacks must all exist.
An empty `tick` is sufficient for this viewer. Every displayed field must be
published again on each sample; the runtime clears snapshots between samples.

## Native field contract

All names below are literal runtime snapshot names. Values are live observations,
not defaults to show while the party is unavailable.

| Field | Type | Meaning |
|---|---|---|
| `lgp.ready` | integer 0/1 | At least one validated live party slot is available |
| `lgp.party_count` | integer 0..6 | Validated live party count |
| `lgp.status_text` | UTF-8 text | Reader status, such as searching, no party yet or live party found |
| `lgp.p{i}.valid` | integer 0/1 | Slot i is currently validated; i is 0..5 |
| `lgp.p{i}.name` | UTF-8 text | Decoded PB7 nickname |
| `lgp.p{i}.species` | integer | Observed species ID |
| `lgp.p{i}.species_name` | UTF-8 text | Name read from the user's RomFS, or the explicit fallback `Species <id>` |
| `lgp.p{i}.level` | integer | Observed party level |
| `lgp.p{i}.hp`, `lgp.p{i}.hpmax` | integers | Current and maximum party HP |
| `lgp.p{i}.atk`, `lgp.p{i}.def`, `lgp.p{i}.spa`, `lgp.p{i}.spd`, `lgp.p{i}.spe` | integers | Observed attack, defense, special attack, special defense and speed |
| `lgp.p{i}.move0` through `lgp.p{i}.move3` | integers | Observed move IDs; 0 means no move |
| `lgp.p{i}.move0_name` through `lgp.p{i}.move3_name` | UTF-8 text | Name read from the user's RomFS; fallback `Move <id>`, or `None` for ID 0 |

Publish `lgp.ready=0` and every slot's `valid=0` while the party cannot be
validated. Do not keep displaying a previous party after validation fails. The UI
shows a waiting state and the reader's status. A module load/build failure is
shown separately using the runtime's `module_ready`, `module_error` and
`module_error_message` fields.

Selection is UI-local page navigation: the overview has six selectable cards,
and each slot has a detail page with HP, level, species name/ID, five stats and four
move names/IDs. The detail pages also provide a party overview button and six slot
selectors. No `selected_slot` callback or input injection is needed. All actions
are `kind: page`; `nav` is disabled so companion navigation does not consume the
game's controller buttons. Names and their explicit numeric IDs are displayed
together. The native reader always supplies a text field, using the documented
ID fallback if its read-only RomFS lookup is unavailable; the UI does not invent
or bundle any game strings. Nicknames remain the live PB7 nickname, separately
from the species name.

## Checked runtime schema subset

The UI uses the `rect`, `label`, `value`, `bar` and `button` types accepted by
`ParseWidgetType`. Widget rectangles, text bindings, integer/max bindings, need/hide
gates, text-fit options and page actions follow `ParseManifestJson` in the
[runtime parser](../../../dm/peanuts/rt/mods/mod_manifest.cpp). The materializer
checks the exact subset used here: rectangle shapes/bounds, property types,
binding names against the native contract, distinct page IDs, known page targets,
ABI/build gate and module declaration parity. It rejects unknown widget keys and
any action other than local page navigation. This check does not execute the
native parser or establish that the package has rendered correctly on a device.

## Installation and verification

The existing runtime can load this package without an APK rebuild. Put the
materialized `PokemonLetsGoPikachu` folder under the running pack's sibling
`peanuts` folder, while the game is closed. For app-owned packs the root is the
app's external `files/packs/peanuts` folder. The loader verifies the module digest
and stages the library into private executable storage; `run-as` is not required.

Before claiming live support, verify the exact game build, `module_ready=1`, a
real nonzero party, changes in HP/party order and transitions back to a state
where no party is available. Verify the second-screen selection controls while
the game remains responsive. Source checks establish schema/contract consistency,
not live memory correctness or final visual layout on the Thor.
