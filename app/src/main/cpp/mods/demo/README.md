# NyxCore Demo Mod

This directory is the public developer sample for a minimal native mod.

- `mod.properties` declares the mod id, output library, feature switches, and release allowlist.
- `mod.cmake` contributes only this mod's sources to the root native target.
- `src/main.cpp` registers one `sdk::IMod` implementation and uses only `sdk/include` headers.

Private project mods can stay under `app/src/main/cpp/mods`, but release checks only publish mods with `publish=true`.
