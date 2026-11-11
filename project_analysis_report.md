# rmt-pipeline 工程分析报告

**日期**: 2026-08-19 **分析范围**: 全部 C 源码 (20 个 .c + rmt.h, 约 5.9k 行核心 + 3.6k 行 demo/测试)、8 个 P4 数据面、构建/基准脚本、配套论文 (`paper_innetwork_computing.md` 等)
**基线环境**: 8 vCPU 虚拟机 (VirtualBox, 与宿主共享核), GCC -O3 -flto -march=native, 全部测试通过 (9/9 suites, 0 failures)

---

## 1. 项目定位

纯 C11、零依赖的 **RMT (Reconfigurable Match-Action Tables) 可编程数据面软件参考实现**，1:1 复现硬件流水线执行语义（VLIW 两相提交、读旧写新、PHV 字段总线、级联 trie 确定性查找），定位是教学/研究模型而非线速转发器。工程同时是一条**论文生产线**：核心引擎 + EdgeRMT 数据面原型支撑 `paper_innetwork_computing.md`（星载 5G UPF 场景的服务感知路由 + 网络编码）与 `paper_edge_rmt.md`，含审稿回复、插图规格与 md→PDF 工具链。

## 2. 功能归纳

### 2.1 核心引擎（src/, include/rmt.h）

| 模块 | 文件/行数 | 功能 |
|---|---|---|
| 可编程解析器/逆解析器 | parser.c (187) | 表驱动解析图 FSM；bit 级提取，字节对齐 memcpy 快路径；`extract_array` 运行时栈深；deparse 按头有效位重串接 + payload/cache 注入 |
| PHV 字段总线 | rmt.h/phv.c | 160×128-bit 大端字段 + uint64 头有效位图；全部位操作 `static inline` |
| Match 引擎 | match.c (158) | 每表 ≤8 key 字段级联二叉 bit-trie，exact/LPM 混用，最长前缀回溯，O(Σ位宽) 确定性查找 |
| Action 引擎 | alu.c (398) | 每级 16 ALU VLIW 执行器，27 条三地址 RISC 指令，computed-goto 分发；两相提交（读旧写新）；extern 指令直达有状态引擎 |
| 流水线编排 | pipeline.c (368) | parse→≤8 stage→deparse；per-stage 表索引、模板 PHV 复位、batch/burst 批处理（共享时间戳） |
| P4 编译器 | p4c.c (933) | 单遍：手写词法 + 递归下降，边解析边生成流水线配置；含编译期 VLIW 依赖调度（RAW/WAW 推迟、WAR 同 word） |
| 控制面 | runtime.c (212) | `table_add` 表项文件解析下发（IPv4/IPv6/MAC/hex 多格式，LPM 前缀长） |
| cache extern | cache.c (284) | 数据面可写哈希存储：FNV-1a + 线性探测(8) + 最老淘汰 + 自旋锁；支撑 NDN CS/PIT 与望远镜 epoch 老化视图 |
| dnn extern | dnn.c (168) | Q16.16 定点 MLP（1 隐层 ReLU + argmax），控制面浮点训练→量化下发，前向纯整数 |
| 多队列引擎 | mq.c (389) | RSS 5 元组流哈希(含 VLAN/IPv6 扩展头/SRH) → SPSC 无锁环(缓存行分离) → 绑核 worker，burst 出队，自适应 spin→yield 退让，最多 128 队列 |

### 2.2 数据面与验证（8 个 P4 + demo）

能力递进排列：MPLS（头栈运行时深度）→ VXLAN（VLIW 读旧写新、双向单流水线）→ SRv6（MOVX 索引寄存器栈、5 元组级联 trie ACL）→ INT（运行时 add_header、3 跳遥测）→ NDN（数据面可写 CS/PIT、缓存本地应答）→ DNN-IDS（训练→量化→在线检出 SYN flood）→ Telescope（MORP4 风格未用地址推断、epoch 老化、IPv4/IPv6 双栈）→ Enc-Classify（4 类 MLP 加密流量分类 + DSCP 联动）。另有 **nc_demo**（服务感知网络编码：GF(2^8) RLNC/XOR 按类分级 FEC，论文 Algorithm 3 原型）与 **pcap_test**（真实 pcap 回放分类评测）。

### 2.3 测试与基准体系

`run.sh` 一键构建/测试；`tests.c` 逐字节端到端校验 + bulk/par/mq/tcpmq 压测；`scripts/bench_all.sh` 七类测试 (T1 功能 / T2 分类精度 / T3 时延 / T4 多核扩展 / T5 负载均衡 / T6 网络编码 / T7 路由+编码联合)；`make pgo` 两遍 PGO 构建；`test_match.c` match 运行时单元测试。

## 3. 性能基线（今日本机实测）

| 场景 | 结果 |
|---|---|
| SRv6 bulk 单线程 (126B) | 1535 ns/pkt, 0.65 Mpps |
| SRv6 batch64 | 1243 ns/pkt, 0.80 Mpps |
| SRv6 mq×8 | 205–340 ns/pkt, 2.9–4.9 Mpps, **4.2–6.1x**（VM 噪声大，多次运行方差显著） |
| TCP 按流分派 mq×8 (54B, 2048 流) | 92–98 ns/pkt, 10.2–10.8 Mpps, **4.6–4.9x** |
| mq 扩展曲线 (tcpmq) | 2q 1.67x → 4q 2.48x → 8q 4.56x → 16q 4.10x（8 核超订阅回落） |
| 联合分类+编码 (nc_demo) | 668 ns/pkt；RLNC k=4 190 ns/代，XOR 5 ns/代 |

**gprof 热点（SRv6 bulk，200k 包）**：

| 函数 | 占比 |
|---|---|
| `rmt_table_lookup`（bit-trie 指针链） | **66.7%** |
| `extract_hdr` + `rmt_parse` + `rmt_deparse`（解析/逆解析） | 20.1% |
| `rmt_action_exec`（VLIW） | 2.2% |
| 其余（测试构包等） | ~11% |

结论：**match 查找是绝对热点**（每包 4 表 × 逐 bit 指针追逐，/128 LPM 最深 128 层依赖访存）；解析/逆解析合计约两成；ALU 执行本身很便宜。

## 4. 功能提升方向

### P0 — 确认的语义缺陷（建议先修）

1. ~~**多字段 LPM 跨字段回溯未实现**~~ ✅ **已修复**（第二轮审查确认）：
   `match.c` 的 `rmt_table_lookup` 已实现完整的迭代式深度优先回溯算法
   （lines 299-339），`test_match.c` Test 8 和 Test 8b 两个用例均通过。
   本报告初版对此功能的判断为误报。

### P1 — 引擎能力补全

2. **ternary 匹配**（任意掩码）：README 已承认缺失，目前用 lpm+exact 近似，无法表达一般 ACL/开放范围。可对每字段建 (value, mask) 感知的决策结构，或引入位图/TCAM 模拟层。
3. **checksum 单元**：P4 子集无校验和原语，示例 IPv4/UDP csum 置 0——真实部署的硬门槛。需增加 `update_checksum`/增量更新指令（字段改写后 csum 增量修正，一个 ALU 槽位即可完成 16-bit 加减折叠）。
4. **运行时并发更新**：`rmt_load_entries()`/`rmt_dnn_load()` 无同步，worker 在跑时只能 stop-the-world（论文 §6.1 自认）。方向：trie 走 RCU/影子表交换，DNN 权重双缓冲 + 原子指针切换。
5. **同级多表打包**：当前一个 `apply` 占一个物理 stage（上限 8），相互独立的表不能同级并行查找——既限制程序规模，也偏离 RMT 论文的多表级内并行语义。
6. **VXLAN 内层 UDP 槽位**（README §13 自认）：封装方向租户 UDP 会占用外层 udp 槽，补一个内层头实例即可。
7. **NDN PIT 多端口复制**：当前单端口，组播/多路径 Interest 聚合表达不了。

### P2 — 与论文 future work 对齐的扩展（paper §6.4 六条）

8. **GTP-U 解析/解封装阶段**（外层 IPv4/UDP→TEID→内层重解析），使分类器可部署于 N3/N9 而非仅 N6。
9. **QUIC/UDP 流量分类**：解析器与特征模型适配 QUIC Initial。
10. **更深 DNN + 流状态特征**：2–3 隐层、IAT/包计数等每流状态，8–16 类细粒度分类（dnn.c 当前仅单隐层、无流状态、输入特征 ≤8）。
11. **流间网络编码（COPE 风格）** + **补齐编码接收路径**：nc_demo 目前只有发送侧系统码。
12. **卫星信道参数化评测**：netem GEO/LEO 时延丢包轨迹驱动的闭环实验。

### P3 — 工程质量

13. **模糊测试**：数据面吃不可信报文，parser/extract_array/RSS 解析器都应上 libFuzzer/AFL（现有防御性检查是好的种子目标）。
14. **sanitizer 常态化**：debug 目标补 `-fsanitize=address,undefined`（README 声称 TSan 干净，但 Makefile 无对应 target）。
15. **CI**：run.sh all + bench_all.sh --quick 入 CI；`gmon.out` 这类生成物不应入库（.gitignore）。
16. **真实 I/O**：pcap 回放之外接 AF_XDP/TAP，验证 mq 引擎与真实收发路径的对接（README 称结构可直接对 DPDK 硬件 RSS，缺实证）。

## 5. 性能提升方向（按预期收益排序）

### 5.1 Match 引擎重构 —— 最大杠杆（热点 66.7%，预期端到端 2–3.5x）

现状：每 key 字段一棵逐 bit 二叉 trie，**每 bit 一次依赖指针访存**；节点逐个 `calloc`，堆布局随机，缓存局部性差；`fv_get_bit` 每 bit 重算位位置；exact 字段也走全宽位链（5 元组 ACL 三个 exact 字段 ≈ 40 次指针追逐，/128 LPM 最深 128 层）。lookup 内 `stk[8]×cand[132]` 每调用写 ~8.5KB 栈。

改法（可分步）：
- **exact 字段 → 哈希索引**：表级预编译全 key 哈希（cuckoo/hopscotch/线性探测），O(1) 替代逐 bit 链。5 元组 ACL 直接受益。
- **LPM → 多比特步长 trie**（stride 4–8，DIR-24-8 式两级直接索引用于 IPv4；IPv6 用 16-8-8… 分层）：依赖访存次数除以步长。
- **节点 arena 池分配 + BFS 序布局**：消除 calloc 碎片，预取友好；现有 `RMT_PREFETCH` 才能真正发挥作用。
- **路径压缩**（单子节点链折叠）+ key 预抽取（进表前一次性把各字段 key 抽到连续字节数组，trie 内用移位而非 `fv_get_bit`）。
- 缩减候选栈：非末字段只保留最深候选（配合 P0 回溯修复时按需记录）。

### 5.2 原位编辑快路径（预期 -10~20% 单包时延）

当前每包 = 解析提取 → 重写 PHV → **全量 deparse 重串接** → **payload memcpy**。对纯转发类负载（SRv6/MPLS），大多数头部字节未变。可提供 fast-path 模式：输出缓冲先整体复制输入包，仅把被 action 修改过的字段**原位补丁**回写（commit 阶段已知 fd→可推导字节偏移），省掉 deparse 逐字段写与 payload 拷贝（gprof 中 parse+extract+deparse 合计 ~20%，payload memcpy 另计）。语义上是软件加速选项，不改变硬件模型语义（默认关闭，`rmt_process_inplace()` 并列提供）。

### 5.3 mq 引擎扩展性（8 队列 4.6x → 目标 ≥7x）

实测扩展曲线在 8 核止于 ~4.6x（tcpmq），制约因素：
- **单分发线程串行**：每包 RSS 哈希（FNV-1a 逐字节，IPv6 ~40 次乘异或 ≈ 10–15ns）+ 逐包 `ring_push`（每包一次 tail acquire 读 + head release 写）。改法：① `rmt_mq_submit_burst()` 批量提交，一次 head release 摊还整批；② 流哈希换 **CRC32c 硬件指令**（SSE4.2，8 字节/指令，约 5x）；③ 多分发分片或让 worker 自取（对接真实 NIC RSS 时无分发线程）。
- **核规划**：nworkers==ncpu 时分发线程与某个 worker 抢核（`id % ncpu` 映射）；建议默认 workers=ncpu-1 或提供分发线程独立亲和性参数。
- 环境噪声：本次 VM 实测 mq×4 出现过 0.02x 的异常值（宿主争用），论文级数据应固定裸机/隔离核 + 多轮中位数上报。

### 5.4 去掉 DNN 每推理互斥锁（多核分类负载的隐形天花板）

`rmt_dnn_infer()` 结尾对**每包**做一次 `pthread_mutex_lock` 只为了统计计数（dnn.c:150）——IDS/分类数据面在多 worker 下所有推理在此串行。改为 per-thread 计数器（worker id 索引，mq 已有）或 `_Atomic` fetch_add；print_stats 时汇总。改动 ~10 行，消除跨核串行点。

### 5.5 cache extern 并发（NDN/望远镜负载）

单缓存一把自旋锁，8 worker 共享 NDN CS/PIT 时串行化；且 `rmt_cache_get` 在锁内 memcpy 最多 1KB 内容。改法：① 按 name 哈希**分片**（shard 数 = worker 数即可，正确性不受流亲和影响——同名必落同片）；② 读多写少的 CS 用 seqlock/版本号实现无锁读；③ 锁内只拷指针+长度，内容复制移到锁外。

### 5.6 小项

- **PHV 复位**：每包 memcpy nfields×18B 模板；可做脏字段追踪（commit 阶段记录写掩码，下包只复位脏字段），省 3–5%。
- **batch 时间戳**：`clock_gettime(CLOCK_MONOTONIC)` 已摊到批级；若平台支持可换 vDSO RDTCP 粗时钟。
- **SIMD PHV**（README 展望）：AVX2/AVX-512 并行多字段 mov/add，作用于 deparse 与批量 ALU；收益取决于 5.1 之后 match 是否仍主导。
- **PGO**：`make pgo` 已就绪（+10–20%），建议所有性能对比默认启用并在 bench 脚本固化。

## 6. 建议路线图

| 阶段 | 内容 | 预期 |
|---|---|---|
| 立即 (1–2 天) | ~~P0 回溯修复~~ ✅ 已完成；calloc NULL 检查、deparse 错误处理、unterminated 注释保护 ✅；DNN 统计锁改原子/分线程；.gitignore 清理 | 正确性 + 多核分类扩展性 |
| 短期 (1–2 周) | Match 重构第一步：exact 哈希化 + 节点 arena；mq batch submit + CRC32c 哈希 | 单线程 2x+，mq 8q ≥6x |
| 中期 | LPM 步长 trie；原位编辑快路径；ternary/checksum 单元；运行时原子更新 | 逼近软件模型实用上限 |
| 论文线 | GTP-U 解析、QUIC、深 DNN+流状态、流间 NC、卫星 trace 评测 | 对齐 paper §6.4 六条 future work |

> 现实定位提醒：100 Gbps@126B 需 ~99 Mpps，当前软件最优 ~10 Mpps（54B 小包），差一个数量级。软件侧优化（5.1–5.6 全做完）乐观估计再提 3–5x；真正的线速路径仍是 README 所述——本模型的 P4 程序与表项格式迁移到 Tofino/SmartNIC，mq 结构对接硬件 RSS。工程的最大价值在**语义正确性参考 + 算法实验台**，性能工作应以"让实验迭代更快"为目标，而非追逐线速。
