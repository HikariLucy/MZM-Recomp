#pragma once

#include <cstdint>

// BMXE rev 0. The linked symbol is Thumb 0x08087939; DMA3_COPY_16 copies
// 0x20 halfwords, including four bytes beyond the 0x3c-byte function.
namespace mzm_chozodia {
constexpr std::uint32_t kRuntimeStart = 0x03001730u;
constexpr std::uint32_t kThumbPointer = kRuntimeStart + 1u;
constexpr std::uint32_t kSourceStart = 0x08087938u;
constexpr std::uint32_t kFunctionSize = 0x3cu;
constexpr std::uint32_t kCopySize = 0x40u;
constexpr std::uint32_t kBufferSize = 0x80u;

// Image identification only. Native dispatch requires RAM-PC semantics for
// IRQ preemption, resumption, and cycle timing in the generated function.
template <typename ReadByte>
bool identify(std::uint32_t pc, bool thumb, ReadByte read_byte) {
    if (!thumb || pc != kRuntimeStart) return false;
    for (std::uint32_t i = 0; i < kCopySize; ++i) {
        if (read_byte(kRuntimeStart + i) != read_byte(kSourceStart + i))
            return false;
    }
    return true;
}
}  // namespace mzm_chozodia
