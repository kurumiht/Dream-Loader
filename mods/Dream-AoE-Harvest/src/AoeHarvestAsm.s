.intel_syntax noprefix
.text

# ============================================================
# Dream-AoE-Harvest Dual Pipeline ASM Relays (Enshrouded 1.0)
# Supports both:
# 1) immediate_loot_action (128-byte event buffer: Crop Harvesting & Pickups)
# 2) interaction_attempt   (24-byte event buffer: General Interactions)
# ============================================================

# ------------------------------------------------------------
# 1. immediate_loot_action Relay (Server RVA 0x19FB4B / Client RVA 0x3FD0EB)
# ------------------------------------------------------------
.extern OnExpandImmediateLootAction
.extern pResumeImmediateLootAction

.globl Hook_ImmediateLoot_Relay
Hook_ImmediateLoot_Relay:
    push rax
    push rcx
    push rdx
    push r8
    push r9
    push r10
    push r11
    sub rsp, 0x58

    movdqu [rsp + 0x20], xmm0
    movdqu [rsp + 0x30], xmm1
    movdqu [rsp + 0x40], xmm2

    # Microsoft x64 calling convention:
    # 1st arg: rcx = r9  (pQueue)
    # 2nd arg: rdx = rsi (pRawChunk / 128-byte event buffer)
    # 3rd arg: r8  = r12 (pContext)
    # 4th arg: r9  = rbp (pRbp)
    mov rcx, r9
    mov rdx, rsi
    mov r8, r12
    mov r9, rbp
    call OnExpandImmediateLootAction

    movdqu xmm2, [rsp + 0x40]
    movdqu xmm1, [rsp + 0x30]
    movdqu xmm0, [rsp + 0x20]
    add rsp, 0x58
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdx
    pop rcx
    pop rax

    # Displaced instructions (16 bytes)
    # [r9 + 0x18] contains the expanded count!
    mov eax, dword ptr [r9 + 0x18]
    mov edx, r10d
    mov dword ptr [rbp + 0x7468], edx
    mov r8d, r10d

    # Resume at RVA +16
    jmp qword ptr [rip + pResumeImmediateLootAction]


# ------------------------------------------------------------
# 2. interaction_attempt Relay (Server RVA 0x0A1241 / Client RVA 0x2A2EE1)
# ------------------------------------------------------------
.extern OnExpandImmediateLootBatch
.extern pResumeExpandImmediateBatch

.globl Hook_ExpandImmediateLootBatch_Relay
Hook_ExpandImmediateLootBatch_Relay:
    push rax
    push rcx
    push rdx
    push r8
    push r9
    push r10
    push r11
    sub rsp, 0x58

    movdqu [rsp + 0x20], xmm0
    movdqu [rsp + 0x30], xmm1
    movdqu [rsp + 0x40], xmm2

    # Microsoft x64 calling convention:
    # 1st arg: rcx = rsi (pRawChunk / 24-byte event buffer)
    # 2nd arg: rdx = rbx (pContext)
    # 3rd arg: r8  = r9  (pBatch)
    # 4th arg: r9  = rbp (pRbp)
    mov rcx, rsi
    mov rdx, rbx
    mov r8, r9
    mov r9, rbp
    call OnExpandImmediateLootBatch

    movdqu xmm2, [rsp + 0x40]
    movdqu xmm1, [rsp + 0x30]
    movdqu xmm0, [rsp + 0x20]
    add rsp, 0x58
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdx
    pop rcx
    pop rax

    # Displaced instructions (14 bytes):
    mov eax, dword ptr [r9 + 0x18]
    xor edx, edx
    xor r8d, r8d
    mov qword ptr [rsp + 0x30], rsi

    # Resume at RVA +14
    jmp qword ptr [rip + pResumeExpandImmediateBatch]
