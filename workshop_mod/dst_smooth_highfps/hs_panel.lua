-- =============================================================================
-- HyperSmooth v2.8.0 Complete 7-Tab In-Game Live Visual & Lighting Studio Panel
--   - Right-docked translucent overlay inside PlayerHUD (never pauses world or
--     blocks center viewport: walk, zoom & preview 0ms live changes on the fly!)
--   - 7-Tab 100% Parameter-Driven Studio: 10 Master Toggles + 3 Engine Switches
--     + 3 Player Self-Save Custom Slots (A/B/C) + All 73 Live Studio Controls
--   - Pure core DST widgets (images/global.xml + images/global_redux.xml)
-- =============================================================================

local G = GLOBAL
local pcall = G.pcall
local rawget = G.rawget
local type = G.type
local tonumber = G.tonumber
local tostring = G.tostring
local pairs = G.pairs
local ipairs = G.ipairs
local string = G.string
local math = G.math
local table = G.table

local ok_req, Widget, Image, ImageButton, Text = pcall(function()
    return G.require("widgets/widget"),
           G.require("widgets/image"),
           G.require("widgets/imagebutton"),
           G.require("widgets/text")
end)
if not (ok_req and Widget and Image and ImageButton and Text) then
    return
end
local ok_te, TextEdit = pcall(function()
    return G.require("widgets/textedit")
end)
if not ok_te then TextEdit = nil end

local panel_root = nil
local panel_visible = false
local panel_refresh_fn = nil
local panel_telemetry_fn = nil
local panel_ui_mode = "simple"

local function IsChineseLocale()
    if type(HS_IsUiZh) == "function" then
        return HS_IsUiZh() == true
    end
    local lt = G.LanguageTranslator
    if type(lt) == "table" and type(lt.defaultlang) == "string" and #lt.defaultlang > 0 then
        local dl = lt.defaultlang
        if dl == "zh" or dl == "zhr" or dl == "zht" then
            return true
        end
        if dl ~= "en" then
            return false
        end
    end
    local opt_str = G.STRINGS and G.STRINGS.UI and G.STRINGS.UI.OPTIONS
    if type(opt_str) == "table" and type(opt_str.ENABLED) == "string" then
        return opt_str.ENABLED == "启用" or opt_str.ENABLED == "啟用"
    end
    return false
end

local function CreateLiveCustomizerPanel(hud)
    local api = HS_PanelApi
    if type(api) ~= "table" or not hud or not hud.AddChild then
        return nil
    end
    local is_zh = IsChineseLocale()
    local FONT_BODY = G.BODYTEXTFONT or G.UIFONT or G.NUMBERFONT
    local FONT_NUM  = G.NUMBERFONT or FONT_BODY

    local root = hud:AddChild(Widget("HyperSmoothLivePanel"))
    root:SetHAnchor(G.ANCHOR_RIGHT)
    root:SetVAnchor(G.ANCHOR_MIDDLE)
    if G.SCALEMODE_PROPORTIONAL and root.SetScaleMode then
        root:SetScaleMode(G.SCALEMODE_PROPORTIONAL)
    end
    root:Hide()

    local W, H = 400, 666
    -- Position inner child container inside SCALEMODE_PROPORTIONAL anchor so offsets use
    -- resolution-independent virtual 720p units instead of unscaled raw screen pixels.
    local panel = root:AddChild(Widget("HyperSmoothLivePanelBody"))
    panel:SetPosition(-244, -10, 0)

    -- All shared widget registries live in tables to stay far below Lua 5.1's 200-local / 60-upvalue limits.
    local UI = { zh_now = is_zh, info = nil, pro_chrome = {} }
    local pages = {}
    local ApplyMode = nil

    -- ------------------------------------------------------------------------
    -- Tiny widget factory helpers (only methods present on every DST widget build)
    -- ------------------------------------------------------------------------
    local function NewImage(parent, x, y, w, h, r, g, b, a)
        local im = parent:AddChild(Image("images/global.xml", "square.tex"))
        im:ScaleToSize(w, h)
        im:SetPosition(x, y, 0)
        im:SetTint(r, g, b, a)
        return im
    end

    local function NewText(parent, size, str, x, y, w, h, align, r, g, b, a, font)
        local t = parent:AddChild(Text(font or FONT_BODY, size, str or ""))
        if w then t:SetRegionSize(w, h or (size + 6)) end
        if align then t:SetHAlign(align) end
        t:SetPosition(x, y, 0)
        if r then t:SetColour(r, g, b, a or 1.0) end
        t:SetClickable(false)
        return t
    end

    -- Chained focus handlers (a widget can have several listeners: hover glow + description bar ...)
    local function OnFocus(w, gainfn, losefn)
        if w == nil or type(w.SetOnGainFocus) ~= "function" then return end
        local lst = rawget(w, "_hs_focus")
        if lst == nil then
            lst = { gain = {}, lose = {} }
            G.rawset(w, "_hs_focus", lst)
            w:SetOnGainFocus(function()
                for i = 1, #lst.gain do lst.gain[i]() end
            end)
            if type(w.SetOnLoseFocus) == "function" then
                w:SetOnLoseFocus(function()
                    for i = 1, #lst.lose do lst.lose[i]() end
                end)
            end
        end
        if gainfn then lst.gain[#lst.gain + 1] = gainfn end
        if losefn then lst.lose[#lst.lose + 1] = losefn end
    end

    -- Hover glow + press flash overlay (animated by the root OnUpdate; never touched by state refresh)
    UI.fx_active = {}
    local function FxKick(fx) UI.fx_active[fx] = true end
    local function PressFX(btn)
        local fx = rawget(btn, "_hs_fx")
        if fx then
            fx.press = 1.0
            FxKick(fx)
        end
    end

    local function NewButton(parent, x, y, w, h, str, size, onclick, font, nofx)
        local fx = nil
        if not nofx then
            fx = { hover = 0.0, hover_t = 0.0, press = 0.0 }
            fx.img = NewImage(parent, x, y, w, h, 1.0, 0.95, 0.80, 0.0)
        end
        local btn = parent:AddChild(ImageButton("images/global_redux.xml", "blank.tex", "spinner_focus.tex"))
        btn.scale_on_focus = false
        btn.move_on_click = false
        btn:ForceImageSize(w, h)
        btn:SetPosition(x, y, 0)
        if str ~= nil then
            btn:SetText(str)
            btn:SetTextSize(size or 14)
            btn:SetFont(font or FONT_BODY)
        end
        if fx then
            G.rawset(btn, "_hs_fx", fx)
            OnFocus(btn,
                function() fx.hover_t = 1.0; FxKick(fx) end,
                function() fx.hover_t = 0.0; FxKick(fx) end)
        end
        if onclick then
            btn:SetOnClick(function()
                if fx then fx.press = 1.0; FxKick(fx) end
                onclick()
            end)
        end
        return btn
    end

    local function DoRefresh()
        if panel_refresh_fn then panel_refresh_fn() end
    end

    -- Plain-language text wrapper (CJK = 1 unit, ASCII = 0.52 unit; breaks EN at spaces)
    local function WrapText(s, max_units, max_lines)
        if type(s) ~= "string" or s == "" then return "" end
        local lines, cur, cur_u, last_sp = {}, {}, 0.0, nil
        local function Flush()
            lines[#lines + 1] = table.concat(cur)
            cur, cur_u, last_sp = {}, 0.0, nil
        end
        for ch in string.gmatch(s, "[%z\1-\127\194-\244][\128-\191]*") do
            local u = (#ch == 1) and 0.52 or 1.0
            if ch == "\n" then
                Flush()
            else
                if cur_u + u > max_units then
                    if ch ~= " " and last_sp ~= nil then
                        local carry = {}
                        for j = last_sp + 1, #cur do carry[#carry + 1] = cur[j] end
                        for j = #cur, last_sp, -1 do cur[j] = nil end
                        Flush()
                        cur = carry
                        for j = 1, #cur do cur_u = cur_u + ((#cur[j] == 1) and 0.52 or 1.0) end
                    else
                        Flush()
                    end
                end
                if not (ch == " " and #cur == 0) then
                    cur[#cur + 1] = ch
                    cur_u = cur_u + u
                    if ch == " " then last_sp = #cur end
                end
            end
        end
        if #cur > 0 then Flush() end
        if max_lines and #lines > max_lines then
            lines[max_lines] = lines[max_lines] .. "..."
            for j = #lines, max_lines + 1, -1 do lines[j] = nil end
        end
        return table.concat(lines, "\n")
    end
    UI.WrapText = WrapText

    -- Sliding pill switch (replaces ugly [ON]/[OFF] text)
    local function MakeSwitch(parent, x, y)
        local sw = { x = x, y = y }
        sw.track = NewImage(parent, x, y, 36, 16, 0.22, 0.24, 0.30, 0.95)
        sw.knob = NewImage(parent, x - 9, y, 12, 12, 0.66, 0.69, 0.75, 1.0)
        return sw
    end
    local function SetSwitch(sw, on)
        if not sw then return end
        if on then
            sw.track:SetTint(0.14, 0.64, 0.56, 0.98)
            sw.knob:SetTint(1.0, 1.0, 1.0, 1.0)
            sw.knob:SetPosition(sw.x + 9, sw.y, 0)
        else
            sw.track:SetTint(0.22, 0.24, 0.30, 0.95)
            sw.knob:SetTint(0.66, 0.69, 0.75, 1.0)
            sw.knob:SetPosition(sw.x - 9, sw.y, 0)
        end
    end

    -- ------------------------------------------------------------------------
    -- Frame: deep-navy glass body, amber hairline, header band
    -- ------------------------------------------------------------------------
    NewImage(panel, 0, 0, W + 4, H + 4, 0.95, 0.74, 0.36, 0.50)
    NewImage(panel, 0, 0, W, H, 0.046, 0.054, 0.074, 0.95)
    NewImage(panel, 0, H * 0.5 - 24, W, 48, 0.078, 0.092, 0.130, 0.99)
    NewImage(panel, 0, H * 0.5 - 48, W, 2, 0.95, 0.74, 0.36, 0.85)

    UI.title = NewText(panel, 22, "HyperShade", -W * 0.5 + 112, H * 0.5 - 17, 210, 28, G.ANCHOR_LEFT, 0.98, 0.80, 0.42, 1.0)
    UI.subtitle = NewText(panel, 12, "", -W * 0.5 + 102, H * 0.5 - 37, 200, 16, G.ANCHOR_LEFT, 0.62, 0.70, 0.80, 0.95)

    -- Feedback QQ group (always visible in both modes); click = copy group number to clipboard
    UI.QQ_GROUP = "978808336"
    UI.qq_btn = NewButton(panel, W * 0.5 - 104, H * 0.5 - 37, 196, 18, "", 13, function()
        local fnClip = rawget(G, "HyperSmooth_SetClipboard")
        local copied = false
        if type(fnClip) == "function" then
            local okc, res = pcall(fnClip, UI.QQ_GROUP)
            copied = okc and (res ~= false)
        end
        UI.qq_flash = copied and 1 or 2
        local fnNow = rawget(G, "GetTimeRealSeconds")
        UI.qq_flash_t = (type(fnNow) == "function") and fnNow() or 0
        DoRefresh()
    end)
    UI.qq_btn:SetTextFocusColour(1.0, 0.96, 0.74, 1.0)

    local close_btn = NewButton(panel, W * 0.5 - 24, H * 0.5 - 22, 32, 24, "X", 18, function()
        if type(HS_TogglePanel) == "function" then
            HS_TogglePanel(false)
        end
    end, FONT_NUM)
    close_btn:SetTextColour(0.95, 0.55, 0.55, 1.0)
    close_btn:SetTextFocusColour(1.0, 0.78, 0.78, 1.0)

    -- ------------------------------------------------------------------------
    -- Mode switch: [Simple] [Pro] [Preset Gallery]
    -- ------------------------------------------------------------------------
    local mode_defs = {
        { id = "simple", x = -126, zh = "简易模式", en = "Simple" },
        { id = "pro",    x =    0, zh = "专业模式", en = "Pro" },
    }
    UI.mode_btns = {}
    for i = 1, #mode_defs do
        local md = mode_defs[i]
        local mbg = NewImage(panel, md.x, H * 0.5 - 62, 120, 22, 0.12, 0.14, 0.19, 0.95)
        local mbtn = NewButton(panel, md.x, H * 0.5 - 62, 120, 22, is_zh and md.zh or md.en, 14, function()
            if panel_ui_mode ~= md.id then
                panel_ui_mode = md.id
                if type(api.SetUiMode) == "function" then pcall(api.SetUiMode, md.id) end
                if ApplyMode then ApplyMode() end
                DoRefresh()
            end
        end)
        mbtn:SetTextFocusColour(1.0, 0.96, 0.74, 1.0)
        UI.mode_btns[i] = { id = md.id, zh = md.zh, en = md.en, bg = mbg, btn = mbtn }
    end
    UI.preset_card = { visible = false, cards = {} }
    do
        local pc = UI.preset_card
        pc.top_bg = NewImage(panel, 126, H * 0.5 - 62, 120, 22, 0.16, 0.22, 0.28, 0.95)
        pc.top_btn = NewButton(panel, 126, H * 0.5 - 62, 120, 22, is_zh and "预设面板" or "Presets", 14, function()
            if UI.TogglePresetCard then UI.TogglePresetCard() end
        end)
        pc.top_btn:SetTextColour(0.76, 0.94, 1.0, 1.0)
        pc.top_btn:SetTextFocusColour(1.0, 0.96, 0.74, 1.0)
    end

    -- ------------------------------------------------------------------------
    -- Status strip (2 short lines): live FPS / ping + native engine connection
    -- ------------------------------------------------------------------------
    UI.st_bg = NewImage(panel, 0, H * 0.5 - 91, W - 22, 32, 0.035, 0.045, 0.065, 0.80)
    UI.st1 = NewText(panel, 14, "", 0, H * 0.5 - 83, W - 26, 18, nil, 0.52, 1.0, 0.70, 0.96)
    UI.st2 = NewText(panel, 13, "", 0, H * 0.5 - 99, W - 26, 16, nil, 0.70, 0.82, 0.94, 0.92)

    -- ------------------------------------------------------------------------
    -- Pro-only chrome: compact preset pills + 7 category tabs
    -- ------------------------------------------------------------------------
    local preset_labels_zh = { [0] = "原版纯净", [1] = "护眼高清", [2] = "电影光影", [3] = "自定义" }
    local preset_labels_en = { [0] = "Vanilla",  [1] = "EyeHD",    [2] = "Cinema",   [3] = "Custom" }
    local preset_desc_zh = { [0] = "不改动画面", [1] = "清晰·不变色", [2] = "光影全开", [3] = "我的参数" }
    local preset_desc_en = { [0] = "No changes", [1] = "Crisp, true color", [2] = "Full lighting", [3] = "My own tuning" }
    local preset_btns = {}
    local pp_w, pp_gap = 90, 6
    local preset_y = 209
    for pid = 0, 3 do
        local px = -189 + pp_w * 0.5 + pid * (pp_w + pp_gap) + 3
        local pbg = NewImage(panel, px, preset_y, pp_w, 22, 0.14, 0.14, 0.18, 0.90)
        local pb = NewButton(panel, px, preset_y, pp_w, 22, is_zh and preset_labels_zh[pid] or preset_labels_en[pid], 15, function()
            api.SetPreset(pid)
            DoRefresh()
        end)
        pb:SetTextFocusColour(1.0, 0.96, 0.74, 1.0)
        preset_btns[#preset_btns + 1] = { id = pid, bg = pbg, btn = pb }
        UI.pro_chrome[#UI.pro_chrome + 1] = pbg
        UI.pro_chrome[#UI.pro_chrome + 1] = pb
    end

    local active_tab = 2
    local tab_roots = {}
    for tid = 1, 8 do
        tab_roots[tid] = panel:AddChild(Widget("HS_Tab_" .. tid))
        if tid ~= 1 then tab_roots[tid]:Hide() end
    end
    local tab_defs = {
        { id = 2, zh = "清晰", en = "Clarity" },
        { id = 3, zh = "色彩", en = "Color" },
        { id = 4, zh = "光影", en = "Light" },
        { id = 5, zh = "水景", en = "Water" },
        { id = 6, zh = "动作", en = "Motion" },
        { id = 7, zh = "系统", en = "System" },
        { id = 8, zh = "扩展", en = "Ext" },
    }
    local tab_btns = {}
    local tw_w, tw_gap = 52, 2.5
    for i = 1, #tab_defs do
        local td = tab_defs[i]
        local tx = -189 + tw_w * 0.5 + (i - 1) * (tw_w + tw_gap)
        local tbg = NewImage(panel, tx, 180, tw_w, 24, 0.14, 0.16, 0.22, 0.90)
        local tbtn = NewButton(panel, tx, 180, tw_w, 24, is_zh and td.zh or td.en, 14, function()
            active_tab = td.id
            if ApplyMode then ApplyMode() end
            DoRefresh()
        end)
        tbtn:SetTextFocusColour(1.0, 0.96, 0.74, 1.0)
        tab_btns[i] = { id = td.id, zh = td.zh, en = td.en, bg = tbg, btn = tbtn }
        UI.pro_chrome[#UI.pro_chrome + 1] = tbg
        UI.pro_chrome[#UI.pro_chrome + 1] = tbtn
    end

    -- ------------------------------------------------------------------------
    -- Description bar (Pro): plain-language explanation of the hovered/clicked option
    -- ------------------------------------------------------------------------
    local function ShowInfo(info)
        UI.info = info
        if not UI.desc_title then return end
        local zh_now = UI.zh_now == true
        local head = zh_now and info.zh or info.en
        local body = zh_now and info.dzh or info.den
        UI.desc_title:SetString(head or "")
        UI.desc_body:SetString(WrapText(body or "", 29.0, 2))
    end
    local function BindInfo(w, info)
        if w ~= nil and info ~= nil then
            OnFocus(w, function() ShowInfo(info) end, nil)
        end
    end
    UI.ShowInfo = ShowInfo

    -- ------------------------------------------------------------------------
    -- Section banner + stepper row + toggle row + group reset
    -- ------------------------------------------------------------------------
    local section_banners = {}
    local function AddSectionBanner(parent, ry, zh_s, en_s, tint)
        local sbg = NewImage(parent, 0, ry, W - 22, 20, tint[1], tint[2], tint[3], tint[4])
        local stxt = NewText(parent, 13, is_zh and zh_s or en_s, 0, ry, W - 28, 18, nil, 0.88, 0.94, 1.0, 0.96)
        section_banners[#section_banners + 1] = { txt = stxt, zh = zh_s, en = en_s }
        return sbg, stxt
    end

    -- ------------------------------------------------------------------------
    -- Slider stepper: label | [-] ---o------ [+] | value
    --   * drag the knob / click the rail to set a value (mouse position calibrated through widget world positions)
    --   * [-]/[+] press once = 1 step, hold = auto-repeat (accelerating)
    --   * amber tick on the rail marks the default value; knob magnets to it
    --   * click the value to reset to default
    -- ------------------------------------------------------------------------
    local TRACK_X0, TRACK_W = 10, 96
    local function NowSec()
        local fn = rawget(G, "GetTimeRealSeconds")
        if type(fn) == "function" then
            local v = fn()
            if type(v) == "number" then return v end
        end
        return 0
    end

    local function PaintKnob(sw)
        if sw.active then
            if sw.hot then
                sw.knob_edge:SetTint(0.98, 0.78, 0.38, 1.0)
            else
                sw.knob_edge:SetTint(0.20, 0.72, 0.64, 1.0)
            end
            sw.knob:SetTint(0.95, 0.99, 1.0, 1.0)
        else
            sw.knob_edge:SetTint(0.32, 0.36, 0.42, 1.0)
            sw.knob:SetTint(0.66, 0.70, 0.76, 1.0)
        end
    end

    local function MouseFrac(sw)
        local TI = rawget(G, "TheInput")
        if not (TI and type(TI.GetScreenPosition) == "function"
                and type(sw.hit.GetWorldPosition) == "function"
                and type(sw.ref.GetWorldPosition) == "function") then
            return nil
        end
        local mp = TI:GetScreenPosition()
        local c = sw.hit:GetWorldPosition()
        local r = sw.ref:GetWorldPosition()
        if not (mp and c and r and mp.x and c.x and r.x) then return nil end
        local half = r.x - c.x
        if half < 2 then return nil end
        return (mp.x - c.x) / (2 * half) + 0.5
    end

    local function DragApply(sw, force)
        local frac = MouseFrac(sw)
        if frac == nil then return false end
        if frac < 0 then frac = 0 elseif frac > 1 then frac = 1 end
        local b = UI.bounds and UI.bounds[sw.key] or nil
        if not b then return false end
        local span = b.max - b.min
        if span <= 0 then return false end
        local q = span / 200
        local v = b.min + math.floor(frac * span / q + 0.5) * q
        if math.abs(v - (b.default or v)) <= span * 0.02 then v = b.default end
        local t = NowSec()
        if force or ((t - (sw.drag_t or 0)) >= 0.05 and math.abs(v - (sw.drag_v or -1e9)) > q * 0.5) then
            sw.drag_t = t
            sw.drag_v = v
            api.SetTune(sw.key, v)
            DoRefresh()
        end
        return true
    end

    local function DragBegin(sw)
        if UI.slider_bad then return end
        if not UI.bounds then
            local sn = api.GetSnapshot()
            UI.bounds = sn and sn.bounds or nil
        end
        local f = MouseFrac(sw)
        if f == nil then return end
        if f < -0.25 or f > 1.25 then
            UI.slider_bad = true
            if G.print then G.print("[HyperSmooth] slider drag disabled: mouse/widget coordinate mismatch (use -/+ buttons)") end
            return
        end
        UI.drag = sw
        sw.drag_v = nil
        sw.hot = true
        PaintKnob(sw)
        DragApply(sw, true)
    end

    local all_steppers = {}
    UI.steppers = all_steppers
    local function AddStepperRow(parent, ry, row_h, def)
        local rbg = NewImage(parent, 0, ry, W - 22, row_h, 0.095, 0.108, 0.145, 0.90)
        local mark = NewImage(parent, -W * 0.5 + 12.5, ry, 3, row_h, 0.95, 0.74, 0.36, 0.0)
        local rlbl = NewText(parent, 14, is_zh and def.zh or def.en, -101, ry, 160, row_h - 2, G.ANCHOR_LEFT, 0.90, 0.93, 0.96, 1.0)
        local info_btn = NewButton(parent, -101, ry, 160, row_h, nil, nil, function() ShowInfo(def) end)
        local bh = row_h - 4
        local tc = TRACK_X0 + TRACK_W * 0.5

        -- [-] (press = step, hold = auto-repeat)
        NewImage(parent, -8, ry, 22, bh, 0.20, 0.22, 0.29, 0.95)
        local minus_btn = NewButton(parent, -8, ry, 22, bh, "-", 18, nil, FONT_NUM)
        minus_btn:SetTextColour(1.0, 0.88, 0.65, 1.0)

        -- rail + fill + default tick + knob
        NewImage(parent, tc, ry, TRACK_W + 8, 8, 0.12, 0.14, 0.19, 1.0)
        NewImage(parent, tc, ry, TRACK_W, 4, 0.025, 0.032, 0.05, 1.0)
        local fill = NewImage(parent, TRACK_X0 + 0.5, ry, 1, 4, 0.20, 0.72, 0.64, 0.95)
        local tick = NewImage(parent, TRACK_X0, ry, 2, 12, 0.95, 0.74, 0.36, 0.0)
        local knob_edge = NewImage(parent, TRACK_X0, ry, 10, 18, 0.20, 0.72, 0.64, 1.0)
        local knob = NewImage(parent, TRACK_X0, ry, 6, 14, 0.95, 0.99, 1.0, 1.0)
        local ref = NewImage(parent, tc + TRACK_W * 0.5, ry, 1, 1, 0.0, 0.0, 0.0, 0.0)
        local hit = NewButton(parent, tc, ry, TRACK_W + 14, row_h, nil, nil, nil, nil, true)

        -- [+]
        NewImage(parent, 120, ry, 22, bh, 0.20, 0.22, 0.29, 0.95)
        local plus_btn = NewButton(parent, 120, ry, 22, bh, "+", 18, nil, FONT_NUM)
        plus_btn:SetTextColour(1.0, 0.88, 0.65, 1.0)

        -- value readout (click = reset to default)
        local val_btn = NewButton(parent, 160, ry, 52, bh, "", 13, function()
            if type(api.ResetTuneField) == "function" then
                api.ResetTuneField(def.key)
                DoRefresh()
            end
            ShowInfo(def)
        end, FONT_NUM)
        val_btn:SetTextColour(0.45, 1.0, 0.72, 1.0)
        val_btn:SetTextFocusColour(1.0, 0.96, 0.68, 1.0)

        local entry = {
            key = def.key, flag = def.flag, native_ws = def.native_ws, skeletal_only = def.skeletal_only,
            hitfeel_only = def.hitfeel_only, fmt = def.fmt, scale = def.scale, zh = def.zh, en = def.en,
            bg = rbg, lbl = rlbl, val_btn = val_btn, fill = fill, mark = mark, tick = tick,
            knob = knob, knob_edge = knob_edge, hit = hit, ref = ref, ry = ry, bh = bh,
            minus = minus_btn, plus = plus_btn,
            active = true, hot = false,
        }

        local function StepOnce(sign)
            if type(api.StepTune) == "function" then
                api.StepTune(def.key, sign * def.step)
                DoRefresh()
            end
            ShowInfo(def)
        end
        local function BindStep(btn, sign)
            if type(btn.SetOnDown) == "function" then
                btn:SetOnDown(function()
                    PressFX(btn)
                    StepOnce(sign)
                    UI.hold = { fn = function() StepOnce(sign) end, t = 0.0, nxt = 0.40 }
                end)
            else
                btn:SetOnClick(function()
                    PressFX(btn)
                    StepOnce(sign)
                end)
            end
        end
        BindStep(minus_btn, -1)
        BindStep(plus_btn, 1)

        if type(hit.SetOnDown) == "function" then
            hit:SetOnDown(function() DragBegin(entry) end)
        end
        hit:SetOnClick(function()
            if not UI.drag then
                if not UI.bounds then
                    local sn = api.GetSnapshot()
                    UI.bounds = sn and sn.bounds or nil
                end
                if not UI.slider_bad then DragApply(entry, true) end
            end
            ShowInfo(def)
        end)
        OnFocus(hit,
            function() entry.hot = true; PaintKnob(entry) end,
            function() if UI.drag ~= entry then entry.hot = false; PaintKnob(entry) end end)

        BindInfo(info_btn, def)
        BindInfo(minus_btn, def)
        BindInfo(hit, def)
        BindInfo(plus_btn, def)
        BindInfo(val_btn, def)

        all_steppers[#all_steppers + 1] = entry
        return entry
    end

    -- Toggle row: label (+ optional short description) | sliding switch
    local toggle_widgets = {}
    local function DoToggle(def)
        local s = def.src
        if s == "tune" then
            if type(api.ToggleTuneBool) == "function" then api.ToggleTuneBool(def.key) end
        elseif s == "flag" then
            api.ToggleVisualFlag(def.key)
        elseif s == "skeletal" then
            api.ToggleSkeletal()
        elseif s == "lowlat" then
            api.ToggleLowLatency()
        elseif s == "clip" then
            if type(api.ToggleClipCursor) == "function" then api.ToggleClipCursor() end
        elseif s == "hud" then
            if type(api.CycleHudMode) == "function" then api.CycleHudMode() end
        elseif s == "bundle" then
            if type(api.ToggleBundle) == "function" then api.ToggleBundle(def.key) end
        end
        DoRefresh()
        ShowInfo(def)
    end
    local function AddToggleRow(parent, ry, row_h, def)
        local big = (def.big == true)
        local rbg = NewImage(parent, 0, ry, W - 22, row_h, 0.095, 0.108, 0.145, 0.90)
        local rbtn = NewButton(parent, 0, ry, W - 22, row_h, nil, nil, function() DoToggle(def) end)
        local rlbl = NewText(parent, big and 15 or 14, is_zh and def.zh or def.en, -35, ry + (big and 8 or 0), 292, big and 20 or (row_h - 2), G.ANCHOR_LEFT, 0.90, 0.93, 0.96, 1.0)
        local dlbl = nil
        if big then
            dlbl = NewText(parent, 12, is_zh and def.hzh or def.hen, -35, ry - 9, 292, 16, G.ANCHOR_LEFT, 0.58, 0.66, 0.76, 0.95)
        end
        local entry = { def = def, bg = rbg, lbl = rlbl, dlbl = dlbl }
        if def.src == "hud" then
            entry.val = NewText(parent, 14, "", 143, ry, 74, 20, G.ANCHOR_RIGHT, 0.55, 0.58, 0.64, 0.88)
        else
            entry.sw = MakeSwitch(parent, 160, ry)
        end
        BindInfo(rbtn, def)
        toggle_widgets[#toggle_widgets + 1] = entry
        return entry
    end

    local tab_reset_btns = {}
    local function AddGroupResetButton(parent, ry, zh_lbl, en_lbl, keys)
        NewImage(parent, 0, ry, W - 22, 20, 0.20, 0.16, 0.12, 0.90)
        local rbtn = NewButton(parent, 0, ry, W - 22, 20, is_zh and zh_lbl or en_lbl, 13, function()
            if type(api.ResetTuneGroup) == "function" then
                api.ResetTuneGroup(keys)
                DoRefresh()
            end
        end)
        rbtn:SetTextColour(0.96, 0.86, 0.62, 1.0)
        rbtn:SetTextFocusColour(1.0, 0.96, 0.78, 1.0)
        tab_reset_btns[#tab_reset_btns + 1] = { btn = rbtn, zh = zh_lbl, en = en_lbl }
    end

    -- Slot row: "我的方案" + 3 x [Save][Load]
    local slot_widgets = {}
    local function MakeSlotRow(parent, ry)
        NewText(parent, 13, is_zh and "我的方案" or "My Looks", -153, ry, 68, 20, G.ANCHOR_LEFT, 0.80, 0.86, 0.94, 0.95)
        UI.slot_lbl_cnt = (UI.slot_lbl_cnt or 0) + 1
        local sb_w, sb_gap = 42, 3
        for sidx = 1, 3 do
            local idx_c = sidx
            local gx = -110 + (sidx - 1) * (2 * sb_w + sb_gap + 12)
            local sx_save = gx + sb_w * 0.5
            local sx_load = sx_save + sb_w + sb_gap
            NewImage(parent, sx_save, ry, sb_w, 24, 0.24, 0.19, 0.12, 0.92)
            local sbtn = NewButton(parent, sx_save, ry, sb_w, 24, is_zh and ("存" .. idx_c) or ("S" .. idx_c), 14, function()
                if type(api.SaveCustomSlot) == "function" then
                    api.SaveCustomSlot(idx_c)
                    DoRefresh()
                end
            end)
            sbtn:SetTextColour(1.0, 0.88, 0.58, 1.0)
            local lbg = NewImage(parent, sx_load, ry, sb_w, 24, 0.12, 0.22, 0.28, 0.92)
            local lbtn = NewButton(parent, sx_load, ry, sb_w, 24, is_zh and ("读" .. idx_c) or ("L" .. idx_c), 14, function()
                if type(api.LoadCustomSlot) == "function" then
                    api.LoadCustomSlot(idx_c)
                    DoRefresh()
                end
            end)
            lbtn:SetTextColour(0.68, 0.92, 1.0, 1.0)
            slot_widgets[#slot_widgets + 1] = { idx = idx_c, sbtn = sbtn, lbg = lbg, lbtn = lbtn }
        end
    end

    -- ------------------------------------------------------------------------
    -- Accordion pages (Pro): one section open at a time, rows created once and re-laid out
    -- ------------------------------------------------------------------------
    local ROWS = {}
    local function S(key, step, fmt, scale, gate, zh, en, dzh, den)
        local r = { kind = "step", key = key, step = step, fmt = fmt, scale = scale, zh = zh, en = en, dzh = dzh, den = den }
        if gate == "native" then r.native_ws = true
        elseif gate == "skel" then r.skeletal_only = true
        elseif gate == "hit" then r.hitfeel_only = true
        elseif gate then r.flag = gate end
        ROWS[key] = r
    end
    local function T(key, zh, en, dzh, den)
        ROWS[key] = { kind = "toggle", src = "tune", key = key, zh = zh, en = en, dzh = dzh, den = den }
    end
    local function F(key, zh, en, dzh, den)
        ROWS["flag_" .. key] = { kind = "toggle", src = "flag", key = key, zh = zh, en = en, dzh = dzh, den = den }
    end
    local function E(id, src, zh, en, dzh, den)
        ROWS["eng_" .. id] = { kind = "toggle", src = src, key = id, zh = zh, en = en, dzh = dzh, den = den }
    end

    local function MakeSectionHeader(parent, sd, onclick)
        local hroot = parent:AddChild(Widget("HS_SecHdr"))
        local hdr = { root = hroot }
        hdr.bg = NewImage(hroot, 0, 0, W - 22, 24, 0.13, 0.16, 0.22, 0.96)
        hdr.accent = NewImage(hroot, -W * 0.5 + 12, 0, 4, 24, 0.95, 0.74, 0.36, 0.95)
        hdr.lbl = NewText(hroot, 14, "", -22, 0, 300, 22, G.ANCHOR_LEFT, 0.96, 0.92, 0.80, 1.0)
        hdr.badge = NewText(hroot, 12, "", 160, 0, 54, 20, G.ANCHOR_RIGHT, 0.60, 0.68, 0.78, 0.95)
        hdr.btn = NewButton(hroot, 0, 0, W - 22, 24, nil, nil, onclick)
        return hdr
    end

    local function LayoutPage(page)
        local y = 164
        for si = 1, #page.secs do
            local sec = page.secs[si]
            local open = (page.open == si)
            sec.hdr.root:SetPosition(0, y - 12, 0)
            y = y - 26
            for ri = 1, #sec.rows do
                local r = sec.rows[ri]
                if open then
                    r.root:SetPosition(0, y - (r.h + 2) * 0.5, 0)
                    r.root:Show()
                    y = y - (r.h + 2)
                else
                    r.root:Hide()
                end
            end
            if open then y = y - 4 end
        end
    end

    local function BuildRow(rc, rs)
        if rs.custom then
            rs.build(rc)
            return rs.h or 24
        end
        local def = ROWS[rs.id]
        if def == nil then
            return 24
        end
        if def.kind == "step" then
            AddStepperRow(rc, 0, 24, def)
        else
            AddToggleRow(rc, 0, 24, def)
        end
        return 24
    end

    local function BuildProPage(tid, secs, reset_zh, reset_en)
        local root_t = tab_roots[tid]
        local page = { secs = {}, open = 1 }
        local reset_keys = {}
        for si = 1, #secs do
            local sd = secs[si]
            local sec = { zh = sd.zh, en = sd.en, rows = {}, count = 0 }
            for ri = 1, #sd.rows do
                local rs0 = sd.rows[ri]
                local def0 = (not rs0.custom) and ROWS[rs0.id] or nil
                if def0 then
                    sec.count = sec.count + 1
                    if def0.kind == "step" or def0.src == "tune" then
                        reset_keys[#reset_keys + 1] = def0.key
                    end
                end
            end
            sec.hdr = MakeSectionHeader(root_t, sd, function()
                page.open = (page.open == si) and 0 or si
                LayoutPage(page)
                DoRefresh()
            end)
            for ri = 1, #sd.rows do
                local rc = root_t:AddChild(Widget("HS_Row"))
                local h = BuildRow(rc, sd.rows[ri])
                rc:Hide()
                sec.rows[#sec.rows + 1] = { root = rc, h = h }
            end
            page.secs[si] = sec
        end
        LayoutPage(page)
        if reset_zh then
            AddGroupResetButton(root_t, -261, reset_zh, reset_en, reset_keys)
        end
        pages[tid] = page
    end
    UI.BuildProPage = BuildProPage

    -- ------------------------------------------------------------------------
    -- Plain-language option catalogue (name + one-sentence explanation, ZH/EN)
    -- Gate: flag name = needs that master flag; "native" = needs dinput8.dll world studio;
    --       "skel" = needs 240Hz engine; "hit" = needs Hit-Feel master switch.
    -- ------------------------------------------------------------------------
    -- Clarity
    F("cas", "边缘锐化（总开关）", "Edge Sharpening (master)", "让画面线条与边缘更清晰锐利，手绘风格效果最明显。", "Crisper outlines and edges; most visible on hand-drawn art.")
    S("sharp", 0.15, "%.0f%%", 100, "cas", "锐化强度", "Sharpen Strength", "边缘越利越清晰，过高会出现白边。推荐 80%~100%。", "Higher = crisper edges; too high causes halos. Try 80-100%.")
    S("ink", 0.15, "%.0f%%", 100, "cas", "墨线加深", "Ink Line Boost", "加深角色和物体的黑色描边，让线条更有力。", "Darkens the black outlines on characters and objects.")
    F("af16", "斜视角贴图清晰", "Sharp Angled Textures", "地面和斜着看的贴图更清楚，几乎不耗性能（16× 各向异性）。", "Ground and angled textures stay clear. Nearly free (16x anisotropic).")
    F("mip_sharp", "贴图防发糊", "Anti-Blur Textures", "缩小或远处的贴图保持清晰，不再糊成一团。", "Keeps distant or shrunk textures sharp instead of blurry.")
    S("mip_bias", 0.05, "%+.2f", 1, "mip_sharp", "贴图清晰度偏移", "Texture Bias", "数值越负越清晰，太负可能闪烁。推荐 -0.45。", "More negative = sharper, but may shimmer. Recommended -0.45.")
    F("full_lm", "高清光照图", "HD Light Map", "光照与黑暗的边缘更细腻，夜晚火光更精致（1.0× 分辨率）。", "Finer light/dark edges and nicer firelight at night (1.0x resolution).")
    S("deconv_gate", 0.005, "%.3f", 1, "cas", "墨线识别门限", "Ink Edge Threshold", "多强的边缘才算墨线。调低会处理更多细线。", "How strong an edge must be to count as ink. Lower = more thin lines.")
    S("deconv_penetr", 0.10, "%.0f%%", 100, "cas", "暗部墨线穿透", "Ink in Dark Areas", "让深色区域里的墨线也被加强。", "Also boosts ink lines that sit inside dark areas.")
    S("anti_ring", 0.10, "%.0f%%", 100, "cas", "防白边光晕", "Anti-Halo", "抑制锐化产生的亮边/暗边，越高越干净。", "Suppresses bright/dark halos from sharpening. Higher = cleaner.")
    S("dark_prot", 0.05, "%.0f%%", 100, "cas", "暗部保护", "Shadow Protection", "避免暗处被锐化压成死黑。", "Stops dark areas from being crushed to pure black.")
    S("chroma_prot", 0.10, "%.0f%%", 100, "cas", "色边保护", "Color Edge Guard", "避免彩色边缘出现偏色杂边。", "Avoids color fringes along colored edges.")
    S("noise_reduce", 0.10, "%.0f%%", 100, "cas", "平坦区去噪", "Flat-Area Denoise", "纯色区域不放大噪点，让画面更干净。", "Keeps flat areas clean instead of amplifying noise.")
    S("range_sigma", 0.04, "%.2f", 1, "cas", "边缘判断宽容度", "Edge Tolerance", "高级参数：越大越多像素参与平滑，一般不用动。", "Advanced: larger = more pixels get smoothed. Usually leave as is.")
    S("spatial_sigma", 0.15, "%.2f", 1, "cas", "平滑范围", "Smoothing Radius", "高级参数：平滑时参考的像素半径，一般不用动。", "Advanced: pixel radius used when smoothing. Usually leave as is.")
    S("noise_floor", 0.004, "%.3f", 1, "cas", "噪声底线", "Noise Floor", "高级参数：低于此强度的细节视为噪点。", "Advanced: details weaker than this are treated as noise.")
    S("ar_l_over", 0.005, "%.3f", 1, "cas", "亮边过冲上限", "Light Overshoot Cap", "高级参数：限制亮边最多亮出多少，防止白边。", "Advanced: caps how bright an edge can overshoot.")
    S("ar_d_over", 0.005, "%.3f", 1, "cas", "暗边过冲上限", "Dark Overshoot Cap", "高级参数：限制暗边最多暗出多少，防止黑边。", "Advanced: caps how dark an edge can overshoot.")
    S("dither", 0.25, "%.0f%%", 100, "cas", "防色带抖动", "Anti-Banding Dither", "渐变的天空、阴影不再出现一条条色带。", "Removes visible bands in gradients such as sky and shadows.")
    S("sprite_detail", 0.10, "%.0f%%", 100, "native", "角色边缘精修", "Sprite Edge Polish", "高分辨率屏幕上让角色边缘更干净、墨线更实。需 DLL。", "Cleaner sprite edges and bolder ink on hi-res screens. Needs DLL.")
    S("ground_detail", 0.10, "%.0f%%", 100, "native", "地面细腻纹理", "Ground Micro-Texture", "给地皮增加细小的颗粒质感，近看更有层次。需 DLL。", "Adds fine grain to the ground so it looks richer up close. Needs DLL.")

    -- Color
    F("tone", "电影调色（总开关）", "Cinematic Grading (master)", "开启后，下面的调色参数才会生效。", "The grading values below only take effect when this is on.")
    S("exposure", 0.05, "%+.2f", 1, "tone", "曝光", "Exposure", "整体变亮或变暗。", "Makes the whole picture brighter or darker.")
    S("temp", 0.08, "%+.2f", 1, "tone", "色温（冷↔暖）", "Temperature", "往右偏暖黄，往左偏冷蓝。", "Right = warmer/yellow, left = cooler/blue.")
    S("contrast", 0.03, "%+.0f%%", 100, "tone", "对比度", "Contrast", "明暗反差，越高越浓烈。", "Light/dark difference. Higher = punchier.")
    S("sat", 0.05, "%.0f%%", 100, "tone", "饱和度", "Saturation", "颜色鲜艳程度，100% 为原样。", "How vivid colors are. 100% = unchanged.")
    S("vibrance", 0.05, "%+.0f%%", 100, "tone", "智能鲜艳度", "Smart Vibrance", "只提升偏灰的颜色，不会让本来鲜艳的颜色过艳。", "Boosts dull colors only; vivid ones are left alone.")
    S("tint", 0.05, "%+.2f", 1, "tone", "色调（绿↔品红）", "Tint (green/magenta)", "微调画面偏绿或偏紫红。", "Nudges the picture toward green or magenta.")
    S("lightness", 0.02, "%+.2f", 1, "tone", "整体明度", "Lightness Offset", "整体加亮或压暗，不改变对比。", "Brightens or darkens everything without changing contrast.")
    S("gamma", 0.04, "%.2f", 1, "tone", "中间调亮度", "Midtone Gamma", "调整中间调亮度，最暗和最亮基本不变。", "Adjusts midtones; deepest shadows and highlights stay similar.")
    S("filmic", 0.10, "%.0f%%", 100, "tone", "电影高光曲线", "Filmic Highlights", "高光更柔和不过曝，像电影胶片（ACES）。", "Softer, non-clipping highlights like film (ACES).")
    S("hl_desat", 0.10, "%.0f%%", 100, "tone", "高光去饱和", "Highlight Desat.", "最亮处略微发白，防止颜色溢出。", "Very bright areas turn slightly white to avoid color blowout.")
    S("sun_fill", 0.10, "%.0f%%", 100, "tone", "天光补光", "Sun Fill Light", "左上方天光对画面的补光，增加空间感。", "Directional fill from the upper-left sky for more depth.")
    S("original_mix", 0.10, "%.0f%%", 100, "tone", "回退原画比例", "Original Mix", "与原版画面混合的比例，100% 等于没调色。", "Blend with the original picture; 100% = no grading.")
    S("slope_r", 0.04, "%.2f", 1, "tone", "亮部 · 红", "Highlights · Red", "专业调色(Slope)：亮部红色倍率。", "Pro grading (Slope): red gain in highlights.")
    S("slope_g", 0.04, "%.2f", 1, "tone", "亮部 · 绿", "Highlights · Green", "专业调色(Slope)：亮部绿色倍率。", "Pro grading (Slope): green gain in highlights.")
    S("slope_b", 0.04, "%.2f", 1, "tone", "亮部 · 蓝", "Highlights · Blue", "专业调色(Slope)：亮部蓝色倍率。", "Pro grading (Slope): blue gain in highlights.")
    S("offset_r", 0.02, "%+.2f", 1, "tone", "暗部 · 红", "Shadows · Red", "专业调色(Offset)：给暗部整体加/减红色。", "Pro grading (Offset): adds/removes red in shadows.")
    S("offset_g", 0.02, "%+.2f", 1, "tone", "暗部 · 绿", "Shadows · Green", "专业调色(Offset)：给暗部整体加/减绿色。", "Pro grading (Offset): adds/removes green in shadows.")
    S("offset_b", 0.02, "%+.2f", 1, "tone", "暗部 · 蓝", "Shadows · Blue", "专业调色(Offset)：给暗部整体加/减蓝色。", "Pro grading (Offset): adds/removes blue in shadows.")
    S("power_r", 0.04, "%.2f", 1, "tone", "中间调 · 红", "Midtones · Red", "专业调色(Power)：中间调红色曲线。", "Pro grading (Power): red curve in midtones.")
    S("power_g", 0.04, "%.2f", 1, "tone", "中间调 · 绿", "Midtones · Green", "专业调色(Power)：中间调绿色曲线。", "Pro grading (Power): green curve in midtones.")
    S("power_b", 0.04, "%.2f", 1, "tone", "中间调 · 蓝", "Midtones · Blue", "专业调色(Power)：中间调蓝色曲线。", "Pro grading (Power): blue curve in midtones.")
    S("vignette", 0.03, "%.0f%%", 100, "tone", "边缘暗角", "Edge Vignette", "画面四周压暗，视线更集中在中心。", "Darkens the screen edges to focus attention on the center.")
    S("grain", 0.04, "%.0f%%", 100, "tone", "胶片颗粒", "Film Grain", "加入细微的动态颗粒，有胶片质感。", "Adds subtle animated grain for a film look.")
    S("vanilla_cc", 0.10, "%.0f%%", 100, nil, "官方季节滤镜", "Official Season Filter", "游戏自带的四季/昼夜色调强度，100% 为原版。", "Strength of the game's own season/day colors; 100% = vanilla.")
    S("snow_cap", 0.10, "%.0f%%", 100, nil, "冬季积雪遮挡", "Winter Snow Overlay", "冬天屏幕边缘的积雪遮挡程度，调低看得更清。", "How much snow covers the screen in winter; lower to see more.")
    T("sanity_color", "低理智保持原色", "Keep Color at Low Sanity", "理智很低时画面不再变灰暗。", "The screen no longer turns grey when sanity is low.")
    T("distort_free", "低理智去晃动", "No Sanity Wobble", "理智很低时画面不再扭曲波动。", "The screen no longer warps when sanity is low.")
    T("sand_filter", "沙尘暴护眼", "Hide Sandstorm Dust", "夏季沙尘暴时隐藏遮挡视野的风沙层。", "Hides the view-blocking dust layer during summer sandstorms.")
    F("clean_filter", "滤镜净化（一键）", "Clean Filters (one-click)", "一键应用上面几项护眼选项。", "Applies the eye-care filter options above in one click.")

    -- Light & Glow & Shadow
    F("soft_bloom", "柔光泛光（总开关）", "Soft Bloom (master)", "灯光、火焰周围有柔和光晕。需在游戏设置里开启泛光。", "Soft halos around lights and fire. Turn on Bloom in game settings.")
    S("bloom_str", 0.10, "%.0f%%", 100, "soft_bloom", "光晕强度", "Bloom Strength", "光晕有多明显。", "How visible the glow is.")
    S("bloom_warmth", 0.15, "%+.0f%%", 100, "soft_bloom", "光晕冷暖", "Glow Warmth", "往右偏暖橙，往左偏冷蓝。", "Right = warm orange, left = cool blue.")
    S("bloom_spread", 0.10, "%.0f%%", 100, "soft_bloom", "光晕扩散范围", "Glow Spread", "越大越柔越宽，越小越紧凑。", "Larger = softer and wider; smaller = tighter.")
    S("god_rays", 0.15, "%.0f%%", 100, "soft_bloom", "屏幕日光束", "Screen Sun Rays", "太阳方向的屏幕斜向光晕，白天氛围感。", "Screen-space light shafts from the sun for daytime atmosphere.")
    S("vol_lightrays", 0.15, "%.0f%%", 100, nil, "林间 3D 体积光柱", "3D Forest Light Rays", "在森林/桦树林/洞穴生成真·世界空间斜射光柱（白天暖金/黄昏橙红/满月银白），走近自动柔隐不挡脸。0 为关闭。", "True 3D world-space light shafts in forests & caves (gold by day, amber at dusk, silver at full moon). Auto-fades up close. 0 = off.")
    T("vol_lightrays_everywhere", "全域/基地晒太阳光柱", "Sunbeams Everywhere (Base)", "开启后不仅限森林，连基地草地、地砖和任意地上树旁也会自然洒下水中木斜射阳光柱（纯客户端公服可用）。默认关闭。", "Spawns Waterlogged-style 3D sunbeams across bases, planted trees & all walkable surface turfs—not just forests (client-only, public-server safe). Default off.")
    S("vol_ground_fog", 0.15, "%.0f%%", 100, nil, "林间/沼泽/洞穴受光地雾", "Lit Ground Mist", "在森林/沼泽/洞穴地面浮起轻薄水平迷雾，吃真实世界光照图（篝火旁泛暖金、暗处泛幽蓝），角色脚下留空不挡视野。0 为关闭。", "Subtle horizontal mist over forest, marsh & cave floors lit by the true world lightmap (warm near campfires, cool in shade). 0 = off.")
    S("vfx_fire_embers", 0.15, "%.0f%%", 100, nil, "篝火升腾火星", "Fire Embers", "火堆/火炉上方飘起细碎火星（吸热火堆为冰蓝星火），配柔光氛围极佳。0 为关闭。", "Rising warm embers above fires (icy sparks on endo fires). Pairs with Soft Bloom. 0 = off.")
    S("vfx_ambient_motes", 0.15, "%.0f%%", 100, nil, "空气光尘与孢子", "Ambient Motes", "白昼微光尘、秋冬落叶、洞穴荧光孢子，轻度点缀不挡视野。0 为关闭。", "Subtle sun motes, seasonal leaves, and glowing cave spores. 0 = off.")
    S("bloom_thresh", 0.04, "%.2f", 1, "soft_bloom", "起晕亮度门槛", "Glow Threshold", "多亮的东西才会发光，调低则更多物体发光。", "How bright something must be to glow. Lower = more glowing things.")
    S("bloom_knee", 0.10, "%.0f%%", 100, "soft_bloom", "过渡柔和度", "Glow Softness", "光晕出现得更平滑，防止突然炸白。", "Makes the glow ramp in smoothly instead of popping.")
    S("bloom_chr", 0.05, "%+.0f%%", 100, "soft_bloom", "光晕饱和度", "Glow Saturation", "光晕颜色的浓淡。", "How colorful the glow is.")
    S("bloom_prot", 0.05, "%.0f%%", 100, "soft_bloom", "防洗白保护", "Anti-Washout", "高亮区域不会被光晕冲成一片白。", "Stops bright areas from washing out to white.")
    S("bloom_compress", 0.05, "%.0f%%", 100, "soft_bloom", "高光保色", "Highlight Color Keep", "亮部压一压，保留颜色不发白。", "Compresses highlights so they keep their color.")
    S("glow_tail", 0.10, "%.0f%%", 100, "soft_bloom", "夜晚光晕长尾", "Night Glow Tail", "夜里光晕边缘更自然地渐隐。", "Glow edges fade out more naturally at night.")
    S("light_wrap", 0.10, "%.0f%%", 100, "soft_bloom", "暗部空气光", "Dark Air Light", "暗处也有一层薄薄的光雾包裹。", "A thin haze of light wraps around dark areas.")
    S("glow_rim", 0.10, "%.0f%%", 100, "soft_bloom", "光源轮廓光", "Light Rim", "光源附近一圈细亮边。", "A thin bright rim near light sources.")
    F("ao", "接地阴影（总开关）", "Contact Shadow (master)", "人物、生物脚下有柔和的接地阴影，更有立体感。", "Soft shadows under characters and creatures for more depth.")
    S("ao_str", 0.15, "%.0f%%", 100, "ao", "接地阴影强度", "Contact Shadow", "脚下阴影的深浅。", "How dark the shadow under feet is.")
    S("surface_light", 0.15, "%.0f%%", 100, "native", "3D 立体受光", "3D Surface Light", "物体按光照方向出现明暗面，更像立体物件。需 DLL。", "Objects get lit and shaded sides like real 3D. Needs DLL.")
    S("shade_warm", 0.10, "%.0f%%", 100, "native", "受光面冷暖", "Lit Side Warmth", "朝光的一面偏冷或偏暖。", "Whether the lit side looks cool or warm.")
    S("shade_contrast", 0.10, "%.0f%%", 100, "native", "明暗反差", "Light/Dark Contrast", "立体明暗面的反差大小。", "Contrast between lit and shaded sides.")
    S("shade_cool", 0.10, "%.0f%%", 100, "native", "背光面偏蓝", "Backlit Cool Tint", "背光的一面带点冷蓝，更有空气感。", "Backlit side gets a cool-blue tint for atmosphere.")
    S("shade_dark", 0.10, "%.0f%%", 100, "native", "背光面压暗", "Backlit Darkening", "背光的一面压暗多少。", "How dark the backlit side becomes.")
    F("dark_clarity", "夜晚/洞穴提亮（总开关）", "Night/Cave Clarity (master)", "夜里和洞穴的暗部更容易看清细节。", "Dark areas at night and in caves show more detail.")
    S("dark_str", 0.15, "%.0f%%", 100, "dark_clarity", "提亮强度", "Clarity Strength", "暗部提亮多少。", "How much dark areas are lifted.")
    S("shadow_density", 0.10, "%.0f%%", 100, "native", "长影浓度", "Long Shadow Density", "树木、建筑投出的斜长阴影有多浓。0 为关闭。需 DLL。", "How dark long shadows from trees and buildings are. 0 = off. Needs DLL.")
    S("vol_cloud_shade", 0.15, "%.0f%%", 100, nil, "动态流动云影", "Drifting Cloud Shadows", "利用官方原生阴影器让大尺度柔和云影缓慢掠过草原与海面，打破白天死平均匀亮度。0 为关闭。", "Native C++ ShadeRenderer drifting soft cloud shadows across plains & oceans by day. 0 = off.")
    S("vol_canopy_shade", 0.15, "%.0f%%", 100, nil, "密林树冠斑驳影", "Forest Canopy Shade", "走进常青林/桦树林时地面浮现随风摇曳的树叶碎影，与林间体积光柱明暗呼应。0 为关闭。", "Dappled swaying tree-canopy shade on forest floors, pairing with 3D light rays. 0 = off.")
    T("shadow_point_lights", "篝火/提灯真实投影", "Firelight Shadows", "火光和提灯周围的物体向四周投影。", "Objects near fire and lanterns cast shadows in all directions.")
    T("shadow_night", "夜间火光反向投影", "Night Light Shadows", "夜晚阴影方向跟着火光走。", "At night shadows follow the direction of firelight.")
    S("shadow_length", 0.10, "%.2fx", 1, "native", "影子长度", "Shadow Length", "影子拉得多长。", "How long shadows are stretched.")
    S("shadow_soft", 0.10, "%.2f", 1, "native", "阴影边缘柔和度", "Shadow Softness", "0 为像素级锐利剪影，越大阴影边缘越柔虚。", "0 = pixel-crisp silhouette; larger = softer, blurrier edges.")
    S("shadow_point_strength", 0.10, "%.0f%%", 100, "native", "火光投影强度", "Firelight Shadows", "篝火/提灯投影有多明显。", "How visible firelight shadows are.")
    S("shadow_entity_recv", 0.08, "%.0f%%", 100, "native", "树荫遮挡人物", "Shade on Characters", "走进树荫时角色会变暗。", "Characters get darker when walking into shade.")
    S("shadow_oval_keep", 0.08, "%.0f%%", 100, "native", "保留原版脚底圆影", "Vanilla Foot Shadow", "0 = 有长影时完全隐藏原版黑圆圈（无长影/远火源自动恢复），100% = 始终保留。", "0 = cleanly hide vanilla oval under live shadows (auto-restores in darkness/far from fire); 100% = always keep.")
    S("shadow_fade", 0.10, "%.0f%%", 100, "native", "阴影渐隐", "Shadow Fade", "影子远端渐渐变淡，更自然。", "Shadows fade toward their far end for a natural look.")
    S("shadow_sway", 0.10, "%.0f%%", 100, "native", "树冠摇曳", "Canopy Sway", "阴影随风轻轻晃动的幅度。", "How much shadows sway gently in the wind.")
    S("shadow_smooth", 0.10, "%.0f%%", 100, "native", "光影平滑", "Light Smoothing", "太阳与阴影移动更平滑，避免跳变。", "Smoother sun/shadow motion without jumps.")
    S("shadow_weather", 0.15, "%.0f%%", 100, "native", "雨雪与四季衰减", "Weather & Season Fade", "雨雪天阴影平滑变淡、冬天柔和、夏天更浓（100% = 雨雪×0.6 / 冬×0.8 / 夏×1.2）。0 为关闭。", "Smoothly dims sun shadows in rain/snow & winter and boosts in summer (100% = rain 0.6x / winter 0.8x / summer 1.2x). 0 = off.")
    S("shadow_moon_blue", 0.15, "%.0f%%", 100, "native", "满月自动冷蓝影", "Full-Moon Blue Tint", "白天保持自定义影色（如纯黑），仅满月自动切换幽蓝月影。0 为关闭。", "Keeps your tint by day/dusk (e.g. pure black) and automatically shifts to moonlit blue on full moons. 0 = off.")
    S("shadow_dusk_point", 0.15, "%.0f%%", 100, "native", "黄昏火影强度", "Dusk Firelight Shadows", "独立控制黄昏时段火堆/提灯是否投影。0 = 黄昏仅留太阳影，入夜再出火影。", "Controls firelight shadows independently at dusk. 0 = solar shadows only at dusk, fire shadows at night.")
    S("shadow_min_height", 0.10, "%.2f", 1, "native", "过滤低矮小物影", "Filter Low Clutter", "隐藏低于此高度的地面掉落物与矮花杂物投影（0.85 可净空地物，保留树木角色建筑）。0 为全部投影。", "Skips shadows for low ground drops & tiny flowers below this world height (0.85 cleans clutter while keeping trees/mobs/structures). 0 = cast all.")
    T("shadow_flash_lights", "闪电/瞬光动态影", "Lightning & Flash Shadows", "闪电、爆炸等瞬态强光打出 1 秒淡出瞬影。", "Lightning strikes and explosive flashes cast brief 1s decaying dynamic shadows.")
    S("shadow_tint_r", 0.08, "%.2f", 1, "native", "阴影颜色 · 红", "Shadow Color · Red", "阴影的颜色偏向（默认偏蓝紫）。", "Color cast of shadows (default leans blue-purple).")
    S("shadow_tint_g", 0.08, "%.2f", 1, "native", "阴影颜色 · 绿", "Shadow Color · Green", "阴影的颜色偏向（默认偏蓝紫）。", "Color cast of shadows (default leans blue-purple).")
    S("shadow_tint_b", 0.08, "%.2f", 1, "native", "阴影颜色 · 蓝", "Shadow Color · Blue", "阴影的颜色偏向（默认偏蓝紫）。", "Color cast of shadows (default leans blue-purple).")
    T("shadow_hd", "超清阴影画布", "Ultra-HD Shadow Canvas", "阴影更精细（2048 超清分辨率，锐影轮廓极清晰），略增显存。", "Ultra-crisp shadows (2048 resolution for razor-sharp silhouettes) at a modest VRAM cost.")
    T("shadow_skip_nightmare", "暗影生物不投影", "No Shadow on Shadow Creatures", "影怪本来就是影子，不再多投一个。", "Shadow creatures are shadows already, so they cast none.")

    -- Water & Nature
    S("ocean_pbr", 0.15, "%.0f%%", 100, "native", "PBR 立体海浪与水色", "PBR Ocean Waves & Tint", "实时 3 层解析法线海浪、视差立体水纹、菲涅尔立体受光与浅滩/深海色差。0 为原版海水。需 DLL。", "Real-time 3-octave analytic PBR waves, parallax heightfield, Fresnel diffuse & shallow/deep tint. 0 = vanilla. Needs DLL.")
    S("ocean_sky_refl", 0.15, "%.0f%%", 100, "native", "海面天空流云倒影", "Sky & Cloud Reflection", "在海面上实时映射缓慢飘动的镜面天穹云影，随菲涅尔视角自然渐变。0 为关闭。需 DLL。", "Procedural drifting sky & cloud reflections across the ocean modulated by Fresnel angle. 0 = off. Needs DLL.")
    S("ocean_sun_glow", 0.15, "%.0f%%", 100, "native", "海面日月碎金高光", "Sun/Moon Mie Glint", "跟踪实时太阳/满月方位在细碎波纹上折射出物理级米氏散射碎金粼光。0 为关闭。需 DLL。", "Physically-based Mie specular sun/moon sparkle tracking live celestial direction across micro-ripples. 0 = off. Needs DLL.")
    T("ocean_hide_waves", "隐藏原版 2D 纸片浪", "Hide Vanilla 2D Waves", "开启后在 GPU 端瞬间隐藏原版 2D 纸片海浪，搭配 PBR 海洋呈现极净丝滑镜面海（随时可切回）。", "Instantly hides vanilla 2D paper sea wave sprites on the GPU for an ultra-clean PBR mirror ocean (reversible anytime).")
    S("vol_edge_fog", 0.15, "%.0f%%", 100, nil, "世界海界边缘体积雾", "World-Edge Ocean Mist", "在世界深海/虚空边缘自动平铺缓慢飘动的大尺度受光海雾，柔化地图尽头锯齿边界。0 为关闭。", "Drifting horizontal lightmap-lit mist blanketing ocean & abyss world edges to soften the void boundary. 0 = off.")
    S("glint_str", 0.10, "%.0f%%", 100, "native", "海面波光强度", "Sea Sparkle", "海面闪闪发光的焦散光纹。需 DLL。", "Sparkling caustic light patterns on the sea. Needs DLL.")
    S("glint_density", 0.04, "%.2f", 1, "native", "光纹锐利度", "Sparkle Sharpness", "光纹线条的清晰程度。", "How crisp the light patterns are.")
    S("glint_grain", 0.15, "%.2f", 1, "native", "光纹颗粒大小", "Sparkle Grain", "光纹是细碎还是粗大。", "Whether sparkles are fine or coarse.")
    S("glint_soft", 0.10, "%.0f%%", 100, "native", "浅滩与海色融合", "Shallows Blend", "浅水沙底与海水颜色融合程度。", "How the sandy bottom blends into the sea color.")
    S("water_refl_str", 0.10, "%.0f%%", 100, "native", "水面倒影强度", "Water Reflection", "海边的树、船会倒映在水里。内陆/洞穴无消耗。", "Trees and boats reflect in water. Free inland and in caves.")
    S("water_refl_ripple", 0.10, "%.0f%%", 100, "native", "倒影波动", "Reflection Ripple", "倒影随海浪晃动扭曲的程度。", "How much reflections wobble with the waves.")
    T("wind_sway_enabled", "植被风动（开关）", "Foliage Sway (switch)", "草和树微风摇曳，走过草丛会被拨开。", "Grass and trees sway in the breeze and part when you walk through.")
    S("wind_sway_strength", 0.10, "%.0f%%", 100, "native", "微风摇曳幅度", "Breeze Strength", "草木摇摆幅度。", "How much plants sway.")
    S("wind_wake_strength", 0.10, "%.0f%%", 100, "native", "穿行拨草幅度", "Walk-Through Parting", "走过草丛时被拨开的幅度。", "How much grass parts as you walk through.")
    F("env", "近岸/雨天湿润（总开关）", "Shore & Rain Wetness (master)", "靠近海岸、下雨时地面更有湿润质感。", "Ground looks wetter near shores and in the rain.")
    S("env_str", 0.15, "%.0f%%", 100, "env", "湿润强度", "Wetness Strength", "湿润质感的强弱。", "How strong the wet look is.")

    -- Motion & Combat
    E("skeletal", "skeletal", "240Hz 超顺滑补帧", "240Hz Ultra-Smooth", "让角色、怪物、粒子、镜头从 30 帧变丝滑，不影响联机判定。", "Characters, mobs, particles and camera glide smoothly. Gameplay unaffected.")
    S("anim_crossfade", 5.0, "%.0fms", 1, "skel", "动作切换融合", "Action Blend Time", "跑→停、跑→砍之间平滑过渡，单位毫秒。", "Smooth transitions between run, stop and attack (milliseconds).")
    S("anim_spline", 0.10, "%.0f%%", 100, "skel", "曲线平滑", "Curve Smoothing", "让肢体摆动更圆润不生硬，100% 最平滑。", "Rounder, less stiff limb motion. 100% = smoothest.")
    S("vfx_footsteps", 0.15, "%.0f%%", 100, nil, "角色地皮微尘", "Footstep Particles", "跑动/骑牛时按地皮带起脚步尘、冬日雪沫、雨天水花和草叶（站立 0 消耗）。0 为关闭。", "Velocity-driven dust, snow puffs, rain splashes & grass rustle when moving. 0 = off.")
    T("hitfeel_enabled", "打击感表演（总开关）", "Hit-Feel (master)", "开启后，挥砍与受击会有顿帧、前冲、闪光与落地物理碎屑（零屏幕晃动）。只改显示，不改判定。", "Adds hit-stop, strike lunge, flash & ground-physics impact sparks (zero screen shake). Visual only.")
    S("hitfeel_hitstop", 0.15, "%.0f%%", 100, "hit", "卡肉顿帧", "Hit Stop", "命中时画面微微定格，手感更扎实。", "A tiny freeze on impact for a heavier feel.")
    S("hitfeel_recoil", 0.15, "%.0f%%", 100, "hit", "受击震颤与弹性", "Recoil & Squash", "被打的一方会震一下、被压扁再弹回。", "The target shakes, squashes and bounces back.")
    S("hitfeel_flash", 0.15, "%.0f%%", 100, "hit", "受击闪光", "Hit Flash", "被打的瞬间闪一下亮光。", "A bright flash the instant something is hit.")
    S("hitfeel_campunch", 0.15, "%.0f%%", 100, "hit", "出招前冲回弹", "Strike Lunge", "出刀命中时角色沿出招方向轻微前冲再回弹，零镜头晃动不晕屏。", "Character leans slightly into each strike and settles back. Zero screen shake.")
    S("hitfeel_vfx", 0.15, "%.0f%%", 100, "hit", "刀光火花与碎屑", "Slash & Impact FX", "挥砍月牙刀光、刀尖流光、命中局部地光，以及真落地物理的木屑/碎石/火星。", "Slash arcs, weapon-tip glints, localized hit glow, plus ground-physics wood chips, rocks & sparks.")

    -- System
    E("lowlat", "lowlat", "低延迟操作", "Low-Latency Input", "减少操作到画面反馈的延迟，急甩鼠标也不丢点击。", "Less delay from input to screen, and fast mouse flicks never drop clicks.")
    E("clipcursor", "clip", "双屏防甩出鼠标", "Dual-Monitor Cursor Lock", "多显示器时鼠标不会被甩出游戏窗口（暂停/切屏自动解除）。", "Keeps the mouse inside the game on multi-monitor setups (auto-unlocks on pause/Alt-Tab).")
    E("hudmode", "hud", "左上角性能显示", "Performance Overlay", "点击切换：简洁 / 详细 / 隐藏。", "Click to cycle: Compact / Detailed / Hidden.")

    -- ========================================================================
    -- HOME (Simple mode): style cards, 6 plain-language feature cards, 4 quick sliders
    -- ========================================================================
    local tab1 = tab_roots[1]
    do
        local _, btxt = AddSectionBanner(tab1, 208, "① 先选一个画面风格", "1. Pick a look", { 0.16, 0.14, 0.10, 0.88 })
        if btxt then
            btxt:SetRegionSize(276, 18)
            btxt:SetHAlign(G.ANCHOR_LEFT)
            btxt:SetPosition(-46, 208, 0)
        end
        local pc = UI.preset_card
        pc.home_open_bg = NewImage(tab1, 142, 208, 86, 18, 0.24, 0.30, 0.36, 0.95)
        pc.home_open_btn = NewButton(tab1, 142, 208, 86, 18, is_zh and "6套预设 >>" or "6 Presets >>", 12, function()
            if UI.TogglePresetCard then UI.TogglePresetCard() end
        end)
        pc.home_open_btn:SetTextColour(1.0, 0.90, 0.62, 1.0)
        pc.home_open_btn:SetTextFocusColour(1.0, 0.98, 0.82, 1.0)
    end
    local home_cards = {}
    for pid = 0, 3 do
        local cx = -144 + pid * 96
        local cborder = NewImage(tab1, cx, 172, 92, 48, 0.95, 0.74, 0.36, 0.0)
        local cbg = NewImage(tab1, cx, 172, 88, 44, 0.10, 0.115, 0.15, 0.96)
        local cname = NewText(tab1, 15, "", cx, 181, 86, 20, nil, 0.90, 0.93, 0.96, 1.0)
        local cdesc = NewText(tab1, 12, "", cx, 163, 86, 16, nil, 0.58, 0.66, 0.76, 0.95)
        NewButton(tab1, cx, 172, 88, 44, nil, nil, function()
            api.SetPreset(pid)
            DoRefresh()
        end)
        home_cards[#home_cards + 1] = { id = pid, border = cborder, bg = cbg, name = cname, desc = cdesc }
    end

    AddSectionBanner(tab1, 130, "② 常用功能（点一下开 / 关）", "2. Main features (click to toggle)", { 0.10, 0.18, 0.20, 0.88 })
    local home_defs = {
        { src = "bundle", key = "smooth", zh = "240 帧超顺滑", en = "Ultra-Smooth 240 FPS",
          hzh = "角色、怪物、镜头动作从 30 帧变丝滑", hen = "Characters, mobs & camera glide smoothly",
          dzh = "让角色、怪物、粒子、镜头从 30 帧变丝滑，不影响联机判定。", den = "Characters, mobs, particles and camera glide smoothly. Gameplay unaffected." },
        { src = "bundle", key = "lowlat", zh = "低延迟操作", en = "Low-Latency Input",
          hzh = "走位、攻击响应更快，急甩鼠标不丢点击", hen = "Faster response; fast mouse flicks never miss",
          dzh = "减少操作到画面反馈的延迟，急甩鼠标也不丢点击。", den = "Less delay from input to screen, and fast flicks never drop clicks." },
        { src = "bundle", key = "clarity", zh = "画面更清晰", en = "Sharper Picture",
          hzh = "线条锐利、斜视角贴图不糊", hen = "Crisp lines; angled textures stay clear",
          dzh = "同时开启边缘锐化、16× 各向异性与贴图防发糊。", den = "Turns on edge sharpening, 16x anisotropic filtering and anti-blur textures together." },
        { src = "bundle", key = "glow", zh = "柔光与接地阴影", en = "Soft Glow & Contact Shadow",
          hzh = "火光有柔和光晕，脚下有接地阴影", hen = "Soft halos around fire; shadows under feet",
          dzh = "同时开启柔光泛光与脚下接地阴影。泛光需在游戏设置里打开。", den = "Turns on soft bloom and contact shadows. Bloom must be enabled in game settings." },
        { src = "bundle", key = "world3d", zh = "立体光影与氛围微粒", en = "3D Light, Shadows & Atmosphere",
          hzh = "长影、立体受光、倒影、篝火火星与光尘", hen = "Long shadows, 3D lighting, reflections, embers & motes",
          dzh = "开启长影、3D 立体受光、水面倒影、植被风动，以及篝火火星与环境光尘。", den = "Long shadows, 3D surface light, reflections, foliage sway, fire embers & ambient motes." },
        { src = "bundle", key = "hitfeel", zh = "无震动打击感与脚步尘", en = "Zero-Shake Hit-Feel & Footsteps",
          hzh = "卡肉前冲、手绘落地碎屑火星与地皮脚步尘", hen = "Hit-stop, lunge, ground-physics sparks & footsteps",
          dzh = "挥砍与受击的顿帧、出招前冲、落地木屑/碎石/火星及角色地皮微尘（零屏幕晃动）。", den = "Hit-stop, strike lunge, ground-physics wood chips/rocks/sparks & footstep dust (zero screen shake)." },
    }
    for i = 1, #home_defs do
        home_defs[i].big = true
        AddToggleRow(tab1, 100 - (i - 1) * 38, 34, home_defs[i])
    end

    AddSectionBanner(tab1, -124, "③ 快速调节（点右侧数值可复位）", "3. Quick tuning (click value to reset)", { 0.14, 0.14, 0.20, 0.88 })
    local home_slider_keys = { "sharp", "bloom_str", "shadow_density", "temp" }
    for i = 1, #home_slider_keys do
        AddStepperRow(tab1, -148 - (i - 1) * 29, 26, ROWS[home_slider_keys[i]])
    end

    MakeSlotRow(tab1, -268)

    -- Bottom bar: restore vanilla | share | go Pro
    local function HomeBottomButton(x, w, tint)
        local bgw = NewImage(tab1, x, -307, w, 30, tint[1], tint[2], tint[3], tint[4])
        return bgw
    end
    HomeBottomButton(-126, 122, { 0.30, 0.22, 0.12, 0.94 })
    local home_reset_btn = NewButton(tab1, -126, -307, 122, 30, is_zh and "恢复原版纯净" or "Restore Vanilla", 15, function()
        api.SetPreset(0)
        DoRefresh()
    end)
    home_reset_btn:SetTextColour(1.0, 0.88, 0.58, 1.0)
    home_reset_btn:SetTextFocusColour(1.0, 0.96, 0.78, 1.0)

    HomeBottomButton(0, 122, { 0.24, 0.20, 0.11, 0.94 })
    local share_open_btn = NewButton(tab1, 0, -307, 122, 30, is_zh and "分享 / 导入方案码" or "Share / Import", 15)
    share_open_btn:SetTextColour(1.0, 0.90, 0.62, 1.0)
    share_open_btn:SetTextFocusColour(1.0, 0.98, 0.80, 1.0)

    HomeBottomButton(126, 122, { 0.12, 0.24, 0.30, 0.94 })
    local home_pro_btn = NewButton(tab1, 126, -307, 122, 30, is_zh and "专业调参 >>" or "Pro Tuning >>", 15, function()
        panel_ui_mode = "pro"
        if type(api.SetUiMode) == "function" then pcall(api.SetUiMode, "pro") end
        if ApplyMode then ApplyMode() end
        DoRefresh()
    end)
    home_pro_btn:SetTextColour(0.68, 0.92, 1.0, 1.0)
    home_pro_btn:SetTextFocusColour(0.88, 0.98, 1.0, 1.0)


    -- =========================================================================
    -- Floating Share & JSON Library Card (docked left of Studio Panel, zero pause!)
    -- Strict 3-Phase Transactional Import: Validate -> Atomic Apply -> Persist
    -- =========================================================================
    local SW, SH = 356, 480
    share_card = panel:AddChild(Widget("HS_ShareCodeCard"))
    share_card:SetPosition(-(W * 0.5 + SW * 0.5 + 10), -10, 0)
    share_card:Hide()
    local share_card_visible = false

    local sc_border = share_card:AddChild(Image("images/global.xml", "square.tex"))
    sc_border:ScaleToSize(SW + 4, SH + 4)
    sc_border:SetTint(0.76, 0.62, 0.34, 0.72)

    local sc_bg = share_card:AddChild(Image("images/global.xml", "square.tex"))
    sc_bg:ScaleToSize(SW, SH)
    sc_bg:SetTint(0.08, 0.08, 0.11, 0.96)

    local sc_title = share_card:AddChild(Text(FONT_BODY, 18, is_zh and "HyperShade · 方案分享与视觉闭环" or "HyperShade · Share & Look Match"))
    sc_title:SetPosition(-16, SH * 0.5 - 22, 0)
    sc_title:SetColour(0.96, 0.86, 0.58, 1.0)

    local sc_close = share_card:AddChild(ImageButton("images/global_redux.xml", "blank.tex", "spinner_focus.tex"))
    sc_close.scale_on_focus = false
    sc_close.move_on_click = false
    sc_close:ForceImageSize(28, 22)
    sc_close:SetPosition(SW * 0.5 - 20, SH * 0.5 - 22, 0)
    sc_close:SetText("X")
    sc_close:SetTextSize(16)
    sc_close:SetFont(FONT_NUM)
    sc_close:SetTextColour(0.95, 0.55, 0.55, 1.0)
    sc_close:SetOnClick(function()
        share_card_visible = false
        share_card:Hide()
    end)

    local sc_top_div = share_card:AddChild(Image("images/global.xml", "square.tex"))
    sc_top_div:ScaleToSize(SW - 24, 1)
    sc_top_div:SetPosition(0, SH * 0.5 - 39, 0)
    sc_top_div:SetTint(0.76, 0.62, 0.34, 0.35)

    -- Current scheme summary row ("当前方案：xxx" + green "已保存" badge + subtitle)
    local sc_cur_lbl = share_card:AddChild(Text(FONT_BODY, 15, is_zh and "当前方案：原版纯净" or "Active: Vanilla Pure"))
    sc_cur_lbl:SetRegionSize(SW - 96, 20)
    sc_cur_lbl:SetHAlign(G.ANCHOR_LEFT)
    sc_cur_lbl:SetPosition(-36, 184, 0)
    sc_cur_lbl:SetColour(0.96, 0.96, 0.98, 1.0)

    local sc_saved_bg = share_card:AddChild(Image("images/global.xml", "square.tex"))
    sc_saved_bg:ScaleToSize(58, 20)
    sc_saved_bg:SetPosition(SW * 0.5 - 41, 184, 0)
    sc_saved_bg:SetTint(0.11, 0.32, 0.19, 0.96)

    local sc_saved_txt = share_card:AddChild(Text(FONT_BODY, 12, is_zh and "已保存" or "Saved"))
    sc_saved_txt:SetPosition(SW * 0.5 - 41, 184, 0)
    sc_saved_txt:SetColour(0.56, 1.0, 0.72, 1.0)

    local sc_sub_lbl = share_card:AddChild(Text(FONT_BODY, 12, is_zh and "自定义着色器参数 · 留空标题可生成极致短码" or "Custom shader params · blank title = shortest code"))
    sc_sub_lbl:SetRegionSize(SW - 24, 16)
    sc_sub_lbl:SetHAlign(G.ANCHOR_LEFT)
    sc_sub_lbl:SetPosition(0, 165, 0)
    sc_sub_lbl:SetColour(0.66, 0.72, 0.80, 0.92)

    local function MakeOptionalTextEdit(parent, px, py, ew, eh, limit, prompt_s)
        local bg_i = parent:AddChild(Image("images/global.xml", "square.tex"))
        bg_i:ScaleToSize(ew, eh)
        bg_i:SetPosition(px, py, 0)
        bg_i:SetTint(0.03, 0.04, 0.06, 0.92)
        if TextEdit ~= nil then
            local ok_e, ed = pcall(function()
                return TextEdit(FONT_BODY, 14, "")
            end)
            if ok_e and ed then
                parent:AddChild(ed)
                ed:SetPosition(px, py, 0)
                if ed.SetRegionSize then ed:SetRegionSize(ew - 10, eh - 2) end
                if ed.SetHAlign then ed:SetHAlign(G.ANCHOR_LEFT) end
                if ed.SetTextLengthLimit then ed:SetTextLengthLimit(limit) end
                if ed.EnableScrollEditWindow then ed:EnableScrollEditWindow(true) end
                if ed.SetIdleTextColour then ed:SetIdleTextColour(0.92, 0.95, 0.98, 1.0) end
                if ed.SetEditTextColour then ed:SetEditTextColour(1.0, 0.96, 0.78, 1.0) end
                if ed.SetEditCursorColour then ed:SetEditCursorColour(1.0, 0.85, 0.45, 1.0) end
                if ed.SetTextPrompt then ed:SetTextPrompt(prompt_s, { 0.52, 0.56, 0.64, 0.85 }) end
                return ed, true
            end
        end
        local fb = parent:AddChild(Text(FONT_BODY, 13, prompt_s))
        fb:SetRegionSize(ew - 10, eh - 2)
        fb:SetHAlign(G.ANCHOR_LEFT)
        fb:SetPosition(px, py, 0)
        fb:SetColour(0.75, 0.82, 0.90, 0.92)
        return fb, false
    end

    local function ReadEditString(w, is_edit)
        if not w then return "" end
        if is_edit and type(w.GetLineEditString) == "function" then
            local ok_g, s = pcall(w.GetLineEditString, w)
            if ok_g and type(s) == "string" then return s end
        end
        if type(w.GetString) == "function" then
            local ok_g, s = pcall(w.GetString, w)
            if ok_g and type(s) == "string" then return s end
        end
        return ""
    end

    local sc_name_ed, sc_name_is_ed = MakeOptionalTextEdit(share_card, 0, 140, SW - 24, 24, 36, is_zh and "方案标题（可选，如：暗黑童话）" or "Preset title (optional)")

    -- Share code preview strip + integrated Copy button on the right
    local sc_code_bg = share_card:AddChild(Image("images/global.xml", "square.tex"))
    sc_code_bg:ScaleToSize(SW - 24, 32)
    sc_code_bg:SetPosition(0, 106, 0)
    sc_code_bg:SetTint(0.05, 0.08, 0.11, 0.94)

    local sc_code_txt = share_card:AddChild(Text(FONT_NUM, 13, ""))
    sc_code_txt:SetRegionSize(SW - 120, 24)
    sc_code_txt:SetHAlign(G.ANCHOR_LEFT)
    sc_code_txt:SetPosition(-44, 106, 0)
    sc_code_txt:SetColour(0.54, 1.0, 0.74, 0.98)

    local sc_status_bg = share_card:AddChild(Image("images/global.xml", "square.tex"))
    sc_status_bg:ScaleToSize(SW - 24, 54)
    sc_status_bg:SetPosition(0, -196, 0)
    sc_status_bg:SetTint(0.10, 0.13, 0.17, 0.88)

    local sc_status_txt = share_card:AddChild(Text(FONT_BODY, 12, ""))
    sc_status_txt:SetRegionSize(SW - 32, 48)
    sc_status_txt:SetPosition(0, -196, 0)
    sc_status_txt:SetColour(0.80, 0.88, 0.96, 0.95)

    local function SetShareStatus(kind, msg)
        if kind == "ok" then
            sc_status_bg:SetTint(0.09, 0.24, 0.15, 0.94)
            sc_status_txt:SetColour(0.52, 1.0, 0.70, 1.0)
        elseif kind == "err" then
            sc_status_bg:SetTint(0.32, 0.10, 0.10, 0.94)
            sc_status_txt:SetColour(1.0, 0.68, 0.68, 1.0)
        else
            sc_status_bg:SetTint(0.10, 0.14, 0.20, 0.90)
            sc_status_txt:SetColour(0.82, 0.90, 1.0, 0.96)
        end
        sc_status_txt:SetString(msg)
    end

    local sc_copy_bg = share_card:AddChild(Image("images/global.xml", "square.tex"))
    sc_copy_bg:ScaleToSize(82, 24)
    sc_copy_bg:SetPosition(SW * 0.5 - 57, 106, 0)
    sc_copy_bg:SetTint(0.18, 0.32, 0.22, 0.96)

    local sc_copy_btn = share_card:AddChild(ImageButton("images/global_redux.xml", "blank.tex", "spinner_focus.tex"))
    sc_copy_btn.scale_on_focus = false
    sc_copy_btn.move_on_click = false
    sc_copy_btn:ForceImageSize(82, 24)
    sc_copy_btn:SetPosition(SW * 0.5 - 57, 106, 0)
    sc_copy_btn:SetText(is_zh and "复制分享码" or "Copy Code")
    sc_copy_btn:SetTextSize(13)
    sc_copy_btn:SetFont(FONT_BODY)
    sc_copy_btn:SetTextColour(0.70, 1.0, 0.80, 1.0)
    sc_copy_btn:SetOnClick(function()
        if type(api.ExportShareCode) ~= "function" then return end
        local zh_now = IsChineseLocale()
        local title_in = ReadEditString(sc_name_ed, sc_name_is_ed)
        local code, copied, diffs = api.ExportShareCode(title_in)
        sc_code_txt:SetString(code)
        if copied then
            SetShareStatus("ok", zh_now
                and string.format("已复制到剪贴板！（%d 字符 · 含 %d 项参数差异）", #code, diffs or 0)
                or string.format("Copied to clipboard! (%d chars, %d diffs)", #code, diffs or 0))
        else
            if G.print then G.print("[HyperSmooth Share] " .. tostring(code)) end
            SetShareStatus("info", zh_now
                and string.format("已生成分享码（%d 字，已输出控制台；装新版DLL可一键复制）", #code)
                or string.format("Generated (%d chars, printed to console; DLL enables 1-click copy)", #code))
        end
        if panel_refresh_fn then panel_refresh_fn() end
    end)

    local sc_json_bg = share_card:AddChild(Image("images/global.xml", "square.tex"))
    sc_json_bg:ScaleToSize(SW - 24, 24)
    sc_json_bg:SetPosition(0, 73, 0)
    sc_json_bg:SetTint(0.14, 0.20, 0.28, 0.94)

    local sc_json_btn = share_card:AddChild(ImageButton("images/global_redux.xml", "blank.tex", "spinner_focus.tex"))
    sc_json_btn.scale_on_focus = false
    sc_json_btn.move_on_click = false
    sc_json_btn:ForceImageSize(SW - 24, 24)
    sc_json_btn:SetPosition(0, 73, 0)
    sc_json_btn:SetText(is_zh and "导出全部方案至 hypersmooth_presets.json" or "Export All to hypersmooth_presets.json")
    sc_json_btn:SetTextSize(13)
    sc_json_btn:SetFont(FONT_BODY)
    sc_json_btn:SetTextColour(0.76, 0.92, 1.0, 1.0)
    sc_json_btn:SetOnClick(function()
        if type(api.ExportJsonLibrary) == "function" then
            local zh_now = IsChineseLocale()
            local _, cnt = api.ExportJsonLibrary()
            SetShareStatus("ok", zh_now
                and string.format("已导出 hypersmooth_presets.json（共 %d 套方案）", cnt or 0)
                or string.format("Exported hypersmooth_presets.json (%d presets)", cnt or 0))
        end
    end)

    local sc_div = share_card:AddChild(Image("images/global.xml", "square.tex"))
    sc_div:ScaleToSize(SW - 24, 2)
    sc_div:SetPosition(0, 52, 0)
    sc_div:SetTint(0.76, 0.62, 0.34, 0.40)

    local sc_imp_lbl = share_card:AddChild(Text(FONT_BODY, 14, is_zh and "导入其他玩家的方案（先校验、再应用、最后保存）" or "Import Player Preset (Validate -> Apply -> Save)"))
    sc_imp_lbl:SetRegionSize(SW - 24, 20)
    sc_imp_lbl:SetHAlign(G.ANCHOR_LEFT)
    sc_imp_lbl:SetPosition(0, 34, 0)
    sc_imp_lbl:SetColour(0.92, 0.94, 0.98, 0.96)

    local sc_imp_ed, sc_imp_is_ed = MakeOptionalTextEdit(share_card, 0, 6, SW - 24, 26, 384, is_zh and "粘贴 HS1- 分享码（留空则读剪贴板）" or "Paste HS1-... share code (blank = read clipboard)")

    local function GetImportCandidateText()
        local s = ReadEditString(sc_imp_ed, sc_imp_is_ed)
        if type(s) == "string" and #(s:match("^%s*(.-)%s*$") or "") > 0 then
            return s
        end
        if type(api.ReadClipboard) == "function" then
            local cb = api.ReadClipboard()
            if type(cb) == "string" and #cb > 0 then
                if sc_imp_is_ed and sc_imp_ed.SetString then pcall(sc_imp_ed.SetString, sc_imp_ed, cb) end
                return cb
            end
        end
        return ""
    end

    -- Button 1: Dry-Run Validate ONLY (touches ZERO live settings!)
    local sc_chk_bg = share_card:AddChild(Image("images/global.xml", "square.tex"))
    sc_chk_bg:ScaleToSize(96, 26)
    sc_chk_bg:SetPosition(-118, -30, 0)
    sc_chk_bg:SetTint(0.18, 0.22, 0.30, 0.94)

    local sc_chk_btn = share_card:AddChild(ImageButton("images/global_redux.xml", "blank.tex", "spinner_focus.tex"))
    sc_chk_btn.scale_on_focus = false
    sc_chk_btn.move_on_click = false
    sc_chk_btn:ForceImageSize(96, 26)
    sc_chk_btn:SetPosition(-118, -30, 0)
    sc_chk_btn:SetText(is_zh and "导入前校验" or "Validate")
    sc_chk_btn:SetTextSize(14)
    sc_chk_btn:SetFont(FONT_BODY)
    sc_chk_btn:SetTextColour(0.78, 0.92, 1.0, 1.0)
    sc_chk_btn:SetOnClick(function()
        if type(api.ValidateShareCode) ~= "function" then return end
        local zh_now = IsChineseLocale()
        local raw = GetImportCandidateText()
        local ok, err, st = api.ValidateShareCode(raw)
        if ok and st then
            local nm = (#st.name > 0) and ("[" .. st.name .. "] ") or ""
            SetShareStatus("ok", zh_now
                and string.format("校验通过：%s基底=预设%d · 改动%d项（尚未覆盖）", nm, st.base_id or 0, st.diff_count or 0)
                or string.format("Valid: %sbase=%d, %d diffs (not applied yet)", nm, st.base_id or 0, st.diff_count or 0))
        else
            SetShareStatus("err", tostring(err or (zh_now and "校验失败（原配置未改动）" or "Invalid code (config unchanged)")))
        end
    end)

    -- Button 2: Transactional Paste & Safe Import (Validate -> Atomic Apply -> Persist!)
    local sc_go_bg = share_card:AddChild(Image("images/global.xml", "square.tex"))
    sc_go_bg:ScaleToSize(140, 26)
    sc_go_bg:SetPosition(4, -30, 0)
    sc_go_bg:SetTint(0.28, 0.22, 0.11, 0.96)

    local sc_go_btn = share_card:AddChild(ImageButton("images/global_redux.xml", "blank.tex", "spinner_focus.tex"))
    sc_go_btn.scale_on_focus = false
    sc_go_btn.move_on_click = false
    sc_go_btn:ForceImageSize(140, 26)
    sc_go_btn:SetPosition(4, -30, 0)
    sc_go_btn:SetText(is_zh and "安全导入并应用" or "Safe Import")
    sc_go_btn:SetTextSize(14)
    sc_go_btn:SetFont(FONT_BODY)
    sc_go_btn:SetTextColour(1.0, 0.90, 0.60, 1.0)
    sc_go_btn:SetOnClick(function()
        if type(api.ImportShareCode) ~= "function" then return end
        local zh_now = IsChineseLocale()
        local raw = GetImportCandidateText()
        local ok, err, st = api.ImportShareCode(raw)
        if ok and st then
            local nm = (#st.name > 0) and ("[" .. st.name .. "] ") or ""
            SetShareStatus("ok", zh_now
                and string.format("校验并导入成功：%s已应用 %d 项并保存", nm, st.diff_count or 0)
                or string.format("Imported %s(%d params applied & saved)", nm, st.diff_count or 0))
            if panel_refresh_fn then panel_refresh_fn() end
        else
            SetShareStatus("err", tostring(err or (zh_now and "导入中止：原配置未被覆盖" or "Import aborted: config untouched")))
        end
    end)

    -- Button 3: 1-Click Undo Last Import
    local sc_undo_bg = share_card:AddChild(Image("images/global.xml", "square.tex"))
    sc_undo_bg:ScaleToSize(80, 26)
    sc_undo_bg:SetPosition(124, -30, 0)
    sc_undo_bg:SetTint(0.22, 0.15, 0.18, 0.92)

    local sc_undo_btn = share_card:AddChild(ImageButton("images/global_redux.xml", "blank.tex", "spinner_focus.tex"))
    sc_undo_btn.scale_on_focus = false
    sc_undo_btn.move_on_click = false
    sc_undo_btn:ForceImageSize(80, 26)
    sc_undo_btn:SetPosition(124, -30, 0)
    sc_undo_btn:SetText(is_zh and "撤销导入" or "Undo")
    sc_undo_btn:SetTextSize(14)
    sc_undo_btn:SetFont(FONT_BODY)
    sc_undo_btn:SetTextColour(0.95, 0.78, 0.82, 1.0)
    sc_undo_btn:SetOnClick(function()
        if type(api.UndoLastImport) == "function" and api.UndoLastImport() then
            local zh_now = IsChineseLocale()
            SetShareStatus("info", zh_now and "已撤销上次导入，恢复原配置" or "Reverted to previous config")
            if panel_refresh_fn then panel_refresh_fn() end
        end
    end)

    local sc_hint = share_card:AddChild(Text(FONT_BODY, 11, is_zh and "坏码拦截 · 控制台: c_hsshare / c_hslook / c_hsjson" or "Corrupt codes blocked · Console: c_hsshare / c_hslook"))
    sc_hint:SetRegionSize(SW - 24, 16)
    sc_hint:SetPosition(0, -56, 0)
    sc_hint:SetColour(0.64, 0.70, 0.78, 0.90)

    -- Visual Closed-Loop Section (stored in UI.look_card to consume 0 extra locals!)
    UI.look_card = {}
    do
        local lc = UI.look_card
        lc.div = share_card:AddChild(Image("images/global.xml", "square.tex"))
        lc.div:ScaleToSize(SW - 24, 2)
        lc.div:SetPosition(0, -72, 0)
        lc.div:SetTint(0.32, 0.66, 0.72, 0.45)

        lc.title = share_card:AddChild(Text(FONT_BODY, 14, is_zh and "视觉闭环 · 从任意截图拟合 HS1- 分享码" or "Visual Closed-Loop · Match Image -> HS1-"))
        lc.title:SetRegionSize(SW - 24, 18)
        lc.title:SetHAlign(G.ANCHOR_LEFT)
        lc.title:SetPosition(0, -89, 0)
        lc.title:SetColour(0.58, 0.92, 1.0, 0.98)

        lc.sub = share_card:AddChild(Text(FONT_BODY, 11, is_zh and "Win+Shift+S/复制图片即可 · 仅映射自有可控管线（参数状态复现）" or "Copy any screenshot/image · Owned pipeline knobs only (param state)"))
        lc.sub:SetRegionSize(SW - 24, 16)
        lc.sub:SetHAlign(G.ANCHOR_LEFT)
        lc.sub:SetPosition(0, -106, 0)
        lc.sub:SetColour(0.66, 0.74, 0.82, 0.92)

        local function TriggerLookMatch(mode_str)
            local zh_now = IsChineseLocale()
            if type(api.StartLookMatch) ~= "function" then
                SetShareStatus("err", zh_now and "当前未加载视觉分析接口" or "Look Match API unavailable")
                return
            end
            SetShareStatus("info", zh_now
                and "正在后台线程解码并拟合视觉特征（主线程 0 阻塞）..."
                or "Decoding & fitting visual features on worker thread...")
            local ok_s, detail = api.StartLookMatch(mode_str, function(ok_r, msg_r, code_r)
                if ok_r then
                    if type(code_r) == "string" and #code_r > 0 then
                        sc_code_txt:SetString(code_r)
                    end
                    SetShareStatus("ok", tostring(msg_r or ""))
                    if panel_refresh_fn then panel_refresh_fn() end
                else
                    SetShareStatus("err", tostring(msg_r or ""))
                end
            end)
            if not ok_s then
                SetShareStatus("err", tostring(detail or (zh_now and "启动失败" or "Start failed")))
            end
        end

        lc.cb_bg = share_card:AddChild(Image("images/global.xml", "square.tex"))
        lc.cb_bg:ScaleToSize(160, 28)
        lc.cb_bg:SetPosition(-84, -135, 0)
        lc.cb_bg:SetTint(0.11, 0.28, 0.34, 0.96)

        lc.cb_btn = share_card:AddChild(ImageButton("images/global_redux.xml", "blank.tex", "spinner_focus.tex"))
        lc.cb_btn.scale_on_focus = false
        lc.cb_btn.move_on_click = false
        lc.cb_btn:ForceImageSize(160, 28)
        lc.cb_btn:SetPosition(-84, -135, 0)
        lc.cb_btn:SetText(is_zh and "吸取剪贴板截图" or "Match Clipboard")
        lc.cb_btn:SetTextSize(14)
        lc.cb_btn:SetFont(FONT_BODY)
        lc.cb_btn:SetTextColour(0.68, 0.96, 1.0, 1.0)
        lc.cb_btn:SetOnClick(function() TriggerLookMatch("clipboard") end)

        lc.fr_bg = share_card:AddChild(Image("images/global.xml", "square.tex"))
        lc.fr_bg:ScaleToSize(160, 28)
        lc.fr_bg:SetPosition(84, -135, 0)
        lc.fr_bg:SetTint(0.16, 0.24, 0.30, 0.94)

        lc.fr_btn = share_card:AddChild(ImageButton("images/global_redux.xml", "blank.tex", "spinner_focus.tex"))
        lc.fr_btn.scale_on_focus = false
        lc.fr_btn.move_on_click = false
        lc.fr_btn:ForceImageSize(160, 28)
        lc.fr_btn:SetPosition(84, -135, 0)
        lc.fr_btn:SetText(is_zh and "同帧捕获滤镜" or "Capture Frame")
        lc.fr_btn:SetTextSize(14)
        lc.fr_btn:SetFont(FONT_BODY)
        lc.fr_btn:SetTextColour(0.84, 0.92, 1.0, 1.0)
        lc.fr_btn:SetOnClick(function() TriggerLookMatch("same_frame") end)
    end

    local function RefreshShareCardHeader(snap, zh_now)
        local pnames = zh_now and preset_labels_zh or preset_labels_en
        local cur_nm = (type(snap.scheme_name) == "string" and #snap.scheme_name > 0)
            and snap.scheme_name
            or (pnames[snap.preset] or (zh_now and "自定义" or "Custom"))
        sc_title:SetString(zh_now and "HyperShade · 方案分享与视觉闭环" or "HyperShade · Share & Look Match")
        sc_cur_lbl:SetString((zh_now and "当前方案：" or "Active: ") .. cur_nm)
        sc_saved_txt:SetString(zh_now and "已保存" or "Saved")
        sc_sub_lbl:SetString(zh_now
            and "自定义着色器参数 · 留空标题可生成极致短码"
            or "Custom shader params · blank title = shortest code")
        sc_imp_lbl:SetString(zh_now
            and "导入其他玩家的方案（先校验、再应用、最后保存）"
            or "Import Player Preset (Validate -> Apply -> Save)")
        sc_hint:SetString(zh_now
            and "坏码拦截 · 控制台: c_hsshare / c_hslook / c_hsjson"
            or "Corrupt codes blocked · Console: c_hsshare / c_hslook")
        if UI.look_card then
            UI.look_card.title:SetString(zh_now and "视觉闭环 · 从任意截图拟合 HS1- 分享码" or "Visual Closed-Loop · Match Image -> HS1-")
            UI.look_card.sub:SetString(zh_now
                and "Win+Shift+S/复制图片即可 · 仅映射自有可控管线（参数状态复现）"
                or "Copy any screenshot/image · Owned pipeline knobs only (param state)")
            UI.look_card.cb_btn:SetText(zh_now and "吸取剪贴板截图" or "Match Clipboard")
            UI.look_card.fr_btn:SetText(zh_now and "同帧捕获滤镜" or "Capture Frame")
        end
        if sc_name_is_ed and sc_name_ed and sc_name_ed.SetTextPrompt then
            pcall(sc_name_ed.SetTextPrompt, sc_name_ed, zh_now and "方案标题（可选，如：暗黑童话）" or "Preset title (optional)", { 0.52, 0.56, 0.64, 0.85 })
        end
        if sc_imp_is_ed and sc_imp_ed and sc_imp_ed.SetTextPrompt then
            pcall(sc_imp_ed.SetTextPrompt, sc_imp_ed, zh_now and "粘贴 HS1- 分享码（留空则读剪贴板）" or "Paste HS1-... share code (blank = read clipboard)", { 0.52, 0.56, 0.64, 0.85 })
        end
        if share_card_visible and type(api.PreviewShareCode) == "function" then
            local preview = api.PreviewShareCode()
            if type(preview) == "string" then
                sc_code_txt:SetString(preview)
            end
        end
    end

    local function ToggleShareCard()
        share_card_visible = not share_card_visible
        if share_card_visible then
            if UI.preset_card and UI.preset_card.visible then
                UI.preset_card.visible = false
                if UI.preset_card.root then UI.preset_card.root:Hide() end
            end
            if panel_refresh_fn then panel_refresh_fn() end
            SetShareStatus("info", IsChineseLocale()
                and "已就绪：点击“复制分享码”分享当前画面，或粘贴他人分享码先校验后导入"
                or "Ready: click Copy Code to share, or paste a code to validate & import")
            share_card:Show()
            share_card:MoveToFront()
        else
            share_card:Hide()
        end
    end
    UI.ToggleShareCard = ToggleShareCard
    share_open_btn:SetOnClick(ToggleShareCard)

    -- =========================================================================
    -- Floating Curated Preset Gallery Card (6 Original HyperShade Looks)
    --   - Docked left of main panel (mutually exclusive with Share Card)
    --   - Built inside UI.preset_card to consume 0 extra locals!
    -- =========================================================================
    do
        local pc = UI.preset_card
        local PW, PH = 356, 516
        pc.root = panel:AddChild(Widget("HS_PresetGalleryCard"))
        pc.root:SetPosition(-(W * 0.5 + PW * 0.5 + 10), -10, 0)
        pc.root:Hide()

        NewImage(pc.root, 0, 0, PW + 4, PH + 4, 0.76, 0.62, 0.34, 0.72)
        NewImage(pc.root, 0, 0, PW, PH, 0.062, 0.072, 0.098, 0.97)
        NewImage(pc.root, 0, PH * 0.5 - 22, PW, 44, 0.088, 0.104, 0.145, 0.99)
        NewImage(pc.root, 0, PH * 0.5 - 44, PW, 2, 0.95, 0.74, 0.36, 0.75)

        pc.title = NewText(pc.root, 18, is_zh and "HyperShade · 预设面板" or "HyperShade · Preset Gallery",
            -16, PH * 0.5 - 17, PW - 56, 22, G.ANCHOR_LEFT, 0.98, 0.84, 0.48, 1.0)
        pc.sub = NewText(pc.root, 11, is_zh and "6 套官方实调预设 · 点选即生效，可随时滑块微调或撤销" or "6 curated HyperShade looks · 1-click apply, editable & undoable",
            -6, PH * 0.5 - 35, PW - 36, 16, G.ANCHOR_LEFT, 0.68, 0.76, 0.86, 0.94)

        pc.close_btn = NewButton(pc.root, PW * 0.5 - 20, PH * 0.5 - 18, 28, 22, "X", 16, function()
            pc.visible = false
            pc.root:Hide()
            DoRefresh()
        end, FONT_NUM)
        pc.close_btn:SetTextColour(0.95, 0.55, 0.55, 1.0)
        pc.close_btn:SetTextFocusColour(1.0, 0.78, 0.78, 1.0)

        pc.status_bg = NewImage(pc.root, 0, -234, PW - 24, 34, 0.09, 0.13, 0.19, 0.90)
        pc.status_txt = NewText(pc.root, 12, "", 0, -234, PW - 34, 30, nil, 0.82, 0.90, 0.98, 0.96)
        pc.SetStatus = function(kind, msg)
            if kind == "ok" then
                pc.status_bg:SetTint(0.09, 0.24, 0.15, 0.94)
                pc.status_txt:SetColour(0.54, 1.0, 0.72, 1.0)
            elseif kind == "err" then
                pc.status_bg:SetTint(0.30, 0.10, 0.10, 0.94)
                pc.status_txt:SetColour(1.0, 0.68, 0.68, 1.0)
            else
                pc.status_bg:SetTint(0.09, 0.13, 0.19, 0.90)
                pc.status_txt:SetColour(0.82, 0.90, 0.98, 0.96)
            end
            pc.status_txt:SetString(msg or "")
        end

        local list = (type(api.GetBuiltinPresets) == "function") and api.GetBuiltinPresets() or {}
        pc.cards = {}
        local card_w, card_h, stride = PW - 24, 56, 62
        local start_y = 172
        for idx = 1, math.min(#list, 6) do
            local item = list[idx]
            local cy = start_y - (idx - 1) * stride
            local acc = item.accent or { 0.95, 0.74, 0.36 }

            local cborder = NewImage(pc.root, 0, cy, card_w + 4, card_h + 4, 0.95, 0.74, 0.36, 0.0)
            local cbg = NewImage(pc.root, 0, cy, card_w, card_h, 0.095, 0.108, 0.145, 0.95)
            local cbar = NewImage(pc.root, -card_w * 0.5 + 2, cy, 4, card_h, acc[1], acc[2], acc[3], 0.95)
            local cname = NewText(pc.root, 15, is_zh and item.zh or item.en, -54, cy + 13, 200, 20, G.ANCHOR_LEFT, 0.94, 0.96, 0.98, 1.0)
            local ctag_bg = NewImage(pc.root, 104, cy + 13, 114, 18, 0.13, 0.17, 0.24, 0.92)
            local ctag = NewText(pc.root, 11, is_zh and item.tag_zh or item.tag_en, 104, cy + 13, 110, 16, nil, acc[1], acc[2], acc[3], 1.0)
            local cdesc = NewText(pc.root, 11, WrapText(is_zh and item.desc_zh or item.desc_en, 28.0, 2),
                4, cy - 11, card_w - 18, 26, G.ANCHOR_LEFT, 0.68, 0.76, 0.85, 0.95)
            local cbtn = NewButton(pc.root, 0, cy, card_w, card_h, nil, nil, function()
                if type(api.ApplyBuiltinPreset) == "function" then
                    local ok_p, err_p, applied = api.ApplyBuiltinPreset(item.id)
                    local zh_now = IsChineseLocale()
                    if ok_p and applied then
                        pc.SetStatus("ok", zh_now
                            and string.format("已切换「%s」· 可在右侧自由微调或点下方撤销", applied.zh)
                            or string.format("Applied '%s' · tweak on right or click Undo", applied.en))
                    else
                        pc.SetStatus("err", tostring(err_p or (zh_now and "切换失败" or "Apply failed")))
                    end
                    DoRefresh()
                end
            end)

            pc.cards[#pc.cards + 1] = {
                item = item, border = cborder, bg = cbg, bar = cbar,
                name = cname, tag_bg = ctag_bg, tag = ctag, desc = cdesc, btn = cbtn,
            }
        end

        NewImage(pc.root, 0, -172, PW - 24, 1, 0.76, 0.62, 0.34, 0.35)

        pc.undo_bg = NewImage(pc.root, -112, -195, 104, 26, 0.22, 0.15, 0.18, 0.92)
        pc.undo_btn = NewButton(pc.root, -112, -195, 104, 26, is_zh and "撤销切换" or "Undo", 14, function()
            local zh_now = IsChineseLocale()
            if type(api.UndoLastImport) == "function" and api.UndoLastImport() then
                pc.SetStatus("info", zh_now and "已撤销切换，恢复至上一步配置" or "Reverted to previous configuration")
                DoRefresh()
            else
                pc.SetStatus("info", zh_now and "当前没有可撤销的切换记录" or "Nothing to undo yet")
            end
        end)
        pc.undo_btn:SetTextColour(0.95, 0.78, 0.82, 1.0)
        pc.undo_btn:SetTextFocusColour(1.0, 0.92, 0.94, 1.0)

        pc.share_bg = NewImage(pc.root, 0, -195, 112, 26, 0.14, 0.24, 0.30, 0.94)
        pc.share_btn = NewButton(pc.root, 0, -195, 112, 26, is_zh and "分享 / 吸色码" or "Share / Match", 13, function()
            ToggleShareCard()
        end)
        pc.share_btn:SetTextColour(0.72, 0.94, 1.0, 1.0)
        pc.share_btn:SetTextFocusColour(0.92, 0.98, 1.0, 1.0)

        pc.van_bg = NewImage(pc.root, 112, -195, 104, 26, 0.28, 0.21, 0.11, 0.94)
        pc.van_btn = NewButton(pc.root, 112, -195, 104, 26, is_zh and "原版纯净" or "Vanilla Pure", 14, function()
            if type(api.ApplyBuiltinPreset) == "function" then
                api.ApplyBuiltinPreset("vanilla")
                local zh_now = IsChineseLocale()
                pc.SetStatus("ok", zh_now
                    and "已恢复 100% 原版纯净画质（原生泛光 · 不影响 240 帧）"
                    or "Restored 100% Vanilla Pure visuals (stock bloom · keeps 240Hz)")
                DoRefresh()
            end
        end)
        pc.van_btn:SetTextColour(1.0, 0.88, 0.58, 1.0)
        pc.van_btn:SetTextFocusColour(1.0, 0.96, 0.78, 1.0)

        pc.Refresh = function(snap, zh_now)
            if pc.top_btn then
                pc.top_btn:SetText(zh_now and (pc.visible and "预设面板 ●" or "预设面板") or (pc.visible and "Presets ●" or "Presets"))
                if pc.visible then
                    pc.top_bg:SetTint(0.18, 0.38, 0.44, 0.98)
                    pc.top_btn:SetTextColour(0.98, 0.96, 0.74, 1.0)
                else
                    pc.top_bg:SetTint(0.14, 0.20, 0.26, 0.94)
                    pc.top_btn:SetTextColour(0.76, 0.94, 1.0, 1.0)
                end
            end
            if pc.home_open_btn then
                pc.home_open_btn:SetText(zh_now and "6套预设 >>" or "6 Presets >>")
            end
            pc.title:SetString(zh_now and "HyperShade · 预设面板" or "HyperShade · Preset Gallery")
            pc.sub:SetString(zh_now
                and "6 套官方实调预设 · 点选即生效，可随时滑块微调或撤销"
                or "6 curated HyperShade looks · 1-click apply, editable & undoable")
            pc.undo_btn:SetText(zh_now and "撤销切换" or "Undo")
            pc.share_btn:SetText(zh_now and "分享 / 吸色码" or "Share / Match")
            pc.van_btn:SetText(zh_now and "原版纯净" or "Vanilla Pure")
            if snap.has_undo then
                pc.undo_bg:SetTint(0.28, 0.16, 0.20, 0.95)
                pc.undo_btn:SetTextColour(1.0, 0.82, 0.86, 1.0)
            else
                pc.undo_bg:SetTint(0.14, 0.13, 0.16, 0.78)
                pc.undo_btn:SetTextColour(0.56, 0.58, 0.64, 0.85)
            end

            local active_id = snap.active_builtin_id
            for i = 1, #pc.cards do
                local c = pc.cards[i]
                local it = c.item
                local is_act = (active_id ~= nil and active_id == it.id)
                local acc = it.accent or { 0.95, 0.74, 0.36 }
                c.name:SetString(zh_now and it.zh or it.en)
                c.tag:SetString(zh_now and it.tag_zh or it.tag_en)
                c.desc:SetString(WrapText(zh_now and it.desc_zh or it.desc_en, 28.0, 2))
                if is_act then
                    c.border:SetTint(0.95, 0.74, 0.36, 0.96)
                    c.bg:SetTint(0.20, 0.16, 0.09, 0.98)
                    c.name:SetColour(1.0, 0.92, 0.62, 1.0)
                    c.tag_bg:SetTint(0.32, 0.25, 0.12, 0.96)
                    c.tag:SetColour(1.0, 0.94, 0.72, 1.0)
                    c.desc:SetColour(0.88, 0.84, 0.72, 1.0)
                else
                    c.border:SetTint(0.95, 0.74, 0.36, 0.0)
                    c.bg:SetTint(0.095, 0.108, 0.145, 0.95)
                    c.name:SetColour(0.90, 0.93, 0.97, 1.0)
                    c.tag_bg:SetTint(0.13, 0.17, 0.24, 0.92)
                    c.tag:SetColour(acc[1], acc[2], acc[3], 0.98)
                    c.desc:SetColour(0.66, 0.74, 0.84, 0.95)
                end
            end
        end

        UI.TogglePresetCard = function(force_vis)
            if force_vis == nil then
                pc.visible = not pc.visible
            else
                pc.visible = (force_vis == true)
            end
            if pc.visible then
                if share_card_visible then
                    share_card_visible = false
                    share_card:Hide()
                end
                pc.SetStatus("info", IsChineseLocale()
                    and "点选任意预设立即生效 · 控制台 c_hspreset() 也可快速切换"
                    or "Click any preset to apply live · console: c_hspreset()")
                pc.root:Show()
                pc.root:MoveToFront()
            else
                pc.root:Hide()
            end
            DoRefresh()
        end
    end

    -- ========================================================================
    -- PRO PAGES (7 categories x accordion sections). Rows reference the option catalogue above.
    -- ========================================================================
    UI.texts = {}
    local function RegText(btn, zh, en)
        UI.texts[#UI.texts + 1] = { btn = btn, zh = zh, en = en }
    end

    local mood_defs = {
        { id = "neutral", zh = "中性原色", en = "Neutral" },
        { id = "warm",    zh = "暖阳特调", en = "Warm"    },
        { id = "cool",    zh = "清冷护眼", en = "Cool"    },
        { id = "film",    zh = "胶片电影", en = "Filmic"  },
    }
    local mood_btns = {}
    local function BuildMoodRow(rc)
        for i = 1, #mood_defs do
            local md = mood_defs[i]
            local mx = -144 + (i - 1) * 96
            NewImage(rc, mx, 0, 90, 26, 0.18, 0.16, 0.22, 0.92)
            local mbtn = NewButton(rc, mx, 0, 90, 26, is_zh and md.zh or md.en, 14, function()
                if type(api.ApplyColorMood) == "function" then
                    api.ApplyColorMood(md.id)
                    DoRefresh()
                end
            end)
            mbtn:SetTextColour(0.95, 0.92, 0.82, 1.0)
            mbtn:SetTextFocusColour(1.0, 0.98, 0.72, 1.0)
            mood_btns[i] = { id = md.id, zh = md.zh, en = md.en, btn = mbtn }
        end
    end

    local function BuildHitTestRow(rc)
        NewImage(rc, 0, 0, W - 22, 26, 0.28, 0.17, 0.12, 0.94)
        local b = NewButton(rc, 0, 0, W - 22, 26,
            is_zh and "原地演示打击效果（点一次换一种）" or "Test Hit-Feel (click to cycle styles)", 14, function()
            if type(api.PreviewHitFeel) == "function" then
                api.PreviewHitFeel()
                DoRefresh()
            end
        end)
        b:SetTextColour(1.0, 0.90, 0.62, 1.0)
        b:SetTextFocusColour(1.0, 0.98, 0.80, 1.0)
        RegText(b, "原地演示打击效果（点一次换一种）", "Test Hit-Feel (click to cycle styles)")
    end

    local function BuildSlotsRow(rc)
        MakeSlotRow(rc, 0)
    end

    local function BuildShareRow(rc)
        NewImage(rc, 0, 0, W - 22, 26, 0.24, 0.20, 0.11, 0.94)
        local b = NewButton(rc, 0, 0, W - 22, 26,
            is_zh and "分享 / 导入方案码 (HS1 / JSON)" or "Share / Import Preset Code (HS1 / JSON)", 14, function()
            if UI.ToggleShareCard then UI.ToggleShareCard() end
        end)
        b:SetTextColour(1.0, 0.90, 0.62, 1.0)
        b:SetTextFocusColour(1.0, 0.98, 0.80, 1.0)
        RegText(b, "分享 / 导入方案码 (HS1 / JSON)", "Share / Import Preset Code (HS1 / JSON)")
    end

    local function BuildRestoreRow(rc)
        NewImage(rc, 0, 0, W - 22, 26, 0.30, 0.22, 0.12, 0.94)
        local b = NewButton(rc, 0, 0, W - 22, 26,
            is_zh and "一键恢复原版纯净（不影响 240 帧与低延迟）" or "Restore Vanilla Look (keeps 240Hz & low-latency)", 14, function()
            api.SetPreset(0)
            DoRefresh()
        end)
        b:SetTextColour(1.0, 0.88, 0.58, 1.0)
        b:SetTextFocusColour(1.0, 0.96, 0.78, 1.0)
        RegText(b, "一键恢复原版纯净（不影响 240 帧与低延迟）", "Restore Vanilla Look (keeps 240Hz & low-latency)")
    end

    local function BuildDiagRow(rc)
        NewImage(rc, 0, 0, W - 22, 48, 0.040, 0.050, 0.075, 0.92)
        UI.diag1 = NewText(rc, 12, "", 0, 15, W - 34, 16, G.ANCHOR_LEFT, 0.62, 0.90, 0.74, 0.96)
        UI.diag2 = NewText(rc, 12, "", 0, 0, W - 34, 16, G.ANCHOR_LEFT, 0.70, 0.80, 0.92, 0.92)
        UI.diag3 = NewText(rc, 12, "", 0, -15, W - 34, 16, G.ANCHOR_LEFT, 0.62, 0.68, 0.78, 0.90)
    end

    -- 清晰 (Clarity)
    BuildProPage(2, {
        { zh = "常用：锐化与贴图", en = "Essentials: Sharpness & Textures", rows = {
            { id = "flag_cas" }, { id = "sharp" }, { id = "ink" }, { id = "flag_af16" },
            { id = "flag_mip_sharp" }, { id = "mip_bias" }, { id = "flag_full_lm" } } },
        { zh = "锐化高级参数（一般不用动）", en = "Advanced Sharpening (usually leave)", rows = {
            { id = "deconv_gate" }, { id = "deconv_penetr" }, { id = "anti_ring" }, { id = "dark_prot" },
            { id = "chroma_prot" }, { id = "noise_reduce" }, { id = "range_sigma" }, { id = "spatial_sigma" },
            { id = "noise_floor" }, { id = "ar_l_over" }, { id = "ar_d_over" }, { id = "dither" } } },
        { zh = "高清细节补强", en = "High-DPI Detail", rows = { { id = "sprite_detail" }, { id = "ground_detail" } } },
    }, "重置本页「清晰」为默认", "Reset Clarity Defaults")

    -- 色彩 (Color)
    BuildProPage(3, {
        { zh = "常用：一键风格与基础色调", en = "Essentials: Looks & Basic Tone", rows = {
            { custom = true, h = 26, build = BuildMoodRow },
            { id = "flag_tone" }, { id = "exposure" }, { id = "temp" }, { id = "contrast" },
            { id = "sat" }, { id = "vibrance" } } },
        { zh = "高级调色", en = "Advanced Grading", rows = {
            { id = "tint" }, { id = "lightness" }, { id = "gamma" }, { id = "filmic" },
            { id = "hl_desat" }, { id = "sun_fill" }, { id = "original_mix" } } },
        { zh = "专业 CDL 三段调色 + 胶片", en = "Pro CDL Grading + Film", rows = {
            { id = "slope_r" }, { id = "slope_g" }, { id = "slope_b" },
            { id = "offset_r" }, { id = "offset_g" }, { id = "offset_b" },
            { id = "power_r" }, { id = "power_g" }, { id = "power_b" },
            { id = "vignette" }, { id = "grain" } } },
        { zh = "原版滤镜接管（随时可逆）", en = "Vanilla Filters (always reversible)", rows = {
            { id = "vanilla_cc" }, { id = "snow_cap" }, { id = "sanity_color" },
            { id = "distort_free" }, { id = "sand_filter" }, { id = "flag_clean_filter" } } },
    }, "重置本页「色彩」为默认", "Reset Color Defaults")

    -- 光影 (Light & Shadow)
    BuildProPage(4, {
        { zh = "柔光辉光与体积光雾（常用）", en = "Soft Glow, Light Rays & Mist", rows = {
            { id = "flag_soft_bloom" }, { id = "bloom_str" }, { id = "bloom_warmth" },
            { id = "bloom_spread" }, { id = "god_rays" }, { id = "vol_lightrays" },
            { id = "vol_lightrays_everywhere" }, { id = "vol_ground_fog" },
            { id = "vfx_fire_embers" }, { id = "vfx_ambient_motes" } } },
        { zh = "柔光辉光（高级）", en = "Soft Glow (Advanced)", rows = {
            { id = "bloom_thresh" }, { id = "bloom_knee" }, { id = "bloom_chr" }, { id = "bloom_prot" },
            { id = "bloom_compress" }, { id = "glow_tail" }, { id = "light_wrap" }, { id = "glow_rim" } } },
        { zh = "立体受光与接地阴影", en = "3D Light & Contact Shadow", rows = {
            { id = "flag_ao" }, { id = "ao_str" }, { id = "surface_light" }, { id = "shade_warm" },
            { id = "shade_contrast" }, { id = "shade_cool" }, { id = "shade_dark" },
            { id = "flag_dark_clarity" }, { id = "dark_str" } } },
        { zh = "动态长影与自然投影（常用）", en = "Long, Cloud & Canopy Shadows", rows = {
            { id = "shadow_density" }, { id = "vol_cloud_shade" }, { id = "vol_canopy_shade" },
            { id = "shadow_point_lights" }, { id = "shadow_night" },
            { id = "shadow_length" }, { id = "shadow_soft" }, { id = "shadow_hd" } } },
        { zh = "动态长影（高级）", en = "Long Shadows (Advanced)", rows = {
            { id = "shadow_point_strength" }, { id = "shadow_dusk_point" }, { id = "shadow_flash_lights" },
            { id = "shadow_weather" }, { id = "shadow_moon_blue" }, { id = "shadow_min_height" },
            { id = "shadow_entity_recv" }, { id = "shadow_oval_keep" },
            { id = "shadow_fade" }, { id = "shadow_sway" }, { id = "shadow_smooth" },
            { id = "shadow_tint_r" }, { id = "shadow_tint_g" }, { id = "shadow_tint_b" },
            { id = "shadow_skip_nightmare" } } },
    }, "重置本页「光影」为默认", "Reset Light Defaults")

    -- 水景 (Water & Nature)
    BuildProPage(5, {
        { zh = "PBR 镜面海洋与海界迷雾", en = "PBR Mirror Ocean & Edge Mist", rows = {
            { id = "ocean_pbr" }, { id = "ocean_sky_refl" }, { id = "ocean_sun_glow" },
            { id = "ocean_hide_waves" }, { id = "vol_edge_fog" } } },
        { zh = "海面焦散与近岸倒影", en = "Sea Caustics & Coastal Refl.", rows = {
            { id = "glint_str" }, { id = "glint_density" }, { id = "glint_grain" },
            { id = "glint_soft" }, { id = "water_refl_str" }, { id = "water_refl_ripple" } } },
        { zh = "植被风动", en = "Foliage Sway", rows = {
            { id = "wind_sway_enabled" }, { id = "wind_sway_strength" }, { id = "wind_wake_strength" } } },
        { zh = "近岸与雨天湿润", en = "Shore & Rain Wetness", rows = { { id = "flag_env" }, { id = "env_str" } } },
    }, "重置本页「水景」为默认", "Reset Water Defaults")

    -- 动作 (Motion & Combat)
    BuildProPage(6, {
        { zh = "动作流畅与地皮微尘", en = "Smooth Motion & Footsteps", rows = {
            { id = "eng_skeletal" }, { id = "anim_crossfade" }, { id = "anim_spline" },
            { id = "vfx_footsteps" } } },
        { zh = "无震动打击感表演", en = "Zero-Shake Combat Hit-Feel", rows = {
            { id = "hitfeel_enabled" }, { id = "hitfeel_hitstop" }, { id = "hitfeel_recoil" },
            { id = "hitfeel_flash" }, { id = "hitfeel_campunch" }, { id = "hitfeel_vfx" },
            { custom = true, h = 26, build = BuildHitTestRow } } },
    }, "重置本页「动作」为默认", "Reset Motion Defaults")

    -- 系统 (System)
    BuildProPage(7, {
        { zh = "输入与显示", en = "Input & Display", rows = {
            { id = "eng_lowlat" }, { id = "eng_clipcursor" }, { id = "eng_hudmode" } } },
        { zh = "我的方案与分享", en = "My Looks & Sharing", rows = {
            { custom = true, h = 28, build = BuildSlotsRow },
            { custom = true, h = 26, build = BuildShareRow },
            { custom = true, h = 26, build = BuildRestoreRow } } },
        { zh = "运行诊断（给开发者/反馈用）", en = "Diagnostics (for bug reports)", rows = {
            { custom = true, h = 48, build = BuildDiagRow } } },
    }, nil, nil)


    -- =========================================================================
    -- TAB 8: Extensions — HS_EXT_V1 Standard Shader Plugin Manager & GPU Profiler
    --        Zero-boilerplate auto-UI generated from third-party plugin metadata!
    -- =========================================================================
    local tab8 = tab_roots[8]
    local ext_selected_idx = 1
    local ext_param_page = 1
    local EXT_ROWS_PER_PAGE = 8

    -- Top summary + Master Bypass toggle
    local ext_sum_bg = tab8:AddChild(Image("images/global.xml", "square.tex"))
    ext_sum_bg:ScaleToSize(262, 24)
    ext_sum_bg:SetPosition(-58, 152, 0)
    ext_sum_bg:SetTint(0.10, 0.16, 0.24, 0.90)

    local ext_sum_txt = tab8:AddChild(Text(FONT_BODY, 13, ""))
    ext_sum_txt:SetRegionSize(254, 20)
    ext_sum_txt:SetHAlign(G.ANCHOR_LEFT)
    ext_sum_txt:SetPosition(-58, 152, 0)
    ext_sum_txt:SetColour(0.76, 0.92, 1.0, 0.98)

    local ext_byp_bg = tab8:AddChild(Image("images/global.xml", "square.tex"))
    ext_byp_bg:ScaleToSize(110, 24)
    ext_byp_bg:SetPosition(134, 152, 0)
    ext_byp_bg:SetTint(0.14, 0.30, 0.20, 0.94)

    local ext_byp_btn = tab8:AddChild(ImageButton("images/global_redux.xml", "blank.tex", "spinner_focus.tex"))
    ext_byp_btn.scale_on_focus = false
    ext_byp_btn.move_on_click = false
    ext_byp_btn:ForceImageSize(110, 24)
    ext_byp_btn:SetPosition(134, 152, 0)
    ext_byp_btn:SetText(is_zh and "扩展:运行中" or "Ext: Active")
    ext_byp_btn:SetTextSize(13)
    ext_byp_btn:SetFont(FONT_BODY)
    ext_byp_btn:SetTextColour(0.68, 1.0, 0.80, 1.0)
    ext_byp_btn:SetOnClick(function()
        if type(api.GetExtSnapshot) ~= "function" or type(api.SetExtMasterBypass) ~= "function" then return end
        local cur = api.GetExtSnapshot()
        api.SetExtMasterBypass(not (cur.master_bypass == true))
        if panel_refresh_fn then panel_refresh_fn() end
    end)

    -- Core baseline GPU comparison line
    local ext_core_bg = tab8:AddChild(Image("images/global.xml", "square.tex"))
    ext_core_bg:ScaleToSize(W - 22, 18)
    ext_core_bg:SetPosition(0, 128, 0)
    ext_core_bg:SetTint(0.08, 0.10, 0.14, 0.84)

    local ext_core_txt = tab8:AddChild(Text(FONT_NUM, 12, ""))
    ext_core_txt:SetRegionSize(W - 30, 16)
    ext_core_txt:SetPosition(0, 128, 0)
    ext_core_txt:SetColour(0.68, 0.78, 0.88, 0.94)

    -- Empty-state friendly card (shown when 0 third-party plugins registered)
    local ext_empty_root = tab8:AddChild(Widget("HS_ExtEmpty"))
    local ext_empty_bg = ext_empty_root:AddChild(Image("images/global.xml", "square.tex"))
    ext_empty_bg:ScaleToSize(W - 22, 150)
    ext_empty_bg:SetPosition(0, 20, 0)
    ext_empty_bg:SetTint(0.10, 0.12, 0.17, 0.86)

    local ext_empty_txt = ext_empty_root:AddChild(Text(FONT_BODY, 14, ""))
    ext_empty_txt:SetRegionSize(W - 42, 132)
    ext_empty_txt:SetPosition(0, 20, 0)
    ext_empty_txt:SetColour(0.80, 0.86, 0.94, 0.94)

    -- Active Plugin Browser & Controls container
    local ext_body_root = tab8:AddChild(Widget("HS_ExtBody"))

    -- Selector strip: [<]  [1/N] Name v1.0  [>]
    local ext_prev_bg = ext_body_root:AddChild(Image("images/global.xml", "square.tex"))
    ext_prev_bg:ScaleToSize(30, 24)
    ext_prev_bg:SetPosition(-174, 100, 0)
    ext_prev_bg:SetTint(0.16, 0.18, 0.24, 0.94)

    local ext_prev_btn = ext_body_root:AddChild(ImageButton("images/global_redux.xml", "blank.tex", "spinner_focus.tex"))
    ext_prev_btn.scale_on_focus = false
    ext_prev_btn.move_on_click = false
    ext_prev_btn:ForceImageSize(30, 24)
    ext_prev_btn:SetPosition(-174, 100, 0)
    ext_prev_btn:SetText("<")
    ext_prev_btn:SetTextSize(16)
    ext_prev_btn:SetFont(FONT_NUM)
    ext_prev_btn:SetTextColour(0.92, 0.94, 1.0, 1.0)
    ext_prev_btn:SetOnClick(function()
        if type(api.GetExtSnapshot) ~= "function" then return end
        local n = #(api.GetExtSnapshot().plugins or {})
        if n > 0 then
            ext_selected_idx = ((ext_selected_idx - 2) % n) + 1
            ext_param_page = 1
            if panel_refresh_fn then panel_refresh_fn() end
        end
    end)

    local ext_sel_bg = ext_body_root:AddChild(Image("images/global.xml", "square.tex"))
    ext_sel_bg:ScaleToSize(310, 24)
    ext_sel_bg:SetPosition(0, 100, 0)
    ext_sel_bg:SetTint(0.14, 0.16, 0.22, 0.92)

    local ext_sel_txt = ext_body_root:AddChild(Text(FONT_BODY, 14, ""))
    ext_sel_txt:SetRegionSize(302, 20)
    ext_sel_txt:SetPosition(0, 100, 0)
    ext_sel_txt:SetColour(1.0, 0.92, 0.66, 1.0)

    local ext_next_bg = ext_body_root:AddChild(Image("images/global.xml", "square.tex"))
    ext_next_bg:ScaleToSize(30, 24)
    ext_next_bg:SetPosition(174, 100, 0)
    ext_next_bg:SetTint(0.16, 0.18, 0.24, 0.94)

    local ext_next_btn = ext_body_root:AddChild(ImageButton("images/global_redux.xml", "blank.tex", "spinner_focus.tex"))
    ext_next_btn.scale_on_focus = false
    ext_next_btn.move_on_click = false
    ext_next_btn:ForceImageSize(30, 24)
    ext_next_btn:SetPosition(174, 100, 0)
    ext_next_btn:SetText(">")
    ext_next_btn:SetTextSize(16)
    ext_next_btn:SetFont(FONT_NUM)
    ext_next_btn:SetTextColour(0.92, 0.94, 1.0, 1.0)
    ext_next_btn:SetOnClick(function()
        if type(api.GetExtSnapshot) ~= "function" then return end
        local n = #(api.GetExtSnapshot().plugins or {})
        if n > 0 then
            ext_selected_idx = (ext_selected_idx % n) + 1
            ext_param_page = 1
            if panel_refresh_fn then panel_refresh_fn() end
        end
    end)

    -- Plugin Action Row: [Enable/Disable]  [Stage + Live GPU ms]  [Reset]
    local ext_en_bg = ext_body_root:AddChild(Image("images/global.xml", "square.tex"))
    ext_en_bg:ScaleToSize(92, 24)
    ext_en_bg:SetPosition(-143, 70, 0)
    ext_en_bg:SetTint(0.14, 0.28, 0.20, 0.94)

    local ext_en_btn = ext_body_root:AddChild(ImageButton("images/global_redux.xml", "blank.tex", "spinner_focus.tex"))
    ext_en_btn.scale_on_focus = false
    ext_en_btn.move_on_click = false
    ext_en_btn:ForceImageSize(92, 24)
    ext_en_btn:SetPosition(-143, 70, 0)
    ext_en_btn:SetTextSize(13)
    ext_en_btn:SetFont(FONT_BODY)
    ext_en_btn:SetOnClick(function()
        if type(api.GetExtSnapshot) ~= "function" or type(api.SetExtPluginEnabled) ~= "function" then return end
        local list = api.GetExtSnapshot().plugins or {}
        local p = list[ext_selected_idx]
        if p and not p.faulted then
            api.SetExtPluginEnabled(p.id, not p.enabled)
            if panel_refresh_fn then panel_refresh_fn() end
        end
    end)

    local ext_gpu_bg = ext_body_root:AddChild(Image("images/global.xml", "square.tex"))
    ext_gpu_bg:ScaleToSize(192, 24)
    ext_gpu_bg:SetPosition(3, 70, 0)
    ext_gpu_bg:SetTint(0.08, 0.13, 0.19, 0.90)

    local ext_gpu_txt = ext_body_root:AddChild(Text(FONT_NUM, 12, ""))
    ext_gpu_txt:SetRegionSize(186, 20)
    ext_gpu_txt:SetPosition(3, 70, 0)
    ext_gpu_txt:SetColour(0.60, 0.96, 0.84, 0.98)

    local ext_rst_bg = ext_body_root:AddChild(Image("images/global.xml", "square.tex"))
    ext_rst_bg:ScaleToSize(86, 24)
    ext_rst_bg:SetPosition(146, 70, 0)
    ext_rst_bg:SetTint(0.24, 0.18, 0.12, 0.92)

    local ext_rst_btn = ext_body_root:AddChild(ImageButton("images/global_redux.xml", "blank.tex", "spinner_focus.tex"))
    ext_rst_btn.scale_on_focus = false
    ext_rst_btn.move_on_click = false
    ext_rst_btn:ForceImageSize(86, 24)
    ext_rst_btn:SetPosition(146, 70, 0)
    ext_rst_btn:SetText(is_zh and "重置参数" or "Reset")
    ext_rst_btn:SetTextSize(13)
    ext_rst_btn:SetFont(FONT_BODY)
    ext_rst_btn:SetTextColour(1.0, 0.88, 0.62, 1.0)
    ext_rst_btn:SetOnClick(function()
        if type(api.GetExtSnapshot) ~= "function" or type(api.ResetExtPlugin) ~= "function" then return end
        local list = api.GetExtSnapshot().plugins or {}
        local p = list[ext_selected_idx]
        if p then
            api.ResetExtPlugin(p.id)
            if panel_refresh_fn then panel_refresh_fn() end
        end
    end)

    -- Meta / Author / Fault line
    local ext_meta_bg = ext_body_root:AddChild(Image("images/global.xml", "square.tex"))
    ext_meta_bg:ScaleToSize(W - 22, 24)
    ext_meta_bg:SetPosition(0, 40, 0)
    ext_meta_bg:SetTint(0.09, 0.11, 0.15, 0.84)

    local ext_meta_txt = ext_body_root:AddChild(Text(FONT_BODY, 12, ""))
    ext_meta_txt:SetRegionSize(W - 30, 22)
    ext_meta_txt:SetPosition(0, 40, 0)
    ext_meta_txt:SetColour(0.76, 0.82, 0.90, 0.94)

    -- 8-row reusable dynamic parameter pool (float steppers + bool toggles)
    local ext_param_rows = {}
    for r = 1, EXT_ROWS_PER_PAGE do
        local row_idx = r
        local ry = 10 - (r - 1) * 32
        local rroot = ext_body_root:AddChild(Widget("HS_ExtParamRow_" .. r))

        local rbg = rroot:AddChild(Image("images/global.xml", "square.tex"))
        rbg:ScaleToSize(W - 22, 28)
        rbg:SetPosition(0, ry, 0)
        rbg:SetTint(0.11, 0.12, 0.16, 0.80)

        local rlbl = rroot:AddChild(Text(FONT_BODY, 14, ""))
        rlbl:SetRegionSize(214, 24)
        rlbl:SetHAlign(G.ANCHOR_LEFT)
        rlbl:SetPosition(-76, ry, 0)
        rlbl:SetColour(0.90, 0.92, 0.96, 1.0)

        local mbg = rroot:AddChild(Image("images/global.xml", "square.tex"))
        mbg:ScaleToSize(28, 22)
        mbg:SetPosition(66, ry, 0)
        mbg:SetTint(0.19, 0.20, 0.26, 0.94)

        local mbtn = rroot:AddChild(ImageButton("images/global_redux.xml", "blank.tex", "spinner_focus.tex"))
        mbtn.scale_on_focus = false
        mbtn.move_on_click = false
        mbtn:ForceImageSize(28, 22)
        mbtn:SetPosition(66, ry, 0)
        mbtn:SetText("-")
        mbtn:SetTextSize(16)
        mbtn:SetFont(FONT_NUM)
        mbtn:SetTextColour(0.92, 0.92, 0.95, 1.0)

        local vbtn = rroot:AddChild(ImageButton("images/global_redux.xml", "blank.tex", "spinner_focus.tex"))
        vbtn.scale_on_focus = false
        vbtn.move_on_click = false
        vbtn:ForceImageSize(68, 22)
        vbtn:SetPosition(116, ry, 0)
        vbtn:SetTextSize(13)
        vbtn:SetFont(FONT_NUM)
        vbtn:SetTextColour(1.0, 0.90, 0.60, 1.0)

        local pbg = rroot:AddChild(Image("images/global.xml", "square.tex"))
        pbg:ScaleToSize(28, 22)
        pbg:SetPosition(166, ry, 0)
        pbg:SetTint(0.19, 0.20, 0.26, 0.94)

        local pbtn = rroot:AddChild(ImageButton("images/global_redux.xml", "blank.tex", "spinner_focus.tex"))
        pbtn.scale_on_focus = false
        pbtn.move_on_click = false
        pbtn:ForceImageSize(28, 22)
        pbtn:SetPosition(166, ry, 0)
        pbtn:SetText("+")
        pbtn:SetTextSize(16)
        pbtn:SetFont(FONT_NUM)
        pbtn:SetTextColour(0.92, 0.92, 0.95, 1.0)

        local function GetBoundParam()
            if type(api.GetExtSnapshot) ~= "function" then return nil, nil end
            local list = api.GetExtSnapshot().plugins or {}
            local p = list[ext_selected_idx]
            if not p then return nil, nil end
            local idx = (ext_param_page - 1) * EXT_ROWS_PER_PAGE + row_idx
            return p, p.params[idx]
        end

        mbtn:SetOnClick(function()
            local p, pd = GetBoundParam()
            if not (p and pd and type(api.SetExtPluginParam) == "function") then return end
            local cur = p.values[pd.key] or pd.default
            api.SetExtPluginParam(p.id, pd.key, cur - pd.step)
            if panel_refresh_fn then panel_refresh_fn() end
        end)
        pbtn:SetOnClick(function()
            local p, pd = GetBoundParam()
            if not (p and pd and type(api.SetExtPluginParam) == "function") then return end
            local cur = p.values[pd.key] or pd.default
            api.SetExtPluginParam(p.id, pd.key, cur + pd.step)
            if panel_refresh_fn then panel_refresh_fn() end
        end)
        vbtn:SetOnClick(function()
            local p, pd = GetBoundParam()
            if not (p and pd and type(api.SetExtPluginParam) == "function") then return end
            if pd.type == "bool" then
                local cur = (p.values[pd.key] or 0) > 0.5
                api.SetExtPluginParam(p.id, pd.key, not cur)
            else
                api.SetExtPluginParam(p.id, pd.key, pd.default)
            end
            if panel_refresh_fn then panel_refresh_fn() end
        end)

        rroot:Hide()
        ext_param_rows[r] = { root = rroot, lbl = rlbl, mbg = mbg, mbtn = mbtn, vbtn = vbtn, pbg = pbg, pbtn = pbtn }
    end

    -- Pager strip (only visible if plugin has >8 params)
    local ext_pager_root = ext_body_root:AddChild(Widget("HS_ExtPager"))
    local ext_pg_prev = ext_pager_root:AddChild(ImageButton("images/global_redux.xml", "blank.tex", "spinner_focus.tex"))
    ext_pg_prev.scale_on_focus = false
    ext_pg_prev.move_on_click = false
    ext_pg_prev:ForceImageSize(72, 22)
    ext_pg_prev:SetPosition(-110, -254, 0)
    ext_pg_prev:SetText("<")
    ext_pg_prev:SetTextSize(13)
    ext_pg_prev:SetFont(FONT_BODY)
    ext_pg_prev:SetOnClick(function()
        if ext_param_page > 1 then
            ext_param_page = ext_param_page - 1
            if panel_refresh_fn then panel_refresh_fn() end
        end
    end)

    local ext_pg_txt = ext_pager_root:AddChild(Text(FONT_NUM, 12, "1 / 1"))
    ext_pg_txt:SetPosition(0, -254, 0)
    ext_pg_txt:SetColour(0.78, 0.84, 0.92, 0.94)

    local ext_pg_next = ext_pager_root:AddChild(ImageButton("images/global_redux.xml", "blank.tex", "spinner_focus.tex"))
    ext_pg_next.scale_on_focus = false
    ext_pg_next.move_on_click = false
    ext_pg_next:ForceImageSize(72, 22)
    ext_pg_next:SetPosition(110, -254, 0)
    ext_pg_next:SetText(">")
    ext_pg_next:SetTextSize(13)
    ext_pg_next:SetFont(FONT_BODY)
    ext_pg_next:SetOnClick(function()
        if type(api.GetExtSnapshot) ~= "function" then return end
        local list = api.GetExtSnapshot().plugins or {}
        local p = list[ext_selected_idx]
        if p then
            local pages = math.max(1, math.ceil(#p.params / EXT_ROWS_PER_PAGE))
            if ext_param_page < pages then
                ext_param_page = ext_param_page + 1
                if panel_refresh_fn then panel_refresh_fn() end
            end
        end
    end)
    ext_pager_root:Hide()

    local function RefreshExtTab(zh_now)
        if type(api.GetExtSnapshot) ~= "function" then return end
        local es = api.GetExtSnapshot()
        local list = es.plugins or {}
        local n = #list

        ext_sum_txt:SetString(zh_now
            and string.format("HS_EXT_V%d 扩展：已装 %d · 运行 %d · %.2fms", es.abi_version or 1, n, es.active_count or 0, es.total_gpu_ms or 0)
            or string.format("HS_EXT_V%d: %d inst · %d on · %.2fms", es.abi_version or 1, n, es.active_count or 0, es.total_gpu_ms or 0))

        if es.master_bypass then
            ext_byp_bg:SetTint(0.42, 0.16, 0.12, 0.96)
            ext_byp_btn:SetText(zh_now and "已全部旁路" or "BYPASSED")
            ext_byp_btn:SetTextColour(1.0, 0.72, 0.62, 1.0)
        else
            ext_byp_bg:SetTint(0.12, 0.28, 0.18, 0.94)
            ext_byp_btn:SetText(zh_now and "总开关:运行" or "Ext: Active")
            ext_byp_btn:SetTextColour(0.64, 1.0, 0.78, 1.0)
        end

        local cb = es.core_bloom or {}
        local cf = es.core_final or {}
        if cb.has_dll or cf.has_dll then
            local mode_s = (cf.is_hw or cb.is_hw) and "GPU" or "CPU"
            ext_core_txt:SetString(zh_now
                and string.format("核心基准(%s)：柔光 %.2fms · 终合 %.2fms · 扩展合计 %.2fms", mode_s, cb.ema_ms or 0, cf.ema_ms or 0, es.total_gpu_ms or 0)
                or string.format("Core(%s): Bloom %.2fms · Final %.2fms · Ext %.2fms", mode_s, cb.ema_ms or 0, cf.ema_ms or 0, es.total_gpu_ms or 0))
        else
            ext_core_txt:SetString(zh_now
                and "未加载新版 dinput8.dll：插件纯 Lua 正常运行（装 DLL 可看硬件 GPU 耗时）"
                or "No dinput8.dll: Plugins run in pure Lua (install DLL for HW GPU ms)")
        end

        if n == 0 then
            ext_empty_root:Show()
            ext_body_root:Hide()
            ext_empty_txt:SetString(zh_now
                and "当前未检测到第三方 HS_EXT_V1 扩展模组\n\n第三方开发者只需通过 GLOBAL.HyperSmooth.RegisterPlugin\n注册算法 .ksh 与参数表，即可自动生成本页实时滑块、\n接入标准昼夜/雨雪上下文总线并获得异常熔断保护。\n\n控制台输入 c_hsext() 可查看管线与拒绝日志"
                or "No third-party HS_EXT_V1 shader plugins detected.\n\nAuthors register a .ksh + param schema via\nGLOBAL.HyperSmooth.RegisterPlugin() to get automatic\nlive sliders, shared world context uniforms, fault\nisolation, and per-pass GPU timing.\n\nConsole: c_hsext()")
            return
        end

        ext_empty_root:Hide()
        ext_body_root:Show()
        if ext_selected_idx > n then ext_selected_idx = n end
        if ext_selected_idx < 1 then ext_selected_idx = 1 end
        local p = list[ext_selected_idx]

        local pname = zh_now and p.name_zh or p.name_en
        ext_sel_txt:SetString(string.format("[%d/%d] %s  v%s", ext_selected_idx, n, pname, p.version or "1.0"))
        ext_rst_btn:SetText(zh_now and "重置参数" or "Reset")

        if p.faulted then
            ext_en_bg:SetTint(0.44, 0.10, 0.10, 0.96)
            ext_en_btn:SetText(zh_now and "已熔断隔离" or "ISOLATED")
            ext_en_btn:SetTextColour(1.0, 0.65, 0.65, 1.0)
        elseif p.enabled and not es.master_bypass then
            ext_en_bg:SetTint(0.12, 0.34, 0.20, 0.96)
            ext_en_btn:SetText(zh_now and "已启用" or "ENABLED")
            ext_en_btn:SetTextColour(0.54, 1.0, 0.72, 1.0)
        elseif p.enabled and es.master_bypass then
            ext_en_bg:SetTint(0.32, 0.24, 0.10, 0.92)
            ext_en_btn:SetText(zh_now and "旁路暂停" or "BYPASSED")
            ext_en_btn:SetTextColour(1.0, 0.86, 0.50, 1.0)
        else
            ext_en_bg:SetTint(0.16, 0.16, 0.20, 0.90)
            ext_en_btn:SetText(zh_now and "未启用" or "DISABLED")
            ext_en_btn:SetTextColour(0.72, 0.76, 0.82, 0.92)
        end

        local gpu = p.gpu or {}
        local g_label
        if gpu.has_dll then
            g_label = string.format("%s · %.2fms %s", p.stage, gpu.ema_ms or 0, gpu.is_hw and "GPU" or "CPU")
        else
            g_label = string.format("%s · Lua", p.stage)
        end
        ext_gpu_txt:SetString(g_label)

        if p.faulted then
            ext_meta_bg:SetTint(0.30, 0.09, 0.09, 0.92)
            ext_meta_txt:SetColour(1.0, 0.72, 0.72, 1.0)
            ext_meta_txt:SetString((zh_now and "熔断原因：" or "Fault: ") .. tostring(p.fault_msg or "error"))
        else
            ext_meta_bg:SetTint(0.09, 0.11, 0.15, 0.84)
            ext_meta_txt:SetColour(0.76, 0.82, 0.90, 0.94)
            local desc = zh_now and p.desc_zh or p.desc_en
            local excl = p.exclusive_group and ((zh_now and " · 互斥:" or " · grp:") .. p.exclusive_group) or ""
            ext_meta_txt:SetString(string.format("%s · by %s%s%s", p.id, p.author or "?", excl, (#desc > 0) and (" — " .. desc) or ""))
        end

        local params = p.params or {}
        local total_pages = math.max(1, math.ceil(#params / EXT_ROWS_PER_PAGE))
        if ext_param_page > total_pages then ext_param_page = total_pages end
        local start_i = (ext_param_page - 1) * EXT_ROWS_PER_PAGE
        for r = 1, EXT_ROWS_PER_PAGE do
            local rw = ext_param_rows[r]
            local pd = params[start_i + r]
            if pd then
                rw.root:Show()
                rw.lbl:SetString(zh_now and pd.zh or pd.en)
                local v = p.values[pd.key]
                if v == nil then v = pd.default end
                if pd.type == "bool" then
                    rw.mbg:Hide(); rw.mbtn:Hide(); rw.pbg:Hide(); rw.pbtn:Hide()
                    local on = v > 0.5
                    rw.vbtn:SetText(on and (zh_now and "开" or "ON") or (zh_now and "关" or "OFF"))
                    rw.vbtn:SetTextColour(on and 0.48 or 0.70, on and 1.0 or 0.74, on and 0.68 or 0.80, 1.0)
                else
                    rw.mbg:Show(); rw.mbtn:Show(); rw.pbg:Show(); rw.pbtn:Show()
                    rw.vbtn:SetText(string.format(pd.fmt or "%.2f", v * (pd.scale or 1)))
                    rw.vbtn:SetTextColour(1.0, 0.90, 0.60, 1.0)
                end
            else
                rw.root:Hide()
            end
        end

        if total_pages > 1 then
            ext_pager_root:Show()
            ext_pg_prev:SetText(zh_now and "< 上页" or "< Prev")
            ext_pg_next:SetText(zh_now and "下页 >" or "Next >")
            ext_pg_txt:SetString(string.format("%d / %d", ext_param_page, total_pages))
        else
            ext_pager_root:Hide()
        end
    end

    -- =========================================================================
    -- Description bar (Pro only) + mode application
    -- =========================================================================
    UI.default_info = {
        zh = "说明栏", en = "Help",
        dzh = "把鼠标移到任意选项上（或点一下名称），这里会用大白话告诉你它是干什么的。",
        den = "Hover over (or click) any option and a plain-language explanation shows up here.",
    }
    UI.pro_chrome[#UI.pro_chrome + 1] = NewImage(panel, 0, -299, W - 22, 52, 0.035, 0.045, 0.068, 0.92)
    UI.pro_chrome[#UI.pro_chrome + 1] = NewImage(panel, -W * 0.5 + 12, -299, 3, 52, 0.95, 0.74, 0.36, 0.90)
    UI.desc_title = NewText(panel, 14, "", 0, -283, W - 40, 18, G.ANCHOR_LEFT, 0.98, 0.84, 0.50, 1.0)
    UI.desc_body = NewText(panel, 12, "", 0, -303, W - 40, 30, G.ANCHOR_LEFT, 0.78, 0.84, 0.92, 0.96)
    UI.pro_chrome[#UI.pro_chrome + 1] = UI.desc_title
    UI.pro_chrome[#UI.pro_chrome + 1] = UI.desc_body

    ApplyMode = function()
        local pro = (panel_ui_mode == "pro")
        if pro then tab_roots[1]:Hide() else tab_roots[1]:Show() end
        for tid = 2, 8 do
            if pro and tid == active_tab then tab_roots[tid]:Show() else tab_roots[tid]:Hide() end
        end
        for i = 1, #UI.pro_chrome do
            local cw = UI.pro_chrome[i]
            if pro then cw:Show() else cw:Hide() end
        end
    end

    local function RefreshTelemetry()
        local snap = api.GetSnapshot()
        local zh_now = (snap.ui_zh ~= nil) and (snap.ui_zh == true) or IsChineseLocale()
        local exp_ver = tostring(snap.expected_dll_ver or "2.9.5")
        local ver = tostring(snap.dll_ver or exp_ver)
        if snap.dll_status == "ok" then
            UI.st_bg:SetTint(0.04, 0.12, 0.09, 0.84)
            UI.st2:SetColour(0.50, 1.0, 0.68, 0.96)
            UI.st2:SetString(zh_now
                and string.format("● 原生引擎已连接 v%s%s", ver, snap.has_world_studio and "（3D 光影可用）" or "（基础功能）")
                or string.format("● Native engine connected v%s%s", ver, snap.has_world_studio and " (3D lighting ready)" or " (basic)"))
        elseif snap.dll_status == "outdated" then
            UI.st_bg:SetTint(0.22, 0.12, 0.03, 0.92)
            UI.st2:SetColour(1.0, 0.90, 0.42, 1.0)
            UI.st2:SetString(zh_now
                and string.format("● dinput8.dll 版本过旧 (%s)，请更新到 v%s", ver, exp_ver)
                or string.format("● Outdated dinput8.dll (%s) - please update to v%s", ver, exp_ver))
        else
            UI.st_bg:SetTint(0.20, 0.11, 0.04, 0.88)
            UI.st2:SetColour(1.0, 0.82, 0.48, 1.0)
            UI.st2:SetString(zh_now
                and string.format("● 未检测到新版 dinput8.dll（基础功能可用，3D 光影需 v%s）", exp_ver)
                or string.format("● New dinput8.dll not found (basics OK; 3D lighting needs v%s)", exp_ver))
        end

        local net_s = ""
        if snap.net_connected and (snap.ping_avg or -1) >= 0 then
            if zh_now then
                net_s = ((snap.loss_cur or 0) >= 0.5)
                    and string.format(" | 延迟%dms(丢包%.0f%%)", snap.ping_avg, snap.loss_cur)
                    or string.format(" | 延迟%dms", snap.ping_avg)
            else
                net_s = ((snap.loss_cur or 0) >= 0.5)
                    and string.format(" | %dms(%.0f%%)", snap.ping_avg, snap.loss_cur)
                    or string.format(" | %dms", snap.ping_avg)
            end
        end
        local t1 = zh_now
            and string.format("帧率 %.0f · 低帧 %.0f%s", snap.avg_fps or 0, snap.low1_fps or 0, net_s)
            or string.format("FPS %.0f · 1%%Low %.0f%s", snap.avg_fps or 0, snap.low1_fps or 0, net_s)
        UI.st1:SetString(string.format("[%s] ", tostring(snap.render_backend or "D3D9")) .. t1)

        if UI.diag1 then
            UI.diag1:SetString(zh_now
                and string.format("后端 %s · 峰值忙时 %.1fms · 显存 %.0fMB · DLL %s", tostring(snap.render_backend or "D3D9"), snap.busy_ms or 0, snap.vram_mb or 0, tostring(snap.dll_status or "none"))
                or string.format("Backend %s · busy %.1fms · VRAM %.0fMB · DLL %s", tostring(snap.render_backend or "D3D9"), snap.busy_ms or 0, snap.vram_mb or 0, tostring(snap.dll_status or "none")))
            local lat_s = snap.d3d9ex_active
                and string.format("%d/%d", snap.cur_lat or 1, snap.orig_lat or 3)
                or (snap.has_dll and tostring(snap.cur_lat or 3) or "-")
            UI.diag2:SetString(zh_now
                and string.format("渲染队列 %s · 缓冲池 %.0f%% · 优先包 %d · 异步存档 %d · 防漏点击 %d", lat_s, snap.vb_hit_pct or 0, snap.rpc_imm or 0, snap.async_save_ok or 0, snap.flick_rescues or 0)
                or string.format("Queue %s · VB %.0f%% · QoS %d · AsyncSave %d · FlickRescue %d", lat_s, snap.vb_hit_pct or 0, snap.rpc_imm or 0, snap.async_save_ok or 0, snap.flick_rescues or 0))
            UI.diag3:SetString(zh_now
                and ("反馈/交流 QQ 群：" .. UI.QQ_GROUP .. "（截图本区 + dst_fps_plugin.log）")
                or ("Feedback QQ group: " .. UI.QQ_GROUP .. " (screenshot + dst_fps_plugin.log)"))
        end
        -- QQ group header button (copy feedback flash auto-expires after ~2.5s)
        local fnNow = rawget(G, "GetTimeRealSeconds")
        local now_t = (type(fnNow) == "function") and fnNow() or 0
        if UI.qq_flash and (now_t - (UI.qq_flash_t or 0)) > 2.5 then
            UI.qq_flash = nil
        end
        if UI.qq_flash == 1 then
            UI.qq_btn:SetText(zh_now and "已复制群号，去 QQ 粘贴搜索" or "Copied! Paste in QQ search")
            UI.qq_btn:SetTextColour(0.50, 1.0, 0.70, 1.0)
        elseif UI.qq_flash == 2 then
            UI.qq_btn:SetText(zh_now and ("QQ群 " .. UI.QQ_GROUP .. "（请手动输入）") or ("QQ group " .. UI.QQ_GROUP .. " (type manually)"))
            UI.qq_btn:SetTextColour(1.0, 0.85, 0.50, 1.0)
        else
            UI.qq_btn:SetText(zh_now and ("反馈交流群 " .. UI.QQ_GROUP .. "（点击复制）") or ("QQ " .. UI.QQ_GROUP .. " (click to copy)"))
            UI.qq_btn:SetTextColour(0.98, 0.80, 0.42, 1.0)
        end
        if panel_ui_mode == "pro" and active_tab == 8 then
            RefreshExtTab(zh_now)
        end
    end

    local function ToggleIsOn(d, snap, fl, tn)
        local s = d.src
        if s == "tune" then return (tonumber(tn[d.key]) or 0) > 0.5 end
        if s == "flag" then return fl[d.key] == true end
        if s == "skeletal" then return snap.skeletal == true end
        if s == "lowlat" then return snap.low_latency == true end
        if s == "clip" then return snap.clip_cursor == true end
        if s == "hud" then return snap.hud_show ~= false end
        if s == "bundle" then
            local b = snap.bundles
            return type(b) == "table" and b[d.key] == true
        end
        return false
    end

    local function RefreshStepper(sw, snap, fl, tn, zh_now)
        sw.lbl:SetString(zh_now and sw.zh or sw.en)
        local rawv = tonumber(tn[sw.key]) or 0
        sw.val_btn:SetText(string.format(sw.fmt, rawv * sw.scale))
        local active = true
        if sw.flag ~= nil then
            active = (fl[sw.flag] == true)
        elseif sw.skeletal_only then
            active = (snap.skeletal == true) and (snap.has_dll == true)
        elseif sw.native_ws then
            active = (snap.has_world_studio == true) and (snap.preset ~= 0)
        elseif sw.hitfeel_only then
            active = (tonumber(tn.hitfeel_enabled) or 0) > 0.5 and (snap.preset ~= 0)
        end
        sw.active = active
        local b = (type(snap.bounds) == "table") and snap.bounds[sw.key] or nil
        if b and b.max > b.min then
            local span = b.max - b.min
            local frac = (rawv - b.min) / span
            if frac < 0 then frac = 0 elseif frac > 1 then frac = 1 end
            local kx = TRACK_X0 + frac * TRACK_W
            sw.knob:SetPosition(kx, sw.ry, 0)
            sw.knob_edge:SetPosition(kx, sw.ry, 0)
            local fw = frac * TRACK_W
            if fw < 0.5 then fw = 0.5 end
            sw.fill:ScaleToSize(fw, 4)
            sw.fill:SetPosition(TRACK_X0 + fw * 0.5, sw.ry, 0)
            if active then
                sw.fill:SetTint(0.20, 0.72, 0.64, 0.95)
            else
                sw.fill:SetTint(0.30, 0.34, 0.40, 0.70)
            end
            local dfrac = ((b.default or b.min) - b.min) / span
            if dfrac < 0 then dfrac = 0 elseif dfrac > 1 then dfrac = 1 end
            sw.tick:SetPosition(TRACK_X0 + dfrac * TRACK_W, sw.ry, 0)
            sw.tick:SetTint(0.95, 0.74, 0.36, active and 0.80 or 0.40)
            local changed = math.abs(rawv - (b.default or rawv)) > 1e-4
            sw.mark:SetTint(0.95, 0.74, 0.36, changed and 0.95 or 0.0)
        end
        PaintKnob(sw)
        if active then
            sw.bg:SetTint(0.095, 0.108, 0.145, 0.90)
            sw.lbl:SetColour(0.92, 0.95, 0.98, 1.0)
            sw.val_btn:SetTextColour(0.45, 1.0, 0.72, 1.0)
        else
            sw.bg:SetTint(0.08, 0.085, 0.11, 0.70)
            sw.lbl:SetColour(0.58, 0.62, 0.68, 0.88)
            sw.val_btn:SetTextColour(0.56, 0.60, 0.66, 0.85)
        end
    end

    local function RefreshStates()
        local snap = api.GetSnapshot()
        local zh_now = (snap.ui_zh ~= nil) and (snap.ui_zh == true) or IsChineseLocale()
        local fl = snap.flags or {}
        local tn = snap.tune or {}
        local slots = snap.slot_saved or {}
        UI.zh_now = zh_now
        if type(snap.bounds) == "table" then UI.bounds = snap.bounds end

        UI.subtitle:SetString(zh_now and "超帧与视觉引擎 v2.9.5（原HS）" or "240FPS & Visual Engine v2.9.5")
        share_open_btn:SetText(zh_now and "分享 / 导入方案码" or "Share / Import")
        home_reset_btn:SetText(zh_now and "恢复原版纯净" or "Restore Vanilla")
        home_pro_btn:SetText(zh_now and "专业调参 >>" or "Pro Tuning >>")
        sc_title:SetString(zh_now and "HyperShade · 方案分享与视觉闭环" or "HyperShade · Share & Look Match")
        sc_copy_btn:SetText(zh_now and "复制分享码" or "Copy Code")
        sc_json_btn:SetText(zh_now and "导出全部方案至 hypersmooth_presets.json" or "Export All to hypersmooth_presets.json")
        sc_chk_btn:SetText(zh_now and "导入前校验" or "Validate")
        sc_go_btn:SetText(zh_now and "安全导入并应用" or "Safe Import")
        sc_undo_btn:SetText(zh_now and "撤销导入" or "Undo")
        RefreshShareCardHeader(snap, zh_now)
        if UI.preset_card and UI.preset_card.Refresh then
            UI.preset_card.Refresh(snap, zh_now)
        end
        RefreshExtTab(zh_now)

        for i = 1, #section_banners do
            local sb = section_banners[i]
            sb.txt:SetString(zh_now and sb.zh or sb.en)
        end
        for i = 1, #UI.texts do
            local tx = UI.texts[i]
            tx.btn:SetText(zh_now and tx.zh or tx.en)
        end

        for i = 1, #UI.mode_btns do
            local mb = UI.mode_btns[i]
            mb.btn:SetText(zh_now and mb.zh or mb.en)
            if panel_ui_mode == mb.id then
                mb.bg:SetTint(0.42, 0.31, 0.12, 0.97)
                mb.btn:SetTextColour(1.0, 0.92, 0.62, 1.0)
            else
                mb.bg:SetTint(0.12, 0.14, 0.19, 0.92)
                mb.btn:SetTextColour(0.70, 0.75, 0.82, 0.95)
            end
        end

        for i = 1, #home_cards do
            local hc = home_cards[i]
            hc.name:SetString(zh_now and preset_labels_zh[hc.id] or preset_labels_en[hc.id])
            hc.desc:SetString(zh_now and preset_desc_zh[hc.id] or preset_desc_en[hc.id])
            if snap.preset == hc.id then
                hc.border:SetTint(0.95, 0.74, 0.36, 0.95)
                hc.bg:SetTint(0.22, 0.17, 0.08, 0.98)
                hc.name:SetColour(1.0, 0.92, 0.62, 1.0)
                hc.desc:SetColour(0.86, 0.78, 0.60, 1.0)
            else
                hc.border:SetTint(0.95, 0.74, 0.36, 0.0)
                hc.bg:SetTint(0.10, 0.115, 0.15, 0.96)
                hc.name:SetColour(0.86, 0.89, 0.94, 1.0)
                hc.desc:SetColour(0.58, 0.66, 0.76, 0.95)
            end
        end

        for i = 1, #preset_btns do
            local pb = preset_btns[i]
            pb.btn:SetText(zh_now and preset_labels_zh[pb.id] or preset_labels_en[pb.id])
            if snap.preset == pb.id then
                pb.bg:SetTint(0.45, 0.34, 0.14, 0.95)
                pb.btn:SetTextColour(1.0, 0.92, 0.62, 1.0)
            else
                pb.bg:SetTint(0.14, 0.14, 0.18, 0.88)
                pb.btn:SetTextColour(0.74, 0.77, 0.82, 0.95)
            end
        end

        for i = 1, #tab_btns do
            local tb = tab_btns[i]
            tb.btn:SetText(zh_now and tb.zh or tb.en)
            if active_tab == tb.id then
                tb.bg:SetTint(0.24, 0.36, 0.48, 0.96)
                tb.btn:SetTextColour(0.98, 0.96, 0.78, 1.0)
            else
                tb.bg:SetTint(0.12, 0.13, 0.17, 0.86)
                tb.btn:SetTextColour(0.72, 0.76, 0.82, 0.92)
            end
        end

        for i = 1, #slot_widgets do
            local sw = slot_widgets[i]
            local has_s = slots[sw.idx] == true
            sw.sbtn:SetText(zh_now and ("存" .. sw.idx) or ("S" .. sw.idx))
            sw.lbtn:SetText(zh_now and (has_s and ("读" .. sw.idx) or ("空" .. sw.idx)) or (has_s and ("L" .. sw.idx) or ("-" .. sw.idx)))
            if has_s then
                sw.lbg:SetTint(0.12, 0.26, 0.32, 0.94)
                sw.lbtn:SetTextColour(0.56, 0.96, 1.0, 1.0)
            else
                sw.lbg:SetTint(0.12, 0.13, 0.16, 0.76)
                sw.lbtn:SetTextColour(0.52, 0.56, 0.62, 0.84)
            end
        end

        for i = 1, #mood_btns do
            local mb = mood_btns[i]
            mb.btn:SetText(zh_now and mb.zh or mb.en)
        end
        for i = 1, #tab_reset_btns do
            local rb = tab_reset_btns[i]
            rb.btn:SetText(zh_now and rb.zh or rb.en)
        end

        for i = 1, #all_steppers do
            RefreshStepper(all_steppers[i], snap, fl, tn, zh_now)
        end

        local hud_show = (snap.hud_show ~= false)
        for i = 1, #toggle_widgets do
            local tw = toggle_widgets[i]
            local d = tw.def
            tw.lbl:SetString(zh_now and d.zh or d.en)
            if tw.dlbl then tw.dlbl:SetString(zh_now and d.hzh or d.hen) end
            if d.src == "hud" then
                local detail = (snap.hud_mode == "detail")
                tw.val:SetString(not hud_show
                    and (zh_now and "[隐藏]" or "[Off]")
                    or (detail and (zh_now and "[详细]" or "[Detail]") or (zh_now and "[简洁]" or "[Compact]")))
                if hud_show then
                    tw.val:SetColour(0.42, 0.92, 1.0, 1.0)
                    tw.lbl:SetColour(0.92, 0.95, 0.98, 1.0)
                else
                    tw.val:SetColour(0.55, 0.58, 0.64, 0.88)
                    tw.lbl:SetColour(0.78, 0.82, 0.88, 0.95)
                end
            else
                local on = ToggleIsOn(d, snap, fl, tn)
                SetSwitch(tw.sw, on)
                if on then
                    tw.bg:SetTint(0.09, 0.19, 0.18, 0.93)
                    tw.lbl:SetColour(0.92, 0.99, 0.96, 1.0)
                else
                    tw.bg:SetTint(0.095, 0.108, 0.145, 0.90)
                    tw.lbl:SetColour(0.78, 0.82, 0.88, 0.95)
                end
            end
        end

        for tid = 2, 7 do
            local page = pages[tid]
            if page then
                for si = 1, #page.secs do
                    local sec = page.secs[si]
                    local open = (page.open == si)
                    sec.hdr.lbl:SetString((open and "-  " or "+  ") .. (zh_now and sec.zh or sec.en))
                    sec.hdr.badge:SetString(sec.count > 0 and ("(" .. sec.count .. ")") or "")
                    if open then
                        sec.hdr.bg:SetTint(0.19, 0.25, 0.34, 0.98)
                    else
                        sec.hdr.bg:SetTint(0.13, 0.16, 0.22, 0.96)
                    end
                end
            end
        end

        if UI.info then
            ShowInfo(UI.info)
        else
            ShowInfo(UI.default_info)
        end
        RefreshTelemetry()
    end


    -- Per-frame UI animation: hover glow / press flash, [-]/[+] hold-repeat, slider drag
    root.OnUpdate = function(self, dt)
        if not panel_visible then
            if UI.hold then UI.hold = nil end
            if UI.drag then
                UI.drag.hot = false
                PaintKnob(UI.drag)
                UI.drag = nil
            end
            return
        end
        if type(dt) ~= "number" or dt <= 0 then return end
        if dt > 0.1 then dt = 0.1 end
        for fx in pairs(UI.fx_active) do
            fx.hover = fx.hover + (fx.hover_t - fx.hover) * math.min(1.0, dt * 16.0)
            fx.press = fx.press * math.max(0.0, 1.0 - dt * 7.0)
            local a = fx.hover * 0.10 + fx.press * 0.28
            if math.abs(fx.hover - fx.hover_t) < 0.01 and fx.press < 0.01 then
                fx.hover = fx.hover_t
                fx.press = 0.0
                a = fx.hover * 0.10
                UI.fx_active[fx] = nil
            end
            fx.img:SetTint(1.0, 0.95, 0.80, a)
        end
        local TI = rawget(G, "TheInput")
        local down = false
        if TI ~= nil and type(TI.IsMouseDown) == "function" then
            down = TI:IsMouseDown(rawget(G, "MOUSEBUTTON_LEFT") or 1000) and true or false
        end
        if UI.hold then
            if down then
                local h = UI.hold
                h.t = h.t + dt
                local guard = 0
                while h.t >= h.nxt and guard < 4 do
                    h.fn()
                    h.nxt = h.nxt + ((h.t > 1.6) and 0.03 or 0.07)
                    guard = guard + 1
                end
            else
                UI.hold = nil
            end
        end
        if UI.drag then
            local sw = UI.drag
            if down then
                DragApply(sw, false)
            else
                DragApply(sw, true)
                sw.hot = false
                PaintKnob(sw)
                UI.drag = nil
            end
        end
    end
    if type(root.StartUpdating) == "function" then root:StartUpdating() end
    G.rawset(G, "HS_PanelDebug", UI)

    if type(api.GetUiMode) == "function" then
        local okm, m = pcall(api.GetUiMode)
        if okm and (m == "pro" or m == "simple") then panel_ui_mode = m end
    end
    ApplyMode()

    panel_refresh_fn = RefreshStates
    panel_telemetry_fn = RefreshTelemetry
    RefreshStates()
    return root
end

function HS_TogglePanel(force_state)
    local player = G.ThePlayer
    local hud = player and player.HUD
    if not hud then
        if G.print then
            G.print("[HyperSmooth] Live panel can only be opened in-game (enter a world first).")
        end
        return false
    end
    if not (panel_root and panel_root.inst and panel_root.inst:IsValid()) then
        local ok, built = pcall(CreateLiveCustomizerPanel, hud)
        if not (ok and built) then
            if G.print then G.print("[HyperSmooth] ERROR: Panel build failed: " .. tostring(built)) end
            return false
        end
        panel_root = built
        panel_visible = false
    end
    if force_state == nil then
        panel_visible = not panel_visible
    else
        panel_visible = force_state and true or false
    end
    if panel_visible then
        if panel_refresh_fn then pcall(panel_refresh_fn) end
        panel_root:Show()
        panel_root:MoveToFront()
    else
        panel_root:Hide()
    end
    return panel_visible
end

function HS_PanelTickTelemetry()
    if panel_visible and panel_root and panel_root.inst and panel_root.inst:IsValid() and panel_telemetry_fn then
        pcall(panel_telemetry_fn)
    end
end

function HS_PanelNotifySync()
    if panel_visible and panel_root and panel_root.inst and panel_root.inst:IsValid() and panel_refresh_fn then
        pcall(panel_refresh_fn)
    end
end

G.rawset(G, "c_hsmenu", function(on)
    local vis = HS_TogglePanel(on)
    if G.print then
        local ui_zh = IsChineseLocale()
        G.print(ui_zh
            and ("[HyperSmooth] 实时画质光影工坊 -> " .. (vis and "已打开" or "已关闭"))
            or ("[HyperSmooth] Live Visual Studio Panel -> " .. (vis and "OPEN" or "CLOSED")))
    end
end)
