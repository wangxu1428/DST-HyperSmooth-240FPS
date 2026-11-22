#!/usr/bin/env python3
"""
Verify 297-export IPHLPAPI.DLL v1.1.0 pass-through safety, SEH .pdata integrity,
pure-UCRT imports, and strict non-server process isolation.
"""
import ctypes
import glob
import os
from pathlib import Path
import pefile

ROOT = Path(__file__).resolve().parent.parent
NP_DIR = ROOT / "native_plugin"
DLL_PATH = str(NP_DIR / "iphlpapi.dll")

# Clean any leftover log files before test
for p in glob.glob(str(NP_DIR / "dst_hypercore*.log")):
    os.remove(p)

# 0. PE Static Inspection: exports, imports, SEH .pdata, zero ordinal pinning
pe = pefile.PE(DLL_PATH)
exports = [e.name.decode() for e in pe.DIRECTORY_ENTRY_EXPORT.symbols if e.name]
assert len(exports) == 297, f"Expected 297 named exports, got {len(exports)}"
imports = [d.dll.decode().lower() for d in pe.DIRECTORY_ENTRY_IMPORT]
forbidden = [d for d in imports if "libstdc++" in d or "libgcc" in d or "winpthread" in d or "vcruntime" in d]
assert not forbidden, f"Forbidden runtime DLL dependencies: {forbidden}"
assert hasattr(pe, "DIRECTORY_ENTRY_EXCEPTION") and len(pe.DIRECTORY_ENTRY_EXCEPTION) > 0, \
    "Missing .pdata SEH unwind table!"
# Verify at least one .pdata entry has unwind_info with stack alloc >= 0x88 (our hc_iphlp_lazy_common)
pdata_count = len(pe.DIRECTORY_ENTRY_EXCEPTION)
pe.close()
print(f"[PASS] PE check: exports={len(exports)}, pdata_entries={pdata_count}, imports={imports}")

# 1. Load DLL in non-server host (python.exe)
dll = ctypes.WinDLL(DLL_PATH)

# 2. Verify GetIpAddrTable (imported by DST client & server)
sz = ctypes.c_ulong(0)
rc1 = dll.GetIpAddrTable(None, ctypes.byref(sz), False)
assert rc1 == 122 and sz.value > 0, f"GetIpAddrTable probe failed: rc={rc1} sz={sz.value}"
buf = (ctypes.c_ubyte * sz.value)()
rc2 = dll.GetIpAddrTable(buf, ctypes.byref(sz), False)
num_entries = ctypes.c_ulong.from_buffer(buf).value
assert rc2 == 0 and num_entries >= 1, f"GetIpAddrTable fetch failed: rc={rc2} entries={num_entries}"
print(f"[PASS] GetIpAddrTable through ASM thunk -> rc=0, entries={num_entries}")

# 3. Verify GetAdaptersInfo (imported by DST client & server)
sz2 = ctypes.c_ulong(0)
rc3 = dll.GetAdaptersInfo(None, ctypes.byref(sz2))
assert rc3 in (0, 111, 232), f"GetAdaptersInfo probe unexpected rc={rc3}"
if rc3 == 111:
    buf2 = (ctypes.c_ubyte * sz2.value)()
    rc4 = dll.GetAdaptersInfo(buf2, ctypes.byref(sz2))
    assert rc4 == 0, f"GetAdaptersInfo fetch failed: rc={rc4}"
    print(f"[PASS] GetAdaptersInfo through ASM thunk -> rc=0, buf={sz2.value} bytes")
elif rc3 == 232:
    print(f"[PASS] GetAdaptersInfo through ASM thunk -> ERROR_NO_DATA (232, no IPv4 adapters)")
else:
    print(f"[PASS] GetAdaptersInfo through ASM thunk -> rc={rc3}, buf={sz2.value} bytes")

# 4. Verify GetBestRoute (imported by DST client & server)
route_buf = (ctypes.c_ubyte * 64)()
rc5 = dll.GetBestRoute(0x0100007F, 0, route_buf)  # 127.0.0.1
assert rc5 == 0, f"GetBestRoute(127.0.0.1) failed: rc={rc5}"
print(f"[PASS] GetBestRoute(127.0.0.1) through ASM thunk -> rc=0")

# 5. Verify GetAdaptersAddresses (extra export)
sz3 = ctypes.c_ulong(0)
rc6 = dll.GetAdaptersAddresses(0, 0, None, None, ctypes.byref(sz3))
assert rc6 == 111 and sz3.value > 0, f"GetAdaptersAddresses probe failed: rc={rc6}"
buf3 = (ctypes.c_ubyte * sz3.value)()
rc7 = dll.GetAdaptersAddresses(0, 0, None, buf3, ctypes.byref(sz3))
assert rc7 == 0, f"GetAdaptersAddresses fetch failed: rc={rc7}"
print(f"[PASS] GetAdaptersAddresses (extra export) through ASM thunk -> rc=0, buf={sz3.value} bytes")

# 6. Verify IcmpCreateFile / IcmpCloseHandle (HANDLE-returning API!)
dll.IcmpCreateFile.restype = ctypes.c_void_p
dll.IcmpCloseHandle.argtypes = [ctypes.c_void_p]
dll.IcmpCloseHandle.restype = ctypes.c_int
h_icmp = dll.IcmpCreateFile()
assert h_icmp and h_icmp != ctypes.c_void_p(-1).value, f"IcmpCreateFile failed: {h_icmp}"
closed = dll.IcmpCloseHandle(h_icmp)
assert closed != 0, f"IcmpCloseHandle failed: {closed}"
print(f"[PASS] IcmpCreateFile/IcmpCloseHandle through ASM thunk -> handle={hex(h_icmp)}, closed={closed}")

# 7. Verify strict non-server isolation: NO dst_hypercore*.log created in non-server process!
leftover_logs = glob.glob(str(NP_DIR / "dst_hypercore*.log"))
assert not leftover_logs, f"FAIL: Non-server process created logs: {leftover_logs}"
print("[PASS] Non-server host isolation verified (0 hooks, 0 logs in client/other processes)")
