#!/usr/bin/env python3
"""
End-to-End Live Regression Suite for DST HyperCore Server v1.4.0:
  1. Bare-sandbox modinfo.lua verification under BOTH locale="zh" and locale="en"
     (verifies v1.4.0, localized names/descriptions/labels, exact 7 options & defaults parity).
  2. Live headless Dedicated Server verification with iphlpapi.dll v1.4.0 + hypercore_server v1.4.0:
     - HyperCore_Version() == "DST-HyperCore-Server/1.4.0" & v1.4.0 startup banner
     - [L2-1B] FastJson bridge (encode, encode_compliant, decode null/holes/startPos!=1/comment fallback)
     - [L2-1C] 0-alloc EntityScript:PushEvent_Internal (1-listener, mid-dispatch remove/add, nested, error safety)
     - [L2-1C] Unrolled HasTags / HasOneOfTags + HasAllTags / HasAnyTag parity on real SpawnPrefab entity
     - c_hyperstats() v1.4.0 + FastJson/uncomp stats output
     - c_hyperprof(1) returns true, installs debug hook, and cleans up hook completely
"""
import os
import shutil
import subprocess
import sys

SERVER_REPO = r"C:\Users\Administrator\.gemini\antigravity\scratch\dst_hypercore_server"
SERVER_MOD  = os.path.join(SERVER_REPO, "server_mod")
BENCH_RT    = os.path.join(SERVER_REPO, "bench_runtime")
BIN64       = os.path.join(BENCH_RT, "bin64")
MODS_DIR    = os.path.join(BENCH_RT, "mods")
STORAGE     = os.path.join(BENCH_RT, "storage")
BASE_BACKUP = os.path.join(BENCH_RT, "base_world_backup")
CLUSTER_NAME = "ServerLuaV14Cluster"
CLUSTER_DIR  = os.path.join(STORAGE, "DoNotStarveTogether", CLUSTER_NAME)

HARNESS_NAME = "server_lua_v14_test"
HARNESS_DIR  = os.path.join(MODS_DIR, HARNESS_NAME)

HARNESS_MODINFO = """
name = "HyperCore v1.4.0 Lua Suite"
description = "Headless server Lua verification for HyperCore v1.4.0"
author = "test"
version = "1.4.0"
api_version = 10
dst_compatible = true
all_clients_require_mod = false
client_only_mod = false
"""

HARNESS_MODMAIN = r"""
local G = GLOBAL
local assert = G.assert
local loadfile = G.loadfile
local setfenv = G.setfenv
local tostring = G.tostring
local type = G.type

AddSimPostInit(function()
    if not (G.TheWorld and G.TheWorld.ismastersim) then return end

    G.TheWorld:DoTaskInTime(0.3, function()
        -- =====================================================================
        -- 1. Bare-sandbox modinfo.lua (ZH + EN bilingual)
        -- =====================================================================
        do
            local info_path = "../mods/hypercore_server/modinfo.lua"
            local fn_info, err_info = G.kleiloadlua(info_path)
            assert(type(fn_info) == "function", "Failed to load server modinfo.lua: " .. tostring(fn_info) .. " / " .. tostring(err_info))

            local env_zh = {
                folder_name = "hypercore_server",
                locale = "zh",
                ChooseTranslationTable = function(t) return t.zh or t[1] end,
            }
            setfenv(fn_info, env_zh)
            fn_info()
            assert(env_zh.version == "1.6.0-beta1", "Expected ZH modinfo version 1.6.0-beta1, got " .. tostring(env_zh.version))
            assert(env_zh.dnh_github == "wangxu1428/DST-HyperSmooth-240FPS" and env_zh.dnh_github_asset == "iphlpapi.dll",
                "Missing/invalid DNH declaration in server modinfo.lua")
            assert(#env_zh.configuration_options == 9, "Expected 9 server modinfo options, got " .. tostring(#env_zh.configuration_options))

            local env_en = {
                folder_name = "hypercore_server",
                locale = "en",
                ChooseTranslationTable = function(t) return t.en or t[1] end,
            }
            setfenv(fn_info, env_en)
            fn_info()
            assert(env_en.version == "1.6.0-beta1", "Expected EN modinfo version 1.6.0-beta1, got " .. tostring(env_en.version))
            assert(type(env_en.name) == "string" and env_en.name ~= env_zh.name, "EN/ZH server name must differ")
            assert(type(env_en.description) == "string" and env_en.description ~= env_zh.description, "EN/ZH server description must differ")
            assert(#env_en.configuration_options == 9, "Expected 9 EN options")
            for i = 1, 9 do
                local z = env_zh.configuration_options[i]
                local e = env_en.configuration_options[i]
                assert(z.name == e.name, "Option key mismatch at index " .. tostring(i))
                assert(z.default == e.default, "Option default mismatch for " .. tostring(z.name))
                assert(#z.options == #e.options, "Sub-options count mismatch for " .. tostring(z.name))
                for j = 1, #z.options do
                    assert(z.options[j].data == e.options[j].data, "Sub-option data mismatch for " .. tostring(z.name))
                end
            end
            assert(env_en.configuration_options[8].name == "hardware_tier" and env_en.configuration_options[8].default == 1,
                "Expected option #8 to be hardware_tier with default 1")
            assert(env_en.configuration_options[9].name == "opt_replica_pacing" and env_en.configuration_options[9].default == -1,
                "Expected option #9 to be opt_replica_pacing with default -1")
            print("[SERVER_V14_TEST] 1. Bare-sandbox server modinfo.lua ZH+EN PASS (v1.6.0, 9 opts)")
        end

        -- =====================================================================
        -- 2. Live HyperCore Native Bridge Version
        -- =====================================================================
        do
            assert(type(G.HyperCore_Version) == "function", "Expected HyperCore_Version C API")
            local ver = G.HyperCore_Version()
            assert(ver == "DST-HyperCore-Server/1.6.0", "Expected DST-HyperCore-Server/1.6.0, got " .. tostring(ver))
            assert(type(G.HyperCore_FastJsonEncode) == "function", "Expected HyperCore_FastJsonEncode")
            assert(type(G.HyperCore_FastJsonDecode) == "function", "Expected HyperCore_FastJsonDecode")
            assert(type(G.HyperCore_SetRpcChannelMasks) == "function", "Expected HyperCore_SetRpcChannelMasks")
            assert(type(G.HyperCore_RegisterRpcQosMasks) == "function", "Expected HyperCore_RegisterRpcQosMasks")
            assert(type(G.HyperCore_SetReplicaPacing) == "function", "Expected HyperCore_SetReplicaPacing")
            print("[SERVER_V14_TEST] 2. Native v1.6.0 bridge APIs present PASS (" .. tostring(ver) .. ")")
        end

        -- =====================================================================
        -- 3. [L2-1B] FastJson Live Bridge Parity
        -- =====================================================================
        do
            local empty_json = G.json.encode({})
            assert(empty_json == "[]", "Expected empty table to encode as '[]', got " .. tostring(empty_json))

            local payload = { hp = 2000, tags = { "boss", "shadow" }, ok = true, flag = false }
            local s = G.json.encode(payload)
            local dec = G.json.decode(s)
            assert(type(dec) == "table" and dec.hp == 2000 and dec.ok == true and dec.flag == false, "FastJson roundtrip failed")
            assert(type(dec.tags) == "table" and dec.tags[1] == "boss" and dec.tags[2] == "shadow", "FastJson array failed")

            assert(G.json.decode("null") == nil, "FastJson decode('null') must return nil")
            local hole_arr = G.json.decode("[1,null,3]")
            assert(type(hole_arr) == "table" and hole_arr[1] == 1 and hole_arr[2] == nil and hole_arr[3] == 3,
                   "FastJson hole array parity failed")

            -- startPos != 1 & /*comment*/ fallback paths
            local sub_obj = G.json.decode('xx{"v":42}', 3)
            assert(type(sub_obj) == "table" and sub_obj.v == 42, "json.decode with startPos != 1 failed")
            local cmt_obj = G.json.decode('/*c*/{"v":99}')
            assert(type(cmt_obj) == "table" and cmt_obj.v == 99, "json.decode comment fallback failed")

            if type(G.json.encode_compliant) == "function" then
                local comp = G.json.encode_compliant({ q = "a'b" })
                assert(type(comp) == "string" and #comp > 0, "json.encode_compliant failed")
            end
            print("[SERVER_V14_TEST] 3. Live FastJson bridge (encode/compliant/decode/null/hole/fallback) PASS")
        end

        -- =====================================================================
        -- 4. [L2-1C] 0-alloc EntityScript:PushEvent_Internal + Unrolled HasTags
        -- =====================================================================
        do
            local ES = G.EntityScript
            assert(ES.HasAllTags == ES.HasTags, "Expected HasAllTags rebound to HasTags")
            assert(ES.HasAnyTag == ES.HasOneOfTags, "Expected HasAnyTag rebound to HasOneOfTags")

            local e = G.SpawnPrefab("monstermeat")
            assert(e ~= nil, "Failed spawning monstermeat")
            e.Transform:SetPosition(0, 0, 0)
            e:AddTag("hc_t1")
            e:AddTag("hc_t2")
            e:AddTag("hc_t3")
            e:AddTag("hc_t4")

            assert(e:HasTags({"hc_t1"}) == true and e:HasTags({"hc_missing"}) == false, "1-tag HasTags failed")
            assert(e:HasTags({"hc_t1", "hc_t2"}) == true and e:HasTags({"hc_t1", "nope"}) == false, "2-tag HasTags failed")
            assert(e:HasTags({"hc_t1", "hc_t2", "hc_t3"}) == true and e:HasTags({"hc_t1", "hc_t2", "nope"}) == false, "3-tag HasTags failed")
            assert(e:HasTags({"hc_t1", "hc_t2", "hc_t3", "hc_t4"}) == true and e:HasTags({"hc_t1", "hc_t2", "hc_t3", "nope"}) == false, "4-tag HasTags failed")
            assert(e:HasTags({"hc_t1", "hc_t2", "hc_t3", "hc_t4", "nope"}) == false, "5-tag negative HasTags failed")
            assert(e:HasOneOfTags({"nope", "hc_t2"}) == true and e:HasOneOfTags({"x", "y"}) == false, "2-tag HasOneOfTags failed")
            assert(e:HasOneOfTags({"x", "y", "hc_t3"}) == true and e:HasOneOfTags({"x", "y", "z"}) == false, "3-tag HasOneOfTags failed")
            assert(e:HasOneOfTags({"x", "y", "z", "hc_t4"}) == true and e:HasOneOfTags({"w", "x", "y", "z"}) == false, "4-tag HasOneOfTags failed")
            assert(e:HasOneOfTags({"v", "w", "x", "y", "hc_t1"}) == true, "5-tag HasOneOfTags failed")
            assert(e:HasTags("hc_t1", "hc_t2") == true and e:HasTags("hc_t1", "nope") == false, "vararg HasTags failed")
            assert(e:HasOneOfTags("nope", "hc_t3") == true and e:HasOneOfTags("x", "y") == false, "vararg HasOneOfTags failed")

            -- 1-listener, mid-dispatch remove/add, nested, error safety
            local c1 = 0
            e:ListenForEvent("hc_e1", function(src, d) c1 = c1 + (d and d.n or 1) end, e)
            e:PushEvent_Internal("hc_e1", { n = 5 })
            assert(c1 == 5, "Single listener PushEvent_Internal mismatch")

            local crem = 0
            local rem_cb
            rem_cb = function()
                crem = crem + 1
                e:RemoveEventCallback("hc_erem", rem_cb, e)
            end
            e:ListenForEvent("hc_erem", rem_cb, e)
            e:PushEvent_Internal("hc_erem", {})
            e:PushEvent_Internal("hc_erem", {})
            assert(crem == 1, "Mid-dispatch RemoveEventCallback failed, count=" .. tostring(crem))

            local sum_multi, sum_nested = 0, 0
            e:ListenForEvent("hc_enest", function() sum_nested = sum_nested + 9 end, e)
            e:ListenForEvent("hc_emulti", function()
                sum_multi = sum_multi + 1
                e:PushEvent_Internal("hc_enest", {})
            end, e)
            e:ListenForEvent("hc_emulti", function() sum_multi = sum_multi + 2 end, e)
            e:ListenForEvent("hc_emulti", function() sum_multi = sum_multi + 4 end, e)
            e:PushEvent_Internal("hc_emulti", {})
            assert(sum_multi == 7 and sum_nested == 9, "Nested multi-listener PushEvent_Internal failed")

            e:ListenForEvent("hc_eerr", function() G.error("hc_intentional") end, e)
            local ok_e = G.pcall(function() e:PushEvent_Internal("hc_eerr", {}) end)
            assert(not ok_e, "Expected error propagation from listener")
            local after_err = 0
            e:ListenForEvent("hc_eafter", function() after_err = after_err + 1 end, e)
            e:PushEvent_Internal("hc_eafter", {})
            assert(after_err == 1, "Pool poisoned after listener error")

            e:Remove()
            print("[SERVER_V14_TEST] 4. 0-alloc PushEvent_Internal + Unrolled HasTags/HasOneOfTags PASS")
        end

        -- =====================================================================
        -- 5. c_hyperstats() + c_hyperprof(1)
        -- =====================================================================
        do
            assert(type(G.c_hyperstats) == "function", "Expected GLOBAL.c_hyperstats")
            assert(type(G.c_hyperprof) == "function", "Expected GLOBAL.c_hyperprof")
            -- Exercise live Async libdeflate+Base64 save offload + read barrier inside real Dedicated Server
            local raw_big = string.rep("HYPERCORE_STAGE2_TEST_", 400) -- ~8.8 KB >= 4KB threshold
            local saved_ok = false
            G.TheSim:SetPersistentString("hc_stage2_live_verify", raw_big, true, function(ok)
                saved_ok = (ok == true)
            end)
            assert(saved_ok, "Expected SetPersistentString async defer callback ok=true")
            local read_ok, read_str = false, nil
            G.TheSim:GetPersistentString("hc_stage2_live_verify", function(ok, val)
                read_ok = (ok == true)
                read_str = val
            end)
            assert(read_ok and read_str == raw_big,
                   "Async compressed Save+Barrier readback mismatch! ok=" .. tostring(read_ok) .. " len=" .. tostring(read_str and #read_str))

            -- Live wire verification of RakNet::RPC4::Call CausalQoS on real Dedicated Server:
            --   1) CLIENT_RPC.LearnRecipe (entity-free whitelist) -> Slot "2" -> Ch2 / MEDIUM (offCh0 += 1)
            --   2) CLIENT_RPC.ShowPopup (can carry target entities -> kept causal on Ch0) -> Slot "2" -> Ch0 / IMMEDIATE (imm += 1)
            --   3) CLIENT_RPC.SetSkillActivatedState / AddSkillXP (handshake-coupled -> kept causal on Ch0) -> Slot "2" -> Ch0 / IMMEDIATE (imm += 2, offCh0 += 0)
            G.SendRPCToClient(G.CLIENT_RPC.LearnRecipe, nil, "meatballs", "meat", "berries", "berries", "berries")
            G.SendRPCToClient(G.CLIENT_RPC.ShowPopup, nil, 0, nil, false)

            local st_before = G.HyperCore_GetStats()
            G.SendRPCToClient(G.CLIENT_RPC.SetSkillActivatedState, nil, 1, true)
            G.SendRPCToClient(G.CLIENT_RPC.AddSkillXP, nil, 1)
            local st_tbl = G.HyperCore_GetStats()
            assert(st_tbl.opt_rpc_qos == true, "Expected opt_rpc_qos == true on live server")
            assert(st_tbl.opt_async_save == true, "Expected opt_async_save == true on live server")
            assert((st_tbl.async_save_committed or 0) >= 1,
                   "Expected async_save_committed >= 1, got " .. tostring(st_tbl.async_save_committed))
            assert((st_tbl.async_save_errors or 0) == 0,
                   "Expected async_save_errors == 0, got " .. tostring(st_tbl.async_save_errors))
            assert((st_tbl.rpc_qos_off_ch0 or 0) >= 1,
                   "Expected live RpcQoS off_ch0 >= 1 (LearnRecipe on Slot '2'), got " .. tostring(st_tbl.rpc_qos_off_ch0))
            assert((st_tbl.rpc_qos_off_ch0 or 0) == (st_before.rpc_qos_off_ch0 or 0),
                   "SkillTree ClientRPC leaked off Ch0! before=" .. tostring(st_before.rpc_qos_off_ch0) .. " after=" .. tostring(st_tbl.rpc_qos_off_ch0))
            assert((st_tbl.rpc_qos_imm or 0) >= (st_before.rpc_qos_imm or 0) + 2,
                   "Expected SkillTree ClientRPCs on Ch0 IMMEDIATE_PRIORITY! before=" .. tostring(st_before.rpc_qos_imm) .. " after=" .. tostring(st_tbl.rpc_qos_imm))
            assert((st_tbl.rpc_qos_imm or 0) >= 3,
                   "Expected live RpcQoS imm >= 3 (ShowPopup + SkillTree RPCs causal on Ch0/Slot '2'), got " .. tostring(st_tbl.rpc_qos_imm))
            print(string.format("[SERVER_V14_TEST] 5a. Live VerifiedAtomicSave+RpcQoS Wire PASS (ok=%d err=%d rpc_imm=%d rpc_offCh0=%d rpc_tot=%d)",
                st_tbl.async_save_committed or 0, st_tbl.async_save_errors or 0,
                st_tbl.rpc_qos_imm or 0, st_tbl.rpc_qos_off_ch0 or 0, st_tbl.rpc_qos_total or 0))

            G.c_hyperstats()
            assert(type(G.c_hyperlang) == "function", "Expected GLOBAL.c_hyperlang")
            G.c_hyperlang("zh")
            G.c_hyperstats()
            G.c_hyperlang("auto")

            local ok_prof = G.c_hyperprof(1)
            assert(ok_prof == true, "Expected c_hyperprof(1) == true, got " .. tostring(ok_prof))
            local hfn = G.debug.gethook()
            assert(type(hfn) == "function", "Expected active debug hook right after c_hyperprof(1)")

            local waits = 0
            local function check_cleanup()
                -- Exercise tiny Lua work so count/wall-clock hook fires if needed
                local acc = 0
                for k = 1, 5000 do acc = acc + k end
                local cur = G.debug.gethook()
                if cur == nil then
                    print("[SERVER_V14_TEST] 5. c_hyperstats() + c_hyperprof(1) hook cleanup PASS")
                    -- =========================================================
                    -- 6. Live Server Spike Blackbox Capture + Export + c_hyperblackbox
                    -- =========================================================
                    assert(type(G.HyperCore_ClearBlackbox) == "function", "Expected HyperCore_ClearBlackbox")
                    assert(type(G.HyperCore_GetBlackbox) == "function", "Expected HyperCore_GetBlackbox")
                    assert(type(G.HyperCore_ExportBlackbox) == "function", "Expected HyperCore_ExportBlackbox")
                    assert(type(G.c_hyperblackbox) == "function", "Expected c_hyperblackbox")
                    assert(G.HyperCore_ClearBlackbox() == true, "HyperCore_ClearBlackbox failed")

                    -- Induce a deterministic ~45ms Lua busy-spin on this tick (>33.33ms overrun budget)
                    local spin_t0 = G.os.clock()
                    local spin_i = 0
                    while (G.os.clock() - spin_t0) < 0.045 and spin_i < 50000000 do
                        spin_i = spin_i + 1
                    end

                    G.TheWorld:DoTaskInTime(0.1, function()
                        local tot, cnt, bb_json = G.HyperCore_GetBlackbox(8)
                        assert(type(tot) == "number" and tot >= 1, "Expected blackbox total >= 1, got " .. tostring(tot))
                        assert(type(cnt) == "number" and cnt >= 1, "Expected blackbox count >= 1, got " .. tostring(cnt))
                        assert(type(bb_json) == "string", "Expected blackbox JSON string")
                        local bb = G.json.decode(bb_json)
                        assert(type(bb) == "table" and bb.role == "server", "Expected role=server in blackbox JSON")
                        assert(bb.total == tot and bb.count == cnt and type(bb.events) == "table" and #bb.events >= 1,
                               "Blackbox JSON count/events mismatch")
                        local ev = bb.events[1]
                        assert(ev.culprit == "SRV_LUA", "Expected culprit SRV_LUA, got " .. tostring(ev.culprit))
                        assert(ev.total_us >= 33333, "Expected overrun total_us >= 33333, got " .. tostring(ev.total_us))
                        assert(ev.lua_us >= 25000, "Expected lua_us >= 25000, got " .. tostring(ev.lua_us))
                        assert(ev.epoch_ms > 1700000000000, "Expected valid epoch_ms, got " .. tostring(ev.epoch_ms))

                        local ok_exp, exp_path = G.HyperCore_ExportBlackbox()
                        assert(ok_exp == true and type(exp_path) == "string", "HyperCore_ExportBlackbox failed")
                        assert(G.c_hyperblackbox(5) == true, "c_hyperblackbox(5) EN failed")
                        G.c_hyperlang("zh")
                        assert(G.c_hyperblackbox(5) == true, "c_hyperblackbox(5) ZH failed")
                        assert(G.c_hyperblackbox("export") == true, "c_hyperblackbox export failed")
                        G.c_hyperlang("auto")
                        assert(G.c_hyperblackbox("clear") == true, "c_hyperblackbox clear failed")
                        local tot_after, cnt_after = G.HyperCore_GetBlackbox(5)
                        assert(tot_after == 0 and cnt_after == 0, "Expected cleared blackbox after clear")

                        print(string.format("[SERVER_V14_TEST] 6. Live Server Spike Blackbox PASS (tot=%d lua=%.1fms total=%.1fms exp=%s)",
                            tot, ev.lua_us / 1000.0, ev.total_us / 1000.0, tostring(exp_path)))

                        -- =====================================================
                        -- 7. Tiered Hardware Liberation + c_hypertier
                        -- =====================================================
                        assert(type(G.HyperCore_GetHardwareInfo) == "function", "Expected HyperCore_GetHardwareInfo")
                        assert(type(G.HyperCore_SetHardwareTier) == "function", "Expected HyperCore_SetHardwareTier")
                        assert(type(G.c_hypertier) == "function", "Expected c_hypertier")
                        local hw0 = G.HyperCore_GetHardwareInfo()
                        assert(type(hw0) == "table" and hw0.tier == 1 and hw0.cores >= 1 and hw0.ram_mb >= 1,
                            "Test 7a: Invalid default HardwareInfo")
                        assert(hw0.replica_construct_budget == 96 and hw0.replica_budget_explicit == false,
                            "Test 7a2: Expected Balanced default replica_construct_budget=96 (auto)")
                        local ok_s2, ap_s2 = G.HyperCore_SetHardwareTier(2)
                        assert(ok_s2 == true and ap_s2 == 2, "Test 7b: SetHardwareTier(2) failed")
                        local hw2 = G.HyperCore_GetHardwareInfo()
                        assert(hw2.tier == 2 and hw2.tier_name == "unleashed" and hw2.deflate_level == 2 and hw2.replica_construct_budget == 128,
                            "Test 7c: Unleashed tier fields mismatch")
                        assert(G.c_hypertier(0) == 0 and G.HyperCore_GetHardwareInfo().tier == 0 and G.HyperCore_GetHardwareInfo().replica_construct_budget == 64,
                            "Test 7d: c_hypertier(0) failed")
                        assert(G.c_hypertier("unleashed") == 2 and G.c_hypertier() == 2, "Test 7e: c_hypertier('unleashed') failed")
                        assert(G.c_hypertier(1) == 1 and G.HyperCore_GetHardwareInfo().replica_construct_budget == 96,
                            "Test 7f: restore balanced failed")
                        local st_hw = G.HyperCore_GetStats()
                        assert(st_hw.hardware_tier == 1 and st_hw.hw_cores >= 1 and st_hw.hw_ram_mb >= 1,
                            "Test 7g: GetStats hardware_tier fields missing")
                        print(string.format("[SERVER_V14_TEST] 7. Server Tiered Hardware Liberation + c_hypertier PASS (shard=%s cores=%d P=%d ram=%.1fGB)",
                            tostring(hw2.shard), hw2.cores, hw2.perf_cores, hw2.ram_mb / 1024))

                        -- =====================================================
                        -- 8. Module 10B Replica Construction Pacing + c_hyperpacing
                        -- =====================================================
                        assert(type(G.c_hyperpacing) == "function", "Expected GLOBAL.c_hyperpacing")
                        assert(st_hw.opt_replica_fastpath == true, "Test 8a: Expected opt_replica_fastpath == true")
                        assert(st_hw.replica_proximity_active == true, "Test 8b: Expected verified Win PE proximity active")
                        assert(st_hw.replica_construct_budget == 96, "Test 8c: Expected effective budget == 96")
                        assert(G.c_hyperpacing(0) == 0 and G.HyperCore_GetHardwareInfo().replica_budget_explicit == true,
                            "Test 8d: c_hyperpacing(0) off failed")
                        assert(G.c_hyperpacing(10) == 16, "Test 8e: clamp low [16..1024] failed")
                        assert(G.c_hyperpacing(2048) == 1024, "Test 8f: clamp high [16..1024] failed")
                        assert(G.c_hyperpacing("auto") == 96 and G.HyperCore_GetHardwareInfo().replica_budget_explicit == false,
                            "Test 8g: c_hyperpacing('auto') restore failed")
                        print("[SERVER_V14_TEST] 8. Module 10B Replica Pacing + c_hyperpacing + Live Proximity PASS (budget=64/96/128, prox=true)")

                        print("[SERVER_V14_TEST] ALL_PASS")
                        G.c_shutdown(false)
                    end)
                    return
                end
                waits = waits + 1
                if waits >= 10 then
                    assert(false, "FAIL: c_hyperprof(1) debug hook did not clean up within timeout")
                    G.c_shutdown(false)
                    return
                end
                G.TheWorld:DoTaskInTime(0.3, check_cleanup)
            end
            G.TheWorld:DoTaskInTime(1.15, check_cleanup)
        end
    end)
end)
"""


def main():
    assert os.path.isdir(BASE_BACKUP), f"Missing {BASE_BACKUP}"
    if os.path.exists(CLUSTER_DIR):
        shutil.rmtree(CLUSTER_DIR)
    shutil.copytree(BASE_BACKUP, CLUSTER_DIR)

    # Sync latest native iphlpapi.dll + dst_hypercore.ini + server_mod into bench_runtime
    shutil.copy2(os.path.join(SERVER_REPO, "native_plugin", "iphlpapi.dll"), os.path.join(BIN64, "iphlpapi.dll"))
    shutil.copy2(os.path.join(SERVER_REPO, "native_plugin", "dst_hypercore.ini"), os.path.join(BIN64, "dst_hypercore.ini"))
    dst_hc = os.path.join(MODS_DIR, "hypercore_server")
    os.makedirs(dst_hc, exist_ok=True)
    for fn in ("modinfo.lua", "modmain.lua"):
        shutil.copy2(os.path.join(SERVER_MOD, fn), os.path.join(dst_hc, fn))

    os.makedirs(HARNESS_DIR, exist_ok=True)
    with open(os.path.join(HARNESS_DIR, "modinfo.lua"), "w", encoding="utf-8") as f:
        f.write(HARNESS_MODINFO)
    with open(os.path.join(HARNESS_DIR, "modmain.lua"), "w", encoding="utf-8") as f:
        f.write(HARNESS_MODMAIN)

    overrides_path = os.path.join(CLUSTER_DIR, "Master", "modoverrides.lua")
    bb_export_path = os.path.join(BIN64, "dst_hypercore_blackbox.json")
    if os.path.exists(bb_export_path):
        os.remove(bb_export_path)
    try:
        with open(overrides_path, "w", encoding="utf-8") as f:
            f.write(
                'return {\n'
                '  ["hypercore_server"] = { enabled = true, configuration_options = {} },\n'
                f'  ["{HARNESS_NAME}"] = {{ enabled = true, configuration_options = {{}} }},\n'
                '}\n'
            )

        exe = os.path.join(BIN64, "dontstarve_dedicated_server_nullrenderer_x64.exe")
        proc = subprocess.run(
            [exe, "-console", "-offline", "-port", "11019",
             "-steam_master_server_port", "27032", "-steam_authentication_port", "8782",
             "-persistent_storage_root", STORAGE,
             "-cluster", CLUSTER_NAME, "-shard", "Master"],
            cwd=BIN64,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=90,
        )
        out = proc.stdout + "\n" + proc.stderr
        for line in out.splitlines():
            if "[SERVER_V14_TEST]" in line or "LUA ERROR" in line or "[HyperCore" in line or "FastJson" in line or "HyperCore Server" in line:
                print(line)
        required = (
            "[SERVER_V14_TEST] ALL_PASS",
            "v1.6.0",
            "FastJson",
            "RepFast(true)",
            "RpcQoS(true)",
        )
        if proc.returncode != 0 or any(tok not in out for tok in required):
            print(f"=== FAILURE TAIL (rc={proc.returncode}) ===")
            print("\n".join(out.splitlines()[-60:]))
            sys.exit(1)
        import json
        assert os.path.isfile(bb_export_path), f"Missing exported blackbox file at {bb_export_path}"
        with open(bb_export_path, "r", encoding="utf-8") as bf:
            bb_doc = json.load(bf)
        assert bb_doc["role"] == "server" and bb_doc["total"] >= 1 and bb_doc["count"] >= 1
        assert bb_doc["events"][0]["culprit"] == "SRV_LUA"
        print(f"[PASS] Exported dst_hypercore_blackbox.json verified ({bb_doc['count']} event(s), top={bb_doc['events'][0]['culprit']})")
    finally:
        if os.path.exists(bb_export_path):
            os.remove(bb_export_path)
        shutil.rmtree(HARNESS_DIR, ignore_errors=True)
        shutil.rmtree(CLUSTER_DIR, ignore_errors=True)


if __name__ == "__main__":
    main()
