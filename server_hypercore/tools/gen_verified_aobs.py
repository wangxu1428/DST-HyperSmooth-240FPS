#!/usr/bin/env python3
"""Extract and verify UNIQUE (count == 1) cross-version AOB patterns for DST Server."""
import struct
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

EXE_PATH = r"D:\steam\steamapps\common\Don't Starve Together\bin64\dontstarve_dedicated_server_nullrenderer_x64.exe"

data = open(EXE_PATH, "rb").read()
pe_off = struct.unpack_from("<I", data, 0x3C)[0]
num_sec = struct.unpack_from("<H", data, pe_off + 6)[0]
opt_sz = struct.unpack_from("<H", data, pe_off + 20)[0]
image_base = struct.unpack_from("<Q", data, pe_off + 24 + 24)[0]
sec_off = pe_off + 24 + opt_sz
secs = []
for i in range(num_sec):
    s = sec_off + i * 40
    name = data[s:s+8].rstrip(b"\0").decode(errors="ignore")
    vsz, va, rsz, ra = struct.unpack_from("<IIII", data, s + 8)
    secs.append((name, va, vsz, ra, rsz))

text = [s for s in secs if s[0] == ".text"][0]
tb = data[text[3]:text[3]+text[4]]
text_base = image_base + text[1]

def va2off(va):
    rva = va - image_base
    for _, sva, vsz, ra, rsz in secs:
        if sva <= rva < sva + max(vsz, rsz):
            return ra + (rva - sva)
    return None

md = Cs(CS_ARCH_X86, CS_MODE_64)
md.detail = True

def make_wildcard_aob(va, max_len=40):
    """Build a version-resilient AOB starting at `va` that wildcards RIP-relative/rel32 displacements."""
    o = va2off(va)
    chunk = data[o:o+max_len+16]
    pat = []
    mask = []
    for ins in md.disasm(chunk, va):
        b = bytes(ins.bytes)
        wc = [False] * len(b)
        # Wildcard call/jmp rel32 (E8 / E9 / 0F 8x) and RIP-relative disp32
        if ins.disp_offset > 0 and ins.disp_size == 4:
            # check if RIP-relative (mod==00, rm==101)
            if "[rip" in ins.op_str:
                for k in range(ins.disp_offset, ins.disp_offset + 4):
                    wc[k] = True
        if b[0] in (0xE8, 0xE9) and len(b) == 5:
            for k in range(1, 5):
                wc[k] = True
        if len(b) == 6 and b[0] == 0x0F and (0x80 <= b[1] <= 0x8F):
            for k in range(2, 6):
                wc[k] = True
        for i, byte in enumerate(b):
            pat.append(0x00 if wc[i] else byte)
            mask.append(0 if wc[i] else 1)
        if len(pat) >= max_len:
            break
    return pat[:max_len], mask[:max_len]

def scan(pat, mask):
    n = len(pat)
    hits = []
    # Find first fixed byte for fast prefilter
    first_fixed = next(i for i, m in enumerate(mask) if m == 1)
    fb = pat[first_fixed]
    pos = first_fixed
    limit = len(tb) - n
    while pos <= limit + first_fixed:
        idx = tb.find(fb, pos)
        if idx == -1 or idx - first_fixed > limit:
            break
        base = idx - first_fixed
        ok = True
        for k in range(n):
            if mask[k] and tb[base + k] != pat[k]:
                ok = False
                break
        if ok:
            hits.append(text_base + base)
        pos = idx + 1
    return hits

TARGETS = [
    ("Simulation_Update", 0x1400FC8F0, 32),
    ("Simulation_CollectGarbage", 0x1400F4B20, 44),
    ("Simulation_LuaAlloc", 0x1400F8AE0, 36),
    ("WorldGen_LuaAlloc", 0x14011C100, 36),
    ("EntityManager_FindEntities", 0x140116D30, 40),
    ("SpatialGrid_Query", 0x140114970, 40),
    ("SpatialGrid_EntityVisitor", 0x1401140D0, 40),
    ("Sim_FindEntities_Lua", 0x140127CF0, 40),
    ("Sim_FindEntities_Registered_Lua", 0x140127B00, 40),
    ("SerializeWorldSession_Lua", 0x1401CA8E0, 44),
    ("zlib_compress2", 0x1403582C0, 44),
    ("lua_newstate", 0x140422B70, 36),
    ("lua_gc", 0x14041C8B0, 36),
    ("SetGcBudget", 0x14041FAD0, 9), # Note: small func, will resolve via call inside CollectGarbage!
    ("lua_checkstack", 0x14041B610, 32),
    ("lua_gettop", 0x14041B790, 13),
    ("lua_settop", 0x14041B7A0, 32),
    ("lua_pushvalue", 0x14041B980, 32),
    ("lua_type", 0x14041B9B0, 32),
    ("lua_tonumber", 0x14041BBA0, 32),
    ("lua_toboolean", 0x14041BC10, 32),
    ("lua_tolstring", 0x14041BC40, 36),
    ("lua_pushnil", 0x14041BE70, 17),
    ("lua_pushnumber", 0x14041BE90, 21),
    ("lua_pushlstring", 0x14041BED0, 36),
    ("lua_pushstring", 0x14041BF40, 32),
    ("lua_pushcclosure", 0x14041C040, 36),
    ("lua_pushboolean", 0x14041C120, 28),
    ("lua_getfield", 0x14041C1C0, 36),
    ("lua_createtable", 0x14041C2B0, 36),
    ("lua_setfield", 0x14041C430, 36),
    ("lua_next", 0x14041CA80, 36),
]

print(f"Verifying {len(TARGETS)} cross-version AOBs against {EXE_PATH}:\n")
all_ok = True
for name, va, length in TARGETS:
    pat, mask = make_wildcard_aob(va, length)
    hits = scan(pat, mask)
    status = "OK (UNIQUE)" if hits == [va] else f"FAIL ({len(hits)} hits: {[hex(h) for h in hits[:4]]})"
    if hits != [va] and name != "SetGcBudget":
        all_ok = False
    p_str = " ".join(f"{b:02X}" if m else "??" for b, m in zip(pat, mask))
    print(f"  {name:32s} @ 0x{va:X}  [{status}]")
    print(f"      {p_str}")

print("\nALL_OK =", all_ok)
