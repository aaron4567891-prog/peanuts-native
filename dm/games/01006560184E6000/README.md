# Mortal Kombat 1 (`01006560184E6000`)

Switch update 1.29.0 (`v1900544`), SDK 15.3.2. The game uses Unreal Engine 4, NVN and Wwise. The registered modules are `main`, `subsdk0` (NintendoSDK_movie) and `sdk`. The base build is unregistered.

## Status

Provisional profile for Android bring-up. The update has two Oodle-compressed Switch shader archives, one global and one game-specific, with 131,586 records in total. Both archives were decoded offline. The split produced 123,377 shader keys and 73,665 variation pairs. The raw scan skipped the 24 GiB IoStore data container after its TOC showed no shader library entries.

The memory layout comes from the survey's Unreal preset. The Android rewriter did not recognize the allocator vtable layout, so the game retains its own Unreal allocator. Android boot, rendering and memory use still need validation on a device.
