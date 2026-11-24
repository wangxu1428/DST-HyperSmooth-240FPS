#!/usr/bin/env python3
"""
Standalone C++20 Unit Verifier for spike_blackbox.h across Client & Server repos.
Tests:
  1. Byte-for-byte SHA-256 parity between client and server copies of spike_blackbox.h
  2. 64-slot Ring wrap-around (150 pushes -> total=150, count=64, newest-first order)
  3. All ClientCulprit branches + anim/host non-double-counting in otherUs
  4. All ServerCulprit branches + max(net, replica) non-double-counting in otherUs
  5. Valid JSON emission + tight-buffer graceful clamping (count matches emitted objects, valid closed JSON)
"""
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

CLIENT_HDR = Path(r"C:\Users\Administrator\.gemini\antigravity\scratch\dst_fps_unlocker\native_plugin\spike_blackbox.h")
SERVER_HDR = Path(r"C:\Users\Administrator\.gemini\antigravity\scratch\dst_hypercore_server\native_plugin\spike_blackbox.h")

CPP_HARNESS = r"""
#include <cassert>
#include <cstdio>
#include <cstring>
#include "spike_blackbox.h"

int main() {
    using namespace hs_blackbox;

    // 1. Ring wrap-around test: push 150 -> total 150, count 64, newest-first
    Ring<ClientEvent> ring;
    for (uint32_t i = 1; i <= 150; ++i) {
        ClientEvent ev{};
        ev.epochMs = 1795440000000ULL + i;
        ev.frameSeq = i;
        ev.frameUs = 30000u + i;
        ev.cpuBusyUs = 25000u;
        ev.luaUs = 20000u;
        ComputeClientDerived(ev);
        ring.Push(ev);
    }
    ClientEvent out[kCap]{};
    uint64_t total = 0;
    size_t n = ring.SnapshotLatest(out, kCap, total);
    assert(total == 150);
    assert(n == kCap);
    assert(out[0].frameSeq == 150);
    assert(out[kCap - 1].frameSeq == 87);

    // 2. Non-double-counting checks & all Client culprits
    ClientEvent cCases[9]{};

    // CLIENT_LUA (also verify hostUs inside luaUs and animUs inside worldUs do not deflate/double-subtract otherUs)
    cCases[0].frameUs = 50000; cCases[0].cpuBusyUs = 48000; cCases[0].luaUs = 25000; cCases[0].hostUs = 4000;
    cCases[0].worldUs = 10000; cCases[0].animUs = 8000; cCases[0].triggers = kTrigFrame;
    ComputeClientDerived(cCases[0]);
    assert(cCases[0].otherUs == 13000u); // 48000 - (25000 + 10000)
    assert(cCases[0].culprit == static_cast<uint8_t>(ClientCulprit::ClientLua));

    // CLIENT_GC (emergency full GC forces ClientGc even if time small, plus ratio GC)
    cCases[1].frameUs = 40000; cCases[1].cpuBusyUs = 38000; cCases[1].gcUs = 20000; cCases[1].gcFull = 1;
    cCases[1].triggers = kTrigFrame;
    ComputeClientDerived(cCases[1]);
    assert(cCases[1].culprit == static_cast<uint8_t>(ClientCulprit::ClientGc));

    // GPU_WAIT_OR_SWAP
    cCases[2].frameUs = 45000; cCases[2].cpuBusyUs = 10000; cCases[2].gpuDrainUs = 12000; cCases[2].swapUs = 18000;
    cCases[2].triggers = kTrigFrame | kTrigGpu;
    ComputeClientDerived(cCases[2]);
    assert(cCases[2].culprit == static_cast<uint8_t>(ClientCulprit::GpuWaitOrSwap));

    // CLIENT_IO_HOST
    cCases[3].frameUs = 40000; cCases[3].cpuBusyUs = 39000; cCases[3].texUs = 12000; cCases[3].unzUs = 6000;
    cCases[3].triggers = kTrigFrame;
    ComputeClientDerived(cCases[3]);
    assert(cCases[3].culprit == static_cast<uint8_t>(ClientCulprit::ClientIoHost));

    // CLIENT_RENDER_SUBMIT
    cCases[4].frameUs = 40000; cCases[4].cpuBusyUs = 38000; cCases[4].worldUs = 18000; cCases[4].postUs = 6000;
    cCases[4].triggers = kTrigFrame;
    ComputeClientDerived(cCases[4]);
    assert(cCases[4].culprit == static_cast<uint8_t>(ClientCulprit::ClientRenderSubmit));

    // CLIENT_NET_CPU
    cCases[5].frameUs = 40000; cCases[5].cpuBusyUs = 35000; cCases[5].netCpuUs = 20000;
    cCases[5].triggers = kTrigFrame;
    ComputeClientDerived(cCases[5]);
    assert(cCases[5].culprit == static_cast<uint8_t>(ClientCulprit::ClientNetCpu));

    // CLIENT_CPU_OTHER
    cCases[6].frameUs = 40000; cCases[6].cpuBusyUs = 38000; cCases[6].luaUs = 4000;
    cCases[6].triggers = kTrigFrame;
    ComputeClientDerived(cCases[6]);
    assert(cCases[6].otherUs == 34000u);
    assert(cCases[6].culprit == static_cast<uint8_t>(ClientCulprit::ClientCpuOther));

    // NET_SPIKE (triggers == kTrigNet)
    cCases[7].frameUs = 4166; cCases[7].cpuBusyUs = 1200; cCases[7].pingAvgMs = 30; cCases[7].pingLastMs = 210;
    cCases[7].lossX10 = 35; cCases[7].triggers = kTrigNet;
    ComputeClientDerived(cCases[7]);
    assert(cCases[7].culprit == static_cast<uint8_t>(ClientCulprit::NetSpike));

    // MIXED (30% Lua, 30% World, 20% Swap, 20% Other -> none hits threshold)
    cCases[8].frameUs = 50000; cCases[8].cpuBusyUs = 45000; cCases[8].luaUs = 15000;
    cCases[8].worldUs = 12000; cCases[8].swapUs = 10000; cCases[8].triggers = kTrigFrame;
    ComputeClientDerived(cCases[8]);
    assert(cCases[8].culprit == static_cast<uint8_t>(ClientCulprit::Mixed));

    // 3. All Server culprits + max(net, replica) non-double-counting
    ServerEvent sCases[9]{};

    // SRV_LUA
    sCases[0].totalUs = 50000; sCases[0].luaUs = 32000; sCases[0].netUs = 5000; sCases[0].replicaUs = 3000;
    ComputeServerDerived(sCases[0]);
    assert(sCases[0].otherUs == 13000u); // 50000 - (32000 + max(5000,3000))
    assert(sCases[0].culprit == static_cast<uint8_t>(ServerCulprit::SrvLua));

    // SRV_GC
    sCases[1].totalUs = 45000; sCases[1].gcUs = 25000; sCases[1].gcFull = 1;
    ComputeServerDerived(sCases[1]);
    assert(sCases[1].culprit == static_cast<uint8_t>(ServerCulprit::SrvGc));

    // SRV_NET
    sCases[2].totalUs = 50000; sCases[2].netUs = 28000; sCases[2].replicaUs = 4000;
    ComputeServerDerived(sCases[2]);
    assert(sCases[2].culprit == static_cast<uint8_t>(ServerCulprit::SrvNet));

    // SRV_REPLICA (precedes SrvNet when replica >= 35%)
    sCases[3].totalUs = 50000; sCases[3].netUs = 30000; sCases[3].replicaUs = 24000;
    ComputeServerDerived(sCases[3]);
    assert(sCases[3].culprit == static_cast<uint8_t>(ServerCulprit::SrvReplica));

    // SRV_SPATIAL (precedes SrvLua even when luaUs is also high because spatial is inside luaUpdate)
    sCases[4].totalUs = 60000; sCases[4].luaUs = 50000; sCases[4].findEntUs = 25000;
    ComputeServerDerived(sCases[4]);
    assert(sCases[4].culprit == static_cast<uint8_t>(ServerCulprit::SrvSpatial));

    // SRV_ASTAR (precedes SrvLua)
    sCases[5].totalUs = 60000; sCases[5].luaUs = 48000; sCases[5].astarUs = 26000;
    ComputeServerDerived(sCases[5]);
    assert(sCases[5].culprit == static_cast<uint8_t>(ServerCulprit::SrvAStar));

    // SRV_SAVE_IO (precedes SrvLua)
    sCases[6].totalUs = 80000; sCases[6].luaUs = 70000; sCases[6].saveUs = 40000;
    ComputeServerDerived(sCases[6]);
    assert(sCases[6].culprit == static_cast<uint8_t>(ServerCulprit::SrvSaveIo));

    // SRV_OTHER_OR_OS
    sCases[7].totalUs = 60000; sCases[7].luaUs = 10000; sCases[7].gcUs = 2000;
    ComputeServerDerived(sCases[7]);
    assert(sCases[7].otherUs == 48000u);
    assert(sCases[7].culprit == static_cast<uint8_t>(ServerCulprit::SrvOtherOrOs));

    // SRV_MIXED
    sCases[8].totalUs = 50000; sCases[8].luaUs = 15000; sCases[8].netUs = 12000; sCases[8].gcUs = 10000;
    ComputeServerDerived(sCases[8]);
    assert(sCases[8].culprit == static_cast<uint8_t>(ServerCulprit::Mixed));

    // 4. Emit full JSON + tight-buffer truncated JSON
    char bufC[32768]{};
    size_t wC = 0;
    FormatClientJson(bufC, sizeof(bufC), cCases, 9, 150, 25, &wC);
    assert(wC == 9);

    char bufS[32768]{};
    size_t wS = 0;
    FormatServerJson(bufS, sizeof(bufS), sCases, 9, 9, 33, &wS);
    assert(wS == 9);

    char tightBuf[1200]{};
    size_t wTight = 0;
    FormatClientJson(tightBuf, sizeof(tightBuf), out, kCap, total, 25, &wTight);
    assert(wTight >= 1 && wTight < kCap);

    std::printf("---CLIENT_JSON---\n%s\n---SERVER_JSON---\n%s\n---TIGHT_JSON---\n%s\n---DONE---\n",
                bufC, bufS, tightBuf);
    return 0;
}
"""


def find_zig():
    z = os.environ.get("ZIG_EXE") or shutil.which("zig")
    if z:
        return z
    cand = Path(r"D:\环境\C++ 编译器\ziglang\zig.exe")
    if cand.is_file():
        return str(cand)
    raise RuntimeError("zig compiler not found")


def main():
    assert CLIENT_HDR.is_file() and SERVER_HDR.is_file(), "Missing spike_blackbox.h"
    h_cli = hashlib.sha256(CLIENT_HDR.read_bytes()).hexdigest()
    h_srv = hashlib.sha256(SERVER_HDR.read_bytes()).hexdigest()
    assert h_cli == h_srv, f"spike_blackbox.h mismatch: client={h_cli} vs server={h_srv}"
    print(f"[PASS] Header SHA-256 parity verified: {h_cli[:16]}...")

    zig = find_zig()
    env = os.environ.copy()
    if "ZIG_GLOBAL_CACHE_DIR" not in env and Path(r"D:\环境\C++ 编译器").is_dir():
        env["ZIG_GLOBAL_CACHE_DIR"] = r"D:\环境\C++ 编译器\zig-cache"
        env["ZIG_LOCAL_CACHE_DIR"] = r"D:\环境\C++ 编译器\zig-local-cache"

    with tempfile.TemporaryDirectory() as td:
        cpp_path = Path(td) / "test_bb.cpp"
        exe_path = Path(td) / "test_bb.exe"
        cpp_path.write_text(CPP_HARNESS, encoding="utf-8")

        comp = subprocess.run(
            [zig, "c++", "-std=c++20", "-O2", f"-I{CLIENT_HDR.parent}",
             "-Wall", "-Wextra", "-fno-exceptions", "-fno-rtti",
             str(cpp_path), "-o", str(exe_path)],
            capture_output=True, text=True, env=env, timeout=60
        )
        assert comp.returncode == 0, f"Compile failed:\n{comp.stderr}\n{comp.stdout}"

        run = subprocess.run([str(exe_path)], capture_output=True, text=True, timeout=15)
        assert run.returncode == 0, f"C++ harness assert failed (rc={run.returncode}):\n{run.stderr}\n{run.stdout}"
        out = run.stdout
        assert "---DONE---" in out, f"Incomplete harness output:\n{out}"

        c_json_str = out.split("---CLIENT_JSON---\n", 1)[1].split("\n---SERVER_JSON---", 1)[0]
        s_json_str = out.split("---SERVER_JSON---\n", 1)[1].split("\n---TIGHT_JSON---", 1)[0]
        t_json_str = out.split("---TIGHT_JSON---\n", 1)[1].split("\n---DONE---", 1)[0]

        c_obj = json.loads(c_json_str)
        assert c_obj["role"] == "client" and c_obj["total"] == 150 and c_obj["count"] == 9
        got_c = [e["culprit"] for e in c_obj["events"]]
        exp_c = [
            "CLIENT_LUA", "CLIENT_GC", "GPU_WAIT_OR_SWAP", "CLIENT_IO_HOST",
            "CLIENT_RENDER_SUBMIT", "CLIENT_NET_CPU", "CLIENT_CPU_OTHER", "NET_SPIKE", "MIXED",
        ]
        assert got_c == exp_c, f"Client culprit mismatch: {got_c} != {exp_c}"
        assert c_obj["events"][7]["loss_pct"] == 3.5

        s_obj = json.loads(s_json_str)
        assert s_obj["role"] == "server" and s_obj["total"] == 9 and s_obj["count"] == 9
        got_s = [e["culprit"] for e in s_obj["events"]]
        exp_s = [
            "SRV_LUA", "SRV_GC", "SRV_NET", "SRV_REPLICA",
            "SRV_SPATIAL", "SRV_ASTAR", "SRV_SAVE_IO", "SRV_OTHER_OR_OS", "SRV_MIXED",
        ]
        assert got_s == exp_s, f"Server culprit mismatch: {got_s} != {exp_s}"

        t_obj = json.loads(t_json_str)
        assert t_obj["count"] == len(t_obj["events"]) and t_obj["total"] == 150
        assert 1 <= t_obj["count"] < 64
        assert t_obj["events"][0]["frame_seq"] == 150

        print(f"[PASS] All 9 Client culprits + 9 Server culprits + 64-slot ring + tight JSON bounds verified!")


if __name__ == "__main__":
    main()
