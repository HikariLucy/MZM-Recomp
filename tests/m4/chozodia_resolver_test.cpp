#include "mzm_chozodia_resolver.h"

#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
constexpr std::uint32_t kRomBase = 0x08000000u;
constexpr std::size_t kRomSize = 0x00800000u;

void check(bool ok) {
    if (!ok) throw std::runtime_error("Chozodia image assertion failed");
}

void exercise(const std::array<std::uint8_t, mzm_chozodia::kCopySize>& source) {
    using namespace mzm_chozodia;
    static_assert(kRuntimeStart == 0x030016c4u + 0x6cu);
    static_assert(kFunctionSize == 0x3cu && kCopySize == 0x20u * 2u);
    static_assert(kBufferSize == 128u && kCopySize <= kBufferSize);
    static_assert((kRuntimeStart & 3u) == 0u);
    static_assert(kThumbPointer == (kRuntimeStart | 1u));

    std::array<std::uint8_t, kBufferSize> ram{};
    auto read = [&](std::uint32_t addr) -> std::uint8_t {
        if (addr >= kRuntimeStart && addr < kRuntimeStart + kBufferSize)
            return ram[addr - kRuntimeStart];
        check(addr >= kSourceStart && addr < kSourceStart + kCopySize);
        return source[addr - kSourceStart];
    };
    for (std::uint32_t i = 0; i < kCopySize; ++i) ram[i] = source[i];
    check(identify(kThumbPointer & ~1u, true, read));
    check(!identify(kRuntimeStart, false, read));
    check(!identify(kThumbPointer, true, read));
    check(!identify(kRuntimeStart + 2u, true, read));
    check(!identify(0x03001944u, true, read));
    ram[kCopySize - 1u] ^= 1u;
    check(!identify(kRuntimeStart, true, read));
    ram[kCopySize - 1u] ^= 1u;
    ram.fill(0u);
    for (std::uint32_t i = 0; i < kFunctionSize; ++i) ram[i] = source[i];
    // A function-length-only copy omits the four bytes that DMA really copies.
    if (source[kFunctionSize] == 0u && source[kFunctionSize + 1u] == 0u &&
        source[kFunctionSize + 2u] == 0u && source[kFunctionSize + 3u] == 0u)
        ram[kFunctionSize] = 1u;
    check(!identify(kRuntimeStart, true, read));
    for (std::uint32_t i = 0; i < kCopySize; ++i) ram[i] = source[i];
    ram[kCopySize - 2u] = 0u;
    if (source[kCopySize - 2u] == 0u) ram[kCopySize - 2u] = 1u;
    check(!identify(kRuntimeStart, true, read));
    ram.fill(0x55u);  // unrelated union contents
    if (source[0] == 0x55u) ram[0] ^= 1u;
    check(!identify(kRuntimeStart, true, read));
}
}  // namespace

int main(int argc, char** argv) {
    std::array<std::uint8_t, mzm_chozodia::kCopySize> source{};
    if (argc == 1) {
        for (std::size_t i = 0; i < source.size(); ++i)
            source[i] = static_cast<std::uint8_t>((i * 37u + 11u) & 0xffu);
    } else if (argc == 3 && std::string(argv[1]) == "--rom") {
        std::ifstream rom(argv[2], std::ios::binary | std::ios::ate);
        if (!rom || rom.tellg() != static_cast<std::streamoff>(kRomSize)) {
            std::cerr << "Expected local 8 MiB USA ROM\n";
            return 2;
        }
        rom.seekg(mzm_chozodia::kSourceStart - kRomBase);
        if (!rom.read(reinterpret_cast<char*>(source.data()), source.size())) {
            std::cerr << "Cannot read Chozodia source window\n";
            return 2;
        }
    } else {
        std::cerr << "Usage: chozodia_resolver_test [--rom local-USA.gba]\n";
        return 2;
    }
    exercise(source);
    std::cout << "Chozodia 64-byte image identified; negative cases rejected\n";
}
