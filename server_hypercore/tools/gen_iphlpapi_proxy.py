#!/usr/bin/env python3
"""Generate a 100% transparent, loader-lock-safe, 297-export x86-64 IPHLPAPI.DLL proxy."""
import os
import struct

SYS_DLL = r"C:\Windows\System32\iphlpapi.dll"
OUT_DIR = r"C:\Users\Administrator\.gemini\antigravity\scratch\dst_hypercore_server\native_plugin"

def parse_exports(path):
    data = open(path, "rb").read()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    nsec = struct.unpack_from("<H", data, pe + 6)[0]
    osz = struct.unpack_from("<H", data, pe + 20)[0]
    so = pe + 24 + osz
    secs = [struct.unpack_from("<IIII", data, so + i * 40 + 8) for i in range(nsec)]

    def r2o(rva):
        for vsz, va, rsz, ra in secs:
            if va <= rva < va + max(vsz, rsz):
                return ra + (rva - va)
        return None

    erva = struct.unpack_from("<I", data, pe + 24 + 112)[0]
    eo = r2o(erva)
    base_ord = struct.unpack_from("<I", data, eo + 16)[0]
    num_funcs, num_names = struct.unpack_from("<II", data, eo + 20)
    names_rva, ords_rva = struct.unpack_from("<II", data, eo + 32)
    no = r2o(names_rva)
    oo = r2o(ords_rva)

    exports = []
    for i in range(num_names):
        nrva = struct.unpack_from("<I", data, no + i * 4)[0]
        ord_idx = struct.unpack_from("<H", data, oo + i * 2)[0]
        s_off = r2o(nrva)
        end = data.find(b"\0", s_off)
        name = data[s_off:end].decode("ascii")
        exports.append((name, base_ord + ord_idx))
    return exports

def main():
    exports = parse_exports(SYS_DLL)
    print(f"Parsed {len(exports)} named exports from {SYS_DLL}")

    # 1. iphlpapi.def
    with open(os.path.join(OUT_DIR, "iphlpapi.def"), "w", encoding="ascii", newline="\n") as f:
        f.write("LIBRARY iphlpapi\nEXPORTS\n")
        for idx, (name, ordinal) in enumerate(exports):
            f.write(f"    {name}=hc_iphlp_thunk_{idx} @{ordinal}\n")

    # 2. iphlpapi_names.h
    with open(os.path.join(OUT_DIR, "iphlpapi_names.h"), "w", encoding="ascii", newline="\n") as f:
        f.write("#pragma once\n#include <cstddef>\n\n")
        f.write(f"static constexpr size_t kIphlpExportCount = {len(exports)};\n")
        f.write("static const char* const kIphlpExportNames[kIphlpExportCount] = {\n")
        for name, _ in exports:
            f.write(f'    "{name}",\n')
        f.write("};\n")

    # 3. iphlpapi_thunks.S (GAS Intel syntax, x86_64 Windows ABI)
    # Fast path: 3 instructions, zero stack touch.
    # Cold path: saves all volatile GP+XMM arg registers, resolves System32\iphlpapi.dll lazily,
    # restores all registers verbatim, and tail-jumps to the real function.
    with open(os.path.join(OUT_DIR, "iphlpapi_thunks.S"), "w", encoding="ascii", newline="\n") as f:
        f.write(".intel_syntax noprefix\n")
        f.write(".extern g_iphlp_ptrs\n")
        f.write(".extern hc_iphlp_resolve\n\n")
        f.write(".text\n")
        f.write(".p2align 4\n")
        f.write("hc_iphlp_lazy_common:\n")
        # Entry: r10d = export index; rsp is 8 mod 16 (caller return address at [rsp])
        # Allocate 0x88 bytes (8 + 0x88 = 0x90 -> 16-byte aligned before call!)
        # Layout:
        #   [rsp+0x00..0x1F] shadow space (32 bytes)
        #   [rsp+0x20] rcx
        #   [rsp+0x28] rdx
        #   [rsp+0x30] r8
        #   [rsp+0x38] r9
        #   [rsp+0x40] xmm0
        #   [rsp+0x50] xmm1
        #   [rsp+0x60] xmm2
        #   [rsp+0x70] xmm3
        #   [rsp+0x80] r11 (padding/extra)
        f.write("    sub rsp, 0x88\n")
        f.write("    mov [rsp+0x20], rcx\n")
        f.write("    mov [rsp+0x28], rdx\n")
        f.write("    mov [rsp+0x30], r8\n")
        f.write("    mov [rsp+0x38], r9\n")
        f.write("    movdqu [rsp+0x40], xmm0\n")
        f.write("    movdqu [rsp+0x50], xmm1\n")
        f.write("    movdqu [rsp+0x60], xmm2\n")
        f.write("    movdqu [rsp+0x70], xmm3\n")
        f.write("    mov ecx, r10d\n")
        f.write("    call hc_iphlp_resolve\n")
        f.write("    mov rcx, [rsp+0x20]\n")
        f.write("    mov rdx, [rsp+0x28]\n")
        f.write("    mov r8,  [rsp+0x30]\n")
        f.write("    mov r9,  [rsp+0x38]\n")
        f.write("    movdqu xmm0, [rsp+0x40]\n")
        f.write("    movdqu xmm1, [rsp+0x50]\n")
        f.write("    movdqu xmm2, [rsp+0x60]\n")
        f.write("    movdqu xmm3, [rsp+0x70]\n")
        f.write("    add rsp, 0x88\n")
        f.write("    test rax, rax\n")
        f.write("    jz .Lfail_ret\n")
        f.write("    jmp rax\n")
        f.write(".Lfail_ret:\n")
        f.write("    mov eax, 127\n")  # ERROR_PROC_NOT_FOUND
        f.write("    ret\n\n")

        for idx, (name, _) in enumerate(exports):
            f.write(f".globl hc_iphlp_thunk_{idx}\n")
            f.write(f"hc_iphlp_thunk_{idx}:\n")
            f.write(f"    lea r11, [rip + g_iphlp_ptrs]\n")
            f.write(f"    mov rax, [r11 + {idx * 8}]\n")
            f.write(f"    test rax, rax\n")
            f.write(f"    jz .Llazy_{idx}\n")
            f.write(f"    jmp rax\n")
            f.write(f".Llazy_{idx}:\n")
            f.write(f"    mov r10d, {idx}\n")
            f.write(f"    jmp hc_iphlp_lazy_common\n")

    print("Generated iphlpapi.def, iphlpapi_names.h, and iphlpapi_thunks.S cleanly.")

if __name__ == "__main__":
    main()
