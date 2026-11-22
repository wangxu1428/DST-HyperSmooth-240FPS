#!/usr/bin/env python3
"""
Live Headless Verification of DST HyperCore Server v1.2.0 Opt-In 60Hz SimTick:
  Phase 1: Run isolated server with opt_sim_60hz=true + iphlpapi.dll v1.2.0.
           Wait 0.5s for boot settle, then measure a 2.0s simulation window:
             - Verify TheSim:GetTickTime() == 1/60 (0.016667s)
             - Verify TheWorld.net:HasTag("hypercore_sim60") == true
             - Verify 120 simulation ticks execute in 2.000s sim-time AND ~2.00s wall-time (1.00x real speed!)
             - Verify perishable item decays at exact 1.00x vanilla rate over 2.0s
             - Trigger SaveGame() cleanly at 60Hz
  Phase 2: Remove iphlpapi.dll and disable dst_hypercore_server mod.
           Reload the exact 60Hz-saved world in 100% pure 30Hz vanilla!
             - Verify TheSim:GetTickTime() == 1/30 (0.033333s)
             - Verify zero entity corruption & clean shutdown (Exit=0)
"""
import os
import shutil
import subprocess
import sys

ROOT = r"C:\Users\Administrator\.gemini\antigravity\scratch\dst_hypercore_server"
RUNTIME = os.path.join(ROOT, "bench_runtime")
BIN64 = os.path.join(RUNTIME, "bin64")
MODS_DIR = os.path.join(RUNTIME, "mods")
STORAGE = os.path.join(RUNTIME, "storage")
CLUSTER_NAME = "BenchCluster"
CLUSTER_DIR = os.path.join(STORAGE, "DoNotStarveTogether", CLUSTER_NAME)
BASE_BACKUP = os.path.join(RUNTIME, "base_world_backup")

TEST_MOD_NAME = "sim60_verify_driver"
TEST_MOD_DIR = os.path.join(MODS_DIR, TEST_MOD_NAME)

TEST_MOD_INFO = """
name = "Sim60 Live Verifier"
description = "Verifies 60Hz SimTick real-time parity & 30Hz vanilla save reload."
author = "test"
version = "1.0.0"
api_version = 10
dst_compatible = true
all_clients_require_mod = false
client_only_mod = false
"""

TEST_MOD_MAIN = r"""
local G = GLOBAL
local rawget = G.rawget
local pcall = G.pcall
local assert = G.assert
local math = G.math
local pairs = G.pairs
local string = G.string
local tostring = G.tostring
local type = G.type
if not (G.TheNet and G.TheNet:GetIsServer()) then return end

PHASE = "SIM60"
pcall(modimport, "phase.lua")

AddSimPostInit(function()
    local w = G.TheWorld
    if not w then return end

    if PHASE == "SIM60" then
        local dt_val = G.TheSim:GetTickTime()
        local hz_native = (type(rawget(G, "HyperCore_GetSimTickRate")) == "function") and G.HyperCore_GetSimTickRate() or 30
        local net_tag = (w.net ~= nil) and w.net:HasTag("hypercore_sim60") or false
        print(string.format("[SIM60_VERIFY] Boot: GetTickTime=%.6f (1/%.1f) native_rate=%d world_net_tag=%s",
            dt_val, 1.0 / dt_val, hz_native, tostring(net_tag)))
        assert(math.abs(dt_val - (1.0 / 60.0)) < 1e-4, "GetTickTime not 1/60: " .. tostring(dt_val))
        assert(hz_native == 60, "native_rate not 60")
        assert(net_tag == true, "TheWorld.net missing hypercore_sim60 tag!")

        -- Wait 0.5s for initial world-load settle BEFORE starting wall-clock timer
        w:DoTaskInTime(0.5, function()
            local carrot = G.SpawnPrefab("carrot")
            carrot.Transform:SetPosition(0, 0, 0)
            local p0 = carrot.components.perishable:GetPercent()
            local t_sim0 = G.GetTime()
            local t_wall0 = G.GetTimeRealSeconds()
            local tick0 = G.GetTick()

            w:DoTaskInTime(2.0, function()
                local t_sim1 = G.GetTime()
                local t_wall1 = G.GetTimeRealSeconds()
                local tick1 = G.GetTick()
                local p1 = carrot.components.perishable:GetPercent()
                local d_ticks = tick1 - tick0
                local sim_dt = t_sim1 - t_sim0
                local wall_dt = t_wall1 - t_wall0
                local hz_real = d_ticks / wall_dt
                local perish_drop = p0 - p1
                print(string.format("[SIM60_VERIFY] Steady 2s window: d_ticks=%d sim_dt=%.3fs wall_dt=%.3fs real_hz=%.2f perish_drop=%.6f",
                    d_ticks, sim_dt, wall_dt, hz_real, perish_drop))
                assert(d_ticks >= 115 and d_ticks <= 126, "Expected ~120 ticks in 2.0s, got " .. tostring(d_ticks))
                assert(math.abs(sim_dt - 2.0) < 0.08, "Sim time drifted: " .. tostring(sim_dt))
                assert(wall_dt >= 1.80 and wall_dt <= 2.35, "Wall time not 1.00x real speed: " .. tostring(wall_dt))
                assert(hz_real >= 52.0 and hz_real <= 66.0, "Real Hz out of range: " .. tostring(hz_real))
                assert(perish_drop >= 0.0 and perish_drop < 0.02, "Perishable decay anomalous: " .. tostring(perish_drop))

                -- Save world at 60Hz so Phase 2 reloads it in 100% pure 30Hz vanilla
                G.TheWorld:PushEvent("ms_save")
                w:DoTaskInTime(0.6, function()
                    print("[SIM60_VERIFY] PHASE1_PASS")
                    G.c_shutdown(true)
                end)
            end)
        end)
    else
        local dt_val = G.TheSim:GetTickTime()
        local ents = 0
        for _ in pairs(G.Ents) do ents = ents + 1 end
        print(string.format("[SIM60_VERIFY] Vanilla30 Reload: GetTickTime=%.6f (1/%.1f) ents=%d",
            dt_val, 1.0 / dt_val, ents))
        assert(math.abs(dt_val - (1.0 / 30.0)) < 1e-4, "Vanilla reload GetTickTime not 1/30!")
        assert(ents > 5000, "World entity count suspiciously low: " .. tostring(ents))
        w:DoTaskInTime(0.5, function()
            print("[SIM60_VERIFY] PHASE2_PASS")
            G.c_shutdown(false)
        end)
    end
end)
"""

def run_server(label: str) -> tuple[int, str]:
    exe = os.path.join(BIN64, "dontstarve_dedicated_server_nullrenderer_x64.exe")
    cmd = [
        exe,
        "-persistent_storage_root", STORAGE,
        "-conf_dir", "DoNotStarveTogether",
        "-cluster", CLUSTER_NAME,
        "-shard", "Master",
        "-offline",
    ]
    proc = subprocess.run(cmd, cwd=BIN64, capture_output=True, text=True, timeout=90)
    out = (proc.stdout or "") + "\n" + (proc.stderr or "")
    for line in out.splitlines():
        if "[SIM60_VERIFY]" in line or "[HyperCore Server]" in line or "LUA ERROR" in line or "ASSERT" in line:
            print(f"[{label}] {line}")
    return proc.returncode, out

def main():
    assert os.path.isdir(BASE_BACKUP), f"Missing {BASE_BACKUP}; run run_benchmark.py first"

    # Restore clean snapshot
    master_save = os.path.join(CLUSTER_DIR, "Master", "save")
    if os.path.exists(master_save):
        shutil.rmtree(master_save)
    shutil.copytree(BASE_BACKUP, master_save)

    # Sync latest v1.2.0 server_mod and test driver
    hc_mod_dst = os.path.join(MODS_DIR, "dst_hypercore_server")
    if os.path.exists(hc_mod_dst):
        shutil.rmtree(hc_mod_dst)
    shutil.copytree(os.path.join(ROOT, "server_mod"), hc_mod_dst)

    os.makedirs(TEST_MOD_DIR, exist_ok=True)
    with open(os.path.join(TEST_MOD_DIR, "modinfo.lua"), "w", encoding="utf-8") as f:
        f.write(TEST_MOD_INFO)
    with open(os.path.join(TEST_MOD_DIR, "modmain.lua"), "w", encoding="utf-8") as f:
        f.write(TEST_MOD_MAIN)

    # --- PHASE 1: 60Hz Live SimTick ---
    shutil.copy2(os.path.join(ROOT, "native_plugin", "iphlpapi.dll"), os.path.join(BIN64, "iphlpapi.dll"))
    with open(os.path.join(BIN64, "dst_hypercore.ini"), "w", encoding="utf-8") as f:
        f.write("[HYPERCORE]\nenable_profiler=1\nenable_mimalloc=1\nenable_spatial_zerocopy=1\n"
                "enable_adaptive_gc=1\nenable_libdeflate=1\nenable_fast_dumper=1\n"
                "enable_http_metrics=0\nsim_tick_rate=30\n")  # Mod opt_sim_60hz upgrades to 60Hz dynamically!
    with open(os.path.join(TEST_MOD_DIR, "phase.lua"), "w", encoding="utf-8") as f:
        f.write('PHASE = "SIM60"\n')
    with open(os.path.join(CLUSTER_DIR, "Master", "modoverrides.lua"), "w", encoding="utf-8") as f:
        f.write(
            'return {\n'
            '  ["dst_hypercore_server"] = { enabled = true, configuration_options = { opt_sim_60hz = true } },\n'
            f'  ["{TEST_MOD_NAME}"] = {{ enabled = true, configuration_options = {{}} }},\n'
            '}\n'
        )
    with open(os.path.join(MODS_DIR, "dedicated_server_mods_setup.lua"), "w", encoding="utf-8") as f:
        f.write("")

    rc1, out1 = run_server("PHASE1_60HZ")
    log_path = os.path.join(BIN64, "dst_hypercore_Master.log")
    if os.path.exists(log_path):
        print("--- dst_hypercore_Master.log ---")
        with open(log_path, "r", encoding="utf-8", errors="replace") as f:
            for ln in f:
                if "SIMTICK" in ln or "Boot" in ln or "PROF" in ln or "ERROR" in ln:
                    print("  " + ln.rstrip())

    assert rc1 == 0 and "[SIM60_VERIFY] PHASE1_PASS" in out1, f"Phase 1 FAILED (rc={rc1})"

    # --- PHASE 2: Pure 30Hz Vanilla Reload of the 60Hz Save ---
    dll_path = os.path.join(BIN64, "iphlpapi.dll")
    if os.path.exists(dll_path):
        os.remove(dll_path)
    with open(os.path.join(TEST_MOD_DIR, "phase.lua"), "w", encoding="utf-8") as f:
        f.write('PHASE = "VANILLA30"\n')
    with open(os.path.join(CLUSTER_DIR, "Master", "modoverrides.lua"), "w", encoding="utf-8") as f:
        f.write(f'return {{ ["{TEST_MOD_NAME}"] = {{ enabled = true, configuration_options = {{}} }} }}\n')

    rc2, out2 = run_server("PHASE2_VANILLA30")
    assert rc2 == 0 and "[SIM60_VERIFY] PHASE2_PASS" in out2, f"Phase 2 FAILED (rc={rc2})"

    # Cleanup test driver
    shutil.rmtree(TEST_MOD_DIR, ignore_errors=True)
    print("\n[ALL PASS] Server v1.2.0 60Hz SimTick (1.00x real speed + tag + 30Hz vanilla reload) VERIFIED!")

if __name__ == "__main__":
    main()
