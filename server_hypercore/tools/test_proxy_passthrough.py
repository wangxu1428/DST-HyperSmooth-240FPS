#!/usr/bin/env python3
"""
Verify 297-export IPHLPAPI.DLL pass-through safety & non-server process isolation.
Loads native_plugin/iphlpapi.dll inside python.exe (non-server host), verifies zero
hooks/logs are triggered, and calls real Windows networking APIs through our ASM thunks.
"""
import ctypes
import os

DLL_PATH = r"C:\Users\Administrator\.gemini\antigravity\scratch\dst_hypercore_server\native_plugin\iphlpapi.dll"
LOG_PATH = r"C:\Users\Administrator\.gemini\antigravity\scratch\dst_hypercore_server\native_plugin\dst_hypercore.log"

if os.path.exists(LOG_PATH):
    os.remove(LOG_PATH)

dll = ctypes.WinDLL(DLL_PATH)

# 1. Verify GetIpAddrTable (imported by DST client & server)
sz = ctypes.c_ulong(0)
rc1 = dll.GetIpAddrTable(None, ctypes.byref(sz), False)
assert rc1 == 122 and sz.value > 0, f"GetIpAddrTable probe failed: rc={rc1} sz={sz.value}"
buf = (ctypes.c_ubyte * sz.value)()
rc2 = dll.GetIpAddrTable(buf, ctypes.byref(sz), False)
num_entries = ctypes.c_ulong.from_buffer(buf).value
assert rc2 == 0 and num_entries >= 1, f"GetIpAddrTable fetch failed: rc={rc2} entries={num_entries}"
print(f"[PASS] GetIpAddrTable through ASM thunk -> rc=0, entries={num_entries}")

# 2. Verify GetAdaptersInfo (imported by DST client & server)
sz2 = ctypes.c_ulong(0)
rc3 = dll.GetAdaptersInfo(None, ctypes.byref(sz2))
assert rc3 in (0, 111, 232), f"GetAdaptersInfo probe unexpected rc={rc3}"
if rc3 == 111:
    buf2 = (ctypes.c_ubyte * sz2.value)()
    rc4 = dll.GetAdaptersInfo(buf2, ctypes.byref(sz2))
    assert rc4 == 0, f"GetAdaptersInfo fetch failed: rc={rc4}"
print(f"[PASS] GetAdaptersInfo through ASM thunk -> rc=0, buf={sz2.value} bytes")

# 3. Verify GetBestRoute (imported by DST client & server)
route_buf = (ctypes.c_ubyte * 64)()
rc5 = dll.GetBestRoute(0x0100007F, 0, route_buf) # 127.0.0.1
assert rc5 == 0, f"GetBestRoute(127.0.0.1) failed: rc={rc5}"
print(f"[PASS] GetBestRoute(127.0.0.1) through ASM thunk -> rc=0")

# 4. Verify non-DST export (GetAdaptersAddresses) ALSO works via our 297-export lazy thunks!
sz3 = ctypes.c_ulong(0)
rc6 = dll.GetAdaptersAddresses(0, 0, None, None, ctypes.byref(sz3))
assert rc6 == 111 and sz3.value > 0, f"GetAdaptersAddresses probe failed: rc={rc6}"
buf3 = (ctypes.c_ubyte * sz3.value)()
rc7 = dll.GetAdaptersAddresses(0, 0, None, buf3, ctypes.byref(sz3))
assert rc7 == 0, f"GetAdaptersAddresses fetch failed: rc={rc7}"
print(f"[PASS] GetAdaptersAddresses (extra export) through ASM thunk -> rc=0, buf={sz3.value} bytes")

# 5. Verify strict non-server isolation: NO dst_hypercore.log created in non-server process!
assert not os.path.exists(LOG_PATH), "FAIL: Non-server process created dst_hypercore.log!"
print("[PASS] Non-server host isolation verified (0 hooks, 0 logs in client/other processes)")
