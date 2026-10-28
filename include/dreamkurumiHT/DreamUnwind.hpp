#pragma once
#include <windows.h>
#include <cstdint>

namespace dreamkurumi {

#pragma pack(push, 1)
union UnwindCode {
    struct {
        uint8_t CodeOffset;
        uint8_t UnwindOp : 4;
        uint8_t OpInfo   : 4;
    };
    uint16_t FrameOffset;
};

// Lightweight 8-byte UNWIND_INFO for Frame-Pointer based MidHook/Trampoline stubs
struct MinimalUnwindInfo {
    uint8_t Version       : 3; // 1
    uint8_t Flags         : 5; // 0 (or UNW_FLAG_EHANDLER)
    uint8_t SizeOfProlog;      // Size of prologue in bytes
    uint8_t CountOfCodes;      // 2 codes (SET_FPREG + PUSH_NONVOL)
    uint8_t FrameRegister : 4; // 5 (RBP)
    uint8_t FrameOffset   : 4; // 0
    UnwindCode Codes[2];       // Keeps struct 4-byte aligned (total 8 bytes)
};

// Ultra-lightweight 4-byte UNWIND_INFO for pure leaf / branch stubs
struct LeafUnwindInfo {
    uint8_t Version       : 3; // 1
    uint8_t Flags         : 5; // 0
    uint8_t SizeOfProlog;      // 0
    uint8_t CountOfCodes;      // 0
    uint8_t FrameRegister : 4; // 0
    uint8_t FrameOffset   : 4; // 0
};
#pragma pack(pop)

enum UnwindOpCodes : uint8_t {
    UWOP_PUSH_NONVOL    = 0,
    UWOP_ALLOC_LARGE    = 1,
    UWOP_ALLOC_SMALL    = 2,
    UWOP_SET_FPREG      = 3,
    UWOP_SAVE_NONVOL    = 4,
    UWOP_PUSH_MACHFRAME = 10
};

constexpr uint8_t REG_RBP = 5;

// Initialize once at the start of each 64KB chunk
inline void InitChunkUnwindHeader(MinimalUnwindInfo* outInfo) {
    if (!outInfo) return;
    // Assumes MidHook prologue:
    // +0x00: 68 xx xx xx xx          push low32(ContinuationRIP)
    // +0x05: C7 44 24 04 yy yy yy yy mov dword ptr [rsp+4], high32(ContinuationRIP)
    // +0x0D: 55                      push rbp
    // +0x0E: 48 89 E5                mov rbp, rsp
    // Total prologue size = 0x11 (17 bytes)
    outInfo->Version       = 1;
    outInfo->Flags         = 0;
    outInfo->SizeOfProlog  = 0x11;
    outInfo->CountOfCodes  = 2;
    outInfo->FrameRegister = REG_RBP;
    outInfo->FrameOffset   = 0;

    // Unwind codes are ordered in reverse execution order (from end of prologue to start)
    outInfo->Codes[0].CodeOffset = 0x11; // Offset after 'mov rbp, rsp'
    outInfo->Codes[0].UnwindOp   = UWOP_SET_FPREG;
    outInfo->Codes[0].OpInfo     = 0;

    outInfo->Codes[1].CodeOffset = 0x0E; // Offset after 'push rbp'
    outInfo->Codes[1].UnwindOp   = UWOP_PUSH_NONVOL;
    outInfo->Codes[1].OpInfo     = REG_RBP;
}

inline void InitLeafUnwindHeader(LeafUnwindInfo* outInfo) {
    if (!outInfo) return;
    outInfo->Version       = 1;
    outInfo->Flags         = 0;
    outInfo->SizeOfProlog  = 0;
    outInfo->CountOfCodes  = 0;
    outInfo->FrameRegister = 0;
    outInfo->FrameOffset   = 0;
}

} // namespace dreamkurumi

namespace dreamkurumiHT {
    using namespace dreamkurumi;
}
