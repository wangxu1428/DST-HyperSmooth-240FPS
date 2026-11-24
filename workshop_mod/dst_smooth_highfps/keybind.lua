-- =============================================================================
-- HyperSmooth Interactive Any-Key Binding UI (Mod Config + ESC->Settings->Controls)
-- Inspired by MIT-licensed DST-KeyBind-UI (rtk0c / liolok), cleanly namespaced
-- for 100% conflict-free coexistence alongside other keybind mods.
-- =============================================================================

local G = GLOBAL
local C = G.UICOLOURS
local S = G.STRINGS and G.STRINGS.UI and G.STRINGS.UI.CONTROLSSCREEN
if not (C and S and S.INPUTS and modinfo and modinfo.keys and modinfo.configuration_options) then
    return
end

local Widget            = require("widgets/widget")
local Image             = require("widgets/image")
local ImageButton       = require("widgets/imagebutton")
local PopupDialogScreen = require("screens/redux/popupdialog")
local Text              = require("widgets/text")
local TEMPLATES         = require("widgets/redux/templates")

local EXTRA_KEYBOARD = {
    KEY_QUOTE     = 39,
    KEY_BACKQUOTE = 96,
}

-- DST Control Screen mouse button glyphs (Middle, Mouse 4, Mouse 5) -> keycode
local MOUSE_MAP = {
    ["\238\132\130"] = 1002,
    ["\238\132\131"] = 1005,
    ["\238\132\132"] = 1006,
}

local function NormalizeKey(key)
    if key == nil or key == false or key == "" or key == "KEY_DISABLED" then
        return "KEY_DISABLED"
    end
    return key
end

local function RawKeyCode(key)
    key = NormalizeKey(key)
    if key == "KEY_DISABLED" then return nil end
    return EXTRA_KEYBOARD[key] or MOUSE_MAP[key] or G.rawget(G, key)
end

-- Reverse map: numeric keycode -> canonical option data string
local code_to_str = {}
for i = 1, #modinfo.keys do
    local k = modinfo.keys[i].data
    local num = RawKeyCode(k)
    if num ~= nil and code_to_str[num] == nil then
        code_to_str[num] = k
    end
end

local function StringifyKeyCode(keycode)
    return code_to_str[keycode]
end

local function IsUiZh()
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
    return S.CANCEL ~= nil and S.CANCEL ~= "Cancel" and (S.CANCEL == "取消")
end

local KB_ZH = {
    hotkey_cycle = {
        label = "循环切换帧率热键",
        hover = "点击按任意键盘按键或鼠标中键/侧键自由绑定（也可在 设置 -> 控制 实时改绑，默认禁用零冲突）",
    },
    hotkey_skeletal = {
        label = "开关 240 帧补帧热键",
        hover = "点击绑定按键或鼠标侧键，局内一键开关 240Hz 全场景补帧（默认禁用零冲突）",
    },
    hotkey_hud = {
        label = "切换帧率 HUD 热键",
        hover = "点击绑定按键或鼠标侧键，局内按键循环切换右上角 HUD：简洁 -> 详细 -> 隐藏（默认禁用）",
    },
    hotkey_visual = {
        label = "循环切换画质热键",
        hover = "点击绑定按键或鼠标侧键，局内一键在原版纯净 / 护眼高清 / 柔和绘本间对比（默认禁用）",
    },
    hotkey_panel = {
        label = "开关微调侧栏热键",
        hover = "点击绑定按键或鼠标侧键，局内不暂停呼出右侧微调面板（也可输入 c_hsmenu()，默认禁用）",
    },
}

local KB_EN = {
    hotkey_cycle = {
        label = "Cycle Target FPS Hotkey",
        hover = "Click to bind any key or mouse button (also rebindable in Settings -> Controls; disabled by default)",
    },
    hotkey_skeletal = {
        label = "Toggle 240Hz Interpolation Hotkey",
        hover = "Click to bind any key or mouse button for instant 240Hz vs 30Hz A/B comparison (disabled by default)",
    },
    hotkey_hud = {
        label = "Cycle FPS HUD Mode Hotkey",
        hover = "Click to bind any key or mouse button to cycle top-right HUD: Compact -> Detailed -> Hidden (disabled by default)",
    },
    hotkey_visual = {
        label = "Cycle Visual Preset Hotkey",
        hover = "Click to bind any key or mouse button for 0ms visual preset A/B comparison (disabled by default)",
    },
    hotkey_panel = {
        label = "Toggle In-Game Sidebar Hotkey",
        hover = "Click to bind any key or mouse button to open the live tuning sidebar (or run c_hsmenu(); disabled by default)",
    },
}

local function GetKeyLabel(cfg)
    local dict = IsUiZh() and KB_ZH or KB_EN
    local entry = cfg and dict[cfg.name]
    return (entry and entry.label) or (cfg and cfg.label) or ""
end

local function GetKeyHover(cfg)
    local dict = IsUiZh() and KB_ZH or KB_EN
    local entry = cfg and dict[cfg.name]
    return (entry and entry.hover) or (cfg and cfg.hover) or ""
end

local function LocalizeKey(key)
    local num = RawKeyCode(key)
    if num ~= nil and S.INPUTS[1] and S.INPUTS[1][num] then
        return S.INPUTS[1][num]
    end
    if S.INPUTS[9] and S.INPUTS[9][2] then
        return S.INPUTS[9][2]
    end
    return IsUiZh() and "- 无 -" or "- None -"
end

-- Identify our keybind config entries and hydrate placeholder options
local kb_configs = {}
local is_kb_opt = {}
for i = 1, #modinfo.configuration_options do
    local cfg = modinfo.configuration_options[i]
    if cfg.options == modinfo.keys_placeholder then
        cfg.options = modinfo.keys
    end
    if cfg.options == modinfo.keys then
        kb_configs[#kb_configs + 1] = cfg
        is_kb_opt[cfg.name] = true
    end
end

local function ApplyAllBindings()
    if type(HS_ApplyKeyBind) ~= "function" then return end
    for i = 1, #kb_configs do
        local cfg = kb_configs[i]
        local saved = NormalizeKey(GetModConfigData(cfg.name, true))
        HS_ApplyKeyBind(cfg.name, RawKeyCode(saved))
    end
end

if type(AddGamePostInit) == "function" then
    AddGamePostInit(ApplyAllBindings)
end

--------------------------------------------------------------------------------
-- Interactive Click-to-Record KeyBind Button Widget
--------------------------------------------------------------------------------
local BUTTON_NAME = "hs_keybind_btn@" .. tostring(modname)

local BindButton = Class(Widget, function(self, param)
    Widget._ctor(self, tostring(modname) .. ":HSKeyBindButton")
    self.title     = param.title
    self.default   = NormalizeKey(param.default)
    self.initial   = NormalizeKey(param.initial)
    self.OnSet     = param.OnSet
    self.OnChanged = param.OnChanged

    self.changed_image = self:AddChild(Image("images/global_redux.xml", "wardrobe_spinner_bg.tex"))
    self.changed_image:ScaleToSize(param.width, param.height)
    self.changed_image:SetTint(1, 1, 1, 0.3)
    self.changed_image:Hide()

    self.binding_btn = self:AddChild(ImageButton("images/global_redux.xml", "blank.tex", "spinner_focus.tex"))
    self.binding_btn:SetOnClick(function() self:PopupKeyBindDialog() end)
    self.binding_btn:ForceImageSize(param.width, param.height)
    self.binding_btn:SetText(LocalizeKey(self.initial))
    self.binding_btn:SetTextSize(param.text_size or 30)
    self.binding_btn:SetTextColour(param.text_color or C.GOLD_CLICKABLE)
    self.binding_btn:SetTextFocusColour(C.GOLD_FOCUS)
    self.binding_btn:SetFont(G.CHATFONT)

    self.unbinding_btn = self:AddChild(ImageButton("images/global_redux.xml", "close.tex", "close.tex"))
    self.unbinding_btn:SetPosition(param.width / 2 + (param.offset or 10), 0)
    self.unbinding_btn:SetOnClick(function() self:Set("KEY_DISABLED") end)
    self.unbinding_btn:SetHoverText(S.UNBIND or (IsUiZh() and "解绑" or "Unbind"))
    self.unbinding_btn:SetScale(0.4, 0.4)

    self.focus_forward = self.binding_btn
end)

function BindButton:Set(key)
    key = NormalizeKey(key)
    self.binding_btn:SetText(LocalizeKey(key))
    if self.OnSet then self.OnSet(key) end
    if key == self.initial then
        self.changed_image:Hide()
    else
        if self.OnChanged then self.OnChanged() end
        self.changed_image:Show()
    end
end

function BindButton:PopupKeyBindDialog()
    local fe = G.TheFrontEnd
    if not fe then return end

    local function Setup(key)
        self:Set(key)
        fe:PopScreen()
        local snd = fe.GetSound and fe:GetSound()
        if snd then snd:PlaySound("dontstarve/HUD/click_move") end
    end

    local ui_zh = IsUiZh()
    local buttons = {}
    for _, key in ipairs({ "\238\132\130", "\238\132\131", "\238\132\132" }) do
        buttons[#buttons + 1] = { text = key, cb = function() Setup(key) end }
    end
    buttons[#buttons + 1] = { text = S.CANCEL or (ui_zh and "取消" or "Cancel"), cb = function() fe:PopScreen() end }

    local prompt = (S.CONTROL_SELECT or (ui_zh and "请按任意键绑定" or "Press any key to bind"))
        .. "\n\n" .. string.format(S.DEFAULT_CONTROL_TEXT or (ui_zh and "默认：%s" or "Default: %s"), LocalizeKey(self.default))
    local dialog = PopupDialogScreen(self.title, prompt, buttons)

    dialog.OnRawKey = function(_, keycode, down)
        if down then return end -- wait for key release so keypress doesn't immediately trigger
        local key = StringifyKeyCode(keycode)
        if not key then return end
        Setup(key)
        return true
    end

    fe:PushScreen(dialog)
end

--------------------------------------------------------------------------------
-- 1. Mod Configuration Screen: replace dropdown spinner with click-to-record button
--------------------------------------------------------------------------------
AddClassPostConstruct("screens/redux/modconfigurationscreen", function(self)
    if self.modname ~= modname then return end
    local list = self.options_scroll_list
    if not (list and list.update_fn) then return end

    local old_update_fn = list.update_fn
    list.update_fn = function(context, widget, data, ...)
        old_update_fn(context, widget, data, ...)
        local opt = widget and widget.opt
        if not opt then return end
        local spinner = opt.spinner
        opt.focus_forward = spinner
        if opt[BUTTON_NAME] then
            opt[BUTTON_NAME]:Kill()
            opt[BUTTON_NAME] = nil
        end

        local cfg = data and data.option
        if not (cfg and is_kb_opt[cfg.name]) then return end
        if spinner then spinner:Hide() end

        local button = BindButton({
            width      = 225,
            height     = 40,
            text_size  = 25,
            text_color = C.GOLD,
            offset     = 0,
            title      = GetKeyLabel(cfg),
            default    = cfg.default,
            initial    = NormalizeKey(data.initial_value),
            OnSet = function(key)
                if self.options and self.options[widget.real_index] then
                    self.options[widget.real_index].value = key
                end
                data.selected_value = key
            end,
            OnChanged = function()
                if self.MakeDirty then self:MakeDirty() end
            end,
        })
        if spinner then button:SetPosition(spinner:GetPosition()) end
        button:Set(NormalizeKey(data.selected_value))
        button:Show()
        opt[BUTTON_NAME] = opt:AddChild(button)
        opt.focus_forward = button
    end
    if list.RefreshView then list:RefreshView() end
end)

--------------------------------------------------------------------------------
-- 2. ESC -> Settings -> Controls: append live keybind entries at bottom of list
--------------------------------------------------------------------------------
local pending_keys = {}

local BindEntry = Class(Widget, function(self, parent, cfg)
    Widget._ctor(self, tostring(modname) .. ":HSKeyBindEntry")
    local x = -371
    local button_width = 250
    local button_height = 48
    local label_width = 375

    self:SetHoverText(GetKeyHover(cfg), { offset_x = -60, offset_y = 60, wordwrap = true })
    self:SetScale(1, 1, 0.75)

    self.bg = self:AddChild(TEMPLATES.ListItemBackground(700, button_height))
    self.bg:SetPosition(-60, 0)
    self.bg:SetScale(1.025, 1)

    self.label = self:AddChild(Text(G.CHATFONT, 28, GetKeyLabel(cfg), C.GOLD_UNIMPORTANT))
    self.label:SetHAlign(G.ANCHOR_LEFT)
    self.label:SetRegionSize(label_width, 50)
    self.label:SetPosition(x + label_width / 2, 0)
    self.label:SetClickable(false)

    self[BUTTON_NAME] = self:AddChild(BindButton({
        width     = button_width,
        height    = button_height,
        title     = GetKeyLabel(cfg),
        default   = cfg.default,
        initial   = pending_keys[cfg],
        OnSet     = function(key) pending_keys[cfg] = key end,
        OnChanged = function()
            if parent.MakeDirty then parent:MakeDirty() end
        end,
    }))
    self[BUTTON_NAME]:SetPosition(x + label_width + 15 + button_width / 2, 0)

    -- Fields expected by Klei's OptionsScreen:RefreshControls() / OnControlMapped()
    self.controlId, self.control = 0, {}
    self.changed_image = { Show = function() end, Hide = function() end }
    self.binding_btn = { SetText = function() end }

    self.focus_forward = self[BUTTON_NAME]
end)

local SectionHeader = Class(Widget, function(self, title)
    Widget._ctor(self, tostring(modname) .. ":HSKeyBindHeader")
    self.txt = self:AddChild(Text(G.HEADERFONT, 32, title, C.GOLD_SELECTED))
    self.txt:SetPosition(-60, 0)

    self.bg = self:AddChild(TEMPLATES.ListItemBackground(700, 48))
    self.bg:SetImageNormalColour(0, 0, 0, 0)
    self.bg:SetImageFocusColour(0, 0, 0, 0)
    self.bg:SetPosition(-60, 0)
    self.bg:SetScale(1.025, 1)

    self.controlId, self.control = 0, {}
    self.changed_image = { Show = function() end, Hide = function() end }
    self.binding_btn = { SetText = function() end }
end)

AddClassPostConstruct("screens/redux/optionsscreen", function(self)
    local list = self.kb_controllist
    if not (list and list.items and #kb_configs > 0) then return end

    local items = list.items
    local header_title = IsUiZh() and "HyperSmooth 高刷稳帧" or "HyperSmooth 240FPS"
    items[#items + 1] = list:AddChild(SectionHeader(header_title))
    for i = 1, #kb_configs do
        local cfg = kb_configs[i]
        pending_keys[cfg] = NormalizeKey(GetModConfigData(cfg.name, true))
        items[#items + 1] = list:AddChild(BindEntry(self, cfg))
    end
    list:SetList(items, true)

    -- Chain instance-level Reset Controls cleanly (coexists safely with liolok & other mods!)
    local orig_LoadDefaultControls = self.LoadDefaultControls
    if type(orig_LoadDefaultControls) == "function" then
        self.LoadDefaultControls = function(scr, ...)
            if scr.kb_controllist and scr.kb_controllist.items then
                for _, widget in ipairs(scr.kb_controllist.items) do
                    local btn = widget and widget[BUTTON_NAME]
                    if btn then btn:Set(btn.default) end
                end
            end
            return orig_LoadDefaultControls(scr, ...)
        end
    end

    -- Chain instance-level Save cleanly -> applies live & persists to KnownModIndex!
    local orig_Save = self.Save
    if type(orig_Save) == "function" then
        self.Save = function(scr, ...)
            for cfg, key in pairs(pending_keys) do
                local norm = NormalizeKey(key)
                if type(HS_ApplyKeyBind) == "function" then
                    HS_ApplyKeyBind(cfg.name, RawKeyCode(norm))
                end
                if G.KnownModIndex and G.KnownModIndex.SetConfigurationOption then
                    G.KnownModIndex:SetConfigurationOption(modname, cfg.name, norm)
                end
            end
            if G.KnownModIndex and G.KnownModIndex.SaveConfigurationOptions then
                G.KnownModIndex:SaveConfigurationOptions(function() end, modname, modinfo.configuration_options, true)
            end
            return orig_Save(scr, ...)
        end
    end
end)
