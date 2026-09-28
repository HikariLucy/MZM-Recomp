# Windows x86_64 beta build

The Windows target builds the existing `MZMRecomp` host, MZM launcher,
`recomp-ui`, generated strict-static guest corpus, and GBARecomp runtime.
The repository never distributes the ROM, BIOS, saves, or generated guest code.

## Toolchain decision

GBARecomp has MinGW and MSVC support, including Windows stack sizing, SDL2,
Win32 libraries, and Windows input paths. The preferred Linux-host path is
MinGW-w64 with a Windows SDL2 development package for the same target. On this
development host, `clang`, CMake, and Ninja are present, but the MinGW-w64
compiler, resource compiler, Windows headers/libs, target SDL2, and Wine are
absent. A Windows `.exe` has therefore not been built or run here. Installing
host packages requires a separate authorized environment setup. The native
fallback is Windows x86_64 with MSYS2 MinGW-w64; MSVC/clang-cl is a possible
GBARecomp path but has not been qualified for this product.

## Cross-build from Linux when MinGW-w64 and target SDL2 are available

Keep the generated game sources and generated BIOS sources private and local.
Use the exact GBARecomp checkout already used by the Linux build. Set the
following variables to your own paths:

```bash
GBARECOMP_ROOT=/path/to/pinned/gbarecomp
BIOS_GENERATED=/path/to/private/generated-bios
SDL2_DIR=/path/to/mingw-sdl2/cmake/SDL2

cmake -S . -B build-windows -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64-x86_64.cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DGBARECOMP_ROOT="$GBARECOMP_ROOT" \
  -DGBARECOMP_GENERATED_BIOS_DIR="$BIOS_GENERATED" \
  -DSDL2_DIR="$SDL2_DIR" -DMZM_RECOMP_UI=ON
cmake --build build-windows --parallel 4
```

The target SDL2 CMake package must resolve as `SDL2::SDL2`; a Linux SDL2
installation is insufficient. GBARecomp must also detect the target SDL2
headers and import library. Confirm CMake prints `gbarecomp: SDL2 found` and
`MZM-Recomp: recomp-ui launcher enabled` before packaging. The executable is
`build-windows/MZMRecomp.exe`.

On native Windows x86_64, use the MSYS2 MinGW64 toolchain with Ninja and its
SDL2 development package, omitting `CMAKE_TOOLCHAIN_FILE` and setting the
same GBARecomp and generated BIOS variables. This route also needs a real
build and test before distribution.

## Package

`scripts/package-beta-windows.sh` inspects PE imports with `objdump` and
recursively copies imported non-system DLLs from the build directory or
`MZM_WINDOWS_DLL_DIR`. It refuses to package a DLL without its corresponding
redistribution notice at
`MZM_WINDOWS_LICENSE_DIR/<exact DLL name>.LICENSE.txt`. Source notices for
GBARecomp, recomp-ui, their compiled vendored libraries, and the native file
picker are copied from the linked checkouts. GBARecomp's pinned checkout uses
PolyForm Noncommercial 1.0.0, so this private beta is for noncommercial use.
Supply notices from the actual SDL2/MinGW toolchain distribution; do not substitute
a license from a different binary build. The exact DLL list is determined by
the built executable, not assumed in advance.
The license directory must also contain `SIL-OFL-1.1.txt` for Lato/Noto fonts
and `CC-BY-SA-4.0.txt` for OpenMoji; the packager copies both into the ZIP.

```bash
MZM_GBARECOMP_ROOT="$GBARECOMP_ROOT" \
MZM_WINDOWS_DLL_DIR=/path/to/target/runtime-dlls \
MZM_WINDOWS_LICENSE_DIR=/path/to/verified/license-notices \
bash scripts/package-beta-windows.sh
```

The output is `dist/MZMRecompiled-Beta-Windows/` and
`dist/MZMRecompiled-Beta-Windows.zip`. The script checks PE x86_64, required
launcher assets, imports, licenses, ZIP integrity, and the absence of game,
BIOS, save, generated source, and local path configuration. It does not build
the executable or silently produce a CLI-only package.

## Runtime data and validation

The Windows launcher stores ROM and BIOS path references in
`%APPDATA%\MZMRecompiled\launcher.ini`; logs go to
`%LOCALAPPDATA%\MZMRecompiled\logs\mzm-recompiled.log`. GBARecomp's
keybinds/configuration and sidecar path caches are resolved under
`%APPDATA%\MZMRecompiled\`. SRAM and save-state slots remain beside the
user-selected ROM, following the existing GBARecomp convention.

Assets and `configs/mzm-us.toml` are loaded from the actual executable
directory. The native Windows file dialog is supplied by recomp-ui's vendored
tinyfiledialogs (`GetOpenFileNameW`). The launcher sets
`GBARECOMP_STRICT_STATIC=1` on PLAY. A PE build and import inspection do not
prove runtime strict-static behavior. Before release, a Windows x86_64 tester
must launch, select their files, PLAY, and capture output showing
`cpu_backend=static-recompiled`, `strict_static=ENABLED`,
`dispatch_misses=0`, `interpreted_insns=0`, `unmapped=0`, and
`io_unhandled=0`. Wine can provide an additional smoke check; it does not
replace native Windows testing.
