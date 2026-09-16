# Naming migration

The project was renamed before 1.0 to avoid using the already-crowded `GVID` name.

- GVID / `.gvid` -> **GHV / `.ghv`** — Goou_Zi High-efficiency Video
- GAUD / `.gaud` -> **GHA / `.gha`** — Goou_Zi High-efficiency Audio
- GVC2 -> **GHVC3** for the new 0.4 video bitstream
- GAC1 is renamed **GHAC1** in the new GHA ecosystem

The 0.4 tools intentionally write new magic values and do not pretend that renaming an old `.gvid` file makes it a `.ghv` file. Re-encode source media to create a valid GHV file.
