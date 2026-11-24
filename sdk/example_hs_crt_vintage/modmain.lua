-- =============================================================================
-- Official HS_EXT_V1 Reference Plugin Mod: Vintage Cinema & CRT
-- Zero Klei PostProcessor boilerplate, zero UI code, zero clock hooks!
-- Load-order independent: works whether loaded BEFORE or AFTER HyperSmooth.
-- =============================================================================

local G = GLOBAL

Assets = {
    Asset("SHADER", "shaders/hs_ext_vintage_crt.ksh"),
}

local MY_PLUGIN_SPEC = {
    abi     = 1,
    id      = "demo.vintage_crt",
    version = "1.0.0",
    author  = "HyperSmooth SDK",
    name        = { zh = "复古胶片 / CRT", en = "Vintage Cinema & CRT" },
    description = { zh = "示例插件：扫描线、曲率、复古暖胶片与色散", en = "Demo: scanlines, CRT curvature, warm film & CA" },

    -- Standardized execution stage & optional mutual-exclusion group
    stage           = "FINAL_FX",
    order           = 100,
    exclusive_group = "stylized_retro",
    default_enabled = false,

    -- Shader built with: python tools/hs_pack_shader.py src/vintage_crt.frag --id demo.vintage_crt ...
    shader  = (MODROOT or "") .. "shaders/hs_ext_vintage_crt.ksh",
    context = { "TIME" },

    -- Declarative params -> HyperSmooth Core auto-generates Tab 8 UI, vec4 U0/U1 packing & JSON persistence!
    params = {
        { key = "scanline",   slot = "U0.x", type = "float", min = 0.0, max = 1.0,  step = 0.05, default = 0.30, scale = 100, fmt = "%.0f%%", label = { zh = "CRT 扫描线强度", en = "CRT Scanlines" } },
        { key = "curvature",  slot = "U0.y", type = "float", min = 0.0, max = 0.35, step = 0.02, default = 0.08, scale = 100, fmt = "%.0f%%", label = { zh = "显像管屏幕曲率", en = "Tube Curvature" } },
        { key = "chroma_ab",  slot = "U0.z", type = "float", min = 0.0, max = 1.0,  step = 0.05, default = 0.25, scale = 100, fmt = "%.0f%%", label = { zh = "复古边缘色散",   en = "Chromatic Aberration" } },
        { key = "sepia_warm", slot = "U0.w", type = "float", min = 0.0, max = 1.0,  step = 0.05, default = 0.35, scale = 100, fmt = "%.0f%%", label = { zh = "暖色复古胶片",   en = "Vintage Warm Tint" } },
        { key = "flicker",    slot = "U1.x", type = "bool",  default = false,                                                                   label = { zh = "放映机微弱呼吸感", en = "Projector Flicker" } },
    },
}

-- Load-order independent & strict.lua-safe registration (MUST use rawget/rawset on GLOBAL!)
local hs_q = G.rawget(G, "HS_EXT_QUEUE")
if G.type(hs_q) ~= "table" then
    hs_q = {}
    G.rawset(G, "HS_EXT_QUEUE", hs_q)
end
G.table.insert(hs_q, MY_PLUGIN_SPEC)
local hs_api = G.rawget(G, "HyperSmooth")
if G.type(hs_api) == "table" and G.type(hs_api.RegisterPlugin) == "function" then
    hs_api.RegisterPlugin(MY_PLUGIN_SPEC)
end
