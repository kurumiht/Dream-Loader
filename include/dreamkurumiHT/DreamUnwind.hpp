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

// Fully-specified 44-byte UNWIND_INFO for Frame-Pointer based MidHook/Trampoline stubs
// Perfectly describes MidHook prologue:
// +0x00: pushfq (1)                -> ALLOC_SMALL 8
// +0x01: push rax..r15 (15 pushes) -> PUSH_NONVOL x 15
// +0x18: sub rsp, 0x200 (7)        -> ALLOC_LARGE 0x200
// +0x1F: mov rbp, rsp (3)          -> SET_FPREG RBP
// Total prologue size = 0x22 (34 bytes). 19 unwind code slots (padded to 20 for alignment).
struct MinimalUnwindInfo {
    uint8_t Version       : 3; // 1
    uint8_t Flags         : 5; // 0
    uint8_t SizeOfProlog;      // 0x22 (34 bytes)
    uint8_t CountOfCodes;      // 19 codes
    uint8_t FrameRegister : 4; // 5 (RBP)
    uint8_t FrameOffset   : 4; // 0
    UnwindCode Codes[20];      // 20 slots (40 bytes), keeps struct 4-byte aligned (total 44 bytes)
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

constexpr uint8_t REG_RAX = 0;
constexpr uint8_t REG_RCX = 1;
constexpr uint8_t REG_RDX = 2;
constexpr uint8_t REG_RBX = 3;
constexpr uint8_t REG_RSP = 4;
constexpr uint8_t REG_RBP = 5;
constexpr uint8_t REG_RSI = 6;
constexpr uint8_t REG_RDI = 7;
constexpr uint8_t REG_R8  = 8;
constexpr uint8_t REG_R9  = 9;
constexpr uint8_t REG_R10 = 10;
constexpr uint8_t REG_R11 = 11;
constexpr uint8_t REG_R12 = 12;
constexpr uint8_t REG_R13 = 13;
constexpr uint8_t REG_R14 = 14;
constexpr uint8_t REG_R15 = 15;

// Initialize once at the start of each 64KB chunk
inline void InitChunkUnwindHeader(MinimalUnwindInfo* outInfo) {
    if (!outInfo) return;
    std::memset(outInfo, 0, sizeof(MinimalUnwindInfo));

    outInfo->Version       = 1;
    outInfo->Flags         = 0;
    outInfo->SizeOfProlog  = 0x22;
    outInfo->CountOfCodes  = 19;
    outInfo->FrameRegister = REG_RBP;
    outInfo->FrameOffset   = 0;

    // Codes are in reverse execution order (from end of prologue to start)
    // 0: SET_FPREG at offset 0x22
    outInfo->Codes[0].CodeOffset = 0x22;
    outInfo->Codes[0].UnwindOp   = UWOP_SET_FPREG;
    outInfo->Codes[0].OpInfo     = 0;

    // 1-2: ALLOC_LARGE 0x200 (takes 2 slots) at offset 0x1F
    outInfo->Codes[1].CodeOffset = 0x1F;
    outInfo->Codes[1].UnwindOp   = UWOP_ALLOC_LARGE;
    outInfo->Codes[1].OpInfo     = 0; // Size <= 512KB (size / 8)
    outInfo->Codes[2].FrameOffset = static_cast<uint16_t>(0x200 / 8); // 0x40

    // 3..17: PUSH_NONVOL for 15 GPRs (R15 down to RAX)
    outInfo->Codes[3].CodeOffset = 0x18; outInfo->Codes[3].UnwindOp = UWOP_PUSH_NONVOL; outInfo->Codes[3].OpInfo = REG_R15;
    outInfo->Codes[4].CodeOffset = 0x16; outInfo->Codes[4].UnwindOp = UWOP_PUSH_NONVOL; outInfo->Codes[4].OpInfo = REG_R14;
    outInfo->Codes[5].CodeOffset = 0x14; outInfo->Codes[5].UnwindOp = UWOP_PUSH_NONVOL; outInfo->Codes[5].OpInfo = REG_R13;
    outInfo->Codes[6].CodeOffset = 0x12; outInfo->Codes[6].UnwindOp = UWOP_PUSH_NONVOL; outInfo->Codes[6].OpInfo = REG_R12;
    outInfo->Codes[7].CodeOffset = 0x10; outInfo->Codes[7].UnwindOp = UWOP_PUSH_NONVOL; outInfo->Codes[7].OpInfo = REG_R11;
    outInfo->Codes[8].CodeOffset = 0x0E; outInfo->Codes[8].UnwindOp = UWOP_PUSH_NONVOL; outInfo->Codes[8].OpInfo = REG_R10;
    outInfo->Codes[9].CodeOffset = 0x0C; outInfo->Codes[9].UnwindOp = UWOP_PUSH_NONVOL; outInfo->Codes[9].OpInfo = REG_R9;
    outInfo->Codes[10].CodeOffset = 0x0A; outInfo->Codes[10].UnwindOp = UWOP_PUSH_NONVOL; outInfo->Codes[10].OpInfo = REG_R8;
    outInfo->Codes[11].CodeOffset = 0x08; outInfo->Codes[11].UnwindOp = UWOP_PUSH_NONVOL; outInfo->Codes[11].OpInfo = REG_RBP;
    outInfo->Codes[12].CodeOffset = 0x07; outInfo->Codes[12].UnwindOp = UWOP_PUSH_NONVOL; outInfo->Codes[12].OpInfo = REG_RDI;
    outInfo->Codes[13].CodeOffset = 0x06; outInfo->Codes[13].UnwindOp = UWOP_PUSH_NONVOL; outInfo->Codes[13].OpInfo = REG_RSI;
    outInfo->Codes[14].CodeOffset = 0x05; outInfo->Codes[14].UnwindOp = UWOP_PUSH_NONVOL; outInfo->Codes[14].OpInfo = REG_RDX;
    outInfo->Codes[15].CodeOffset = 0x04; outInfo->Codes[15].UnwindOp = UWOP_PUSH_NONVOL; outInfo->Codes[15].OpInfo = REG_RCX;
    outInfo->Codes[16].CodeOffset = 0x03; outInfo->Codes[16].UnwindOp = UWOP_PUSH_NONVOL; outInfo->Codes[16].OpInfo = REG_RBX;
    outInfo->Codes[17].CodeOffset = 0x02; outInfo->Codes[17].UnwindOp = UWOP_PUSH_NONVOL; outInfo->Codes[17].OpInfo = REG_RAX;

    // 18: ALLOC_SMALL 8 at offset 0x01 (pushfq decrements RSP by 8)
    outInfo->Codes[18].CodeOffset = 0x01;
    outInfo->Codes[18].UnwindOp   = UWOP_ALLOC_SMALL;
    outInfo->Codes[18].OpInfo     = 0; // (8 / 8) - 1 = 0
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
