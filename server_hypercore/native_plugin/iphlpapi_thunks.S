.intel_syntax noprefix
.extern g_iphlp_ptrs
.extern g_iphlp_fail_ret
.extern hc_iphlp_resolve

.text
.p2align 4
.globl hc_iphlp_lazy_common
.seh_proc hc_iphlp_lazy_common
hc_iphlp_lazy_common:
    sub rsp, 0x88
    .seh_stackalloc 0x88
    .seh_endprologue
    mov [rsp+0x20], rcx
    mov [rsp+0x28], rdx
    mov [rsp+0x30], r8
    mov [rsp+0x38], r9
    movdqu [rsp+0x40], xmm0
    movdqu [rsp+0x50], xmm1
    movdqu [rsp+0x60], xmm2
    movdqu [rsp+0x70], xmm3
    mov [rsp+0x80], r10
    mov ecx, r10d
    call hc_iphlp_resolve
    mov rcx, [rsp+0x20]
    mov rdx, [rsp+0x28]
    mov r8,  [rsp+0x30]
    mov r9,  [rsp+0x38]
    movdqu xmm0, [rsp+0x40]
    movdqu xmm1, [rsp+0x50]
    movdqu xmm2, [rsp+0x60]
    movdqu xmm3, [rsp+0x70]
    mov r10, [rsp+0x80]
    add rsp, 0x88
    test rax, rax
    jz .Lfail_ret
    jmp rax
.Lfail_ret:
    lea r11, [rip + g_iphlp_fail_ret]
    mov rax, [r11 + r10*8]
    ret
.seh_endproc

.globl hc_iphlp_thunk_0
hc_iphlp_thunk_0:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 0]
    test rax, rax
    jz .Llazy_0
    jmp rax
.Llazy_0:
    mov r10d, 0
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_1
hc_iphlp_thunk_1:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 8]
    test rax, rax
    jz .Llazy_1
    jmp rax
.Llazy_1:
    mov r10d, 1
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_2
hc_iphlp_thunk_2:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 16]
    test rax, rax
    jz .Llazy_2
    jmp rax
.Llazy_2:
    mov r10d, 2
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_3
hc_iphlp_thunk_3:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 24]
    test rax, rax
    jz .Llazy_3
    jmp rax
.Llazy_3:
    mov r10d, 3
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_4
hc_iphlp_thunk_4:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 32]
    test rax, rax
    jz .Llazy_4
    jmp rax
.Llazy_4:
    mov r10d, 4
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_5
hc_iphlp_thunk_5:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 40]
    test rax, rax
    jz .Llazy_5
    jmp rax
.Llazy_5:
    mov r10d, 5
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_6
hc_iphlp_thunk_6:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 48]
    test rax, rax
    jz .Llazy_6
    jmp rax
.Llazy_6:
    mov r10d, 6
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_7
hc_iphlp_thunk_7:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 56]
    test rax, rax
    jz .Llazy_7
    jmp rax
.Llazy_7:
    mov r10d, 7
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_8
hc_iphlp_thunk_8:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 64]
    test rax, rax
    jz .Llazy_8
    jmp rax
.Llazy_8:
    mov r10d, 8
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_9
hc_iphlp_thunk_9:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 72]
    test rax, rax
    jz .Llazy_9
    jmp rax
.Llazy_9:
    mov r10d, 9
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_10
hc_iphlp_thunk_10:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 80]
    test rax, rax
    jz .Llazy_10
    jmp rax
.Llazy_10:
    mov r10d, 10
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_11
hc_iphlp_thunk_11:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 88]
    test rax, rax
    jz .Llazy_11
    jmp rax
.Llazy_11:
    mov r10d, 11
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_12
hc_iphlp_thunk_12:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 96]
    test rax, rax
    jz .Llazy_12
    jmp rax
.Llazy_12:
    mov r10d, 12
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_13
hc_iphlp_thunk_13:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 104]
    test rax, rax
    jz .Llazy_13
    jmp rax
.Llazy_13:
    mov r10d, 13
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_14
hc_iphlp_thunk_14:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 112]
    test rax, rax
    jz .Llazy_14
    jmp rax
.Llazy_14:
    mov r10d, 14
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_15
hc_iphlp_thunk_15:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 120]
    test rax, rax
    jz .Llazy_15
    jmp rax
.Llazy_15:
    mov r10d, 15
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_16
hc_iphlp_thunk_16:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 128]
    test rax, rax
    jz .Llazy_16
    jmp rax
.Llazy_16:
    mov r10d, 16
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_17
hc_iphlp_thunk_17:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 136]
    test rax, rax
    jz .Llazy_17
    jmp rax
.Llazy_17:
    mov r10d, 17
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_18
hc_iphlp_thunk_18:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 144]
    test rax, rax
    jz .Llazy_18
    jmp rax
.Llazy_18:
    mov r10d, 18
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_19
hc_iphlp_thunk_19:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 152]
    test rax, rax
    jz .Llazy_19
    jmp rax
.Llazy_19:
    mov r10d, 19
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_20
hc_iphlp_thunk_20:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 160]
    test rax, rax
    jz .Llazy_20
    jmp rax
.Llazy_20:
    mov r10d, 20
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_21
hc_iphlp_thunk_21:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 168]
    test rax, rax
    jz .Llazy_21
    jmp rax
.Llazy_21:
    mov r10d, 21
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_22
hc_iphlp_thunk_22:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 176]
    test rax, rax
    jz .Llazy_22
    jmp rax
.Llazy_22:
    mov r10d, 22
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_23
hc_iphlp_thunk_23:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 184]
    test rax, rax
    jz .Llazy_23
    jmp rax
.Llazy_23:
    mov r10d, 23
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_24
hc_iphlp_thunk_24:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 192]
    test rax, rax
    jz .Llazy_24
    jmp rax
.Llazy_24:
    mov r10d, 24
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_25
hc_iphlp_thunk_25:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 200]
    test rax, rax
    jz .Llazy_25
    jmp rax
.Llazy_25:
    mov r10d, 25
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_26
hc_iphlp_thunk_26:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 208]
    test rax, rax
    jz .Llazy_26
    jmp rax
.Llazy_26:
    mov r10d, 26
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_27
hc_iphlp_thunk_27:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 216]
    test rax, rax
    jz .Llazy_27
    jmp rax
.Llazy_27:
    mov r10d, 27
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_28
hc_iphlp_thunk_28:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 224]
    test rax, rax
    jz .Llazy_28
    jmp rax
.Llazy_28:
    mov r10d, 28
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_29
hc_iphlp_thunk_29:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 232]
    test rax, rax
    jz .Llazy_29
    jmp rax
.Llazy_29:
    mov r10d, 29
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_30
hc_iphlp_thunk_30:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 240]
    test rax, rax
    jz .Llazy_30
    jmp rax
.Llazy_30:
    mov r10d, 30
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_31
hc_iphlp_thunk_31:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 248]
    test rax, rax
    jz .Llazy_31
    jmp rax
.Llazy_31:
    mov r10d, 31
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_32
hc_iphlp_thunk_32:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 256]
    test rax, rax
    jz .Llazy_32
    jmp rax
.Llazy_32:
    mov r10d, 32
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_33
hc_iphlp_thunk_33:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 264]
    test rax, rax
    jz .Llazy_33
    jmp rax
.Llazy_33:
    mov r10d, 33
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_34
hc_iphlp_thunk_34:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 272]
    test rax, rax
    jz .Llazy_34
    jmp rax
.Llazy_34:
    mov r10d, 34
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_35
hc_iphlp_thunk_35:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 280]
    test rax, rax
    jz .Llazy_35
    jmp rax
.Llazy_35:
    mov r10d, 35
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_36
hc_iphlp_thunk_36:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 288]
    test rax, rax
    jz .Llazy_36
    jmp rax
.Llazy_36:
    mov r10d, 36
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_37
hc_iphlp_thunk_37:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 296]
    test rax, rax
    jz .Llazy_37
    jmp rax
.Llazy_37:
    mov r10d, 37
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_38
hc_iphlp_thunk_38:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 304]
    test rax, rax
    jz .Llazy_38
    jmp rax
.Llazy_38:
    mov r10d, 38
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_39
hc_iphlp_thunk_39:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 312]
    test rax, rax
    jz .Llazy_39
    jmp rax
.Llazy_39:
    mov r10d, 39
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_40
hc_iphlp_thunk_40:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 320]
    test rax, rax
    jz .Llazy_40
    jmp rax
.Llazy_40:
    mov r10d, 40
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_41
hc_iphlp_thunk_41:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 328]
    test rax, rax
    jz .Llazy_41
    jmp rax
.Llazy_41:
    mov r10d, 41
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_42
hc_iphlp_thunk_42:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 336]
    test rax, rax
    jz .Llazy_42
    jmp rax
.Llazy_42:
    mov r10d, 42
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_43
hc_iphlp_thunk_43:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 344]
    test rax, rax
    jz .Llazy_43
    jmp rax
.Llazy_43:
    mov r10d, 43
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_44
hc_iphlp_thunk_44:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 352]
    test rax, rax
    jz .Llazy_44
    jmp rax
.Llazy_44:
    mov r10d, 44
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_45
hc_iphlp_thunk_45:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 360]
    test rax, rax
    jz .Llazy_45
    jmp rax
.Llazy_45:
    mov r10d, 45
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_46
hc_iphlp_thunk_46:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 368]
    test rax, rax
    jz .Llazy_46
    jmp rax
.Llazy_46:
    mov r10d, 46
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_47
hc_iphlp_thunk_47:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 376]
    test rax, rax
    jz .Llazy_47
    jmp rax
.Llazy_47:
    mov r10d, 47
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_48
hc_iphlp_thunk_48:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 384]
    test rax, rax
    jz .Llazy_48
    jmp rax
.Llazy_48:
    mov r10d, 48
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_49
hc_iphlp_thunk_49:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 392]
    test rax, rax
    jz .Llazy_49
    jmp rax
.Llazy_49:
    mov r10d, 49
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_50
hc_iphlp_thunk_50:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 400]
    test rax, rax
    jz .Llazy_50
    jmp rax
.Llazy_50:
    mov r10d, 50
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_51
hc_iphlp_thunk_51:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 408]
    test rax, rax
    jz .Llazy_51
    jmp rax
.Llazy_51:
    mov r10d, 51
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_52
hc_iphlp_thunk_52:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 416]
    test rax, rax
    jz .Llazy_52
    jmp rax
.Llazy_52:
    mov r10d, 52
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_53
hc_iphlp_thunk_53:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 424]
    test rax, rax
    jz .Llazy_53
    jmp rax
.Llazy_53:
    mov r10d, 53
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_54
hc_iphlp_thunk_54:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 432]
    test rax, rax
    jz .Llazy_54
    jmp rax
.Llazy_54:
    mov r10d, 54
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_55
hc_iphlp_thunk_55:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 440]
    test rax, rax
    jz .Llazy_55
    jmp rax
.Llazy_55:
    mov r10d, 55
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_56
hc_iphlp_thunk_56:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 448]
    test rax, rax
    jz .Llazy_56
    jmp rax
.Llazy_56:
    mov r10d, 56
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_57
hc_iphlp_thunk_57:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 456]
    test rax, rax
    jz .Llazy_57
    jmp rax
.Llazy_57:
    mov r10d, 57
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_58
hc_iphlp_thunk_58:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 464]
    test rax, rax
    jz .Llazy_58
    jmp rax
.Llazy_58:
    mov r10d, 58
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_59
hc_iphlp_thunk_59:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 472]
    test rax, rax
    jz .Llazy_59
    jmp rax
.Llazy_59:
    mov r10d, 59
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_60
hc_iphlp_thunk_60:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 480]
    test rax, rax
    jz .Llazy_60
    jmp rax
.Llazy_60:
    mov r10d, 60
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_61
hc_iphlp_thunk_61:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 488]
    test rax, rax
    jz .Llazy_61
    jmp rax
.Llazy_61:
    mov r10d, 61
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_62
hc_iphlp_thunk_62:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 496]
    test rax, rax
    jz .Llazy_62
    jmp rax
.Llazy_62:
    mov r10d, 62
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_63
hc_iphlp_thunk_63:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 504]
    test rax, rax
    jz .Llazy_63
    jmp rax
.Llazy_63:
    mov r10d, 63
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_64
hc_iphlp_thunk_64:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 512]
    test rax, rax
    jz .Llazy_64
    jmp rax
.Llazy_64:
    mov r10d, 64
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_65
hc_iphlp_thunk_65:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 520]
    test rax, rax
    jz .Llazy_65
    jmp rax
.Llazy_65:
    mov r10d, 65
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_66
hc_iphlp_thunk_66:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 528]
    test rax, rax
    jz .Llazy_66
    jmp rax
.Llazy_66:
    mov r10d, 66
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_67
hc_iphlp_thunk_67:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 536]
    test rax, rax
    jz .Llazy_67
    jmp rax
.Llazy_67:
    mov r10d, 67
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_68
hc_iphlp_thunk_68:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 544]
    test rax, rax
    jz .Llazy_68
    jmp rax
.Llazy_68:
    mov r10d, 68
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_69
hc_iphlp_thunk_69:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 552]
    test rax, rax
    jz .Llazy_69
    jmp rax
.Llazy_69:
    mov r10d, 69
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_70
hc_iphlp_thunk_70:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 560]
    test rax, rax
    jz .Llazy_70
    jmp rax
.Llazy_70:
    mov r10d, 70
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_71
hc_iphlp_thunk_71:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 568]
    test rax, rax
    jz .Llazy_71
    jmp rax
.Llazy_71:
    mov r10d, 71
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_72
hc_iphlp_thunk_72:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 576]
    test rax, rax
    jz .Llazy_72
    jmp rax
.Llazy_72:
    mov r10d, 72
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_73
hc_iphlp_thunk_73:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 584]
    test rax, rax
    jz .Llazy_73
    jmp rax
.Llazy_73:
    mov r10d, 73
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_74
hc_iphlp_thunk_74:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 592]
    test rax, rax
    jz .Llazy_74
    jmp rax
.Llazy_74:
    mov r10d, 74
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_75
hc_iphlp_thunk_75:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 600]
    test rax, rax
    jz .Llazy_75
    jmp rax
.Llazy_75:
    mov r10d, 75
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_76
hc_iphlp_thunk_76:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 608]
    test rax, rax
    jz .Llazy_76
    jmp rax
.Llazy_76:
    mov r10d, 76
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_77
hc_iphlp_thunk_77:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 616]
    test rax, rax
    jz .Llazy_77
    jmp rax
.Llazy_77:
    mov r10d, 77
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_78
hc_iphlp_thunk_78:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 624]
    test rax, rax
    jz .Llazy_78
    jmp rax
.Llazy_78:
    mov r10d, 78
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_79
hc_iphlp_thunk_79:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 632]
    test rax, rax
    jz .Llazy_79
    jmp rax
.Llazy_79:
    mov r10d, 79
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_80
hc_iphlp_thunk_80:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 640]
    test rax, rax
    jz .Llazy_80
    jmp rax
.Llazy_80:
    mov r10d, 80
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_81
hc_iphlp_thunk_81:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 648]
    test rax, rax
    jz .Llazy_81
    jmp rax
.Llazy_81:
    mov r10d, 81
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_82
hc_iphlp_thunk_82:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 656]
    test rax, rax
    jz .Llazy_82
    jmp rax
.Llazy_82:
    mov r10d, 82
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_83
hc_iphlp_thunk_83:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 664]
    test rax, rax
    jz .Llazy_83
    jmp rax
.Llazy_83:
    mov r10d, 83
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_84
hc_iphlp_thunk_84:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 672]
    test rax, rax
    jz .Llazy_84
    jmp rax
.Llazy_84:
    mov r10d, 84
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_85
hc_iphlp_thunk_85:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 680]
    test rax, rax
    jz .Llazy_85
    jmp rax
.Llazy_85:
    mov r10d, 85
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_86
hc_iphlp_thunk_86:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 688]
    test rax, rax
    jz .Llazy_86
    jmp rax
.Llazy_86:
    mov r10d, 86
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_87
hc_iphlp_thunk_87:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 696]
    test rax, rax
    jz .Llazy_87
    jmp rax
.Llazy_87:
    mov r10d, 87
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_88
hc_iphlp_thunk_88:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 704]
    test rax, rax
    jz .Llazy_88
    jmp rax
.Llazy_88:
    mov r10d, 88
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_89
hc_iphlp_thunk_89:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 712]
    test rax, rax
    jz .Llazy_89
    jmp rax
.Llazy_89:
    mov r10d, 89
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_90
hc_iphlp_thunk_90:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 720]
    test rax, rax
    jz .Llazy_90
    jmp rax
.Llazy_90:
    mov r10d, 90
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_91
hc_iphlp_thunk_91:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 728]
    test rax, rax
    jz .Llazy_91
    jmp rax
.Llazy_91:
    mov r10d, 91
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_92
hc_iphlp_thunk_92:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 736]
    test rax, rax
    jz .Llazy_92
    jmp rax
.Llazy_92:
    mov r10d, 92
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_93
hc_iphlp_thunk_93:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 744]
    test rax, rax
    jz .Llazy_93
    jmp rax
.Llazy_93:
    mov r10d, 93
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_94
hc_iphlp_thunk_94:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 752]
    test rax, rax
    jz .Llazy_94
    jmp rax
.Llazy_94:
    mov r10d, 94
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_95
hc_iphlp_thunk_95:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 760]
    test rax, rax
    jz .Llazy_95
    jmp rax
.Llazy_95:
    mov r10d, 95
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_96
hc_iphlp_thunk_96:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 768]
    test rax, rax
    jz .Llazy_96
    jmp rax
.Llazy_96:
    mov r10d, 96
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_97
hc_iphlp_thunk_97:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 776]
    test rax, rax
    jz .Llazy_97
    jmp rax
.Llazy_97:
    mov r10d, 97
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_98
hc_iphlp_thunk_98:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 784]
    test rax, rax
    jz .Llazy_98
    jmp rax
.Llazy_98:
    mov r10d, 98
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_99
hc_iphlp_thunk_99:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 792]
    test rax, rax
    jz .Llazy_99
    jmp rax
.Llazy_99:
    mov r10d, 99
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_100
hc_iphlp_thunk_100:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 800]
    test rax, rax
    jz .Llazy_100
    jmp rax
.Llazy_100:
    mov r10d, 100
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_101
hc_iphlp_thunk_101:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 808]
    test rax, rax
    jz .Llazy_101
    jmp rax
.Llazy_101:
    mov r10d, 101
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_102
hc_iphlp_thunk_102:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 816]
    test rax, rax
    jz .Llazy_102
    jmp rax
.Llazy_102:
    mov r10d, 102
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_103
hc_iphlp_thunk_103:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 824]
    test rax, rax
    jz .Llazy_103
    jmp rax
.Llazy_103:
    mov r10d, 103
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_104
hc_iphlp_thunk_104:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 832]
    test rax, rax
    jz .Llazy_104
    jmp rax
.Llazy_104:
    mov r10d, 104
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_105
hc_iphlp_thunk_105:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 840]
    test rax, rax
    jz .Llazy_105
    jmp rax
.Llazy_105:
    mov r10d, 105
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_106
hc_iphlp_thunk_106:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 848]
    test rax, rax
    jz .Llazy_106
    jmp rax
.Llazy_106:
    mov r10d, 106
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_107
hc_iphlp_thunk_107:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 856]
    test rax, rax
    jz .Llazy_107
    jmp rax
.Llazy_107:
    mov r10d, 107
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_108
hc_iphlp_thunk_108:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 864]
    test rax, rax
    jz .Llazy_108
    jmp rax
.Llazy_108:
    mov r10d, 108
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_109
hc_iphlp_thunk_109:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 872]
    test rax, rax
    jz .Llazy_109
    jmp rax
.Llazy_109:
    mov r10d, 109
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_110
hc_iphlp_thunk_110:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 880]
    test rax, rax
    jz .Llazy_110
    jmp rax
.Llazy_110:
    mov r10d, 110
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_111
hc_iphlp_thunk_111:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 888]
    test rax, rax
    jz .Llazy_111
    jmp rax
.Llazy_111:
    mov r10d, 111
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_112
hc_iphlp_thunk_112:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 896]
    test rax, rax
    jz .Llazy_112
    jmp rax
.Llazy_112:
    mov r10d, 112
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_113
hc_iphlp_thunk_113:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 904]
    test rax, rax
    jz .Llazy_113
    jmp rax
.Llazy_113:
    mov r10d, 113
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_114
hc_iphlp_thunk_114:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 912]
    test rax, rax
    jz .Llazy_114
    jmp rax
.Llazy_114:
    mov r10d, 114
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_115
hc_iphlp_thunk_115:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 920]
    test rax, rax
    jz .Llazy_115
    jmp rax
.Llazy_115:
    mov r10d, 115
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_116
hc_iphlp_thunk_116:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 928]
    test rax, rax
    jz .Llazy_116
    jmp rax
.Llazy_116:
    mov r10d, 116
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_117
hc_iphlp_thunk_117:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 936]
    test rax, rax
    jz .Llazy_117
    jmp rax
.Llazy_117:
    mov r10d, 117
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_118
hc_iphlp_thunk_118:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 944]
    test rax, rax
    jz .Llazy_118
    jmp rax
.Llazy_118:
    mov r10d, 118
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_119
hc_iphlp_thunk_119:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 952]
    test rax, rax
    jz .Llazy_119
    jmp rax
.Llazy_119:
    mov r10d, 119
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_120
hc_iphlp_thunk_120:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 960]
    test rax, rax
    jz .Llazy_120
    jmp rax
.Llazy_120:
    mov r10d, 120
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_121
hc_iphlp_thunk_121:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 968]
    test rax, rax
    jz .Llazy_121
    jmp rax
.Llazy_121:
    mov r10d, 121
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_122
hc_iphlp_thunk_122:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 976]
    test rax, rax
    jz .Llazy_122
    jmp rax
.Llazy_122:
    mov r10d, 122
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_123
hc_iphlp_thunk_123:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 984]
    test rax, rax
    jz .Llazy_123
    jmp rax
.Llazy_123:
    mov r10d, 123
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_124
hc_iphlp_thunk_124:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 992]
    test rax, rax
    jz .Llazy_124
    jmp rax
.Llazy_124:
    mov r10d, 124
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_125
hc_iphlp_thunk_125:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1000]
    test rax, rax
    jz .Llazy_125
    jmp rax
.Llazy_125:
    mov r10d, 125
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_126
hc_iphlp_thunk_126:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1008]
    test rax, rax
    jz .Llazy_126
    jmp rax
.Llazy_126:
    mov r10d, 126
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_127
hc_iphlp_thunk_127:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1016]
    test rax, rax
    jz .Llazy_127
    jmp rax
.Llazy_127:
    mov r10d, 127
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_128
hc_iphlp_thunk_128:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1024]
    test rax, rax
    jz .Llazy_128
    jmp rax
.Llazy_128:
    mov r10d, 128
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_129
hc_iphlp_thunk_129:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1032]
    test rax, rax
    jz .Llazy_129
    jmp rax
.Llazy_129:
    mov r10d, 129
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_130
hc_iphlp_thunk_130:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1040]
    test rax, rax
    jz .Llazy_130
    jmp rax
.Llazy_130:
    mov r10d, 130
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_131
hc_iphlp_thunk_131:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1048]
    test rax, rax
    jz .Llazy_131
    jmp rax
.Llazy_131:
    mov r10d, 131
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_132
hc_iphlp_thunk_132:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1056]
    test rax, rax
    jz .Llazy_132
    jmp rax
.Llazy_132:
    mov r10d, 132
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_133
hc_iphlp_thunk_133:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1064]
    test rax, rax
    jz .Llazy_133
    jmp rax
.Llazy_133:
    mov r10d, 133
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_134
hc_iphlp_thunk_134:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1072]
    test rax, rax
    jz .Llazy_134
    jmp rax
.Llazy_134:
    mov r10d, 134
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_135
hc_iphlp_thunk_135:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1080]
    test rax, rax
    jz .Llazy_135
    jmp rax
.Llazy_135:
    mov r10d, 135
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_136
hc_iphlp_thunk_136:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1088]
    test rax, rax
    jz .Llazy_136
    jmp rax
.Llazy_136:
    mov r10d, 136
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_137
hc_iphlp_thunk_137:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1096]
    test rax, rax
    jz .Llazy_137
    jmp rax
.Llazy_137:
    mov r10d, 137
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_138
hc_iphlp_thunk_138:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1104]
    test rax, rax
    jz .Llazy_138
    jmp rax
.Llazy_138:
    mov r10d, 138
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_139
hc_iphlp_thunk_139:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1112]
    test rax, rax
    jz .Llazy_139
    jmp rax
.Llazy_139:
    mov r10d, 139
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_140
hc_iphlp_thunk_140:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1120]
    test rax, rax
    jz .Llazy_140
    jmp rax
.Llazy_140:
    mov r10d, 140
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_141
hc_iphlp_thunk_141:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1128]
    test rax, rax
    jz .Llazy_141
    jmp rax
.Llazy_141:
    mov r10d, 141
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_142
hc_iphlp_thunk_142:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1136]
    test rax, rax
    jz .Llazy_142
    jmp rax
.Llazy_142:
    mov r10d, 142
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_143
hc_iphlp_thunk_143:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1144]
    test rax, rax
    jz .Llazy_143
    jmp rax
.Llazy_143:
    mov r10d, 143
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_144
hc_iphlp_thunk_144:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1152]
    test rax, rax
    jz .Llazy_144
    jmp rax
.Llazy_144:
    mov r10d, 144
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_145
hc_iphlp_thunk_145:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1160]
    test rax, rax
    jz .Llazy_145
    jmp rax
.Llazy_145:
    mov r10d, 145
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_146
hc_iphlp_thunk_146:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1168]
    test rax, rax
    jz .Llazy_146
    jmp rax
.Llazy_146:
    mov r10d, 146
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_147
hc_iphlp_thunk_147:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1176]
    test rax, rax
    jz .Llazy_147
    jmp rax
.Llazy_147:
    mov r10d, 147
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_148
hc_iphlp_thunk_148:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1184]
    test rax, rax
    jz .Llazy_148
    jmp rax
.Llazy_148:
    mov r10d, 148
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_149
hc_iphlp_thunk_149:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1192]
    test rax, rax
    jz .Llazy_149
    jmp rax
.Llazy_149:
    mov r10d, 149
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_150
hc_iphlp_thunk_150:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1200]
    test rax, rax
    jz .Llazy_150
    jmp rax
.Llazy_150:
    mov r10d, 150
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_151
hc_iphlp_thunk_151:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1208]
    test rax, rax
    jz .Llazy_151
    jmp rax
.Llazy_151:
    mov r10d, 151
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_152
hc_iphlp_thunk_152:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1216]
    test rax, rax
    jz .Llazy_152
    jmp rax
.Llazy_152:
    mov r10d, 152
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_153
hc_iphlp_thunk_153:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1224]
    test rax, rax
    jz .Llazy_153
    jmp rax
.Llazy_153:
    mov r10d, 153
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_154
hc_iphlp_thunk_154:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1232]
    test rax, rax
    jz .Llazy_154
    jmp rax
.Llazy_154:
    mov r10d, 154
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_155
hc_iphlp_thunk_155:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1240]
    test rax, rax
    jz .Llazy_155
    jmp rax
.Llazy_155:
    mov r10d, 155
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_156
hc_iphlp_thunk_156:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1248]
    test rax, rax
    jz .Llazy_156
    jmp rax
.Llazy_156:
    mov r10d, 156
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_157
hc_iphlp_thunk_157:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1256]
    test rax, rax
    jz .Llazy_157
    jmp rax
.Llazy_157:
    mov r10d, 157
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_158
hc_iphlp_thunk_158:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1264]
    test rax, rax
    jz .Llazy_158
    jmp rax
.Llazy_158:
    mov r10d, 158
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_159
hc_iphlp_thunk_159:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1272]
    test rax, rax
    jz .Llazy_159
    jmp rax
.Llazy_159:
    mov r10d, 159
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_160
hc_iphlp_thunk_160:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1280]
    test rax, rax
    jz .Llazy_160
    jmp rax
.Llazy_160:
    mov r10d, 160
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_161
hc_iphlp_thunk_161:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1288]
    test rax, rax
    jz .Llazy_161
    jmp rax
.Llazy_161:
    mov r10d, 161
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_162
hc_iphlp_thunk_162:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1296]
    test rax, rax
    jz .Llazy_162
    jmp rax
.Llazy_162:
    mov r10d, 162
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_163
hc_iphlp_thunk_163:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1304]
    test rax, rax
    jz .Llazy_163
    jmp rax
.Llazy_163:
    mov r10d, 163
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_164
hc_iphlp_thunk_164:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1312]
    test rax, rax
    jz .Llazy_164
    jmp rax
.Llazy_164:
    mov r10d, 164
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_165
hc_iphlp_thunk_165:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1320]
    test rax, rax
    jz .Llazy_165
    jmp rax
.Llazy_165:
    mov r10d, 165
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_166
hc_iphlp_thunk_166:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1328]
    test rax, rax
    jz .Llazy_166
    jmp rax
.Llazy_166:
    mov r10d, 166
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_167
hc_iphlp_thunk_167:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1336]
    test rax, rax
    jz .Llazy_167
    jmp rax
.Llazy_167:
    mov r10d, 167
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_168
hc_iphlp_thunk_168:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1344]
    test rax, rax
    jz .Llazy_168
    jmp rax
.Llazy_168:
    mov r10d, 168
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_169
hc_iphlp_thunk_169:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1352]
    test rax, rax
    jz .Llazy_169
    jmp rax
.Llazy_169:
    mov r10d, 169
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_170
hc_iphlp_thunk_170:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1360]
    test rax, rax
    jz .Llazy_170
    jmp rax
.Llazy_170:
    mov r10d, 170
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_171
hc_iphlp_thunk_171:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1368]
    test rax, rax
    jz .Llazy_171
    jmp rax
.Llazy_171:
    mov r10d, 171
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_172
hc_iphlp_thunk_172:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1376]
    test rax, rax
    jz .Llazy_172
    jmp rax
.Llazy_172:
    mov r10d, 172
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_173
hc_iphlp_thunk_173:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1384]
    test rax, rax
    jz .Llazy_173
    jmp rax
.Llazy_173:
    mov r10d, 173
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_174
hc_iphlp_thunk_174:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1392]
    test rax, rax
    jz .Llazy_174
    jmp rax
.Llazy_174:
    mov r10d, 174
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_175
hc_iphlp_thunk_175:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1400]
    test rax, rax
    jz .Llazy_175
    jmp rax
.Llazy_175:
    mov r10d, 175
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_176
hc_iphlp_thunk_176:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1408]
    test rax, rax
    jz .Llazy_176
    jmp rax
.Llazy_176:
    mov r10d, 176
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_177
hc_iphlp_thunk_177:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1416]
    test rax, rax
    jz .Llazy_177
    jmp rax
.Llazy_177:
    mov r10d, 177
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_178
hc_iphlp_thunk_178:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1424]
    test rax, rax
    jz .Llazy_178
    jmp rax
.Llazy_178:
    mov r10d, 178
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_179
hc_iphlp_thunk_179:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1432]
    test rax, rax
    jz .Llazy_179
    jmp rax
.Llazy_179:
    mov r10d, 179
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_180
hc_iphlp_thunk_180:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1440]
    test rax, rax
    jz .Llazy_180
    jmp rax
.Llazy_180:
    mov r10d, 180
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_181
hc_iphlp_thunk_181:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1448]
    test rax, rax
    jz .Llazy_181
    jmp rax
.Llazy_181:
    mov r10d, 181
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_182
hc_iphlp_thunk_182:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1456]
    test rax, rax
    jz .Llazy_182
    jmp rax
.Llazy_182:
    mov r10d, 182
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_183
hc_iphlp_thunk_183:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1464]
    test rax, rax
    jz .Llazy_183
    jmp rax
.Llazy_183:
    mov r10d, 183
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_184
hc_iphlp_thunk_184:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1472]
    test rax, rax
    jz .Llazy_184
    jmp rax
.Llazy_184:
    mov r10d, 184
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_185
hc_iphlp_thunk_185:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1480]
    test rax, rax
    jz .Llazy_185
    jmp rax
.Llazy_185:
    mov r10d, 185
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_186
hc_iphlp_thunk_186:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1488]
    test rax, rax
    jz .Llazy_186
    jmp rax
.Llazy_186:
    mov r10d, 186
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_187
hc_iphlp_thunk_187:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1496]
    test rax, rax
    jz .Llazy_187
    jmp rax
.Llazy_187:
    mov r10d, 187
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_188
hc_iphlp_thunk_188:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1504]
    test rax, rax
    jz .Llazy_188
    jmp rax
.Llazy_188:
    mov r10d, 188
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_189
hc_iphlp_thunk_189:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1512]
    test rax, rax
    jz .Llazy_189
    jmp rax
.Llazy_189:
    mov r10d, 189
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_190
hc_iphlp_thunk_190:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1520]
    test rax, rax
    jz .Llazy_190
    jmp rax
.Llazy_190:
    mov r10d, 190
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_191
hc_iphlp_thunk_191:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1528]
    test rax, rax
    jz .Llazy_191
    jmp rax
.Llazy_191:
    mov r10d, 191
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_192
hc_iphlp_thunk_192:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1536]
    test rax, rax
    jz .Llazy_192
    jmp rax
.Llazy_192:
    mov r10d, 192
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_193
hc_iphlp_thunk_193:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1544]
    test rax, rax
    jz .Llazy_193
    jmp rax
.Llazy_193:
    mov r10d, 193
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_194
hc_iphlp_thunk_194:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1552]
    test rax, rax
    jz .Llazy_194
    jmp rax
.Llazy_194:
    mov r10d, 194
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_195
hc_iphlp_thunk_195:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1560]
    test rax, rax
    jz .Llazy_195
    jmp rax
.Llazy_195:
    mov r10d, 195
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_196
hc_iphlp_thunk_196:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1568]
    test rax, rax
    jz .Llazy_196
    jmp rax
.Llazy_196:
    mov r10d, 196
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_197
hc_iphlp_thunk_197:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1576]
    test rax, rax
    jz .Llazy_197
    jmp rax
.Llazy_197:
    mov r10d, 197
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_198
hc_iphlp_thunk_198:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1584]
    test rax, rax
    jz .Llazy_198
    jmp rax
.Llazy_198:
    mov r10d, 198
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_199
hc_iphlp_thunk_199:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1592]
    test rax, rax
    jz .Llazy_199
    jmp rax
.Llazy_199:
    mov r10d, 199
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_200
hc_iphlp_thunk_200:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1600]
    test rax, rax
    jz .Llazy_200
    jmp rax
.Llazy_200:
    mov r10d, 200
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_201
hc_iphlp_thunk_201:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1608]
    test rax, rax
    jz .Llazy_201
    jmp rax
.Llazy_201:
    mov r10d, 201
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_202
hc_iphlp_thunk_202:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1616]
    test rax, rax
    jz .Llazy_202
    jmp rax
.Llazy_202:
    mov r10d, 202
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_203
hc_iphlp_thunk_203:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1624]
    test rax, rax
    jz .Llazy_203
    jmp rax
.Llazy_203:
    mov r10d, 203
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_204
hc_iphlp_thunk_204:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1632]
    test rax, rax
    jz .Llazy_204
    jmp rax
.Llazy_204:
    mov r10d, 204
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_205
hc_iphlp_thunk_205:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1640]
    test rax, rax
    jz .Llazy_205
    jmp rax
.Llazy_205:
    mov r10d, 205
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_206
hc_iphlp_thunk_206:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1648]
    test rax, rax
    jz .Llazy_206
    jmp rax
.Llazy_206:
    mov r10d, 206
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_207
hc_iphlp_thunk_207:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1656]
    test rax, rax
    jz .Llazy_207
    jmp rax
.Llazy_207:
    mov r10d, 207
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_208
hc_iphlp_thunk_208:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1664]
    test rax, rax
    jz .Llazy_208
    jmp rax
.Llazy_208:
    mov r10d, 208
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_209
hc_iphlp_thunk_209:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1672]
    test rax, rax
    jz .Llazy_209
    jmp rax
.Llazy_209:
    mov r10d, 209
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_210
hc_iphlp_thunk_210:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1680]
    test rax, rax
    jz .Llazy_210
    jmp rax
.Llazy_210:
    mov r10d, 210
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_211
hc_iphlp_thunk_211:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1688]
    test rax, rax
    jz .Llazy_211
    jmp rax
.Llazy_211:
    mov r10d, 211
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_212
hc_iphlp_thunk_212:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1696]
    test rax, rax
    jz .Llazy_212
    jmp rax
.Llazy_212:
    mov r10d, 212
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_213
hc_iphlp_thunk_213:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1704]
    test rax, rax
    jz .Llazy_213
    jmp rax
.Llazy_213:
    mov r10d, 213
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_214
hc_iphlp_thunk_214:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1712]
    test rax, rax
    jz .Llazy_214
    jmp rax
.Llazy_214:
    mov r10d, 214
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_215
hc_iphlp_thunk_215:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1720]
    test rax, rax
    jz .Llazy_215
    jmp rax
.Llazy_215:
    mov r10d, 215
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_216
hc_iphlp_thunk_216:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1728]
    test rax, rax
    jz .Llazy_216
    jmp rax
.Llazy_216:
    mov r10d, 216
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_217
hc_iphlp_thunk_217:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1736]
    test rax, rax
    jz .Llazy_217
    jmp rax
.Llazy_217:
    mov r10d, 217
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_218
hc_iphlp_thunk_218:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1744]
    test rax, rax
    jz .Llazy_218
    jmp rax
.Llazy_218:
    mov r10d, 218
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_219
hc_iphlp_thunk_219:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1752]
    test rax, rax
    jz .Llazy_219
    jmp rax
.Llazy_219:
    mov r10d, 219
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_220
hc_iphlp_thunk_220:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1760]
    test rax, rax
    jz .Llazy_220
    jmp rax
.Llazy_220:
    mov r10d, 220
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_221
hc_iphlp_thunk_221:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1768]
    test rax, rax
    jz .Llazy_221
    jmp rax
.Llazy_221:
    mov r10d, 221
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_222
hc_iphlp_thunk_222:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1776]
    test rax, rax
    jz .Llazy_222
    jmp rax
.Llazy_222:
    mov r10d, 222
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_223
hc_iphlp_thunk_223:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1784]
    test rax, rax
    jz .Llazy_223
    jmp rax
.Llazy_223:
    mov r10d, 223
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_224
hc_iphlp_thunk_224:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1792]
    test rax, rax
    jz .Llazy_224
    jmp rax
.Llazy_224:
    mov r10d, 224
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_225
hc_iphlp_thunk_225:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1800]
    test rax, rax
    jz .Llazy_225
    jmp rax
.Llazy_225:
    mov r10d, 225
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_226
hc_iphlp_thunk_226:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1808]
    test rax, rax
    jz .Llazy_226
    jmp rax
.Llazy_226:
    mov r10d, 226
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_227
hc_iphlp_thunk_227:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1816]
    test rax, rax
    jz .Llazy_227
    jmp rax
.Llazy_227:
    mov r10d, 227
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_228
hc_iphlp_thunk_228:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1824]
    test rax, rax
    jz .Llazy_228
    jmp rax
.Llazy_228:
    mov r10d, 228
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_229
hc_iphlp_thunk_229:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1832]
    test rax, rax
    jz .Llazy_229
    jmp rax
.Llazy_229:
    mov r10d, 229
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_230
hc_iphlp_thunk_230:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1840]
    test rax, rax
    jz .Llazy_230
    jmp rax
.Llazy_230:
    mov r10d, 230
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_231
hc_iphlp_thunk_231:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1848]
    test rax, rax
    jz .Llazy_231
    jmp rax
.Llazy_231:
    mov r10d, 231
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_232
hc_iphlp_thunk_232:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1856]
    test rax, rax
    jz .Llazy_232
    jmp rax
.Llazy_232:
    mov r10d, 232
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_233
hc_iphlp_thunk_233:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1864]
    test rax, rax
    jz .Llazy_233
    jmp rax
.Llazy_233:
    mov r10d, 233
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_234
hc_iphlp_thunk_234:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1872]
    test rax, rax
    jz .Llazy_234
    jmp rax
.Llazy_234:
    mov r10d, 234
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_235
hc_iphlp_thunk_235:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1880]
    test rax, rax
    jz .Llazy_235
    jmp rax
.Llazy_235:
    mov r10d, 235
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_236
hc_iphlp_thunk_236:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1888]
    test rax, rax
    jz .Llazy_236
    jmp rax
.Llazy_236:
    mov r10d, 236
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_237
hc_iphlp_thunk_237:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1896]
    test rax, rax
    jz .Llazy_237
    jmp rax
.Llazy_237:
    mov r10d, 237
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_238
hc_iphlp_thunk_238:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1904]
    test rax, rax
    jz .Llazy_238
    jmp rax
.Llazy_238:
    mov r10d, 238
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_239
hc_iphlp_thunk_239:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1912]
    test rax, rax
    jz .Llazy_239
    jmp rax
.Llazy_239:
    mov r10d, 239
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_240
hc_iphlp_thunk_240:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1920]
    test rax, rax
    jz .Llazy_240
    jmp rax
.Llazy_240:
    mov r10d, 240
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_241
hc_iphlp_thunk_241:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1928]
    test rax, rax
    jz .Llazy_241
    jmp rax
.Llazy_241:
    mov r10d, 241
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_242
hc_iphlp_thunk_242:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1936]
    test rax, rax
    jz .Llazy_242
    jmp rax
.Llazy_242:
    mov r10d, 242
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_243
hc_iphlp_thunk_243:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1944]
    test rax, rax
    jz .Llazy_243
    jmp rax
.Llazy_243:
    mov r10d, 243
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_244
hc_iphlp_thunk_244:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1952]
    test rax, rax
    jz .Llazy_244
    jmp rax
.Llazy_244:
    mov r10d, 244
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_245
hc_iphlp_thunk_245:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1960]
    test rax, rax
    jz .Llazy_245
    jmp rax
.Llazy_245:
    mov r10d, 245
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_246
hc_iphlp_thunk_246:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1968]
    test rax, rax
    jz .Llazy_246
    jmp rax
.Llazy_246:
    mov r10d, 246
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_247
hc_iphlp_thunk_247:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1976]
    test rax, rax
    jz .Llazy_247
    jmp rax
.Llazy_247:
    mov r10d, 247
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_248
hc_iphlp_thunk_248:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1984]
    test rax, rax
    jz .Llazy_248
    jmp rax
.Llazy_248:
    mov r10d, 248
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_249
hc_iphlp_thunk_249:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 1992]
    test rax, rax
    jz .Llazy_249
    jmp rax
.Llazy_249:
    mov r10d, 249
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_250
hc_iphlp_thunk_250:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 2000]
    test rax, rax
    jz .Llazy_250
    jmp rax
.Llazy_250:
    mov r10d, 250
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_251
hc_iphlp_thunk_251:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 2008]
    test rax, rax
    jz .Llazy_251
    jmp rax
.Llazy_251:
    mov r10d, 251
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_252
hc_iphlp_thunk_252:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 2016]
    test rax, rax
    jz .Llazy_252
    jmp rax
.Llazy_252:
    mov r10d, 252
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_253
hc_iphlp_thunk_253:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 2024]
    test rax, rax
    jz .Llazy_253
    jmp rax
.Llazy_253:
    mov r10d, 253
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_254
hc_iphlp_thunk_254:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 2032]
    test rax, rax
    jz .Llazy_254
    jmp rax
.Llazy_254:
    mov r10d, 254
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_255
hc_iphlp_thunk_255:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 2040]
    test rax, rax
    jz .Llazy_255
    jmp rax
.Llazy_255:
    mov r10d, 255
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_256
hc_iphlp_thunk_256:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 2048]
    test rax, rax
    jz .Llazy_256
    jmp rax
.Llazy_256:
    mov r10d, 256
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_257
hc_iphlp_thunk_257:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 2056]
    test rax, rax
    jz .Llazy_257
    jmp rax
.Llazy_257:
    mov r10d, 257
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_258
hc_iphlp_thunk_258:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 2064]
    test rax, rax
    jz .Llazy_258
    jmp rax
.Llazy_258:
    mov r10d, 258
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_259
hc_iphlp_thunk_259:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 2072]
    test rax, rax
    jz .Llazy_259
    jmp rax
.Llazy_259:
    mov r10d, 259
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_260
hc_iphlp_thunk_260:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 2080]
    test rax, rax
    jz .Llazy_260
    jmp rax
.Llazy_260:
    mov r10d, 260
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_261
hc_iphlp_thunk_261:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 2088]
    test rax, rax
    jz .Llazy_261
    jmp rax
.Llazy_261:
    mov r10d, 261
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_262
hc_iphlp_thunk_262:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 2096]
    test rax, rax
    jz .Llazy_262
    jmp rax
.Llazy_262:
    mov r10d, 262
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_263
hc_iphlp_thunk_263:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 2104]
    test rax, rax
    jz .Llazy_263
    jmp rax
.Llazy_263:
    mov r10d, 263
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_264
hc_iphlp_thunk_264:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 2112]
    test rax, rax
    jz .Llazy_264
    jmp rax
.Llazy_264:
    mov r10d, 264
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_265
hc_iphlp_thunk_265:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 2120]
    test rax, rax
    jz .Llazy_265
    jmp rax
.Llazy_265:
    mov r10d, 265
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_266
hc_iphlp_thunk_266:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 2128]
    test rax, rax
    jz .Llazy_266
    jmp rax
.Llazy_266:
    mov r10d, 266
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_267
hc_iphlp_thunk_267:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 2136]
    test rax, rax
    jz .Llazy_267
    jmp rax
.Llazy_267:
    mov r10d, 267
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_268
hc_iphlp_thunk_268:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 2144]
    test rax, rax
    jz .Llazy_268
    jmp rax
.Llazy_268:
    mov r10d, 268
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_269
hc_iphlp_thunk_269:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 2152]
    test rax, rax
    jz .Llazy_269
    jmp rax
.Llazy_269:
    mov r10d, 269
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_270
hc_iphlp_thunk_270:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 2160]
    test rax, rax
    jz .Llazy_270
    jmp rax
.Llazy_270:
    mov r10d, 270
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_271
hc_iphlp_thunk_271:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 2168]
    test rax, rax
    jz .Llazy_271
    jmp rax
.Llazy_271:
    mov r10d, 271
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_272
hc_iphlp_thunk_272:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 2176]
    test rax, rax
    jz .Llazy_272
    jmp rax
.Llazy_272:
    mov r10d, 272
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_273
hc_iphlp_thunk_273:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 2184]
    test rax, rax
    jz .Llazy_273
    jmp rax
.Llazy_273:
    mov r10d, 273
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_274
hc_iphlp_thunk_274:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 2192]
    test rax, rax
    jz .Llazy_274
    jmp rax
.Llazy_274:
    mov r10d, 274
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_275
hc_iphlp_thunk_275:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 2200]
    test rax, rax
    jz .Llazy_275
    jmp rax
.Llazy_275:
    mov r10d, 275
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_276
hc_iphlp_thunk_276:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 2208]
    test rax, rax
    jz .Llazy_276
    jmp rax
.Llazy_276:
    mov r10d, 276
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_277
hc_iphlp_thunk_277:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 2216]
    test rax, rax
    jz .Llazy_277
    jmp rax
.Llazy_277:
    mov r10d, 277
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_278
hc_iphlp_thunk_278:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 2224]
    test rax, rax
    jz .Llazy_278
    jmp rax
.Llazy_278:
    mov r10d, 278
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_279
hc_iphlp_thunk_279:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 2232]
    test rax, rax
    jz .Llazy_279
    jmp rax
.Llazy_279:
    mov r10d, 279
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_280
hc_iphlp_thunk_280:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 2240]
    test rax, rax
    jz .Llazy_280
    jmp rax
.Llazy_280:
    mov r10d, 280
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_281
hc_iphlp_thunk_281:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 2248]
    test rax, rax
    jz .Llazy_281
    jmp rax
.Llazy_281:
    mov r10d, 281
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_282
hc_iphlp_thunk_282:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 2256]
    test rax, rax
    jz .Llazy_282
    jmp rax
.Llazy_282:
    mov r10d, 282
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_283
hc_iphlp_thunk_283:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 2264]
    test rax, rax
    jz .Llazy_283
    jmp rax
.Llazy_283:
    mov r10d, 283
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_284
hc_iphlp_thunk_284:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 2272]
    test rax, rax
    jz .Llazy_284
    jmp rax
.Llazy_284:
    mov r10d, 284
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_285
hc_iphlp_thunk_285:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 2280]
    test rax, rax
    jz .Llazy_285
    jmp rax
.Llazy_285:
    mov r10d, 285
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_286
hc_iphlp_thunk_286:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 2288]
    test rax, rax
    jz .Llazy_286
    jmp rax
.Llazy_286:
    mov r10d, 286
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_287
hc_iphlp_thunk_287:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 2296]
    test rax, rax
    jz .Llazy_287
    jmp rax
.Llazy_287:
    mov r10d, 287
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_288
hc_iphlp_thunk_288:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 2304]
    test rax, rax
    jz .Llazy_288
    jmp rax
.Llazy_288:
    mov r10d, 288
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_289
hc_iphlp_thunk_289:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 2312]
    test rax, rax
    jz .Llazy_289
    jmp rax
.Llazy_289:
    mov r10d, 289
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_290
hc_iphlp_thunk_290:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 2320]
    test rax, rax
    jz .Llazy_290
    jmp rax
.Llazy_290:
    mov r10d, 290
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_291
hc_iphlp_thunk_291:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 2328]
    test rax, rax
    jz .Llazy_291
    jmp rax
.Llazy_291:
    mov r10d, 291
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_292
hc_iphlp_thunk_292:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 2336]
    test rax, rax
    jz .Llazy_292
    jmp rax
.Llazy_292:
    mov r10d, 292
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_293
hc_iphlp_thunk_293:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 2344]
    test rax, rax
    jz .Llazy_293
    jmp rax
.Llazy_293:
    mov r10d, 293
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_294
hc_iphlp_thunk_294:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 2352]
    test rax, rax
    jz .Llazy_294
    jmp rax
.Llazy_294:
    mov r10d, 294
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_295
hc_iphlp_thunk_295:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 2360]
    test rax, rax
    jz .Llazy_295
    jmp rax
.Llazy_295:
    mov r10d, 295
    jmp hc_iphlp_lazy_common
.globl hc_iphlp_thunk_296
hc_iphlp_thunk_296:
    lea r11, [rip + g_iphlp_ptrs]
    mov rax, [r11 + 2368]
    test rax, rax
    jz .Llazy_296
    jmp rax
.Llazy_296:
    mov r10d, 296
    jmp hc_iphlp_lazy_common
