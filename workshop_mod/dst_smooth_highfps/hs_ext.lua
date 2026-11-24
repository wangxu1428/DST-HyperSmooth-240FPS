-- =============================================================================
-- DST HyperSmooth Standard Shader Extension Runtime (HS_EXT_V1)
--   "Compatible with a Standard, not with every individual mod."
--
-- Features:
--   1. Load-order independent registration (GLOBAL.HS_EXT_QUEUE + GLOBAL.HyperSmooth)
--   2. Strict transactional spec validation (malformed specs isolated, never crash)
--   3. 6 standardized PostProcessor execution stages + deterministic sorting
--   4. Exclusive group arbitration (e.g. sharpen / bloom / tonemap)
--   5. Shared zero-duplicate Context Uniform Bus (HS_CTX_TIME/SUN/ENV/CAM)
--   6. Multi-pass offscreen subpasses + custom texture samplers + automatic U0..U3 packing
--   7. Per-plugin xpcall fault-isolation circuit breaker + 1-click Master Bypass
--   8. Native C++ hardware GL_TIME_ELAPSED GPU cost telemetry bridge (dinput8.dll)
--   9. Full persistence & hypersmooth_presets.json "plugins" integration
-- =============================================================================

local G = GLOBAL
local type = G.type
local pairs = G.pairs
local ipairs = G.ipairs
local pcall = G.pcall
local error = G.error
local tonumber = G.tonumber
local tostring = G.tostring
local rawget = G.rawget
local rawset = G.rawset
local math = G.math
local string = G.string
local table = G.table
local unpack = G.unpack or table.unpack

local ABI_VERSION = 1

local STAGE_ORDER = {
    "PRE_BLOOM",
    "BLOOM",
    "POST_BLOOM",
    "COLOR_GRADE",
    "POST_SHARPEN",
    "FINAL_FX",
}

local STAGE_RANK = {}
for i = 1, #STAGE_ORDER do
    STAGE_RANK[STAGE_ORDER[i]] = i
end

local VALID_CONTEXTS = {
    TIME = "HS_CTX_TIME",
    SUN  = "HS_CTX_SUN",
    ENV  = "HS_CTX_ENV",
    CAM  = "HS_CTX_CAM",
}

local COMP_INDEX = { x = 1, y = 2, z = 3, w = 4, r = 1, g = 2, b = 3, a = 4 }

local function IsFiniteNum(v)
    return type(v) == "number" and v == v and v ~= math.huge and v ~= -math.huge
end

local function SanitizeUniformToken(id)
    local s = tostring(id or ""):upper():gsub("[^A-Z0-9]+", "_")
    s = s:gsub("^_+", ""):gsub("_+$", "")
    if #s > 28 then s = s:sub(1, 28) end
    if #s == 0 then s = "PLUGIN" end
    return "HSEXT_" .. s .. "_"
end

local function ExtractLocalized(val, fallback)
    if type(val) == "table" then
        local zh = (type(val.zh) == "string" and #val.zh > 0) and val.zh or nil
        local en = (type(val.en) == "string" and #val.en > 0) and val.en or nil
        return zh or en or fallback, en or zh or fallback
    elseif type(val) == "string" and #val > 0 then
        return val, val
    end
    return fallback, fallback
end

local function ParseSlotSpec(slot_str)
    if type(slot_str) ~= "string" then return nil, nil end
    local u_idx, comp = slot_str:match("^[Uu]([0-3])%.([xyzwrgba])$")
    if not u_idx or not comp then return nil, nil end
    return tonumber(u_idx) + 1, COMP_INDEX[comp]
end

local function FormatSlotSpec(vec_idx, comp_idx)
    local comps = { "x", "y", "z", "w" }
    return "U" .. tostring(vec_idx - 1) .. "." .. (comps[comp_idx] or "x")
end

-- Runtime state
local plugins_by_id = {}      -- id -> plugin record
local plugins_ordered = {}    -- sorted list of plugin records
local rejected_records = {}   -- { { id, reason } } for diagnostics
local unloaded_saved = {}     -- persisted plugin configs for currently uninstalled plugins
local master_bypass = false
local shaders_built = false
local shaders_sorted = false
local ctx_uniform_ids = {}    -- "TIME" -> PostProcessor uniform id
local active_ctx_subs = { TIME = 0, SUN = 0, ENV = 0, CAM = 0 }
local on_state_changed_cb = nil
local resolve_zh_fn = function() return true end
local ext_elapsed_time = 0.0
local ext_frame_counter = 0

local function ResolvePath(rel)
    local rfp = rawget(G, "resolvefilepath")
    if type(rfp) == "function" then
        local ok, resolved = pcall(rfp, rel)
        if ok and type(resolved) == "string" and #resolved > 0 then
            return resolved
        end
    end
    return rel
end

local function ClampParamValue(pdef, val)
    if pdef.type == "bool" then
        if type(val) == "boolean" then return val and 1.0 or 0.0 end
        local n = tonumber(val) or 0.0
        return (n > 0.5) and 1.0 or 0.0
    end
    local n = tonumber(val)
    if not IsFiniteNum(n) then return pdef.default end
    if n < pdef.min then n = pdef.min end
    if n > pdef.max then n = pdef.max end
    return n
end

-- Strict read-only validation & normalization of a third-party plugin spec
local function ValidateAndNormalizeSpec(spec)
    if type(spec) ~= "table" then
        return nil, "spec must be a table"
    end
    if spec.abi ~= ABI_VERSION then
        return nil, "unsupported abi (expected " .. tostring(ABI_VERSION) .. ", got " .. tostring(spec.abi) .. ")"
    end
    local id = spec.id
    if type(id) ~= "string" or #id < 3 or #id > 48 or not id:match("^[a-z0-9][a-z0-9_.-]*$") then
        return nil, "invalid plugin id (must be 3..48 lowercase [a-z0-9_.-])"
    end
    if id:sub(1, 7) == "__core." then
        return nil, "reserved namespace '__core.'"
    end
    if type(spec.stage) ~= "string" or STAGE_RANK[spec.stage] == nil then
        return nil, "invalid stage '" .. tostring(spec.stage) .. "'"
    end
    if type(spec.shader) ~= "string" or #spec.shader < 5 or not spec.shader:lower():match("%.ksh$") then
        return nil, "shader must be a .ksh path string"
    end

    local name_zh, name_en = ExtractLocalized(spec.name, id)
    local desc_zh, desc_en = ExtractLocalized(spec.description, "")
    local author = (type(spec.author) == "string" and #spec.author > 0) and spec.author or "Unknown"
    local version = (type(spec.version) == "string" and #spec.version > 0) and spec.version or "1.0.0"
    local order = IsFiniteNum(spec.order) and spec.order or 100
    local excl_group = (type(spec.exclusive_group) == "string" and #spec.exclusive_group > 0) and spec.exclusive_group or nil
    local default_enabled = (spec.default_enabled == true)

    local u_prefix = (type(spec.uniform_prefix) == "string" and spec.uniform_prefix:match("^[A-Za-z_][A-Za-z0-9_]*$"))
        and spec.uniform_prefix
        or SanitizeUniformToken(id)

    -- Validate context subscriptions
    local contexts = {}
    local ctx_set = {}
    if spec.context ~= nil then
        if type(spec.context) ~= "table" then
            return nil, "context must be an array of strings"
        end
        for i = 1, #spec.context do
            local ck = spec.context[i]
            if type(ck) ~= "string" or VALID_CONTEXTS[ck] == nil then
                return nil, "unknown context uniform '" .. tostring(ck) .. "'"
            end
            if not ctx_set[ck] then
                ctx_set[ck] = true
                contexts[#contexts + 1] = ck
            end
        end
    end

    -- Validate params (max 16 -> packs cleanly into up to 4 vec4 uniforms U0..U3)
    local params = {}
    local params_by_key = {}
    local slot_used = {}
    local max_vec_idx = 0
    local auto_cursor = 0
    if spec.params ~= nil then
        if type(spec.params) ~= "table" then
            return nil, "params must be an array"
        end
        if #spec.params > 16 then
            return nil, "too many params (max 16 per HS_EXT_V1 plugin)"
        end
        for i = 1, #spec.params do
            local pd = spec.params[i]
            if type(pd) ~= "table" then
                return nil, "param #" .. i .. " must be a table"
            end
            local pk = pd.key
            if type(pk) ~= "string" or #pk < 1 or #pk > 32 or not pk:match("^[a-zA-Z_][a-zA-Z0-9_]*$") then
                return nil, "invalid param key at #" .. i
            end
            if params_by_key[pk] ~= nil then
                return nil, "duplicate param key '" .. pk .. "'"
            end
            local ptype = (pd.type == "bool") and "bool" or "float"
            local pmin, pmax, pstep, pdef, pscale, pfmt
            if ptype == "bool" then
                pmin, pmax, pstep = 0.0, 1.0, 1.0
                if type(pd.default) == "boolean" then
                    pdef = pd.default and 1.0 or 0.0
                else
                    pdef = ((tonumber(pd.default) or 0) > 0.5) and 1.0 or 0.0
                end
                pscale, pfmt = 1, "%.0f"
            else
                pmin = tonumber(pd.min)
                pmax = tonumber(pd.max)
                if not (IsFiniteNum(pmin) and IsFiniteNum(pmax) and pmax > pmin) then
                    return nil, "param '" .. pk .. "' requires finite min < max"
                end
                pdef = tonumber(pd.default)
                if not IsFiniteNum(pdef) then pdef = pmin end
                if pdef < pmin or pdef > pmax then
                    return nil, "param '" .. pk .. "' default out of [min, max]"
                end
                pstep = tonumber(pd.step)
                if not (IsFiniteNum(pstep) and pstep > 0) then
                    pstep = (pmax - pmin) * 0.05
                end
                pscale = IsFiniteNum(pd.scale) and pd.scale or 1
                pfmt = (type(pd.fmt) == "string" and #pd.fmt > 0) and pd.fmt or "%.2f"
            end

            local v_idx, c_idx
            if pd.slot ~= nil or pd.uniform ~= nil then
                v_idx, c_idx = ParseSlotSpec(pd.slot or pd.uniform)
                if not v_idx then
                    return nil, "param '" .. pk .. "' invalid slot (expected U0.x..U3.w)"
                end
            else
                while auto_cursor < 16 do
                    local tv = math.floor(auto_cursor / 4) + 1
                    local tc = (auto_cursor % 4) + 1
                    auto_cursor = auto_cursor + 1
                    if not slot_used[tv * 8 + tc] then
                        v_idx, c_idx = tv, tc
                        break
                    end
                end
            end
            if not v_idx or slot_used[v_idx * 8 + c_idx] then
                return nil, "param '" .. pk .. "' uniform slot collision"
            end
            slot_used[v_idx * 8 + c_idx] = true
            if v_idx > max_vec_idx then max_vec_idx = v_idx end

            local lzh, len = ExtractLocalized(pd.label, pk)
            local norm_p = {
                key     = pk,
                type    = ptype,
                min     = pmin,
                max     = pmax,
                step    = pstep,
                default = pdef,
                scale   = pscale,
                fmt     = pfmt,
                zh      = lzh,
                en      = len,
                vec_idx = v_idx,
                comp    = c_idx,
                slot    = FormatSlotSpec(v_idx, c_idx),
            }
            params[#params + 1] = norm_p
            params_by_key[pk] = norm_p
        end
    end

    -- Validate optional subpasses (max 4 offscreen SamplerEffects)
    local subpasses = {}
    local subpass_names = {}
    if spec.subpasses ~= nil then
        if type(spec.subpasses) ~= "table" or #spec.subpasses > 4 then
            return nil, "subpasses must be an array of at most 4 items"
        end
        for i = 1, #spec.subpasses do
            local sp = spec.subpasses[i]
            if type(sp) ~= "table" or type(sp.name) ~= "string" or #sp.name == 0 then
                return nil, "subpass #" .. i .. " missing valid name"
            end
            if subpass_names[sp.name] then
                return nil, "duplicate subpass name '" .. sp.name .. "'"
            end
            if type(sp.shader) ~= "string" or not sp.shader:lower():match("%.ksh$") then
                return nil, "subpass '" .. sp.name .. "' requires .ksh shader path"
            end
            local smode = (sp.size_mode == "STATIC") and "STATIC" or "RELATIVE"
            local sw = tonumber(sp.w) or ((smode == "STATIC") and 256 or 0.5)
            local sh = tonumber(sp.h) or ((smode == "STATIC") and 256 or 0.5)
            if not (IsFiniteNum(sw) and IsFiniteNum(sh) and sw > 0 and sh > 0) then
                return nil, "subpass '" .. sp.name .. "' invalid dimensions"
            end
            local sbase = sp.base or "BLOOM_SAMPLER"
            if sbase ~= "BLOOM_SAMPLER" and sbase ~= "SUBPASS" and sbase ~= "TEXTURE" and sbase ~= "SMOKE" then
                return nil, "subpass '" .. sp.name .. "' invalid base '" .. tostring(sbase) .. "'"
            end
            if sbase == "SUBPASS" and not subpass_names[sp.source] then
                return nil, "subpass '" .. sp.name .. "' references unknown earlier subpass '" .. tostring(sp.source) .. "'"
            end
            subpass_names[sp.name] = true
            subpasses[#subpasses + 1] = {
                name           = sp.name,
                shader         = sp.shader,
                size_mode      = smode,
                w              = sw,
                h              = sh,
                base           = sbase,
                source         = sp.source,
                extra_samplers = (type(sp.extra_samplers) == "table") and sp.extra_samplers or nil,
                use_params     = (sp.use_params == true),
            }
        end
    end

    -- Validate optional main pass samplers (max 6)
    local samplers = {}
    if spec.samplers ~= nil then
        if type(spec.samplers) ~= "table" or #spec.samplers > 6 then
            return nil, "samplers must be an array of at most 6 items"
        end
        for i = 1, #spec.samplers do
            local sm = spec.samplers[i]
            if type(sm) ~= "table" or type(sm.base) ~= "string" then
                return nil, "sampler #" .. i .. " invalid"
            end
            if sm.base == "SUBPASS" then
                if not subpass_names[sm.name or sm.source] then
                    return nil, "sampler #" .. i .. " references unknown subpass"
                end
            elseif sm.base == "TEXTURE" then
                if type(sm.path) ~= "string" or #sm.path == 0 then
                    return nil, "texture sampler #" .. i .. " missing path"
                end
            elseif sm.base ~= "BLOOM_SAMPLER" and sm.base ~= "SMOKE" then
                return nil, "unsupported sampler base '" .. tostring(sm.base) .. "'"
            end
            samplers[#samplers + 1] = {
                base   = sm.base,
                name   = sm.name or sm.source,
                path   = sm.path,
                filter = sm.filter or "LINEAR",
                wrap   = sm.wrap or "CLAMP",
            }
        end
    end

    local values = {}
    for i = 1, #params do
        values[params[i].key] = params[i].default
    end

    return {
        abi             = ABI_VERSION,
        id              = id,
        version         = version,
        name_zh         = name_zh,
        name_en         = name_en,
        desc_zh         = desc_zh,
        desc_en         = desc_en,
        author          = author,
        stage           = spec.stage,
        stage_rank      = STAGE_RANK[spec.stage],
        order           = order,
        exclusive_group = excl_group,
        default_enabled = default_enabled,
        enabled         = default_enabled,
        shader          = spec.shader,
        uniform_prefix  = u_prefix,
        vec_count       = max_vec_idx,
        contexts        = contexts,
        ctx_set         = ctx_set,
        params          = params,
        params_by_key   = params_by_key,
        values          = values,
        subpasses       = subpasses,
        samplers        = samplers,
        on_init         = (type(spec.on_init) == "function") and spec.on_init or nil,
        on_enable       = (type(spec.on_enable) == "function") and spec.on_enable or nil,
        on_disable      = (type(spec.on_disable) == "function") and spec.on_disable or nil,
        on_frame        = (type(spec.on_frame) == "function") and spec.on_frame or nil,
        -- Runtime GPU handles & fault state
        compiled        = false,
        faulted         = false,
        fault_msg       = nil,
        pp_effect_id    = nil,
        pp_uniform_ids  = {}, -- [1..vec_count] -> uniform handle
        pp_subpass_ids  = {}, -- name -> sampler effect handle
        pp_tex_ids      = {}, -- path -> texture sampler handle
        dirty_uniforms  = true,
    }, nil
end

local function ResortPluginList()
    plugins_ordered = {}
    for _, p in pairs(plugins_by_id) do
        plugins_ordered[#plugins_ordered + 1] = p
    end
    table.sort(plugins_ordered, function(a, b)
        if a.stage_rank ~= b.stage_rank then return a.stage_rank < b.stage_rank end
        if a.order ~= b.order then return a.order < b.order end
        return a.id < b.id
    end)
end

local function RecomputeActiveCtxSubs()
    active_ctx_subs.TIME = 0
    active_ctx_subs.SUN  = 0
    active_ctx_subs.ENV  = 0
    active_ctx_subs.CAM  = 0
    if master_bypass then return end
    for i = 1, #plugins_ordered do
        local p = plugins_ordered[i]
        if p.compiled and not p.faulted and p.enabled then
            for j = 1, #p.contexts do
                local ck = p.contexts[j]
                active_ctx_subs[ck] = (active_ctx_subs[ck] or 0) + 1
            end
        end
    end
end

local function TripCircuitBreaker(p, phase, err)
    p.faulted = true
    p.enabled = false
    p.fault_msg = tostring(phase) .. ": " .. tostring(err)
    local pp = rawget(G, "PostProcessor")
    if pp and p.pp_effect_id ~= nil and type(pp.EnablePostProcessEffect) == "function" then
        pcall(pp.EnablePostProcessEffect, pp, p.pp_effect_id, false)
    end
    RecomputeActiveCtxSubs()
    if G.print then
        G.print(string.format("[HyperSmooth EXT][ISOLATED] Plugin '%s' disabled after error in %s: %s",
            p.id, tostring(phase), tostring(err)))
    end
end

local function PushPluginUniforms(p)
    if not (p.compiled and not p.faulted) or p.vec_count <= 0 then return end
    local pp = rawget(G, "PostProcessor")
    if not (pp and type(pp.SetUniformVariable) == "function") then return end
    local vecs = {}
    for v = 1, p.vec_count do
        vecs[v] = { 0.0, 0.0, 0.0, 0.0 }
    end
    for i = 1, #p.params do
        local pd = p.params[i]
        vecs[pd.vec_idx][pd.comp] = p.values[pd.key] or pd.default
    end
    for v = 1, p.vec_count do
        local uid = p.pp_uniform_ids[v]
        if uid ~= nil then
            local ok_u, err_u = pcall(pp.SetUniformVariable, pp, uid, vecs[v][1], vecs[v][2], vecs[v][3], vecs[v][4])
            if not ok_u then
                TripCircuitBreaker(p, "SetUniform", err_u)
                return
            end
        end
    end
    p.dirty_uniforms = false
end

local function SyncPluginEffectState(p, fire_callbacks)
    local pp = rawget(G, "PostProcessor")
    local want_on = (not master_bypass) and p.compiled and (not p.faulted) and (p.enabled == true)
    if pp and p.pp_effect_id ~= nil and type(pp.EnablePostProcessEffect) == "function" then
        if want_on and p.dirty_uniforms then
            PushPluginUniforms(p)
            want_on = (not master_bypass) and p.compiled and (not p.faulted) and (p.enabled == true)
        end
        local ok_e, err_e = pcall(pp.EnablePostProcessEffect, pp, p.pp_effect_id, want_on)
        if not ok_e then
            TripCircuitBreaker(p, "EnableEffect", err_e)
            return
        end
    end
    if fire_callbacks and not p.faulted then
        if want_on and p.on_enable then
            local ok_c, err_c = pcall(p.on_enable, { id = p.id, values = p.values })
            if not ok_c then TripCircuitBreaker(p, "on_enable", err_c) end
        elseif (not want_on) and p.on_disable then
            local ok_c, err_c = pcall(p.on_disable, { id = p.id, values = p.values })
            if not ok_c then TripCircuitBreaker(p, "on_disable", err_c) end
        end
    end
end

local function SyncAllPluginStates(fire_callbacks)
    RecomputeActiveCtxSubs()
    for i = 1, #plugins_ordered do
        SyncPluginEffectState(plugins_ordered[i], fire_callbacks == true)
    end
end

local function NotifyStateChanged()
    if type(on_state_changed_cb) == "function" then
        pcall(on_state_changed_cb)
    end
end

-- Public Registration (idempotent by spec.id; safe before or after HyperSmooth init)
local function RegisterPlugin(spec)
    local norm, err = ValidateAndNormalizeSpec(spec)
    if not norm then
        local bad_id = (type(spec) == "table" and type(spec.id) == "string") and spec.id or "<unknown>"
        rejected_records[#rejected_records + 1] = { id = bad_id, reason = tostring(err) }
        if G.print then
            G.print("[HyperSmooth EXT] Rejected plugin '" .. bad_id .. "': " .. tostring(err))
        end
        return false, err
    end
    -- Idempotent dedup: if already registered (e.g. via HS_EXT_QUEUE + explicit call), succeed cleanly
    if plugins_by_id[norm.id] ~= nil then
        return true, norm.id
    end
    if shaders_built then
        -- Klei only runs BuildModShaders() once at boot -> late registration cannot allocate GPU passes!
        norm.faulted = true
        norm.enabled = false
        norm.fault_msg = "late_register: must register before AddModShadersInit (top-level modmain)"
    end
    -- Apply any previously persisted saved state for this plugin ID (loaded from hypersmooth_presets.json)
    local saved = unloaded_saved[norm.id]
    if type(saved) == "table" then
        if type(saved.enabled) == "boolean" and not norm.faulted then
            norm.enabled = saved.enabled
        end
        if type(saved.params) == "table" then
            for k, v in pairs(saved.params) do
                local pd = norm.params_by_key[k]
                if pd then
                    norm.values[k] = ClampParamValue(pd, v)
                end
            end
        end
        unloaded_saved[norm.id] = nil
    end
    plugins_by_id[norm.id] = norm
    ResortPluginList()
    return true, norm.id
end

local function DrainGlobalQueue()
    local q = rawget(G, "HS_EXT_QUEUE")
    if type(q) ~= "table" then return end
    for i = 1, #q do
        RegisterPlugin(q[i])
    end
end

-- Enforce exclusive_group mutual exclusion cleanly
local function ApplyExclusiveGroupFor(target)
    if not (target.enabled and target.exclusive_group) then return end
    for i = 1, #plugins_ordered do
        local other = plugins_ordered[i]
        if other ~= target and other.enabled and other.exclusive_group == target.exclusive_group then
            other.enabled = false
            SyncPluginEffectState(other, true)
        end
    end
end

local function SetPluginEnabled(id, enabled)
    local p = plugins_by_id[id]
    if not p then return false, "unknown plugin" end
    if p.faulted then return false, p.fault_msg or "plugin isolated" end
    local want = (enabled == true)
    if p.enabled == want then return true end
    p.enabled = want
    if want then
        ApplyExclusiveGroupFor(p)
    end
    RecomputeActiveCtxSubs()
    SyncPluginEffectState(p, true)
    NotifyStateChanged()
    return true
end

local function SetPluginParam(id, key, val)
    local p = plugins_by_id[id]
    if not p then return false, "unknown plugin" end
    local pd = p.params_by_key[key]
    if not pd then return false, "unknown param '" .. tostring(key) .. "'" end
    local clamped = ClampParamValue(pd, val)
    p.values[key] = clamped
    p.dirty_uniforms = true
    if p.compiled and not p.faulted and p.enabled and not master_bypass then
        PushPluginUniforms(p)
    end
    NotifyStateChanged()
    return true, clamped
end

local function ResetPlugin(id)
    local p = plugins_by_id[id]
    if not p then return false, "unknown plugin" end
    for i = 1, #p.params do
        local pd = p.params[i]
        p.values[pd.key] = pd.default
    end
    p.dirty_uniforms = true
    if p.compiled and not p.faulted and p.enabled and not master_bypass then
        PushPluginUniforms(p)
    end
    NotifyStateChanged()
    return true
end

local function SetMasterBypass(bypass)
    local b = (bypass == true)
    if master_bypass == b then return master_bypass end
    master_bypass = b
    SyncAllPluginStates(true)
    NotifyStateChanged()
    return master_bypass
end

-- Phase A: Called inside HyperSmooth's AddModShadersInit AFTER Core's own shaders are registered
local function BuildShaders()
    DrainGlobalQueue()
    shaders_built = true

    local pp  = rawget(G, "PostProcessor")
    local uv  = rawget(G, "UniformVariables")
    local SS  = rawget(G, "SamplerSizes")
    local SCM = rawget(G, "SamplerColourMode")
    local SEB = rawget(G, "SamplerEffectBase")
    local FM  = rawget(G, "FILTER_MODE")
    local MFM = rawget(G, "MIP_FILTER_MODE")
    local WM  = rawget(G, "WRAP_MODE")
    if not (pp and uv and SS and SCM and SEB and FM and MFM and WM) then
        return
    end

    -- Register built-in Core GPU profiler probes so UI can compare Core vs Plugin GPU cost
    local fnProbe = rawget(G, "HyperSmooth_ExtRegisterProbe")
    if type(fnProbe) == "function" then
        pcall(fnProbe, "__core.bloom", "HS_BLOOM_PARAMS")
        pcall(fnProbe, "__core.visual_final", "HS_VISUAL_PARAMS")
    end

    if #plugins_ordered == 0 then
        return
    end

    -- Allocate the 4 shared HS_CTX_* uniforms ONCE across all plugins
    for ck, uname in pairs(VALID_CONTEXTS) do
        local ok_u, uid = pcall(pp.AddUniformVariable, pp, uname, 4)
        if ok_u and uid ~= nil then
            uv[uname] = uid
            ctx_uniform_ids[ck] = uid
        end
    end

    -- Compile each registered plugin inside its own strict fault-isolation boundary
    for i = 1, #plugins_ordered do
        local p = plugins_ordered[i]
        if not p.faulted then
            local ok_p, err_p = pcall(function()
                -- 1) Allocate plugin's U0..U3 uniforms
                local bound_unis = {}
                local probe_uni_name = nil
                for v = 1, p.vec_count do
                    local uname = p.uniform_prefix .. "U" .. tostring(v - 1)
                    if not probe_uni_name then probe_uni_name = uname end
                    local uid = pp:AddUniformVariable(uname, 4)
                    if uid == nil then error("AddUniformVariable failed for " .. uname) end
                    uv[uname] = uid
                    p.pp_uniform_ids[v] = uid
                    bound_unis[#bound_unis + 1] = uid
                end
                for j = 1, #p.contexts do
                    local cuid = ctx_uniform_ids[p.contexts[j]]
                    if cuid ~= nil then
                        bound_unis[#bound_unis + 1] = cuid
                    end
                end

                -- 2) Register native GPU profiler probe BEFORE AddPostProcessEffect so LinkProgram sees it!
                if type(fnProbe) == "function" then
                    pcall(fnProbe, p.id, probe_uni_name)
                end

                -- 3) Allocate any custom texture samplers declared by subpasses or main pass
                local function GetOrAllocTex(sm)
                    local rpath = ResolvePath(sm.path)
                    if p.pp_tex_ids[rpath] ~= nil then return p.pp_tex_ids[rpath] end
                    local tid = pp:AddTextureSampler(rpath)
                    if tid == nil then error("AddTextureSampler failed: " .. tostring(rpath)) end
                    if type(pp.SetTextureSamplerFilter) == "function" then
                        local flt = (sm.filter == "POINT") and (FM.POINT or FM.LINEAR) or FM.LINEAR
                        pcall(pp.SetTextureSamplerFilter, pp, tid, flt, flt, MFM.NONE)
                    end
                    if type(pp.SetTextureSamplerState) == "function" then
                        local wrm = (sm.wrap == "REPEAT") and (WM.REPEAT or WM.CLAMP_TO_EDGE) or WM.CLAMP_TO_EDGE
                        pcall(pp.SetTextureSamplerState, pp, tid, wrm)
                    end
                    p.pp_tex_ids[rpath] = tid
                    return tid
                end

                -- 4) Allocate optional multi-pass offscreen subpasses
                for s = 1, #p.subpasses do
                    local sp = p.subpasses[s]
                    local s_path = ResolvePath(sp.shader)
                    local s_sz = (sp.size_mode == "STATIC") and (SS.Static or SS.Relative) or SS.Relative
                    local se_id
                    if sp.base == "BLOOM_SAMPLER" then
                        se_id = pp:AddSamplerEffect(s_path, s_sz, sp.w, sp.h, SCM.RGB, SEB.BloomSampler)
                    elseif sp.base == "SMOKE" then
                        se_id = pp:AddSamplerEffect(s_path, s_sz, sp.w, sp.h, SCM.RGB, SEB.Smoke)
                    elseif sp.base == "SUBPASS" then
                        se_id = pp:AddSamplerEffect(s_path, s_sz, sp.w, sp.h, SCM.RGB, SEB.Shader, p.pp_subpass_ids[sp.source])
                    elseif sp.base == "TEXTURE" then
                        local tid = GetOrAllocTex({ path = sp.source, filter = "LINEAR", wrap = "CLAMP" })
                        se_id = pp:AddSamplerEffect(s_path, s_sz, sp.w, sp.h, SCM.RGB, SEB.Texture, tid)
                    end
                    if se_id == nil then error("AddSamplerEffect failed for subpass " .. sp.name) end
                    if type(sp.extra_samplers) == "table" then
                        for k = 1, #sp.extra_samplers do
                            local es = sp.extra_samplers[k]
                            if es.base == "SUBPASS" and p.pp_subpass_ids[es.name or es.source] then
                                pp:AddSampler(se_id, SEB.Shader, p.pp_subpass_ids[es.name or es.source])
                            elseif es.base == "BLOOM_SAMPLER" then
                                pp:AddSampler(se_id, SEB.BloomSampler)
                            elseif es.base == "TEXTURE" and es.path then
                                pp:AddSampler(se_id, SEB.Texture, GetOrAllocTex(es))
                            end
                        end
                    end
                    local sp_unis = {}
                    if uv.SAMPLER_PARAMS ~= nil then sp_unis[#sp_unis + 1] = uv.SAMPLER_PARAMS end
                    if sp.use_params then
                        for b = 1, #bound_unis do sp_unis[#sp_unis + 1] = bound_unis[b] end
                    end
                    if #sp_unis > 0 then
                        pp:SetEffectUniformVariables(se_id, unpack(sp_unis))
                    end
                    if type(pp.SetSamplerEffectState) == "function" then
                        pp:SetSamplerEffectState(se_id, WM.CLAMP_TO_EDGE)
                    end
                    if type(pp.SetSamplerEffectFilter) == "function" then
                        pp:SetSamplerEffectFilter(se_id, FM.LINEAR, FM.LINEAR, MFM.NONE)
                    end
                    p.pp_subpass_ids[sp.name] = se_id
                end

                -- 5) Allocate main fullscreen PostProcessEffect
                local main_path = ResolvePath(p.shader)
                local eff_id = pp:AddPostProcessEffect(main_path)
                if eff_id == nil then
                    error("AddPostProcessEffect failed for " .. tostring(main_path))
                end
                for s = 1, #p.samplers do
                    local sm = p.samplers[s]
                    if sm.base == "BLOOM_SAMPLER" then
                        pp:AddSampler(eff_id, SEB.BloomSampler)
                    elseif sm.base == "SMOKE" then
                        pp:AddSampler(eff_id, SEB.Smoke)
                    elseif sm.base == "SUBPASS" then
                        pp:AddSampler(eff_id, SEB.Shader, p.pp_subpass_ids[sm.name])
                    elseif sm.base == "TEXTURE" then
                        pp:AddSampler(eff_id, SEB.Texture, GetOrAllocTex(sm))
                    end
                end
                if #bound_unis > 0 then
                    pp:SetEffectUniformVariables(eff_id, unpack(bound_unis))
                end

                p.pp_effect_id = eff_id
                p.compiled = true
                p.dirty_uniforms = true

                if p.on_init then
                    p.on_init({ id = p.id, values = p.values, effect_id = eff_id })
                end
            end)
            if not ok_p then
                TripCircuitBreaker(p, "BuildShaders", err_p)
            end
        end
    end
end

-- Phase B: Called inside HyperSmooth's AddModShadersSortAndEnable AFTER Core's own sort
local function SortAndEnable()
    shaders_sorted = true
    local pp  = rawget(G, "PostProcessor")
    local ppe = rawget(G, "PostProcessorEffects")
    if not (pp and ppe) then return end

    local bloom_anchor = ppe.HS_Bloom or ppe.Bloom
    local final_anchor = ppe.HS_VisualFinal
    local cc_anchor    = ppe.MoonPulseGrading or ppe.MoonPulse or ppe.Lunacy or ppe.ColourCube

    local stage_tail = {
        PRE_BLOOM    = nil,
        BLOOM        = bloom_anchor,
        POST_BLOOM   = nil,
        COLOR_GRADE  = nil,
        POST_SHARPEN = final_anchor or cc_anchor,
        FINAL_FX     = nil,
    }

    -- Enforce exclusive groups across initial default_enabled plugins before enabling
    local seen_excl = {}
    for i = 1, #plugins_ordered do
        local p = plugins_ordered[i]
        if p.compiled and not p.faulted and p.enabled and p.exclusive_group then
            if seen_excl[p.exclusive_group] then
                p.enabled = false
            else
                seen_excl[p.exclusive_group] = p.id
            end
        end
    end

    for i = 1, #plugins_ordered do
        local p = plugins_ordered[i]
        if p.compiled and not p.faulted and p.pp_effect_id ~= nil then
            local ok_s, err_s = pcall(function()
                local eff = p.pp_effect_id
                local st = p.stage
                if st == "PRE_BLOOM" then
                    if stage_tail.PRE_BLOOM ~= nil then
                        pp:SetPostProcessEffectAfter(eff, stage_tail.PRE_BLOOM)
                    elseif ppe.Bloom ~= nil and type(pp.SetPostProcessEffectBefore) == "function" then
                        pp:SetPostProcessEffectBefore(eff, ppe.Bloom)
                    elseif bloom_anchor ~= nil then
                        pp:SetPostProcessEffectAfter(eff, bloom_anchor)
                    end
                    stage_tail.PRE_BLOOM = eff
                elseif st == "BLOOM" then
                    if stage_tail.BLOOM ~= nil then
                        pp:SetPostProcessEffectAfter(eff, stage_tail.BLOOM)
                    end
                    stage_tail.BLOOM = eff
                elseif st == "POST_BLOOM" then
                    local anchor = stage_tail.POST_BLOOM or stage_tail.BLOOM or bloom_anchor
                    if anchor ~= nil then
                        pp:SetPostProcessEffectAfter(eff, anchor)
                    end
                    stage_tail.POST_BLOOM = eff
                elseif st == "COLOR_GRADE" then
                    if stage_tail.COLOR_GRADE ~= nil then
                        pp:SetPostProcessEffectAfter(eff, stage_tail.COLOR_GRADE)
                    elseif final_anchor ~= nil and type(pp.SetPostProcessEffectBefore) == "function" then
                        pp:SetPostProcessEffectBefore(eff, final_anchor)
                    elseif cc_anchor ~= nil then
                        pp:SetPostProcessEffectAfter(eff, cc_anchor)
                    end
                    stage_tail.COLOR_GRADE = eff
                elseif st == "POST_SHARPEN" then
                    local anchor = stage_tail.POST_SHARPEN or final_anchor or cc_anchor
                    if anchor ~= nil then
                        pp:SetPostProcessEffectAfter(eff, anchor)
                    end
                    stage_tail.POST_SHARPEN = eff
                elseif st == "FINAL_FX" then
                    local anchor = stage_tail.FINAL_FX or stage_tail.POST_SHARPEN or final_anchor or cc_anchor
                    if anchor ~= nil then
                        pp:SetPostProcessEffectAfter(eff, anchor)
                    end
                    stage_tail.FINAL_FX = eff
                end
            end)
            if not ok_s then
                TripCircuitBreaker(p, "SortAndEnable", err_s)
            end
        end
    end

    SyncAllPluginStates(true)
end

-- Per-frame / 10Hz dynamic context bus + optional plugin on_frame callback
local function Tick(dt, light_state)
    if #plugins_ordered == 0 or master_bypass then return end
    local pp = rawget(G, "PostProcessor")
    if not (pp and type(pp.SetUniformVariable) == "function") then return end

    local dt_s = IsFiniteNum(dt) and math.max(0.0, math.min(0.25, dt)) or 0.016
    ext_elapsed_time = ext_elapsed_time + dt_s
    ext_frame_counter = (ext_frame_counter + 1) % 60

    local w = rawget(G, "TheWorld")
    local wst = (w and type(w.state) == "table") and w.state or nil
    local p_inst = rawget(G, "ThePlayer")
    local cam = rawget(G, "TheCamera")

    local w_time = (wst and type(wst.time) == "number") and wst.time or 0.5
    local sun_sx = (light_state and light_state.sun_sx) or 0.25
    local sun_sy = (light_state and light_state.sun_sy) or 0.18
    local sun_el = (light_state and light_state.sun_elev) or 0.70
    local night_f = (light_state and light_state.day_factor ~= nil) and (1.0 - light_state.day_factor) or 0.0

    local rain_r = (wst and wst.israining and type(wst.precipitationrate) == "number") and wst.precipitationrate or 0.0
    local snow_l = (wst and type(wst.snowlevel) == "number") and wst.snowlevel or 0.0
    local is_cave = (w and type(w.HasTag) == "function" and w:HasTag("cave")) and 1.0 or 0.0
    local sanity = 1.0
    if p_inst and p_inst.replica and p_inst.replica.sanity and type(p_inst.replica.sanity.GetPercent) == "function" then
        local ok_s, sp = pcall(p_inst.replica.sanity.GetPercent, p_inst.replica.sanity)
        if ok_s and IsFiniteNum(sp) then sanity = sp end
    end

    local cam_zoom = (cam and type(cam.distance) == "number") and cam.distance or 30.0
    local cam_head = (cam and type(cam.heading) == "number") and (cam.heading * math.pi / 180.0) or 0.0
    local aspect = 16.0 / 9.0
    local sim = rawget(G, "TheSim")
    if sim and type(sim.GetScreenSize) == "function" then
        local ok_sz, sw, sh = pcall(sim.GetScreenSize, sim)
        if ok_sz and type(sw) == "number" and type(sh) == "number" and sh > 0 then
            aspect = sw / sh
        end
    end
    local is_paused = (type(rawget(G, "IsPaused")) == "function" and rawget(G, "IsPaused")()) and 1.0 or 0.0

    if active_ctx_subs.TIME > 0 and ctx_uniform_ids.TIME ~= nil then
        pcall(pp.SetUniformVariable, pp, ctx_uniform_ids.TIME, ext_elapsed_time, dt_s, ext_frame_counter / 60.0, w_time)
    end
    if active_ctx_subs.SUN > 0 and ctx_uniform_ids.SUN ~= nil then
        pcall(pp.SetUniformVariable, pp, ctx_uniform_ids.SUN, sun_sx, sun_sy, sun_el, night_f)
    end
    if active_ctx_subs.ENV > 0 and ctx_uniform_ids.ENV ~= nil then
        pcall(pp.SetUniformVariable, pp, ctx_uniform_ids.ENV, rain_r, snow_l, is_cave, sanity)
    end
    if active_ctx_subs.CAM > 0 and ctx_uniform_ids.CAM ~= nil then
        pcall(pp.SetUniformVariable, pp, ctx_uniform_ids.CAM, cam_zoom, cam_head, aspect, is_paused)
    end

    -- Invoke optional plugin on_frame hooks under strict fault isolation
    local frame_ctx = nil
    for i = 1, #plugins_ordered do
        local p = plugins_ordered[i]
        if p.compiled and not p.faulted and p.enabled and p.on_frame then
            if not frame_ctx then
                frame_ctx = {
                    time = ext_elapsed_time, dt = dt_s, world_time = w_time,
                    sun_x = sun_sx, sun_y = sun_sy, sun_elev = sun_el, night = night_f,
                    rain = rain_r, snow = snow_l, is_cave = (is_cave > 0.5), sanity = sanity,
                    cam_zoom = cam_zoom, cam_heading = cam_head, aspect = aspect,
                }
            end
            local ok_f, err_f = pcall(p.on_frame, frame_ctx, p.values)
            if not ok_f then
                TripCircuitBreaker(p, "on_frame", err_f)
            elseif p.dirty_uniforms then
                PushPluginUniforms(p)
            end
        end
    end
end

local function QueryNativeGpuStats(id)
    local fn = rawget(G, "HyperSmooth_ExtGetGpuStats")
    if type(fn) == "function" then
        local ok, progs, ema, peak, draws, is_hw = pcall(fn, id)
        if ok then
            return {
                has_dll = true,
                progs   = tonumber(progs) or 0,
                ema_ms  = tonumber(ema) or 0.0,
                peak_ms = tonumber(peak) or 0.0,
                draws   = tonumber(draws) or 0,
                is_hw   = (is_hw == true),
            }
        end
    end
    return { has_dll = false, progs = 0, ema_ms = 0.0, peak_ms = 0.0, draws = 0, is_hw = false }
end

-- Persistence Bridge for hypersmooth_presets.json "plugins" section
local function ExportPersistedMap()
    local out = {}
    -- Preserve saved settings for temporarily unloaded plugins so toggling mods never loses config
    for uid, ucfg in pairs(unloaded_saved) do
        if type(ucfg) == "table" then
            local cp = {}
            if type(ucfg.params) == "table" then
                for k, v in pairs(ucfg.params) do cp[k] = v end
            end
            out[uid] = { enabled = (ucfg.enabled == true), params = cp }
        end
    end
    for i = 1, #plugins_ordered do
        local p = plugins_ordered[i]
        local pm = {}
        for j = 1, #p.params do
            local pd = p.params[j]
            if pd.type == "bool" then
                pm[pd.key] = (p.values[pd.key] or 0) > 0.5
            else
                pm[pd.key] = math.floor((p.values[pd.key] or pd.default) * 10000 + 0.5) / 10000
            end
        end
        out[p.id] = { enabled = (p.enabled == true), params = pm }
    end
    return out
end

local function ApplyPersistedMap(pmap)
    if type(pmap) ~= "table" then return 0 end
    local applied = 0
    for pid, cfg in pairs(pmap) do
        if type(pid) == "string" and type(cfg) == "table" then
            local p = plugins_by_id[pid]
            if p then
                local valid = true
                local staged_vals = {}
                if type(cfg.params) == "table" then
                    for k, v in pairs(cfg.params) do
                        local pd = p.params_by_key[k]
                        if pd then
                            if pd.type == "bool" then
                                if type(v) == "boolean" then
                                    staged_vals[k] = v and 1.0 or 0.0
                                elseif IsFiniteNum(v) then
                                    staged_vals[k] = (v > 0.5) and 1.0 or 0.0
                                else
                                    valid = false; break
                                end
                            else
                                if not IsFiniteNum(v) or v < (pd.min - 1e-4) or v > (pd.max + 1e-4) then
                                    valid = false; break
                                end
                                staged_vals[k] = ClampParamValue(pd, v)
                            end
                        end
                    end
                end
                if valid then
                    for k, v in pairs(staged_vals) do
                        p.values[k] = v
                    end
                    p.dirty_uniforms = true
                    if type(cfg.enabled) == "boolean" and not p.faulted then
                        p.enabled = cfg.enabled
                        if p.enabled then ApplyExclusiveGroupFor(p) end
                    end
                    applied = applied + 1
                end
            else
                -- Store passively for uninstalled plugins so re-export preserves them
                local safe_p = {}
                if type(cfg.params) == "table" then
                    for k, v in pairs(cfg.params) do
                        if type(k) == "string" and (IsFiniteNum(v) or type(v) == "boolean") then
                            safe_p[k] = v
                        end
                    end
                end
                unloaded_saved[pid] = { enabled = (cfg.enabled == true), params = safe_p }
            end
        end
    end
    if shaders_sorted then
        SyncAllPluginStates(false)
    end
    return applied
end

-- Snapshot for hs_panel.lua dynamic Tab 8 UI & console inspection
local function GetSnapshot()
    local list = {}
    local active_count = 0
    local fault_count = 0
    local total_gpu_ms = 0.0
    for i = 1, #plugins_ordered do
        local p = plugins_ordered[i]
        local gpu = QueryNativeGpuStats(p.id)
        if p.compiled and not p.faulted and p.enabled and not master_bypass then
            active_count = active_count + 1
            total_gpu_ms = total_gpu_ms + (gpu.ema_ms or 0.0)
        end
        if p.faulted then fault_count = fault_count + 1 end
        local vals_copy = {}
        for j = 1, #p.params do
            local k = p.params[j].key
            vals_copy[k] = p.values[k]
        end
        list[#list + 1] = {
            id              = p.id,
            version         = p.version,
            name_zh         = p.name_zh,
            name_en         = p.name_en,
            desc_zh         = p.desc_zh,
            desc_en         = p.desc_en,
            author          = p.author,
            stage           = p.stage,
            order           = p.order,
            exclusive_group = p.exclusive_group,
            enabled         = p.enabled,
            compiled        = p.compiled,
            faulted         = p.faulted,
            fault_msg       = p.fault_msg,
            params          = p.params,
            values          = vals_copy,
            gpu             = gpu,
        }
    end
    return {
        abi_version   = ABI_VERSION,
        master_bypass = master_bypass,
        total_count   = #plugins_ordered,
        active_count  = active_count,
        fault_count   = fault_count,
        rejected      = rejected_records,
        total_gpu_ms  = total_gpu_ms,
        core_bloom    = QueryNativeGpuStats("__core.bloom"),
        core_final    = QueryNativeGpuStats("__core.visual_final"),
        plugins       = list,
    }
end

local function PrintStatusReport()
    local zh = resolve_zh_fn()
    local snap = GetSnapshot()
    if G.print then
        G.print(zh
            and string.format("[HyperSmooth 扩展管线 HS_EXT_V%d] 已注册=%d · 运行中=%d · 隔离=%d · 总旁路=%s · 扩展GPU合计=%.2fms",
                snap.abi_version, snap.total_count, snap.active_count, snap.fault_count, tostring(snap.master_bypass), snap.total_gpu_ms)
            or string.format("[HyperSmooth EXT v%d] registered=%d active=%d faulted=%d bypass=%s extGpu=%.2fms",
                snap.abi_version, snap.total_count, snap.active_count, snap.fault_count, tostring(snap.master_bypass), snap.total_gpu_ms))
        for i = 1, #snap.plugins do
            local p = snap.plugins[i]
            local st = p.faulted and ("FAULT(" .. tostring(p.fault_msg) .. ")")
                or (p.enabled and (snap.master_bypass and "BYPASSED" or "ON") or "OFF")
            local gstr = p.gpu.has_dll
                and string.format("%.2fms %s (peak %.2fms)", p.gpu.ema_ms, p.gpu.is_hw and "GPU" or "CPU", p.gpu.peak_ms)
                or "no-DLL"
            G.print(string.format("  #%d [%s] %s v%s by %s | stage=%s | %s | %s",
                i, st, p.id, p.version, p.author, p.stage, zh and p.name_zh or p.name_en, gstr))
        end
        for i = 1, #snap.rejected do
            local r = snap.rejected[i]
            G.print(string.format("  [REJECTED] %s -> %s", r.id, r.reason))
        end
    end
end

-- Expose the official public GLOBAL.HyperSmooth ABI table
local public_api = {
    ABI_VERSION      = ABI_VERSION,
    STAGES           = STAGE_ORDER,
    RegisterPlugin   = RegisterPlugin,
    GetPlugins       = function() return GetSnapshot().plugins end,
    GetPlugin        = function(id)
        local snap = GetSnapshot()
        for i = 1, #snap.plugins do
            if snap.plugins[i].id == id then return snap.plugins[i] end
        end
        return nil
    end,
    SetPluginEnabled = SetPluginEnabled,
    SetPluginParam   = SetPluginParam,
    ResetPlugin      = ResetPlugin,
    SetMasterBypass  = SetMasterBypass,
    IsMasterBypass   = function() return master_bypass end,
}
rawset(G, "HyperSmooth", public_api)

-- Console helper: c_hsext([action, id_or_bool, key, val])
rawset(G, "c_hsext", function(action, a2, a3, a4)
    if action == nil or action == "list" or action == "status" then
        PrintStatusReport()
        return
    end
    local act = tostring(action):lower()
    if act == "bypass" then
        local b = (a2 == nil) and (not master_bypass) or (a2 == true or a2 == 1 or a2 == "on")
        SetMasterBypass(b)
        PrintStatusReport()
    elseif act == "enable" or act == "on" then
        local ok, err = SetPluginEnabled(tostring(a2 or ""), true)
        if not ok and G.print then G.print("[HyperSmooth EXT] enable failed: " .. tostring(err)) end
    elseif act == "disable" or act == "off" then
        local ok, err = SetPluginEnabled(tostring(a2 or ""), false)
        if not ok and G.print then G.print("[HyperSmooth EXT] disable failed: " .. tostring(err)) end
    elseif act == "set" then
        local ok, err = SetPluginParam(tostring(a2 or ""), tostring(a3 or ""), a4)
        if not ok and G.print then G.print("[HyperSmooth EXT] set failed: " .. tostring(err)) end
    elseif act == "reset" then
        local ok, err = ResetPlugin(tostring(a2 or ""))
        if not ok and G.print then G.print("[HyperSmooth EXT] reset failed: " .. tostring(err)) end
    else
        if G.print then
            G.print("[HyperSmooth EXT] Usage: c_hsext() | c_hsext('enable'|'disable'|'reset', id) | c_hsext('set', id, key, val) | c_hsext('bypass', true|false)")
        end
    end
end)

-- Drain any third-party plugins that already loaded BEFORE HyperSmooth!
DrainGlobalQueue()

-- Internal controller interface consumed by modmain.lua & hs_panel.lua
HS_ExtRuntime = {
    ABI_VERSION        = ABI_VERSION,
    RegisterPlugin     = RegisterPlugin,
    DrainQueue         = DrainGlobalQueue,
    BuildShaders       = BuildShaders,
    SortAndEnable      = SortAndEnable,
    Tick               = Tick,
    GetSnapshot        = GetSnapshot,
    SetPluginEnabled   = SetPluginEnabled,
    SetPluginParam     = SetPluginParam,
    ResetPlugin        = ResetPlugin,
    SetMasterBypass    = SetMasterBypass,
    IsMasterBypass     = function() return master_bypass end,
    ExportPersistedMap = ExportPersistedMap,
    ApplyPersistedMap  = ApplyPersistedMap,
    PrintStatusReport  = PrintStatusReport,
    SetHooks           = function(on_change, is_zh_fn)
        on_state_changed_cb = on_change
        if type(is_zh_fn) == "function" then resolve_zh_fn = is_zh_fn end
    end,
}
