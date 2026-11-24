# Exact-build party reader

This reader supports only title `010003F003A34000`, main build
`1c375692de4f4b879abe88742dc0ca60` (remaining NSO build bytes are zero).
Addresses below are relative to the loaded main image, not host addresses.
Static evidence comes from the user's matching main module, checked independently
against the public [Let's Go recompilation](https://github.com/raulzrrs/letsgo-recomp).
Static checks and synthetic tests do not establish live gameplay correctness.

## Party chain

With `B` the main base and `read64` a checked guest memory read:

```
a = read64(B + 0x15FAD08)
b = read64(a)
context = read64(b + 0x98)
party = read64(context + 0x60)
```

The base party object's vtable is `B + 0x159C668`. The saved-game subclass
uses `B + 0x1539EF8`. Its count is a byte at `party + 0x88`. Only the base
class has six inline parameter pointers at `party + 0x58`.
Functions at main offsets `0x197E60`, `0x2462B0` and `0x198AF0` corroborate
the root/context chain. The slot getter at `0x721AA0` bounds indices to 0..5;
the count helper at `0x721AC0` reads offset `0x88`.

For the saved-game class, manager is `read64(context + 0x58)`, save is
`read64(manager + 0xB0)` and metadata is `read64(save + 0x3F7F8)`.
The slot index is `read16(metadata + 0x54 + 2 * slot)`; reject sentinel 1001
or indices outside `read16(metadata + 0x62)`. The parameter pointer is
`read64(save + 0x3F800 + 8 * index)`. Constructor `0x1716C0` installs this
subclass, and its slot getter `0x171E80` follows that index map. Reading the
base class's inline slots in this subclass would return the wrong records.
The full saved-data chain and index map are included in each before/after check.

For each parameter, `read64(parameter + 0x98)` yields the accessor. Its pointers
at `+8` and `+0x10` address the 28-byte calculated data and 232-byte core.
The byte at `+0x18` determines whether XOR decryption is needed. The core
remains block shuffled in either state; calculated data is never shuffled.
The core checksum, species, level, nature, HP and stat bounds must all pass.

Every sample uses two equal copies of both records, checks the encryption
flag and accessor pointers again, and rechecks the complete party header.
Failed validation clears the current party rather than retaining old values.
The module requests only read-only RomFS access, never memory writes or guest
function calls. Names are decoded from the user's English message tables.

## Verification boundary

`decoder_test.cpp` covers PB7 decoding and invalid records. `module_test.cpp`
uses an isolated synthetic guest memory fixture to check the exact build gate,
pointer chain, changing HP, empty/invalid/torn records, and absence of writes.
Both passed with C++17, warnings treated as errors. The Android module linked
with NDK 28, API 33 and 16 KiB-compatible load segments.

Live party contents, party reordering and game transitions still require a
saved-game device run. No synthetic values are included in the production
module.

## Bag and loaded-world readers

Bag starts at `read64(read64(context + 0x58) + 0x68)`. Seven pouch pointers
begin at bag offset `0xDE8`, with capacities 60/108/200/150/50/150/150.
Each four-byte entry holds a 15-bit item ID and a 15-bit quantity shifted by 15.
The reader double-snapshots all pouches, rechecks all pointers, rejects duplicate
IDs and quantities above 999, and bounds IDs against the owner's item names.
The current `bag_<pouch>_<page>` UI page selects ten published rows. Local page
navigation changes no guest state. Pocket label indices are `(pouch + 1) % 7`.
The packed layout is independently corroborated by
[PKHeX InventoryItem7b](https://github.com/kwsch/PKHeX/blob/master/PKHeX.Core/Saves/Substructures/Inventory/Item/InventoryItem7b.cs).

For world detection, field is `read64(context + 0x90)`, its vector is at `+0x130`
and its 64-bit count at `+0x138`. A player candidate has selector zero at
`actor + 0xB20`. It must use the exact readiness predicate `main + 0x212380`
through vtable slot `0x78`, which reads a nonzero word at `actor + 0x1D4`.
Unknown predicates are not called or assumed equivalent. World is
`read64(context + 0xB8)`; its first entry pointer is at `+0x328` and count at
`+0x378`. The entry's area ID is at `+0x3C0`. Two complete equal observations
are required. A session-local context latch preserves the companion through
battles and menus once an active overworld has been seen; context destruction
or replacement clears it.

The owner's 43-record town-map table supplies area ranges and place-name enums.
Place-name text row is enum plus two. The external materializer uses the owner's
map image and pane coordinates to render an area marker, rather than exact
player coordinates. The map image shows all routes; it does not claim live
visited-state filtering.

The game's area ID is a 64-bit hash, rather than the table's ordinal. Hash
the owner's ASCII identifiers with Game Freak's FNV-1a variant: seed
`0xCBF29CE484222645`, prime `0x100000001B3`, XOR each byte then multiply
modulo 64 bits. For two-part entries, replace the trailing `_1` with `_2`,
or append `_2` when no suffix exists. All 48 candidates match the owner's
zone-name table. Live Route 1 and Viridian City IDs also match exactly.
Only after matching the full 64-bit value does the module publish the
small normalized region ordinal for UI marker gates.
