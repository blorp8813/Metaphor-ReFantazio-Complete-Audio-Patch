# Third-party inventory

| Component | Purpose | Source location | License |
|---|---|---|---|
| MinHook | Runtime API hooking | `external-minhook/` | BSD-style; `external-minhook/LICENSE.txt` |
| Hacker Disassembler Engine 32/64 | Instruction decoding vendored with MinHook | `external-minhook/src/hde/` | BSD-style notices in `external-minhook/LICENSE.txt` |
| Ultimate ASI Loader | Loads `MetaphorAudioFix.asi` through packaged `winmm.dll` | `third_party/Ultimate-ASI-Loader/` | MIT; `third_party/Ultimate-ASI-Loader/LICENSE.txt` |
| MinGW-w64 runtime | `libwinpthread-1.dll` supplied by the selected cross-toolchain | build-toolchain output only | Runtime license terms supplied by the toolchain distributor |

The repository does not vendor the MinGW-w64 toolchain. Release builders are responsible for confirming that their chosen `libwinpthread-1.dll` is redistributable and for retaining its applicable notices. The release manifest records the exact file checksum.
