# Helper scripts

Platform convenience scripts live here instead of the repository root.

- `windows/setup.bat` installs Python dependencies and optionally builds the native core.
- `windows/test_all.bat` runs version synchronization and GHVC4–GHVC9 compatibility tests.
- `windows/studio_ghv.bat` and `windows/studio_gha.bat` launch the beta desktop tools.

The native command-line core retains its platform build entry points in
`native/build_windows.bat`, `native/build_linux.sh`, and `native/build_macos.sh`.
