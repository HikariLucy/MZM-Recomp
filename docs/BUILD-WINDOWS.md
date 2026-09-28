# Windows x86_64 beta build

The Windows target builds the existing `MZMRecomp` host, MZM launcher,
`recomp-ui`, generated strict-static guest corpus, and GBARecomp runtime.
The repository never distributes the ROM, BIOS, saves, or generated guest code.

## Toolchain decision

GBARecomp has MinGW and MSVC support, including Windows stack sizing, SDL2,
Win32 libraries, and Windows input paths. The qualified Linux-host build uses
MinGW-w64 GCC 13-win32, GNU windres 2.41, Ninja, and SDL2 2.30.0 cross-built
from the official `libsdl-org/SDL` tag `release-2.30.0` at commit
`859844eae358447be8d66e6da59b6fb3df0ed778`. Wine is absent, so this is
a build and package verification, not Windows execution evidence.

The native fallback is Windows x86_64 with MSYS2 MinGW64; MSVC/clang-cl is a
possible GBARecomp path but has not been qualified for this product.

## Build SDL2 for Windows from Linux

The Ubuntu SDL2 installation is for Linux and must not enter this build.
Keep the official SDL source, build tree, and install prefix under ignored
`.local/` paths. From the repository root:

```bash
git clone --depth 1 --branch release-2.30.0 \
  https://github.com/libsdl-org/SDL.git .local/deps/SDL-2.30.0
cmake -S .local/deps/SDL-2.30.0 -B .local/build/SDL-2.30.0-windows -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/toolchains/mingw-w64-x86_64.cmake" \
  -DCMAKE_INSTALL_PREFIX="$PWD/.local/windows/x86_64-w64-mingw32" \
  -DCMAKE_BUILD_TYPE=Release -DSDL_SHARED=ON -DSDL_STATIC=OFF \
  -DSDL_TEST=OFF -DSDL2_DISABLE_SDL2MAIN=OFF
cmake --build .local/build/SDL-2.30.0-windows --parallel 4
cmake --install .local/build/SDL-2.30.0-windows
```

The prefix must contain `bin/SDL2.dll`, `lib/libSDL2.dll.a`,
`lib/libSDL2main.a`, `include/SDL2/`, and
`lib/cmake/SDL2/SDL2Config.cmake`. The SDL2 DLL must report PE32+ x86-64.

## Cross-build MZM-Recomp

Keep the generated game sources and generated BIOS sources private and local.
Use the exact GBARecomp checkout already used by the Linux build. Set the
following variables to your own paths:

```bash
GBARECOMP_ROOT=/path/to/pinned/gbarecomp
BIOS_GENERATED=/path/to/private/generated-bios
SDL_PREFIX="$PWD/.local/windows/x86_64-w64-mingw32"
TOMLPP="$PWD/build-m1/_deps/tomlplusplus-src"

cmake -S . -B build-windows -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/toolchains/mingw-w64-x86_64.cmake" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$SDL_PREFIX" \
  -DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF \
  -DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF \
  -DGBARECOMP_ROOT="$GBARECOMP_ROOT" \
  -DGBARECOMP_GENERATED_BIOS_DIR="$BIOS_GENERATED" \
  -DGBARECOMP_TOMLPP_INCLUDE_DIR="$TOMLPP" \
  -DGBARECOMP_MINGW_PREFIX_UNIX="$SDL_PREFIX" \
  -DSDL2_DIR="$SDL_PREFIX/lib/cmake/SDL2" \
  -DSDL2_INCLUDE_DIR="$SDL_PREFIX/include/SDL2" \
  -DSDL2_LIBRARY="$SDL_PREFIX/lib/libSDL2.dll.a" \
  -DMZM_RECOMP_UI=ON
cmake --build build-windows --parallel 4
```

The toml++ path above reuses the local checkout at GBARecomp's pinned commit
`30172438cee64926dc41fdd9c11fb3ba5b2ba9de`; verify the commit if using
another source. Without this override, GBARecomp's CMake may fetch that pin.

The target SDL2 CMake package must resolve as `SDL2::SDL2`; a Linux SDL2
installation is insufficient. GBARecomp must also detect the target SDL2
headers and import library. Confirm CMake prints `gbarecomp: SDL2 found` and
`MZM-Recomp: recomp-ui launcher enabled` before packaging. The executable is
`build-windows/MZMRecomp.exe`. Check `build-windows/CMakeCache.txt` and
`build-windows/build.ninja` for SDL paths. Neither may contain
`/usr/include/SDL2` or `/usr/lib/x86_64-linux-gnu/libSDL2`.

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
For this build, the PE imports SDL2.dll, libgcc_s_seh-1.dll, and
libstdc++-6.dll. Their source files came respectively from the local SDL2
prefix and Ubuntu's `gcc-mingw-w64-x86-64-win32-runtime` package. The
redistribution notices came from SDL2's official `LICENSE.txt` and that
package's installed Debian copyright file. Re-audit imports for any rebuild.

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
