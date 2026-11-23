#!/usr/bin/env python3
"""
Phase 3 Linux ELF64 Reverse-Engineering & Verification Inspector
Inspects the real Linux DST Dedicated Server binary:
  dontstarve_dedicated_server_nullrenderer_x64 (Build 762172, PIE ET_DYN)
Extracts exact System V AMD64 function addresses, AOBs, prologue boundaries,
RIP-relative instructions, and GCC libstdc++ struct layouts.
"""
import os
import re
import struct
import sys
import capstone

DEFAULT_LINUX_ELF = (
    r"C:\Users\Administrator\.gemini\antigravity\brain"
    r"\e372e436-3502-4b49-9176-c7ea8760c6a4\scratch\dst_linux_ds\bin64"
    r"\dontstarve_dedicated_server_nullrenderer_x64"
)


class ElfImage:
    def __init__(self, path: str):
        with open(path, "rb") as f:
            self.data = f.read()
        assert self.data[:4] == b"\x7fELF" and self.data[4] == 2, "Not ELF64"
        (
            self.e_type,
            self.e_machine,
            self.e_version,
            self.e_entry,
            self.e_phoff,
            self.e_shoff,
            self.e_flags,
            self.e_ehsize,
            self.e_phentsize,
            self.e_phnum,
            self.e_shentsize,
            self.e_shnum,
            self.e_shstrndx,
        ) = struct.unpack_from("<HHIQQQIHHHHHH", self.data, 16)

        shstr_off, shstr_sz = struct.unpack_from(
            "<QQ", self.data, self.e_shoff + self.e_shstrndx * self.e_shentsize + 24
        )
        shstr = self.data[shstr_off : shstr_off + shstr_sz]
        self.secs = {}
        for i in range(self.e_shnum):
            (
                sh_name,
                sh_type,
                sh_flags,
                sh_addr,
                sh_offset,
                sh_size,
                sh_link,
                sh_info,
                sh_align,
                sh_entsize,
            ) = struct.unpack_from("<IIQQQQIIQQ", self.data, self.e_shoff + i * self.e_shentsize)
            end = shstr.find(b"\x00", sh_name)
            name = shstr[sh_name:end].decode(errors="replace")
            self.secs[name] = {
                "type": sh_type,
                "flags": sh_flags,
                "addr": sh_addr,
                "off": sh_offset,
                "size": sh_size,
                "entsize": sh_entsize,
            }

        self.text_va = self.secs[".text"]["addr"]
        self.text_off = self.secs[".text"]["off"]
        self.text_sz = self.secs[".text"]["size"]
        self.text_bytes = self.data[self.text_off : self.text_off + self.text_sz]

        self.ro_va = self.secs[".rodata"]["addr"]
        self.ro_off = self.secs[".rodata"]["off"]
        self.ro_sz = self.secs[".rodata"]["size"]
        self.ro_bytes = self.data[self.ro_off : self.ro_off + self.ro_sz]

    def cstr_va(self, va: int) -> str | None:
        if self.ro_va <= va < self.ro_va + self.ro_sz:
            off = self.ro_off + (va - self.ro_va)
            end = self.data.find(b"\x00", off, off + 256)
            if end != -1:
                raw = self.data[off:end]
                if len(raw) > 0 and all(32 <= b < 127 for b in raw):
                    return raw.decode("ascii")
        return None

    def find_str_va(self, s: bytes) -> int | None:
        idx = self.ro_bytes.find(s + b"\x00")
        return (self.ro_va + idx) if idx != -1 else None

    def in_text(self, va: int) -> bool:
        return self.text_va <= va < self.text_va + self.text_sz

    def text_slice(self, va: int, size: int) -> bytes:
        off = self.text_off + (va - self.text_va)
        return self.data[off : off + size]

    def ro_u32_refs(self, val: int) -> list[int]:
        pat = struct.pack("<I", val)
        out = []
        p = 0
        while True:
            p = self.ro_bytes.find(pat, p)
            if p == -1:
                break
            out.append(self.ro_va + p)
            p += 1
        return out

    def text_lea_refs(self, target_va: int) -> list[int]:
        hits = []
        tb = self.text_bytes
        for i in range(len(tb) - 7):
            if (tb[i] == 0x48 or tb[i] == 0x4C) and tb[i + 1] == 0x8D and (tb[i + 2] & 0xC7) == 0x05:
                disp = struct.unpack_from("<i", tb, i + 3)[0]
                if self.text_va + i + 7 + disp == target_va:
                    hits.append(self.text_va + i)
        return hits

    def text_u32_mov_refs(self, target_va: int, mask_rip: bool = True) -> list[int]:
        """Find rip-relative load/store referencing target_va anywhere in .text."""
        hits = []
        tb = self.text_bytes
        for i in range(len(tb) - 8):
            # check 4-byte disp32 pointing to target_va assuming insn ends at i+4
            disp = struct.unpack_from("<i", tb, i)[0]
            if self.text_va + i + 4 + disp == target_va:
                hits.append(self.text_va + i)
        return hits

    def find_func_start(self, addr_in_fn: int) -> int:
        # Walk backward looking for align padding (cc/90/66 0f 1f/ret+nop) using .eh_frame or simple int3/nop boundary
        if ".eh_frame_hdr" in self.secs:
            eh_addr = self.secs[".eh_frame_hdr"]["addr"]
            eh_off = self.secs[".eh_frame_hdr"]["off"]
            eh_sz = self.secs[".eh_frame_hdr"]["size"]
            ver, eh_ptr_enc, fde_enc, tbl_enc = self.data[eh_off : eh_off + 4]
            if ver == 1 and tbl_enc == 0x3B and (fde_enc in (0x03, 0x0B, 0x1B, 0x13)):
                # fde_enc is fde_count_enc (usually 0x03 = udata4)
                pos = eh_off + 4
                pos += 4  # eh_frame_ptr (sdata4 when eh_ptr_enc == 0x1B)
                fde_cnt = struct.unpack_from("<I", self.data, pos)[0]
                pos += 4
                lo, hi = 0, fde_cnt - 1
                best = 0
                while lo <= hi:
                    mid = (lo + hi) >> 1
                    init_rel = struct.unpack_from("<i", self.data, pos + mid * 8)[0]
                    fn_va = eh_addr + init_rel
                    if fn_va <= addr_in_fn:
                        best = fn_va
                        lo = mid + 1
                    else:
                        hi = mid - 1
                if best:
                    return best
        return 0

    def parse_reladyn(self) -> dict[int, int]:
        sec = self.secs.get(".rela.dyn")
        if not sec:
            return {}
        off, sz, esz = sec["off"], sec["size"], sec["entsize"] or 24
        rel = {}
        for p in range(off, off + sz, esz):
            r_offset, r_info, r_addend = struct.unpack_from("<QQq", self.data, p)
            if (r_info & 0xFFFFFFFF) == 8:  # R_X86_64_RELATIVE
                rel[r_offset] = r_addend
        return rel

    def dynsym_map(self) -> dict[str, int]:
        y = self.secs[".dynsym"]
        s = self.secs[".dynstr"]
        dynstr = self.data[s["off"] : s["off"] + s["size"]]
        out = {}
        for p in range(y["off"], y["off"] + y["size"], y["entsize"] or 24):
            st_name, _, _, _, st_value, _ = struct.unpack_from("<IBBHQQ", self.data, p)
            if st_value:
                end = dynstr.find(b"\x00", st_name)
                out[dynstr[st_name:end].decode(errors="replace")] = st_value
        return out


def disasm(elf: ElfImage, va: int, size: int = 128, label: str = ""):
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    print(f"\n=== {label} @ 0x{va:x} ({size}B) ===")
    for insn in md.disasm(elf.text_slice(va, size), va):
        note = ""
        m = re.search(r"rip ([+-]) (0x[0-9a-f]+)", insn.op_str)
        if m:
            d = int(m.group(2), 16) * (1 if m.group(1) == "+" else -1)
            tgt = insn.address + insn.size + d
            s = elf.cstr_va(tgt)
            if s is not None:
                note = f' ; "{s}"'
            else:
                note = f" ; 0x{tgt:x}"
        hx = " ".join(f"{b:02x}" for b in insn.bytes)
        print(f"  0x{insn.address:x} (+0x{insn.address-va:02x}, {insn.size:2d}B)  {hx:<26}  {insn.mnemonic} {insn.op_str}{note}")


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_LINUX_ELF
    elf = ElfImage(path)
    print(f"ELF64 type={elf.e_type} entry=0x{elf.e_entry:x} .text=0x{elf.text_va:x}..0x{elf.text_va+elf.text_sz:x}")
    rel = elf.parse_reladyn()
    print(f".rela.dyn R_X86_64_RELATIVE count={len(rel)}")

    # Walk consecutive pointer pairs in .rela.dyn to discover ALL luaL_Reg { name_ptr, func_ptr }
    wanted_lua_regs = {
        "FindEntities",
        "FindEntities_Registered",
        "SerializeWorldSession",
        "GetTickTime",
        "GetLightAtPoint",
        "SetPersistentString",
        "SetNetworkTickRate",
        "collectgarbage",
        "Dump",
    }
    lua_reg_hits = {}
    for off, name_va in sorted(rel.items()):
        if (off + 8) in rel:
            s = elf.cstr_va(name_va)
            if s and s in wanted_lua_regs:
                fn_va = rel[off + 8]
                if elf.in_text(fn_va):
                    lua_reg_hits.setdefault(s, []).append((off, fn_va))
    print("\n=== luaL_Reg entries via .rela.dyn ===")
    for k in sorted(lua_reg_hits):
        print(f"  {k:<25}: {[(hex(o), hex(fn)) for o, fn in lua_reg_hits[k]]}")

    # Test .eh_frame_hdr lookup
    for s in [b"Simulation::luaUpdate", b"Pathfinder", b"LightWatcher", b"not enough memory", b"PANIC: unprotected error"]:
        sva = elf.find_str_va(s)
        refs = elf.text_lea_refs(sva) if sva else []
        fns = [hex(elf.find_func_start(r)) for r in refs]
        print(f"STR {s.decode():<28} refs={[hex(r) for r in refs[:4]]} funcs={fns[:4]}")

    # Check 1/30 float (0x3d088889) and double (0x3fa1111111111111) references in .rodata -> .text!
    refs_f30 = elf.ro_u32_refs(0x3D088889)
    print(f"\n.rodata float 1/30 (0x3d088889) VAs: {[hex(x) for x in refs_f30]}")
    for va in refs_f30:
        trefs = elf.text_u32_mov_refs(va)
        print(f"  0x{va:x} -> text refs ({len(trefs)}): {[(hex(r), hex(elf.find_func_start(r))) for r in trefs[:10]]}")

    # Find Simulation::Update & Key functions cleanly
    if "FindEntities" in lua_reg_hits:
        fe_va = lua_reg_hits["FindEntities"][0][1]
        disasm(elf, fe_va, 220, "Lua_Sim_FindEntities")
    if "FindEntities_Registered" in lua_reg_hits:
        fer_va = lua_reg_hits["FindEntities_Registered"][0][1]
        disasm(elf, fer_va, 220, "Lua_Sim_FindEntities_Registered")
    if "SerializeWorldSession" in lua_reg_hits:
        ser_va = lua_reg_hits["SerializeWorldSession"][0][1]
        disasm(elf, ser_va, 96, "Lua_Sim_SerializeWorldSession")
    if "GetTickTime" in lua_reg_hits:
        gt_va = lua_reg_hits["GetTickTime"][0][1]
        disasm(elf, gt_va, 96, "Lua_Sim_GetTickTime")
    if "GetLightAtPoint" in lua_reg_hits:
        gl_va = lua_reg_hits["GetLightAtPoint"][0][1]
        disasm(elf, gl_va, 200, "Lua_Sim_GetLightAtPoint")


if __name__ == "__main__":
    main()
