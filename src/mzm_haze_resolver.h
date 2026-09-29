#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// MZM USA (BMXE rev 0). The decomp copies sizeof(hazeCode), not the function
// size. Source addresses and function sizes come from its ELF/map; no ROM
// bytes are embedded in this table.
namespace mzm_haze {

constexpr std::uint32_t kRuntimeStart = 0x03001944u;
constexpr std::uint32_t kCopySize = 0x200u;

struct Template {
    const char* name;
    std::uint32_t source_start;
    std::uint32_t function_size;
    bool called_from_ram;
};

constexpr std::array<Template, 7> kTemplates{{
    {"Haze_Bg3", 0x0805D768u, 0xC0u, true},
    {"Haze_Bg3StrongWeak", 0x0805D828u, 0x118u, true},
    {"Haze_Bg3NoneWeak", 0x0805D940u, 0x70u, true},
    {"Haze_Bg3Bg2StrongWeakMedium", 0x0805D9B0u, 0x90u, true},
    {"Haze_Bg3Bg2Bg1", 0x0805DA40u, 0xECu, false},
    {"Haze_PowerBombExpanding", 0x0805DB2Cu, 0x118u, true},
    {"Haze_PowerBombRetracting", 0x0805DC44u, 0x118u, true},
}};

// `pc` is the aligned dispatch PC. The Thumb function pointer is start+1,
// but runtime_dispatch strips that bit before invoking its RAM hook.
// ReadByte reads current guest memory, including the immutable ROM source.
template <typename ReadByte>
int identify(std::uint32_t pc, bool thumb, ReadByte read_byte) {
    if (!thumb || pc != kRuntimeStart) {
        return -1;
    }
    for (std::size_t candidate = 0; candidate < kTemplates.size(); ++candidate) {
        bool equal = true;
        for (std::uint32_t i = 0; i < kCopySize; ++i) {
            if (read_byte(kRuntimeStart + i) !=
                read_byte(kTemplates[candidate].source_start + i)) {
                equal = false;
                break;
            }
        }
        if (equal) {
            return static_cast<int>(candidate);
        }
    }
    return -1;
}

}  // namespace mzm_haze
