#!/usr/bin/env python3
"""
Phase 3 Linux ELF64 + Cross-Binary Static & Capstone Verification Suite.
Verifies:
  1. native_plugin/linux/libhypercore.so:
     - Valid ELF64 ET_DYN EM_X86_64 shared library
     - DT_NEEDED forbids libstdc++.so* and libgcc_s.so*
     - GNU .gnu.version_r VERNEED max GLIBC version <= 2.17
     - .dynsym defines ONLY the 19 expected LD_PRELOAD interposition symbols
  2. native_plugin/linux/hypercore_linux.cpp against real Linux Build 762172 ELF:
     - Every kAob* byte array matches 1-of-1 in .text
     - Every InstallHookSysV stolenLen >= 14 ends on an exact instruction boundary
     - Every RIP-relative instruction inside stolen prologues is covered by RelocLea{off}
     - AStar::Step+0x3cc call target is PathVec::_M_realloc_insert (endbr64 + movabs rcx)
     - All 4 SpatialGrid patch offsets align cleanly on original instruction boundaries
  3. tools/linux_start_with_hypercore.sh:
     - Pure LF line endings (zero CR bytes), valid bash shebang, LD_PRELOAD setup
"""
import os
import re
import struct
import sys
import capstone

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SO_PATH = os.path.join(ROOT, "native_plugin", "linux", "libhypercore.so")
CPP_PATH = os.path.join(ROOT, "native_plugin", "linux", "hypercore_linux.cpp")
SH_PATH = os.path.join(ROOT, "tools", "linux_start_with_hypercore.sh")
sys.path.insert(0, os.path.join(ROOT, "tools"))
from inspect_linux_elf import ElfImage, DEFAULT_LINUX_ELF

EXPECTED_EXPORTS = {
    "fopen", "fopen64", "fwrite", "fclose",
    "open", "open64",
    "__xstat", "__xstat64", "stat", "stat64", "access",
    "opendir", "mkdir", "rmdir", "unlink", "remove", "rename",
    "exit", "_exit",
}


def read_cstr(buf: bytes, off: int) -> str:
    end = buf.find(b"\x00", off)
    return buf[off:end].decode("utf-8", errors="replace") if end != -1 else ""


def verify_libhypercore_so():
    assert os.path.isfile(SO_PATH), f"Missing {SO_PATH}; run tools/build_linux.ps1 first"
    so = ElfImage(SO_PATH)
    assert so.e_type == 3, f"Expected ET_DYN (3), got {so.e_type}"
    assert so.e_machine == 62, f"Expected EM_X86_64 (62), got {so.e_machine}"

    dynstr_sec = so.secs[".dynstr"]
    dynstr = so.data[dynstr_sec["off"] : dynstr_sec["off"] + dynstr_sec["size"]]

    # 1. DT_NEEDED
    dyn_sec = so.secs[".dynamic"]
    needed = []
    for p in range(dyn_sec["off"], dyn_sec["off"] + dyn_sec["size"], 16):
        d_tag, d_val = struct.unpack_from("<qQ", so.data, p)
        if d_tag == 0:
            break
        if d_tag == 1:  # DT_NEEDED
            needed.append(read_cstr(dynstr, d_val))
    forbidden = [n for n in needed if "libstdc++" in n or "libgcc_s" in n or "libc++" in n]
    assert not forbidden, f"Forbidden C++ runtime DT_NEEDED in libhypercore.so: {forbidden}"
    print(f"[PASS] SO ELF64 ET_DYN EM_X86_64 size={len(so.data)}B DT_NEEDED={needed}")

    # 2. GLIBC version ceiling <= 2.17
    glibc_vers = []
    if ".gnu.version_r" in so.secs:
        vr = so.secs[".gnu.version_r"]
        pos = vr["off"]
        end = vr["off"] + vr["size"]
        while pos < end:
            vn_version, vn_cnt, vn_file, vn_aux, vn_next = struct.unpack_from("<HHIII", so.data, pos)
            apos = pos + vn_aux
            for _ in range(vn_cnt):
                vna_hash, vna_flags, vna_other, vna_name, vna_next = struct.unpack_from("<IHHII", so.data, apos)
                vstr = read_cstr(dynstr, vna_name)
                m = re.match(r"^GLIBC_(\d+)\.(\d+)(?:\.(\d+))?$", vstr)
                if m:
                    tup = tuple(int(x or 0) for x in m.groups())
                    glibc_vers.append((vstr, tup))
                    assert tup[:2] <= (2, 17), f"Symbol version exceeds GLIBC 2.17: {vstr}"
                if vna_next == 0:
                    break
                apos += vna_next
            if vn_next == 0:
                break
            pos += vn_next
    max_glibc = max([v[1] for v in glibc_vers], default=(0, 0, 0))
    print(f"[PASS] GLIBC version requirements <= 2.17 (count={len(glibc_vers)}, max={max_glibc}): {[v[0] for v in glibc_vers]}")

    # 3. .dynsym defined exports == exact 19 LD_PRELOAD symbols
    dynsym_sec = so.secs[".dynsym"]
    defined_exports = set()
    for p in range(dynsym_sec["off"], dynsym_sec["off"] + dynsym_sec["size"], dynsym_sec["entsize"] or 24):
        st_name, st_info, st_other, st_shndx, st_value, st_size = struct.unpack_from("<IBBHQQ", so.data, p)
        bind = st_info >> 4
        if st_shndx != 0 and bind in (1, 2):  # STB_GLOBAL or STB_WEAK
            nm = read_cstr(dynstr, st_name)
            if nm and not nm.startswith("__"):
                defined_exports.add(nm)
            elif nm in ("__xstat", "__xstat64"):
                defined_exports.add(nm)
    assert defined_exports == EXPECTED_EXPORTS, (
        f"Export mismatch! missing={sorted(EXPECTED_EXPORTS - defined_exports)}, "
        f"unexpected={sorted(defined_exports - EXPECTED_EXPORTS)}"
    )
    print(f"[PASS] .dynsym strict whitelist verified ({len(defined_exports)}/19 symbols, zero leaks)")


def verify_cpp_against_real_dst_elf():
    assert os.path.isfile(DEFAULT_LINUX_ELF), f"Missing real Linux Build 762172 ELF at {DEFAULT_LINUX_ELF}"
    elf = ElfImage(DEFAULT_LINUX_ELF)
    tb = elf.text_bytes
    src = open(CPP_PATH, "r", encoding="utf-8").read()
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

    # 1. Verify EVERY kAob* array in hypercore_linux.cpp matches 1-of-1 in real ELF .text
    aob_map = {}
    for m in re.finditer(r"constexpr\s+uint8_t\s+(kAob\w+)\[\]\s*=\s*\{([^}]+)\};", src, re.S):
        name = m.group(1)
        bs = bytes(int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]{2})", m.group(2)))
        cnt = tb.count(bs)
        assert cnt == 1, f"{name} matched {cnt} times in real Linux ELF .text (expected 1)!"
        off = tb.index(bs)
        aob_map[name] = elf.text_va + off
    assert len(aob_map) >= 54, f"Expected >=54 AOBs parsed, got {len(aob_map)}"
    print(f"[PASS] All {len(aob_map)}/{len(aob_map)} kAob* arrays in hypercore_linux.cpp match 1-of-1 in real Linux Build 762172 ELF")

    var_to_aob = {}
    for m in re.finditer(r"uint8_t\*\s+(\w+)\s*=\s*FindUniqueAob\((kAob\w+)\)", src):
        var_to_aob[m.group(1)] = aob_map[m.group(2)]

    hook_count = 0
    for m in re.finditer(
        r"InstallHookSysV\(\s*(FindUniqueAob\((kAob\w+)\)|(\w+))\s*,\s*[^;]+?\);",
        src,
        re.S,
    ):
        call_txt = m.group(0)
        aob_name = m.group(2)
        var_name = m.group(3)
        tail = re.search(r",\s*(\d+)\s*,\s*reinterpret_cast<void\*\*>\([^)]+\)(?:,\s*&(\w+)\s*,\s*(\d+))?\s*\);", call_txt)
        assert tail, f"Unparsed InstallHookSysV call: {call_txt}"
        stolen = int(tail.group(1))
        rel_var = tail.group(2)
        va = aob_map[aob_name] if aob_name else var_to_aob[var_name]
        declared_rels = set()
        if rel_var:
            ctx = src[max(0, m.start() - 800) : m.start()]
            rm = list(re.finditer(rf"\b{re.escape(rel_var)}\s*\{{([^}}]+)\}}", ctx))
            assert rm, f"Could not find RelocLea declaration for {rel_var} before {call_txt}"
            declared_rels = {int(x) for x in re.findall(r"\d+", rm[-1].group(1))}

        assert stolen >= 14, f"Stolen len < 14 at 0x{va:x}"
        insns = list(md.disasm(elf.text_slice(va, stolen + 16), va))
        acc = 0
        actual_rips = {}
        exact_end = False
        for ins in insns:
            off_in_pro = ins.address - va
            if "rip" in ins.op_str and off_in_pro < stolen:
                actual_rips[off_in_pro] = (ins.size, ins.bytes[:2], ins.mnemonic, ins.op_str)
            acc += ins.size
            if acc == stolen:
                exact_end = True
                break
            if acc > stolen:
                break
        assert exact_end, f"Hook at 0x{va:x} cuts mid-instruction at stolenLen={stolen}!"
        assert set(actual_rips.keys()) == declared_rels, (
            f"Reloc mismatch at 0x{va:x}: actual={actual_rips}, declared={declared_rels}"
        )
        for roff, (rsz, prefix, mnem, op) in actual_rips.items():
            assert rsz == 7 and prefix == b"\x48\x8d", (
                f"Expected 7B `48 8D` lea [rip+disp32] at 0x{va:x}+{roff}, got {mnem} {op}"
            )
        hook_count += 1
    assert hook_count >= 19, f"Expected >=19 InstallHookSysV hooks verified, got {hook_count}"
    print(f"[PASS] Capstone verified {hook_count}/{hook_count} InstallHookSysV prologues (exact boundaries & movabs-safe RelocLea)")

    # 3. Verify AStar::Step+0x3CC -> PathVec::_M_realloc_insert decode
    va_step = aob_map["kAobAStarStep"]
    call_bytes = elf.text_slice(va_step + 0x3CC, 5)
    assert call_bytes[0] == 0xE8, f"Expected E8 at AStar::Step+0x3CC, got {call_bytes.hex()}"
    disp = struct.unpack_from("<i", call_bytes, 1)[0]
    tgt = va_step + 0x3D1 + disp
    tgt_head = elf.text_slice(tgt, 14)
    assert tgt_head[:4] == b"\xf3\x0f\x1e\xfa" and tgt_head[4:6] == b"\x48\xb9", (
        f"Unexpected PathVec::_M_realloc_insert target 0x{tgt:x}: {tgt_head.hex()}"
    )
    print(f"[PASS] AStar::Step+0x3CC -> PathVec::_M_realloc_insert @ 0x{tgt:x} verified")

    # 4. Verify all 4 SpatialGrid patch anchor + delete boundaries cleanly in ELF
    spatial_checks = [
        ("FindEntities",            "kAobSpatialAnchorFindEnts", 6, 50, 11, ["kAobSpatialDelFindEnts"]),
        ("FindEntities_Registered", "kAobSpatialAnchorFindReg",  6, 50, 11, ["kAobSpatialDelFindReg"]),
        ("LightWatcher_Intensity",  "kAobSpatialAnchorLightInt", 6, 49, 11, ["kAobSpatialDel1LightInt", "kAobSpatialDel2LightInt"]),
        ("LightWatcher_RGB",        "kAobSpatialAnchorLightRGB", 6, 46,  5, ["kAobSpatialDelLightRGB"]),
    ]
    for nm, anc_key, alloc_off, mv_off, mv_len, del_keys in spatial_checks:
        va_anc = aob_map[anc_key]
        # Verify alloc_copy (22B) is exact instructions: call(5) + mov(4) + mov(3) + mov(4) + mov(3) + sub(3) == 22B
        ins_alloc = list(md.disasm(elf.text_slice(va_anc + alloc_off, 22), va_anc + alloc_off))
        assert sum(i.size for i in ins_alloc) == 22, f"{nm} alloc_copy size mismatch"
        assert ins_alloc[0].mnemonic == "call", f"{nm} expected call KleiNew at +{alloc_off}"
        # Verify memmove span ends cleanly on instruction boundary
        ins_mv = list(md.disasm(elf.text_slice(va_anc + mv_off, mv_len), va_anc + mv_off))
        assert sum(i.size for i in ins_mv) == mv_len and ins_mv[-1].mnemonic == "call", f"{nm} memmove mismatch"
        for dk in del_keys:
            va_d = aob_map[dk]
            ins_d = list(md.disasm(elf.text_slice(va_d + 8, 5), va_d + 8))
            assert len(ins_d) == 1 and ins_d[0].size == 5 and ins_d[0].mnemonic == "call", f"{nm} {dk} delete mismatch"
    print("[PASS] All 4/4 SpatialGrid Zero-Copy in-place W^X patch sites verified via Capstone")


def verify_start_script():
    assert os.path.isfile(SH_PATH), f"Missing {SH_PATH}"
    raw = open(SH_PATH, "rb").read()
    assert b"\r" not in raw, "FAIL: tools/linux_start_with_hypercore.sh contains CR (\\r); must be pure LF!"
    txt = raw.decode("utf-8")
    assert txt.startswith("#!/usr/bin/env bash\n"), "Missing #!/usr/bin/env bash shebang"
    assert "LD_PRELOAD=" in txt and "libhypercore.so" in txt, "Missing LD_PRELOAD logic"
    # Quick balanced quote check across lines
    assert txt.count('"') % 2 == 0, "Unbalanced double quotes in bash script"
    print(f"[PASS] {os.path.basename(SH_PATH)} verified pure LF ({len(raw)}B, 0 CRs)")


def main():
    verify_libhypercore_so()
    verify_cpp_against_real_dst_elf()
    verify_start_script()
    print("\nALL LINUX ELF64 & CROSS-BINARY CAPSTONE VERIFICATION CHECKS PASSED!")


if __name__ == "__main__":
    main()
