#include "mzm_haze_resolver.h"

#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr std::uint32_t kRomStart = 0x08000000u;
constexpr std::size_t kRomBytes = 0x00800000u;

void check(bool result) {
    if (!result) {
        throw std::runtime_error("haze resolver assertion failed");
    }
}

void check_images(const std::vector<std::uint8_t>& rom) {
    std::array<std::uint8_t, mzm_haze::kCopySize> ram{};
    auto read = [&](std::uint32_t addr) -> std::uint8_t {
        if (addr >= mzm_haze::kRuntimeStart &&
            addr < mzm_haze::kRuntimeStart + mzm_haze::kCopySize) {
            return ram[addr - mzm_haze::kRuntimeStart];
        }
        check(addr >= kRomStart && addr - kRomStart < rom.size());
        return rom[addr - kRomStart];
    };

    const std::uint32_t thumb_entry = mzm_haze::kRuntimeStart + 1u;
    check((thumb_entry & ~1u) == mzm_haze::kRuntimeStart);
    for (std::size_t n = 0; n < mzm_haze::kTemplates.size(); ++n) {
        const auto& candidate = mzm_haze::kTemplates[n];
        check(candidate.function_size <= mzm_haze::kCopySize);
        for (std::uint32_t i = 0; i < mzm_haze::kCopySize; ++i) {
            ram[i] = read(candidate.source_start + i);
        }
        check(mzm_haze::identify(thumb_entry & ~1u, true, read) ==
              static_cast<int>(n));
        check(mzm_haze::identify(mzm_haze::kRuntimeStart, false, read) == -1);
        check(mzm_haze::identify(thumb_entry, true, read) == -1);
        check(mzm_haze::identify(mzm_haze::kRuntimeStart + 2u, true, read) == -1);
        check(mzm_haze::identify(0x03007000u, true, read) == -1);
        ram[mzm_haze::kCopySize - 1] ^= 1u;
        check(mzm_haze::identify(mzm_haze::kRuntimeStart, true, read) == -1);
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::uint8_t> rom(kRomBytes);
    if (argc == 1) {
        // Redistributable structural test: deterministic synthetic bytes,
        // including overlapping 0x200 source windows.
        for (std::size_t i = 0; i < rom.size(); ++i) {
            rom[i] = static_cast<std::uint8_t>((i * 131u ^ i / 257u) & 0xffu);
        }
    } else if (argc == 3 && std::string(argv[1]) == "--rom") {
        std::ifstream file(argv[2], std::ios::binary);
        if (!file || !file.read(reinterpret_cast<char*>(rom.data()), rom.size())) {
            std::cerr << "Cannot read the local 8 MiB ROM\n";
            return 2;
        }
    } else {
        std::cerr << "Usage: haze_resolver_test [--rom local-USA.gba]\n";
        return 2;
    }
    check_images(rom);
    std::cout << "7/7 haze images identified; negative cases rejected\n";
}
