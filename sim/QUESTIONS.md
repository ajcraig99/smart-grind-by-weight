# Questions logged during the unattended build

Each question the build would have asked, with the conservative choice made so work could continue.

| # | Question | Choice made |
|---|----------|-------------|
| 1 | The repo already has a Windows-only `sim/` desktop simulator. Replace or keep it? | Kept untouched; the twin lives in new subdirectories of `sim/`. |
| 2 | Which hardware variant should the twin model, V1 or V2 panel? | V1 (the default PlatformIO env). V2 differs only in the display driver path. |
| 3 | The brief asks for `sim/README.md`, which was the desktop simulator's README. Replace it? | Rewrote `sim/README.md` for the twin and kept the old text verbatim in `sim/DESKTOP_SIMULATOR.md`, linked from the new README. |
