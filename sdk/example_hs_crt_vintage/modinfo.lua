local is_zh = (locale == "zh" or locale == "zhr" or locale == "zht")
name = is_zh and "HyperSmooth 扩展演示：复古胶片 / CRT" or "HyperSmooth Ext Demo: Vintage Cinema & CRT"
description = is_zh
    and "官方 HS_EXT_V1 标准着色器扩展参考模组：零科雷渲染/UI/存盘样板代码，直接挂载到 HyperSmooth 画质管线并支持 Tab 8 自动滑块与硬件 GPU 耗时统计。"
    or "Official HS_EXT_V1 reference plugin mod demonstrating zero-boilerplate shader registration on HyperSmooth Core with automatic Tab 8 sliders & hardware GPU timing."
author = "HyperSmooth SDK"
version = "1.0.0"
forumthread = ""
api_version = 10
dst_compatible = true
all_clients_require_mod = false
client_only_mod = true
priority = 0
configuration_options = {}
