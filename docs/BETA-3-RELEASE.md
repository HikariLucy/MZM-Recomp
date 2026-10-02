# MZM Recompiled Beta 3 — build and packaging (maintainers)

User-facing documents live in `docs/beta3/` and are copied into both packages.
This page is for whoever builds the archives. Beta 3 is a **Public Runtime Test**.

## Exact dependencies

| Component | Revision | Availability |
|---|---|---|
| MZM-Recomp | the commit printed in the package `BUILD-INFO.txt` | this repository |
| GBARecomp | `266f82f556fe995ece1bfc558cfe59496b0ef31c` (branch `mzm/enhancements-2`) | **local only**: no public remote contains it. It is 31 commits on top of upstream `mstan/gbarecomp` `e772814`. |
| recomp-ui | `2b7e9c6140be55da99941747071e540c026a43cf` | submodule `recomp-ui` (public `mstan/recomp-ui`) |

`CMakeLists.txt` refuses to configure unless `GBARECOMP_ROOT` is exactly the GBARecomp
revision above. Remote reproducibility of that revision is **not** claimed: until it is
published, the dependency is available as a git bundle (`git bundle create
gbarecomp-266f82f.bundle e772814..266f82f`, kept in `dist/public-beta-3/metadata/`, not a
release asset). The generated ROM-derived corpus and generated BIOS corpus are produced locally
(`scripts/generate-m1.sh`, `scripts/generate-bios-m2.sh`) and never distributed.

## Build

Release builds only (`CMAKE_BUILD_TYPE=Release`). The launcher is mandatory: configuration
fails if recomp-ui is missing (pass `-DMZM_RECOMP_UI=OFF` for a CLI-only developer host).
Profiling (`MZM_PERF_PROFILE`) is OFF and must stay OFF.

```bash
# Linux
cmake -S . -B build-b3-linux -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DGBARECOMP_ROOT=<gbarecomp @266f82f> -DGBARECOMP_GENERATED_BIOS_DIR=<generated-bios>
cmake --build build-b3-linux --parallel 4
bash scripts/package-beta-linux.sh          # -> dist/public-beta-3/MZMRecompiled-Beta-3-Linux-x86_64.tar.gz

# Windows x64 (MinGW-w64 cross build, see docs/BUILD-WINDOWS.md for the SDL2 prefix)
cmake -S . -B build-b3-windows -G Ninja -DCMAKE_TOOLCHAIN_FILE=$PWD/cmake/toolchains/mingw-w64-x86_64.cmake \
  -DCMAKE_BUILD_TYPE=Release ... -DMZM_RECOMP_UI=ON
cmake --build build-b3-windows --parallel 4
MZM_GBARECOMP_ROOT=... MZM_WINDOWS_BUILD_DIR=build-b3-windows MZM_WINDOWS_DLL_DIR=... \
  MZM_WINDOWS_LICENSE_DIR=... bash scripts/package-beta-windows.sh
```

The commit SHA embedded in the executable is read at CMake configure time (`-dirty` is
appended when the tree has uncommitted changes), so reconfigure after the last commit.

## Verify

```bash
bash scripts/audit-beta-package.sh <archive> <MZM sha> <GBARecomp sha>   # static hygiene, PE/ELF inspection
python3 scripts/smoke-beta-package.py <linux archive> <rom> <bios>      # Linux: real launcher -> PLAY, clean temp dir
```

The Windows audit never runs the program; Windows runtime status is **UNVERIFIED** until
community reports arrive.
