# DST HyperSmooth 技术参考卷（Vol.2–Vol.4）：Sodium / Folia / 《流放之路 2》底层架构深挖与 DST 全栈映射手册

- **文献维护**：DST HyperSmooth Architecture Team
- **归档路径**：[`docs/research/REF_VOL2_4_SODIUM_FOLIA_POE2.md`](REF_VOL2_4_SODIUM_FOLIA_POE2.md)（承接 [`REF_VOL1_FACTORIO_FFF.md`](REF_VOL1_FACTORIO_FFF.md)，系统汇总 Vol.2、Vol.3、Vol.4 三大跨界底层工程参考）
- **对标工程基线**：DST HyperSmooth / HyperShade Client v3.9 – v4.0、原生光影渲染管线与 HyperCore 专用服务器（演进蓝图见 [`../V4_0_ARCHITECTURE_ROADMAP.md`](../V4_0_ARCHITECTURE_ROADMAP.md)）
- **证据与合规准则**：
  1. 严格区分 **【原厂公开机制/实证】** 与 **【DST 迁移推演】**，绝不编造未经实测的帧率或加速比数字。
  2. 严守开源许可证与洁净室（Clean-Room）工程红线：对受限制协议项目仅提取计算机图形学与并发系统公有原理，严禁复制、派生或链接受限源码。

---

## 卷二（Vol.2）：Minecraft `Sodium` (0.5/0.6) 与 `Iris Shaders` —— 保留态显存竞技场、动态半透明增量排序与着色器管线治理

### 2.0 文献来源与许可证洁净室红线

- **代表项目**：[CaffeineMC / Sodium](https://github.com/CaffeineMC/sodium) ｜ [IrisShaders / Iris](https://github.com/IrisShaders/Iris)
- **【法务与洁净室红线警告】**：`Sodium` 自 0.5.x 后期至 0.6+ 已由 LGPL-3.0 转向 **PolyForm Shield License 1.0.0**（非 OSI 开源协议，严格限制竞争性商业或代码级衍生）；`Iris` 及 `glsl-transformer` 采用 **LGPL-3.0**。对于本项目纯自研 C++20/UCRT 原生二进制而言，**严禁阅读抄袭其具体实现代码或静态/动态链接，仅研究其公开的 OpenGL 4.3–4.6 硬件交互范式与图形学公有原理**。

### 2.1 区域保留态显存竞技场（`GlBufferArena` + Persistent Staging Ring + 显存内自搬迁）

- **【原厂公开机制】**：
  1. **告别单对象 VBO 绑定**：原版引擎为每个小区块或实体单独分配 VBO 并反复调用 `glBindBuffer` + `glBufferData`，驱动层句柄校验与状态切换开销极高。Sodium 将空间聚合成大尺寸 `RenderRegion`，每个 Region 在纯显卡本地显存（Device-Local VRAM，无 CPU Map 标志）分配一整块连续的 `GlBufferArena`，内部用空闲段链表（Segment Free-List）执行子分配（Sub-allocation）。
  2. **持久映射暂存环（Persistent Mapped Staging Ring）**：渲染主线程 **绝不直接 Map 正在被 GPU 读取的本地显存缓冲**（避免触发驱动隐式同步停顿 Implicit Stall）。CPU 后台线程生成的紧凑顶点写入一块带 `GL_MAP_PERSISTENT_BIT | GL_MAP_WRITE_BIT | GL_MAP_COHERENT_BIT` 的环形上传缓冲，再通过 `glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, ...)` 交由 GPU DMA 硬件异步拷入目标 `GlBufferArena`，配合 `glFenceSync` 非阻塞回收环槽。
  3. **零 CPU 介入的 GPU-to-GPU 扩容紧缩**：当某个 `GlBufferArena` 容量不足或碎片率超标时，直接创建新显存块并发出一批 `glCopyBufferSubData`，利用数百 GB/s 的显存内部带宽在亚毫秒内完成存活段搬迁，CPU 无需重新计算或重传旧几何。
- **【DST 迁移推演（对标 HyperSmooth v4.0 持久镜像池）】**：
  - 在构建 `HyperRender-Retained` 持久化静态世界镜像时，大基地 1,500+ 静态建筑（箱子、墙、农田、锅）的局部几何应常驻 `Device-Local Arena`；视野边缘新加载或换装脏更新统一走 `Persistent Staging Ring -> glCopyBufferSubData`，并严格绑定 `Generation + glFenceSync` 延迟退役机制，从物理上杜绝在途 GPU 读写踩踏。

### 2.2 紧凑量化顶点流与 `BaseVertex` / `MultiDrawIndirect` 降带宽范式

- **【原厂公开机制】**：
  - 大规模场景下，光栅化与顶点拉取（Vertex Fetch）极易受限于显存带宽。Sodium 将松散的 28–32 字节浮点顶点压缩至 16–20 字节（坐标与 UV 采用定点短整型量化、颜色与光照打包为 4 字节 RGBA8），削减约 40% 顶点带宽；同时利用同一 `GlBufferArena` 共享单一 VAO/VBO 的特性，用单条 `glMultiDrawElementsBaseVertex` 或 `glMultiDrawElementsIndirect`（借助 `baseVertex` 寻址子段）一次性提交整个区域。
- **【DST 迁移推演】**：
  - 现有 `SpriteBatcher` 兼容路径的 `BatchedVertex` 为 64 字节，GPU-Driven 路径已压至 20 字节 `GpuBatchVertex`（Phase 2 进一步通过 SSE2 向量化拷贝消除标量瓶颈）。在 v4.0 向 GPU 持久化演进时，可将 **不可变的部件局部网格 `(x, y, u, v)`** 与 **每实例动态状态 `(WorldPos, Velocity, ColourXform, BindlessSlot)`** 彻底解耦为 **紧凑静态 VBO + 64B 实例 SSBO**，避免每帧重复搬运不变的顶点局部坐标与 UV。

### 2.3 Sodium 0.6 动态半透明增量拓扑排序（`GFNI` 分隔平面触发机制）—— 破解 2.5D 画家排序算力税

- **【原厂公开机制】**：
  - 半透明几何体必须严格遵守从后往前的画家顺序（Back-to-Front Painter's Order），这使 Sodium 0.1–0.5 长期不敢开启半透明动态排序（每帧全量 CPU 排序会引发严重掉帧）。
  - Sodium 0.6 引入由 `douira` 设计的 **GFNI（Geometry Planes, Forest & Trigger Intervals）增量拓扑排序系统**：
    1. **几何拓扑静态分级**：先分析几何面片关系——单平面或互不遮挡组标记为免排序（Zero-Sort）；轴对齐平行平面组建立一维坐标触发区间；仅复杂交错组建立方向图。
    2. **跨越分隔面才重排（Trigger-Plane Driven）**：空间中两个静止半透明面之间的前后遮挡次序，**仅在摄像机跨过二者决定的分隔平面时才会发生翻转**。镜头平移或微晃只要未越过触发阈值，上一帧排好的索引序列 100% 数学精确有效，无需每帧重排。
- **【DST 迁移推演：动静双流线性归并（Static-Dynamic Merge）】**：
  - 将 Sodium 0.6 的不变量定理投射到 DST 的 2.5D 立牌相机模型，可以得出高度契合的简化推论：
    1. DST 中所有世界立牌均平行于相机视平面，且相机水平方位角（Yaw）**仅在玩家按下 Q/E 旋转视角时才会改变**（平时跑图仅相机中心平移，不改变平行立牌间的相对投影深度次序）。
    2. **推演结论**：在同一个渲染依赖区段（Render Dependency Segment，无跨段 Stencil/FBO 副作用）内，只要相机未旋转且无建筑增删，大基地 **绝大多数静态实体的从后往前排序结果是帧间严格不变的有序序列（Sorted Run）**。
    3. **极速算法**：高频呈现线程无需每帧对全部 `N_total`（如 30,000 个部件）重头基数排序——只需对本帧 **实际存在位移的 `N_dynamic` 个动态部件**（玩家、牛、蜜蜂、怪物，通常仅数百个）重算深度并排序，随后与缓存好的静态有序数组执行一次 **双有序流线性归并（CPU SIMD Merge 或 GPU Merge-Path，`O(N_static + N_dynamic)`）** 即可得到 100% 精确无穿模的全局画家序，同时天然保住同深度原始稳定次序（`OriginalOrder`）。

### 2.4 `Iris Shaders` 的结构化着色器转换与多上下文状态防污染

- **【原厂公开机制】**：
  - 早期着色器注入方案依赖脆弱的正则表达式替换原版 GLSL 源码，遇到宏嵌套或模组自定义着色器极易产生语法损坏或 Uniform 槽位错乱。Iris 引入基于词法与抽象语法树（AST）的结构化重写器（`glsl-transformer`），并为多阶段渲染目标建立显式的 `Flip/Ping-Pong FBO` 状态追踪器与混合模式独立保存/恢复守卫。
- **【DST 迁移推演】**：
  - 对照现有 `ANIM_BATCH_VS -> HD2D -> SHADOW_V2` 的多级着色器注入链：后续扩展新着色器特性时，应统一收口至单一确定性 Token/AST 级转换管道（避免对同一 `shader` ID 重复叠加文本补丁），并强制延续按 `(HGLRC, progId, linkGen)` 复合键隔离元数据缓存的工程铁律。

---

## 卷三（Vol.3）：Minecraft `Folia` 与 `Minestom` —— 空间孤岛分片多核与广播包去重对 HyperCore 专服的边界评估

### 3.0 文献来源与许可证定位

- **代表项目**：[PaperMC / Folia](https://github.com/PaperMC/Folia)（**GPL-3.0**，原版服务端区域化多核改造标杆）｜ [Minestom / Minestom](https://github.com/Minestom/Minestom)（**Apache-2.0**，洁净室零包袱多核网络服务端）
- **【合规边界说明】**：对 GPL-3.0 项目仅做架构原理层面的文献研读与可行性对照，严禁代码级引用或派生；Apache-2.0 架构范式同样以自研 C++ 洁净实现落地。

### 3.1 `Folia` 的动态空间孤岛分片器（`ThreadedRegionizer`）

- **【原厂公开机制】**：
  1. **静态网格分片的死锁定律**：把连续游戏世界按固定坐标网格硬切给多线程极易引发死锁——一旦实体、投射物或寻路跨在网格分界线上，跨线程读写要么触发数据竞争崩溃，要么陷入 AB-BA 锁顺序死锁。
  2. **动态不相邻孤岛（Disjoint Spatial Islands）**：`Spottedleaf` 设计的 `ThreadedRegionizer` 围绕每个活跃玩家/加载票据维护一层“模拟保护光环（Buffer Halo）”，确立核心硬件不变量：**任何时刻并发运行的两个 `TickRegion` 在空间上绝不相邻，中间必隔着未激活缓冲带**。
  3. **Tick 边界动态合并与分裂（Merge & Split）**：当两名玩家相互靠近导致各自孤岛光环接触时，调度器在安全屏障处将两个孤岛 **原子合并** 为单一孤岛，交由单线程无锁串行更新；当玩家分开超过迟滞距离后，再将其 **自动分裂** 回两个并行孤岛。
  4. **彻底废除全局可变表**：移除全局实体表、全局时钟与全局插件调度器，全部改为孤岛私有状态（`RegionizedWorldData`），跨远距传送一律走挂起序列化与收件箱异步移交（Handoff Queue）。

### 3.2 `Minestom` 的单线程亲和借用（`Acquirable`）与网络包单次编码扇出（`CachedPacket`）

- **【原厂公开机制】**：
  1. **零隐式共享**：每个实例/区块绑定唯一 Tick 线程，跨线程访问必须经 `Acquirable` 投递至目标线程下一 Tick 边界执行，热路径内部零互斥锁开销。
  2. **广播序列化去重（`CachedPacket` / `GroupedPacket`）**：当 `M` 名玩家站在同一高密度区域时，传统架构为每个连接独立遍历序列化视野内 `E` 个实体的同步数据，CPU 复杂度高达 `O(M * E)`。Minestom 将无连接差异的公共状态更新预先编码压缩为不可变字节块缓存，直接向 `M` 个客户端通道零拷贝复用扇出，将网络序列化复杂度压降至 `O(E + M)`。

### 3.3 对 DST `HyperCore` 专服的可行性裁决（【DST 迁移推演】）

- **【工程红线裁决：严禁在单进程原版 `lua_State` 内强行实施多线程 Lua Tick】**：
  - 在严守“不替换原版 Lua 5.1 VM、不破坏 Steam 工坊模组”的红线下，DST 单进程专服只有唯一一个共享 `global_State`（含全局字符串表、GC 链表及 `GLOBAL.Ents`、`TheWorld`）。**绝不可让多个线程并发执行同一 `lua_State`，也不可在单进程内私拆多个 `lua_State`**（否则任何跨图检索 `Ents[guid]` 的工坊模组将立即崩溃或丢状态）。
- **【三大高确定性迁移红利】**：
  1. **C++ 原生算力层的“影子孤岛并行”（Shadow Regionizer）**：虽然 Lua 脚本层保持串行，但下沉至 C++ 的重负载计算（如分散在地图各处的多个 Boss/生物并发 `FastAStar` 寻路、多玩家视野分离时的 C++ `SpatialGrid` 批量邻域预筛）天然具备无交集空间孤岛特征，可在 C++ 弹性工作线程池中按孤岛并行算完后，在主线程安全点一次性喂回 Lua。
  2. **RakNet `ReplicaManager3` 公共位流片段缓存（对标 `RM3:0x87` 同步尖峰）**：多人同聚 2,000 天超级大基地时，借鉴 Minestom 的 `CachedPacket` 思想，在服务端状态同步周期内对发往多名同视野玩家的无私有差异实体 BitStream 做单 Tick 编码缓存（需严格排除含玩家私有可见性/归属掩码的组件），显著削减多人同屏时的专服发包 CPU 尖峰；在客户端侧，则对 `RM3:0x87` 大包解析做子阶段遥测拆解（网络解码 vs 实体查找 vs Lua `dirty` 事件触发），针对性节流非关键视觉 `dirty` 回调。
  3. **多 Shard 进程级分片 = DST 原生合规版区域并发**：DST 引擎本身已通过 `Master`（地表）+ `Caves`（洞穴）多进程 + `ShardRPC` 实现了物理隔离的 `lua_State` 并发。在超大规模专服架构中，利用多世界分片（如独立大基地岛 Shard、独立 Boss 战副本 Shard）并优化跨服 `ShardRPC` 通道与存盘队列，即可零风险吃满多核服务器硬件。

---

## 卷四（Vol.4）：《流放之路 2》（Path of Exile 2）与现代 HD-2D 管线 —— 辐射级联 `O(1)` 多光源软阴影与 2.5D 立体代理法线

### 4.0 文献来源与图形学突破背景

- **核心一手文献**：Alexander Sannikov（Grinding Gear Games《流放之路 2》高级图形程序员），[*Radiance Cascades: A Novel Approach to Calculating Global Illumination*](https://drive.google.com/file/d/1L6v1_7HY2X-LV3Ofb6oyTIxgEaP4LOI6/view) 及 ExileCon 实时全局光照技术分享；可视化交互原理解析参见 [radiance-cascades.com](https://radiance-cascades.com/)。

### 4.1 辐射级联（Radiance Cascades）—— 突破多点光源 `O(Lights * Entities)` 性能死墙

- **DST 现存上限痛点**：
  - 在 HyperSmooth / HyperShade 现行多光源动态投影设计中，为了守住 DrawCall 预算，点光源动态阴影上限被严格限制在 **`kMaxPointShadowLights = 3`**（每增加 1 盏点光源，就需要对半径内实体额外补交一轮阴影镜像绘制）。一旦玩家在夜晚大基地布置 **20–50 盏提灯、吸热火坑、龙蝇火炉、星杖矮星与发光建筑**，传统逐光源阴影 Pass 路线要么帧率雪崩，要么只能截断前 3 盏灯，导致走动时主光源切换产生阴影跳变。
- **【原厂公开机制：半影空间-角度对偶定理（Penumbra Duality）】**：
  - Sannikov 从几何光学严格证明了辐射场采样的两大物理规律：
    1. **近场高空间、低角度**：在紧贴遮挡物或光源的近距离（`d -> 0`），阴影边缘极其锐利（空间高频），但光源张角极大（角度低频，仅需 4 条粗方向射线即可无漏采）。
    2. **远场低空间、高角度**：在远离遮挡物的区域（`d -> infinity`），软阴影半影已高度模糊扩散（空间低频，稀疏探针双线性插值即可精确还原），但远处小火源对探针张角极小（角度高频，必须加密射线角度才不会漏光或产生条纹伪影）。
  - **多级级联探针结构（Hierarchy of Cascades）**：
    - 构建 `Cascade 0 .. Cascade K`（通常 5–6 层）：
      - `Cascade 0`：高空间密度（如每 2×2 像素 1 个探针），4 个方向，仅步进极短距离区间 `[0, r_0]`。
      - `Cascade i+1`：空间长宽各减半（探针数量变为上一层的 **`1/4`**），角度方向数翻 4 倍，步进下一段区间 `[r_i, r_{i+1}]`（区间长度同步翻 2–4 倍）。
    - **三大工程特性**：
      1. **关于光源数量的严格 `O(1)` 复杂度**：所有发光体（火堆、提灯、岩浆、荧光果、闪电）与遮挡物只需在首步统一写入同一张 2D 世界空间辐射/遮挡纹理（Emissive + Opacity Grid），后续级联求值耗时 **与同屏有 1 盏灯还是 300 盏灯完全无关**。
      2. **100% 确定性零噪点（Zero Noise）**：完全不依赖蒙特卡洛随机采样，因此 **不需要任何时域累积降噪（TAA / Temporal Accumulation）**——在 240Hz 高速跑动下 **0 鬼影、0 拖尾、0 闪烁**。
      3. **天然物理接触硬化软阴影 + 2D 间接漫反射**：近处树干脚底自动呈现锐利接触硬阴影，远处自然散开为柔和渐变半影，且天然附带篝火暖橘光与吸热火坑冰蓝光的物理混色与漫射反弹。

### 4.2 跳跃洪水算法（Jump Flooding Algorithm, `JFA`）驱动的动态 2D SDF 加速步进

- **【公有图形学机制与 DST 迁移推演】**：
  - 为进一步压低每个级联区间内的纹理采样次数，可在每帧将视野内立牌脚底遮挡锚点写入一张轻量级世界空间网格（如 `512×512`），执行 `log2(512) = 9` 趟极简 Compute Shader **JFA（Jump Flooding）** 生成实时 2D 有符号距离场（Signed Distance Field），使级联射线通过球追踪（Sphere Tracing）每段仅需 2–3 次采样即可精确命中遮挡边界。
  - **HyperSmooth 演进定位**：太阳平行光 `WorldShadow V2` 继续负责全屏高锐度树冠轮廓平行投影；夜晚/洞穴多点光源通道可作为独立实验模块验证 **World-Space 2D Radiance Cascades**（仅在支持 Compute 4.3 的独显档位启用，核显/ANGLE 自动回退现有 3 光源路径），实现高密度多光源场景零额外几何 DrawCall。

### 4.3 现代 HD-2D 零资产依赖“立体代理法线”（Proxy Volumetric Normals）

- **【行业公有机制】**：
  - 现代 HD-2D 渲染管线与《流放之路 2》在处理大量无独立法线贴图的 2D 精灵/粒子受光时，为消除“扁平纸片感”，会在着色器中根据精灵包围盒几何自动合成三维代理法线。
- **【DST 迁移推演（零显存、100% 兼容工坊皮肤）】**：
  - 依托 `SpriteBatcher` 现有的部件局部水平偏移与世界高度 `wy`，在 `HD2D` 着色阶段为直立世界实体实时构造 **半圆柱/椭球包络代理法线**（两侧边缘法线向左右偏转、顶部向天顶偏转），叠加纹理明暗微细节法线与边缘逆光透射（Wrap Rim Light）。无需打包任何额外法线贴图（显存增量 `0 MB`），即可让全部原版建筑与第三方工坊人物模组在多点光源下获得自然的圆润立体受光过渡。

---

## 五、全四卷（Vol.1–Vol.4）跨界范式反哺 DST 全栈路线总表

将 [`Vol.1`](REF_VOL1_FACTORIO_FFF.md) 与本册 Vol.2–Vol.4 的跨界架构范式，按风险分级与实证进展映射至 DST HyperSmooth / HyperCore 全栈演进路线（详见 [`../V4_0_ARCHITECTURE_ROADMAP.md`](../V4_0_ARCHITECTURE_ROADMAP.md)）：

| 引擎层级 | 借鉴标杆与出处 | 核心技术机制 | 注入 DST 目标模块 | 风险与实施阶段定位 |
| :--- | :--- | :--- | :--- | :--- |
| **资源流送与同步消除层** | Sodium Persistent Upload Ring / Factorio FFF #264 | 同上下文 3 帧持久一致 PBO 环 + `glFenceSync`、预分配纹理 ID 池、消除热路径 `glGetError()` 全管线排空 | `native_plugin/pbo_tex_ring.h`、`native_plugin/dinput8.cpp` | **【Phase 1 已交付 / Phase 2 进行中】** RTX 4060 实测将重贴图上传峰值压低 21×，同步消除 1.9–9.68 ms 驱动停顿 |
| **诊断与 CPU 热路径层** | Factorio FFF #421 / #204 / Windows ETW | 无锁双线程卡顿栈采样、1/64 降频遥测、SSE2 向量化、Tick 纪元结果缓存 | `native_plugin/dinput8.cpp`、`native_plugin/sprite_batcher.h`、Lua 桥接层 | **【Phase 1–2 进行中 · 极低风险】** 直接定位真实堆栈热点，削减每帧数万次 QPC / TLS / 标量拷贝开销 |
| **2.5D 排序与保留态合批层** | Sodium 0.6 `GFNI` + `GlBufferArena` | Render Dependency 分段 + **静态有序缓存与动态位移流 `O(N)` 归并** + 本地显存池 | `native_plugin/sprite_batcher.h`、`native_plugin/gl_state_cache.h`、v4.0 `RenderIR` | **【Phase 3 规划 · 高收益】** 避免每帧全量重排静态建筑，零穿模保住画家序与硬件 Stencil 语义 |
| **异步呈现解耦层** | Factorio FFF #421 `Ghost Robot` + 2.0 保护带 | 低频不可变视觉快照 + 动态物理保护带 `v_max * age + radius` + 高频有限阻尼外推呈现 | v4.0 `PresentThread`、三缓冲快照与动态 `FBO_UI` | **【Phase 3 规划 · 需严密生命周期验证】** 隔离逻辑波动对呈现帧率的连坐，严守 WGL 与回退红线 |
| **次世代光影层** | PoE2 Sannikov `Radiance Cascades` + FFF #333 环形缓冲 | `WorldShadow` 环形卷轴增量更新 + Compute 2D 辐射级联 `O(1)` 无限点光源软阴影 + 代理法线 | `native_plugin/world_shadow_engine.h`、`HD2D` 着色器管线 | **【Phase 3+ 规划 · 视觉演进】** 突破 3 点光源上限，零噪点零鬼影实现高密度大基地多光源照明 |
| **专服多核扩展层** | Folia `ThreadedRegionizer` + Minestom `CachedPacket` | C++ 影子孤岛并发寻路/空间预筛 + `ReplicaManager3` 公共位流单 Tick 缓存 + 多 Shard 集群 | `HyperCore`（服务端原生库）、`fast_astar`、`SpatialGrid` | **【专服演进 · 严守单 Lua VM 红线】** 绝不并发写单 `lua_State`，充分释放 C++ 多核与广播带宽 |
