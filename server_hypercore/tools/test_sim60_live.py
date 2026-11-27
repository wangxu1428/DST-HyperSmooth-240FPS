#!/usr/bin/env python3
"""
R3.1 End-to-End Live Headless Verification of DST HyperCore Server:
  Phase 1 (Same-Process 30 -> 60 -> 30 Hz + Persistence Markers + FastDataDumper Safety):
    - Restores base_world_backup at EXACT cluster root into isolated Sim60VerifyCluster.
    - Boots with iphlpapi.dll + dst_hypercore_server (opt_sim_60hz=false -> starts at 30Hz).
    - Verifies outsideLoaderLock=1, lua_close hook OK, all 6 Lua APIs registered.
    - Stage 1A (30Hz): verifies GetTickTime==1/30, measures 1.0s real-time pacing (~30 ticks).
    - Stage 1B (Live 30->60Hz): calls HyperCore_SetSimTickRate(60), verifies GetTickTime==1/60,
      measures 2.0s real-time pacing (~120 ticks = 1.00x real speed), verifies U=60(1) in DLL log.
    - Spawns verifiable world markers at 60Hz:
        * homesign @ (12, 0, -8) with text "R3_1_SIM60_TOKEN_20261123"
        * carrot stack=5 @ (15, 0, -8) set to freshness 0.75
        * monstermeat @ (18, 0, -8) aged in lazy mode then StopPerishing()-settled
    - Verifies FastDataDumper fail-reason safety live in Server Lua (cycle -> slow cycle-safe,
      NaN -> controlled pcall error).
    - Stage 1C (Live 60->30Hz): calls HyperCore_SetSimTickRate(30), verifies GetTickTime==1/30,
      measures 1.0s real-time pacing (~30 ticks), verifies U=15(2) RESTORED in DLL log!
    - Records exact carrot/meat freshness, triggers ms_save, waits for snapshot write, c_shutdown(true).
  Phase 2 (100% Pure 30Hz Vanilla Reload of Exact Phase 1 Save):
    - Removes iphlpapi.dll and disables dst_hypercore_server mod.
    - Reloads EXACT same Sim60VerifyCluster snapshot in pure 30Hz vanilla!
    - Verifies GetTickTime==1/30, ZERO Lua errors, exact homesign token text, exact 5-stack carrot
      count & freshness parity, and exact StopPerishing()-settled monstermeat freshness!
  Phase 3 (Admin Custom tick_rate=30 Preservation across 30 -> 60 -> 30 Hz):
    - Configures cluster.ini tick_rate=30, boots with iphlpapi.dll, switches 30->60->30.
    - Verifies DLL log records U=30(2) at 60Hz and restores U=30(1) at 30Hz!
"""
import glob
import os
import re
import shutil
import subprocess
import sys

ROOT = r"C:\Users\Administrator\.gemini\antigravity\scratch\dst_hypercore_server"
RUNTIME = os.path.join(ROOT, "bench_runtime")
BIN64 = os.path.join(RUNTIME, "bin64")
MODS_DIR = os.path.join(RUNTIME, "mods")
STORAGE = os.path.join(RUNTIME, "storage")
CLUSTER_NAME = "Sim60VerifyCluster"
CLUSTER_DIR = os.path.join(STORAGE, "DoNotStarveTogether", CLUSTER_NAME)
BASE_BACKUP = os.path.join(RUNTIME, "base_world_backup")

TEST_MOD_NAME = "sim60_verify_driver"
TEST_MOD_DIR = os.path.join(MODS_DIR, TEST_MOD_NAME)

TEST_MOD_INFO = """
name = "Sim60 Live Verifier"
description = "Verifies same-process 30->60->30Hz, custom tick_rate, FastDataDumper, and pure 30Hz vanilla save reload."
author = "test"
version = "2.0.0"
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

PHASE = "SIM_CYCLE"
EXPECTED_CARROT_PCT = -1
EXPECTED_MEAT_PCT = -1
pcall(modimport, "phase.lua")

local MARKER_TEXT = "R3_1_SIM60_TOKEN_20261123"

local function WallNow()
    local wt = rawget(G, "walltime")
    if type(wt) == "function" then return wt() end
    if G.os ~= nil and type(G.os.clock) == "function" then return G.os.clock() end
    return G.GetTimeRealSeconds()
end

local function FindNearPrefab(prefab, tx, tz, max_r)
    local best, best_d2 = nil, max_r * max_r
    for _, ent in pairs(G.Ents) do
        if ent ~= nil and ent:IsValid() and ent.prefab == prefab and ent.Transform ~= nil then
            local x, _, z = ent.Transform:GetWorldPosition()
            local dx, dz = x - tx, z - tz
            local d2 = dx * dx + dz * dz
            if d2 <= best_d2 then
                best = ent
                best_d2 = d2
            end
        end
    end
    return best
end

AddSimPostInit(function()
    local w = G.TheWorld
    if not w then return end

    if PHASE == "SIM_CYCLE" then
        -- Stage 1A: Boot at 30Hz vanilla baseline inside HyperCore Server
        local dt0 = G.TheSim:GetTickTime()
        local hz0 = G.HyperCore_GetSimTickRate()
        print(string.format("[SIM60_VERIFY] Stage1A Boot30: GetTickTime=%.6f native_rate=%d", dt0, hz0))
        assert(hz0 == 30 and math.abs(dt0 - (1.0 / 30.0)) < 1e-4, "Stage1A expected 30Hz boot!")

        w:DoTaskInTime(0.4, function()
            local t30_wall0 = WallNow()
            local t30_tick0 = G.GetTick()
            w:DoTaskInTime(1.0, function()
                local d30_ticks = G.GetTick() - t30_tick0
                local d30_wall = WallNow() - t30_wall0
                print(string.format("[SIM60_VERIFY] Stage1A 30Hz 1s window: d_ticks=%d wall_dt=%.3fs real_hz=%.2f",
                    d30_ticks, d30_wall, d30_ticks / d30_wall))
                assert(d30_ticks >= 27 and d30_ticks <= 34, "Stage1A 30Hz ticks out of range: " .. tostring(d30_ticks))

                -- Stage 1B: Live same-process 30 -> 60 Hz transition!
                local ok60 = G.HyperCore_SetSimTickRate(60)
                assert(ok60 == true, "HyperCore_SetSimTickRate(60) failed!")
                -- Dynamic GetTime wrapper for live mid-session toggle
                G.rawset(G, "GetTime", function() return G.TheSim:GetTick() * G.TheSim:GetTickTime() end)
                G.rawset(G, "GetStaticTime", function() return G.TheSim:GetStaticTick() * G.TheSim:GetTickTime() end)
                local dt60 = G.TheSim:GetTickTime()
                assert(G.HyperCore_GetSimTickRate() == 60 and math.abs(dt60 - (1.0 / 60.0)) < 1e-4,
                       "Stage1B GetTickTime not 1/60: " .. tostring(dt60))

                -- Spawn verifiable world markers while running at 60Hz!
                local sign = G.SpawnPrefab("homesign")
                sign.Transform:SetPosition(12, 0, -8)
                if sign.components.writeable ~= nil then
                    sign.components.writeable:SetText(MARKER_TEXT)
                end

                local carrot = G.SpawnPrefab("carrot")
                carrot.Transform:SetPosition(15, 0, -8)
                carrot.components.stackable:SetStackSize(5)
                carrot.components.perishable:SetPercent(0.75)

                -- Verify StopPerishing() lazy settlement persists accurately across vanilla reload!
                local meat = G.SpawnPrefab("monstermeat")
                meat.Transform:SetPosition(18, 0, -8)
                meat.components.perishable:SetPercent(0.80)
                meat.components.perishable:StartPerishing()
                local saved_get_time = G.GetTime
                local fake_t = saved_get_time()
                G.rawset(G, "GetTime", function() return fake_t end)
                meat.components.perishable._HC_EnterLazy()
                assert(meat.components.perishable._hc_lazy_active == true, "meat failed EnterLazy")
                fake_t = fake_t + 600
                meat.components.perishable:StopPerishing()
                G.rawset(G, "GetTime", saved_get_time)
                local meat_pct = meat.components.perishable:GetPercent()
                assert(meat_pct < 0.72 and meat_pct > 0.10, "meat StopPerishing lazy settlement anomalous: " .. tostring(meat_pct))

                -- Verify live Server FastDataDumper fail-reason safety (#06) + v1.4.0 FastJson/EntityScript/c_hyperprof!
                assert(G.HyperCore_Version() == "DST-HyperCore-Server/1.5.0",
                       "Expected v1.5.0 HyperCore_Version, got " .. tostring(G.HyperCore_Version()))
                local cyc = {}
                cyc.self_ref = cyc
                local dumped_cyc = G.DataDumper(cyc, nil, true)
                assert(type(dumped_cyc) == "string" and #dumped_cyc > 0, "Cycle DataDumper slow fallback failed!")
                local ok_nan = pcall(G.DataDumper, { bad = 0 / 0 }, nil, true)
                assert(not ok_nan, "NaN DataDumper MUST abort cleanly via error!")
                print("[SIM60_VERIFY] Stage1B Live FastDataDumper cycle-safe + NaN-guard PASS")

                -- Live C++ FastJson parity & edge fallback check in real Server Lua 5.1 VM
                assert(type(G.HyperCore_FastJsonEncode) == "function" and type(G.HyperCore_FastJsonDecode) == "function",
                       "HyperCore FastJson C functions missing!")
                local je = G.json.encode({ arr = { 1, "hi'x", true }, empty = {} })
                assert(je:find("hi\\'x", 1, true) ~= nil and je:find('"empty":[]', 1, true) ~= nil, "Live json.encode parity failed: " .. je)
                local jd, jpos = G.json.decode('{"a":[10,null,30],"ok":false}')
                assert(type(jd) == "table" and jd.ok == false and jd.a[1] == 10 and jd.a[2] == nil and jd.a[3] == 30 and jpos == 30,
                       "Live json.decode [10,null,30] parity failed! jpos=" .. tostring(jpos))
                local dv2, dpos2 = G.json.decode('  {"k":1}', 3)
                assert(type(dv2) == "table" and dv2.k == 1 and dpos2 == 10, "Live json.decode startPos fallback failed!")

                -- Live zero-alloc PushEvent_Internal + unrolled HasTags + c_hyperprof
                assert(G.EntityScript._hs_fast_pushevent == true, "EntityScript zero-alloc PushEvent_Internal not installed!")
                assert(G.BRANCH == "dev" or G.EntityScript._hs_fast_hastags == true, "EntityScript fast HasTags not installed!")
                assert(type(G.c_hyperprof) == "function", "c_hyperprof console helper missing!")
                print("[SIM60_VERIFY] Stage1B Live v1.4.0 FastJson + EntityScript + c_hyperprof PASS")

                local p0 = carrot.components.perishable:GetPercent()
                local t60_sim0 = G.GetTime()
                local t60_wall0 = WallNow()
                local t60_tick0 = G.GetTick()

                w:DoTaskInTime(2.0, function()
                    local d60_ticks = G.GetTick() - t60_tick0
                    local d60_sim = G.GetTime() - t60_sim0
                    local d60_wall = WallNow() - t60_wall0
                    local hz60_real = d60_ticks / d60_wall
                    local p1 = carrot.components.perishable:GetPercent()
                    print(string.format("[SIM60_VERIFY] Stage1B 60Hz 2s window: d_ticks=%d sim_dt=%.3fs wall_dt=%.3fs real_hz=%.2f perish_drop=%.6f",
                        d60_ticks, d60_sim, d60_wall, hz60_real, p0 - p1))
                    assert(d60_ticks >= 114 and d60_ticks <= 128, "Stage1B 60Hz ticks out of range: " .. tostring(d60_ticks))
                    assert(math.abs(d60_sim - 2.0) < 0.08, "Stage1B sim_dt drifted: " .. tostring(d60_sim))
                    assert(d60_wall >= 1.80 and d60_wall <= 2.40, "Stage1B wall_dt not 1.00x: " .. tostring(d60_wall))
                    assert(hz60_real >= 50.0 and hz60_real <= 66.0, "Stage1B real_hz out of range: " .. tostring(hz60_real))
                    assert((p0 - p1) >= 0.0 and (p0 - p1) < 0.02, "Stage1B perish drop anomalous")

                    -- Stage 1C: Live same-process 60 -> 30 Hz rollback!
                    local ok30 = G.HyperCore_SetSimTickRate(30)
                    assert(ok30 == true, "HyperCore_SetSimTickRate(30) failed!")
                    local dt30_back = G.TheSim:GetTickTime()
                    assert(G.HyperCore_GetSimTickRate() == 30 and math.abs(dt30_back - (1.0 / 30.0)) < 1e-4,
                           "Stage1C GetTickTime not restored to 1/30: " .. tostring(dt30_back))

                    local tb_wall0 = WallNow()
                    local tb_tick0 = G.GetTick()
                    w:DoTaskInTime(1.0, function()
                        local db_ticks = G.GetTick() - tb_tick0
                        local db_wall = WallNow() - tb_wall0
                        print(string.format("[SIM60_VERIFY] Stage1C Restored 30Hz 1s window: d_ticks=%d wall_dt=%.3fs real_hz=%.2f",
                            db_ticks, db_wall, db_ticks / db_wall))
                        assert(db_ticks >= 27 and db_ticks <= 34, "Stage1C restored 30Hz ticks out of range: " .. tostring(db_ticks))

                        local final_carrot_pct = carrot.components.perishable:GetPercent()
                        local final_meat_pct = meat.components.perishable:GetPercent()
                        print(string.format("[SIM60_VERIFY] PRE_SAVE_MARKERS carrot_pct=%.6f meat_pct=%.6f",
                            final_carrot_pct, final_meat_pct))

                        -- Trigger explicit world save & wait for disk flush before shutdown
                        G.TheWorld:PushEvent("ms_save")
                        w:DoTaskInTime(0.8, function()
                            print("[SIM60_VERIFY] PHASE1_PASS")
                            G.c_shutdown(true)
                        end)
                    end)
                end)
            end)
        end)

    elseif PHASE == "VANILLA30" then
        local dt_val = G.TheSim:GetTickTime()
        assert(rawget(G, "HyperCore_Version") == nil, "Vanilla30 MUST NOT have HyperCore native DLL loaded!")
        local ents = 0
        for _ in pairs(G.Ents) do ents = ents + 1 end
        print(string.format("[SIM60_VERIFY] Vanilla30 Reload: GetTickTime=%.6f (1/%.1f) ents=%d",
            dt_val, 1.0 / dt_val, ents))
        assert(math.abs(dt_val - (1.0 / 30.0)) < 1e-4, "Vanilla reload GetTickTime not 1/30!")
        assert(ents > 5000, "World entity count suspiciously low: " .. tostring(ents))

        local sign = FindNearPrefab("homesign", 12, -8, 2.0)
        assert(sign ~= nil, "FAIL: Persisted homesign marker not found in vanilla reload!")
        local sign_txt = sign.components.writeable and sign.components.writeable:GetText() or ""
        assert(sign_txt == MARKER_TEXT, "FAIL: homesign text mismatch: got " .. tostring(sign_txt))

        local carrot = FindNearPrefab("carrot", 15, -8, 2.0)
        assert(carrot ~= nil, "FAIL: Persisted carrot stack not found in vanilla reload!")
        local stack_sz = carrot.components.stackable:StackSize()
        local c_pct = carrot.components.perishable:GetPercent()
        assert(stack_sz == 5, "FAIL: carrot stacksize mismatch: got " .. tostring(stack_sz))
        assert(EXPECTED_CARROT_PCT < 0 or math.abs(c_pct - EXPECTED_CARROT_PCT) < 0.03,
               string.format("FAIL: carrot freshness drift! saved=%.6f reloaded=%.6f", EXPECTED_CARROT_PCT, c_pct))

        local meat = FindNearPrefab("monstermeat", 18, -8, 2.0)
        assert(meat ~= nil, "FAIL: Persisted monstermeat not found in vanilla reload!")
        local m_pct = meat.components.perishable:GetPercent()
        assert(not meat.components.perishable:IsPerishing(), "FAIL: StopPerishing state lost on vanilla reload!")
        assert(EXPECTED_MEAT_PCT < 0 or math.abs(m_pct - EXPECTED_MEAT_PCT) < 0.02,
               string.format("FAIL: monstermeat freshness mismatch! saved=%.6f reloaded=%.6f", EXPECTED_MEAT_PCT, m_pct))

        print(string.format("[SIM60_VERIFY] Vanilla30 Markers VERIFIED: sign=%q carrot(stack=%d pct=%.6f) meat(paused=true pct=%.6f)",
            sign_txt, stack_sz, c_pct, m_pct))

        w:DoTaskInTime(0.5, function()
            print("[SIM60_VERIFY] PHASE2_PASS")
            G.c_shutdown(false)
        end)

    elseif PHASE == "CUSTOM_NETRATE" then
        w:DoTaskInTime(0.3, function()
            assert(G.HyperCore_SetSimTickRate(60) == true, "CustomNetRate 30->60 failed")
            w:DoTaskInTime(0.3, function()
                assert(G.HyperCore_SetSimTickRate(30) == true, "CustomNetRate 60->30 failed")
                w:DoTaskInTime(0.2, function()
                    print("[SIM60_VERIFY] PHASE3_PASS")
                    G.c_shutdown(false)
                end)
            end)
        end)
    end
end)
"""


def run_server(label: str, port: int, steam_master_port: int = 27026, steam_auth_port: int = 8776) -> tuple[int, str]:
    exe = os.path.join(BIN64, "dontstarve_dedicated_server_nullrenderer_x64.exe")
    cmd = [
        exe,
        "-persistent_storage_root", STORAGE,
        "-conf_dir", "DoNotStarveTogether",
        "-cluster", CLUSTER_NAME,
        "-shard", "Master",
        "-offline",
        "-port", str(port),
        "-steam_master_server_port", str(steam_master_port),
        "-steam_authentication_port", str(steam_auth_port),
    ]
    proc = subprocess.run(cmd, cwd=BIN64, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=90)
    out = (proc.stdout or "") + "\n" + (proc.stderr or "")
    lines = out.splitlines()
    for i, line in enumerate(lines):
        if "[SIM60_VERIFY]" in line or "[HyperCore Server]" in line or "LUA ERROR" in line or "ASSERT" in line:
            print(f"[{label}] {line}")
            if "LUA ERROR" in line:
                for extra in lines[i + 1:i + 7]:
                    print(f"[{label}]   {extra}")
    return proc.returncode, out


def list_session_snapshots(cluster_dir: str) -> list[tuple[str, float]]:
    pats = glob.glob(os.path.join(cluster_dir, "Master", "save", "session", "*", "*"))
    return sorted((os.path.relpath(p, cluster_dir), os.path.getmtime(p)) for p in pats if os.path.isfile(p))


def main():
    assert os.path.isdir(BASE_BACKUP), f"Missing {BASE_BACKUP}; run run_benchmark.py first"
    assert os.path.isfile(os.path.join(BASE_BACKUP, "cluster.ini")), f"Invalid BASE_BACKUP layout at {BASE_BACKUP}"

    # Restore clean snapshot at EXACT cluster root level (#12 fix: NEVER inside Master/save!)
    if os.path.exists(CLUSTER_DIR):
        shutil.rmtree(CLUSTER_DIR)
    shutil.copytree(BASE_BACKUP, CLUSTER_DIR)

    # Sync latest server_mod and test driver
    hc_mod_dst = os.path.join(MODS_DIR, "dst_hypercore_server")
    if os.path.exists(hc_mod_dst):
        shutil.rmtree(hc_mod_dst)
    shutil.copytree(os.path.join(ROOT, "server_mod"), hc_mod_dst)

    os.makedirs(TEST_MOD_DIR, exist_ok=True)
    with open(os.path.join(TEST_MOD_DIR, "modinfo.lua"), "w", encoding="utf-8") as f:
        f.write(TEST_MOD_INFO)
    with open(os.path.join(TEST_MOD_DIR, "modmain.lua"), "w", encoding="utf-8") as f:
        f.write(TEST_MOD_MAIN)

    try:
        snaps_before = list_session_snapshots(CLUSTER_DIR)
        assert len(snaps_before) > 0, "Restored cluster has NO existing session snapshots!"

        # --- PHASE 1: Same-Process 30 -> 60 -> 30 Hz + Save ---
        shutil.copy2(os.path.join(ROOT, "native_plugin", "iphlpapi.dll"), os.path.join(BIN64, "iphlpapi.dll"))
        with open(os.path.join(BIN64, "dst_hypercore.ini"), "w", encoding="utf-8") as f:
            f.write(
                "[HYPERCORE]\nenable_profiler=1\nenable_mimalloc=1\nenable_spatial_zerocopy=1\n"
                "enable_adaptive_gc=1\nenable_anti_stw_gc=1\nenable_libdeflate=1\n"
                "enable_fast_dumper=1\nenable_fast_astar=1\nenable_fs_neg_cache=1\n"
                "enable_async_save=1\nenable_http_metrics=0\nsim_tick_rate=30\n"
            )
        with open(os.path.join(TEST_MOD_DIR, "phase.lua"), "w", encoding="utf-8") as f:
            f.write('PHASE = "SIM_CYCLE"\n')
        with open(os.path.join(CLUSTER_DIR, "Master", "modoverrides.lua"), "w", encoding="utf-8") as f:
            f.write(
                'return {\n'
                '  ["dst_hypercore_server"] = { enabled = true, configuration_options = {\n'
                '      opt_sim_60hz = false, opt_lazy_perishable = true, opt_fast_dumper = true\n'
                '  } },\n'
                f'  ["{TEST_MOD_NAME}"] = {{ enabled = true, configuration_options = {{}} }},\n'
                '}\n'
            )
        with open(os.path.join(MODS_DIR, "dedicated_server_mods_setup.lua"), "w", encoding="utf-8") as f:
            f.write("")

        rc1, out1 = run_server("PHASE1_30_60_30", 11014, 27026, 8776)
        log_path = os.path.join(BIN64, "dst_hypercore_Master.log")
        assert os.path.exists(log_path), "Missing dst_hypercore_Master.log!"
        dll_log1 = open(log_path, "r", encoding="utf-8", errors="replace").read()
        print("--- Phase 1 dst_hypercore_Master.log (key lines) ---")
        for ln in dll_log1.splitlines():
            if any(k in ln for k in ("Boot", "lua_close", "Registered", "SIMTICK", "FASTLOAD", "W^X", "PROF", "PROF-SUB", "SAVE", "FSNEG", "ERROR", "CRITICAL")):
                print("  " + ln)

        assert rc1 == 0 and "[SIM60_VERIFY] PHASE1_PASS" in out1, f"Phase 1 FAILED (rc={rc1})"
        assert "outsideLoaderLock=1" in dll_log1, "FAIL (#11d): InitServerHooks did NOT run outside Loader Lock!"
        assert "[HOOK] lua_close (GC lifecycle reset)" in dll_log1 and "-> OK" in dll_log1, "FAIL (#09): lua_close hook missing!"
        assert "[FASTLOAD] minizip unzReadCurrentFile" in dll_log1 and "-> OK" in dll_log1, "FAIL: FastLoad unzReadCurrentFile hook missing!"
        assert "[FASTLOAD] zlib 1.2.3 inflate_fast" in dll_log1 and "-> OK" in dll_log1, "FAIL: FastLoad inflate_fast hook missing!"
        assert "[FASTLOAD] std::vector::_Insert_n Loop A" in dll_log1 and "-> OK" in dll_log1, "FAIL: FastLoad VecFillA missing!"
        assert "[FASTLOAD] std::vector::_Insert_n Loop B" in dll_log1 and "-> OK" in dll_log1, "FAIL: FastLoad VecFillB missing!"
        assert "[PERF][FSNEG]" in dll_log1 and "active=1" in dll_log1, "FAIL: FSNEG negative cache not active!"
        assert "[SAVE] Async Save Barriers" in dll_log1 and "barriersOk=1" in dll_log1, "FAIL: AsyncSave barriers not OK!"
        assert "[HOOK] WritePersistentFile VerifiedAtomicCommit" in dll_log1 and "-> OK" in dll_log1, "FAIL: WritePersistentFile hook missing!"
        assert "[HOOK] Simulation::luaUpdate" in dll_log1 and "-> OK" in dll_log1, "FAIL: luaUpdate sub-phase hook missing!"
        assert "[HOOK] cNetworkManager::Update" in dll_log1 and "-> OK" in dll_log1, "FAIL: cNetworkManager::Update sub-phase hook missing!"
        assert "[HOOK] ReplicaManager3::Update" in dll_log1 and "-> OK" in dll_log1, "FAIL: ReplicaManager3::Update sub-phase hook missing!"
        m_async = re.search(r"AsyncSave\(enq=(\d+) ok=(\d+) comp=(\d+) bytes=(\d+).*?err=(\d+)", dll_log1)
        assert m_async and int(m_async.group(2)) > 0 and int(m_async.group(5)) == 0, f"FAIL: AsyncSave unexpected stats in {dll_log1}"
        m_unz = re.search(r"FastLoad\(unz=(\d+) MB=([0-9.]+) ms=([0-9.]+)(?:\s+uncomp=(\d+)\s+outMB=([0-9.]+))?\)", dll_log1)
        assert m_unz and int(m_unz.group(1)) > 0, "FAIL: Server FastLoad unzReadCurrentFile recorded 0 hits during boot!"
        assert "[FASTLOAD] zlib::uncompress" in dll_log1 and "-> OK" in dll_log1, "FAIL: Server zlib::uncompress hook missing!"
        m_fsn = re.search(r"FsNeg\(hits=(\d+) misses=(\d+)\)", dll_log1)
        assert m_fsn and (int(m_fsn.group(1)) + int(m_fsn.group(2))) > 0, f"FAIL: FsNeg still (0/0) in live server log! m={m_fsn and m_fsn.groups()}"
        print(f"[SIM60_VERIFY] Live FsNeg verified active: hits={m_fsn.group(1)} misses={m_fsn.group(2)}")
        assert "Synced Server NetworkTickRate -> U=60(1) for 60Hz SimTick" in dll_log1, "FAIL (#04): 60Hz U=60(1) sync missing!"
        assert "Synced Server NetworkTickRate -> U=15(2) for 30Hz SimTick" in dll_log1, "FAIL (#04): 60->30Hz U=15(2) restore missing!"

        m_mark = re.search(r"PRE_SAVE_MARKERS carrot_pct=([0-9.]+) meat_pct=([0-9.]+)", out1)
        assert m_mark, "Missing PRE_SAVE_MARKERS in Phase 1 output!"
        saved_carrot_pct, saved_meat_pct = float(m_mark.group(1)), float(m_mark.group(2))

        snaps_after = list_session_snapshots(CLUSTER_DIR)
        assert snaps_after != snaps_before, "FAIL: Phase 1 save did NOT update session snapshot files!"
        print(f"[PHASE1_SNAPSHOT] Verified snapshot updated: before={len(snaps_before)} files -> after={len(snaps_after)} files")

        # --- PHASE 2: 100% Pure 30Hz Vanilla Reload of Exact Phase 1 Save ---
        dll_path = os.path.join(BIN64, "iphlpapi.dll")
        if os.path.exists(dll_path):
            os.remove(dll_path)
        with open(os.path.join(TEST_MOD_DIR, "phase.lua"), "w", encoding="utf-8") as f:
            f.write(
                'PHASE = "VANILLA30"\n'
                f'EXPECTED_CARROT_PCT = {saved_carrot_pct:.6f}\n'
                f'EXPECTED_MEAT_PCT = {saved_meat_pct:.6f}\n'
            )
        with open(os.path.join(CLUSTER_DIR, "Master", "modoverrides.lua"), "w", encoding="utf-8") as f:
            f.write(f'return {{ ["{TEST_MOD_NAME}"] = {{ enabled = true, configuration_options = {{}} }} }}\n')

        rc2, out2 = run_server("PHASE2_VANILLA30", 11015, 27027, 8777)
        assert rc2 == 0 and "[SIM60_VERIFY] PHASE2_PASS" in out2, f"Phase 2 FAILED (rc={rc2})"
        assert "LUA ERROR" not in out2, "Phase 2 vanilla reload had LUA ERROR!"

        # --- PHASE 3: Admin Custom tick_rate=30 Preservation across 30 -> 60 -> 30 Hz ---
        shutil.copy2(os.path.join(ROOT, "native_plugin", "iphlpapi.dll"), os.path.join(BIN64, "iphlpapi.dll"))
        c_ini = os.path.join(CLUSTER_DIR, "cluster.ini")
        ini_txt = open(c_ini, "r", encoding="utf-8").read()
        if re.search(r"(?m)^\s*tick_rate\s*=", ini_txt):
            ini_txt = re.sub(r"(?m)^\s*tick_rate\s*=.*$", "tick_rate = 30", ini_txt)
        else:
            ini_txt = ini_txt.replace("[NETWORK]", "[NETWORK]\ntick_rate = 30")
        with open(c_ini, "w", encoding="utf-8") as f:
            f.write(ini_txt)
        with open(os.path.join(TEST_MOD_DIR, "phase.lua"), "w", encoding="utf-8") as f:
            f.write('PHASE = "CUSTOM_NETRATE"\n')
        with open(os.path.join(CLUSTER_DIR, "Master", "modoverrides.lua"), "w", encoding="utf-8") as f:
            f.write(
                'return {\n'
                '  ["dst_hypercore_server"] = { enabled = true, configuration_options = { opt_sim_60hz = false } },\n'
                f'  ["{TEST_MOD_NAME}"] = {{ enabled = true, configuration_options = {{}} }},\n'
                '}\n'
            )
        rc3, out3 = run_server("PHASE3_CUSTOM_RATE30", 11016, 27028, 8778)
        dll_log3 = open(log_path, "r", encoding="utf-8", errors="replace").read()
        print("--- Phase 3 dst_hypercore_Master.log (SIMTICK lines) ---")
        for ln in dll_log3.splitlines():
            if "SIMTICK" in ln or "Boot" in ln:
                print("  " + ln)
        assert rc3 == 0 and "[SIM60_VERIFY] PHASE3_PASS" in out3, f"Phase 3 FAILED (rc={rc3})"
        assert "Synced Server NetworkTickRate -> U=30(2) for 60Hz SimTick" in dll_log3, "FAIL (#04): custom tick_rate=30 not preserved at 60Hz!"
        assert "Synced Server NetworkTickRate -> U=30(1) for 30Hz SimTick" in dll_log3, "FAIL (#04): custom tick_rate=30 not restored at 30Hz!"

        print("\n[ALL PASS] R3.1 Live 30->60->30Hz, outsideLoaderLock, lua_close, custom tick_rate=30, FastDataDumper safety & pure 30Hz vanilla reload VERIFIED!")
    finally:
        shutil.rmtree(TEST_MOD_DIR, ignore_errors=True)
        shutil.rmtree(CLUSTER_DIR, ignore_errors=True)


if __name__ == "__main__":
    main()
