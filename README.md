# RMT 软件流水线 (Software RMT Pipeline)

基于 C 实现的可编程网络处理流水线软件模型 —— **RMT (Reconfigurable
Match-Action Tables)** 架构，包含可编程协议解析器、≥4 级 Match-Action
流水线（tree 结构查找引擎 + 每级 16 个并行 RISC ALU）、逆解析器、两类
有状态 extern（**in-network cache**、**DNN 推理引擎**）、一个
**P4 语言编译器**（可将 P4 程序编译为整条流水线的配置：解析图、表、ALU
微码、控制流、extern 绑定），以及一个**多队列并行引擎**（RSS 流哈希
分发 + SPSC 无锁环 + 绑核 worker，最多 128 队列，模拟多队列网卡 +
run-to-completion 模型）。

已内置八个示例数据平面并通过逐字节端到端验证：

| P4 程序 | 场景 | 验证内容 |
|---|---|---|
| `p4/mpls.p4` | 多层嵌套 MPLS 标签栈转发 | 逐层 pop / swap、s 位驱动解析 |
| `p4/vxlan.p4` | VXLAN VTEP 网关 | 解封装→overlay 二层转发；封装→underlay 路由 |
| `p4/srv6.p4` | SRv6 End 端点转发 | SL-1、dst:=seg[SL]、5 元组 ACL |
| `p4/int.p4` | INT 带内遥测 | 3 跳逐跳插入元数据、collector 解码 |
| `p4/ndn.p4` | NDN 命名数据网络 | 名字 LPM 转发、CS 缓存命中本地应答、PIT 反向路径 |
| `p4/ids.p4` | DNN 异常流量检测 | MLP 训练/量化/下发、SYN flood 等在线检出 |
| `p4/telescope.p4` | 动态网络望远镜 (MORP4 风格) | 数据面在线推断未用地址、只捕获打向未用空间的流量、老化/复活、IPv6 望远镜 |
| `p4/enc_classify.p4` | MLP 加密流量分类 | 不解密、4 特征 4 类（Web/视频/VoIP/批量）、QoS 标记、99% 准确率 |

### 设计定位

本项目是 RMT 架构的**纯软件教学/研究参考实现**——不追求线速，而是
1:1 复现硬件流水线的执行语义（VLIW 两相提交、读旧写新、PHV 全宽字段总线、
级联 trie 确定性查找），使学习者和算法研究者能在 `gdb`/`valgrind` 下
逐指令观测可编程数据面行为。

八个内置数据平面按照"**能力递进**"排列，每个新增 demo 至少多展示
一项架构特性：

| 数据平面 | 新增展示的架构特性 |
|---|---|
| MPLS | 头栈 `extract_array`（运行时决定栈深度）、多表门控 `valid(mpls[k])` |
| VXLAN | VLIW 读旧写新语义（原头搬内层）、一条流水线双方向（头有效位门控分离） |
| SRv6 | `MOVX` 索引寄存器栈（`dst=seg[SL]`）、5 元组级联 trie ACL |
| INT | `add_header` 运行时推入新头、match key 区分角色（source/transit/sink） |
| NDN | 数据面可写 extern（CS/PIT）、多级 meta 接力表达条件逻辑、payload 注入（缓存应答） |
| DNN-IDS | 神经网络 extern（定点 MLP argmax）、编译期 feature 绑定、训练→量化→下发生命周期 |
| Telescope | cache 利用率视图（epoch 老化）、推断与决策同流水线耦合（正确性）、双栈 IPv4/IPv6 |
| Enc-Classify | 多类 DNN 分类（4-class argmax）、TLS 头解析、QoS DSCP 标记联动、非目标流量旁路 |

## 环境要求

| 依赖 | 最低版本 | 说明 |
|---|---|---|
| GCC | 7.0+ | 需支持 `labels-as-values`（computed goto）、`_Atomic`、`__int128` |
| GNU Make | 3.81+ | 构建系统 |
| Linux | 4.x+ | `pthread_setaffinity_np`（绑核）、`sched_yield` |
| libc | glibc 2.17+ | `clock_gettime(CLOCK_MONOTONIC)` |
| libm | — | DNN demo 训练阶段浮点运算（`-lm`） |

无其他第三方依赖。全部源码为纯 C11，0 外部库，0 警告编译。

---

## 快速开始

```bash
./run.sh              # 编译并运行全部基础测试 (MPLS / VXLAN / SRv6)
./run.sh test-v       # 同上, 打印流水线逐级执行轨迹
./run.sh bulk 1000    # SRv6 1000 报文批量转发测试 + 吞吐统计
./run.sh mq 8         # 多队列并行: RSS 分发到 8 个绑核 worker 队列
./run.sh tcpmq 8      # TCP 按流分派: 2048 条流 5 元组哈希到 8 队列
./run.sh int          # INT 带内遥测 demo
./run.sh ndn          # NDN + in-network cache demo
./run.sh ids          # DNN 异常流量检测 demo
./run.sh telescope    # 动态网络望远镜 demo
./run.sh enc          # MLP 加密流量分类 demo
```

---

## 1. 总体架构

### 1.1 顶层结构

```
                         rmt_pipeline_t  (由 P4 编译器一次性配置)
        ┌─────────────────────────────────────────────────────────────────┐
        │                                                                 │
 pkt ──►│ ┌──────────┐   PHV   ┌────────┐ ┌────────┐ ┌────────┐ ┌────────┐│  PHV  ┌──────────┐
 bytes  │ │Programmable├──────►│ Stage 0│►│ Stage 1│►│ Stage 2│►│ Stage 3│├──────►│ Deparser │──► pkt
        │ │  Parser   │        │ M ─► A │ │ M ─► A │ │ M ─► A │ │ M ─► A ││       │          │   bytes
        │ └──────────┘         └────────┘ └────────┘ └────────┘ └────────┘│       └──────────┘
        │   解析图 FSM               │            │                        │        按头有效位
        │   bit 级提取         ┌─────┴─────┐ ┌────┴──────┐                 │        重新串接
        │   头栈 extract_array │ cache 引擎 │ │ dnn 引擎  │  (extern, 数据面 │        + 追加 payload
        │                     │ CS / PIT  │ │ Q16 MLP   │   可读写有状态)   │        (或 cache 注入内容)
        └─────────────────────┴───────────┴─┴───────────┴─────────────────┘
                                    ▲                        ▲
                       ┌────────────┴─────────┐   ┌──────────┴──────────┐
                       │  P4 编译器 (p4c.c)    │   │ 控制面 (runtime.c)   │
                       │  *.p4 → 流水线配置    │   │ table_add 表项下发   │
                       └──────────────────────┘   │ 训练/量化模型下发     │
                                                  └─────────────────────┘
```

### 1.2 单级 Match-Action 内部结构

每一级物理 stage 的微结构（`pipeline.c` 主循环 + `match.c` + `alu.c`）：

```
              ┌──────────────────── Stage k ────────────────────────────┐
              │                                                         │
   PHV ──────►│ ① 门控 gate                                             │
              │    table.pred_hdr 头无效? ──是──► 本表跳过 (PHV 直通)      │
              │            │否                                           │
              │            ▼                                             │
              │ ② Key 选择器                                             │
              │    从 PHV 取出 keys[0..nkeys-1] 声明的字段值               │
              │    (最多 8 个 key 字段, 如 5 元组)                        │
              │            ▼                                             │
              │ ③ Match: 级联 bit-trie 查找引擎                          │
              │    每字段一棵二叉 trie, exact/LPM, 跨字段最长前缀回溯       │
              │    命中 ──► entry { action_id, params[8] }               │
              │    未命中 ─► default_action + def_params                 │
              │            ▼                                             │
              │ ④ Action: VLIW 微码执行 (rmt_action_exec)                │
              │    word0: │ALU0│ALU1│ ... │ALU15│  ≤16 条并行, 读旧写新   │
              │    word1: │ALU0│ ...             (RAW/WAW 依赖顺延)      │
              │     ...   (每 action ≤8 个 word)                         │
              │    立即数端口可接 entry.params (action data)              │
              │    ┌───────────────┬───────────────┐                     │
              │    │ cache extern  │  dnn extern   │  ← 有状态指令        │
              │    │ lookup/insert │  infer(MLP)   │    直接读写引擎       │
              │    └───────────────┴───────────────┘                     │
              │            ▼                                             │
              │ ⑤ 统一写回: 所有结果同拍锁存进 PHV                         │
              └──────────────────────────┬──────────────────────────────┘
                                         ▼
                              PHV' ──► Stage k+1   (meta.drop==1 则终止)
```

关键设计参数（`include/rmt.h`）：

| 常量 | 值 | 含义 |
|---|---|---|
| `RMT_MIN_STAGES` / `RMT_MAX_STAGES` | 4 / 8 | 物理流水级数（至少 4 级）|
| `RMT_NUM_ALUS` | 16 | 每级 ALU 数 = VLIW 宽度 |
| `RMT_MAX_WORDS` | 8 | 每个 action 最多 VLIW word 数 |
| `RMT_MAX_KEYS` | 8 | 每表最多 key 字段（覆盖 5 元组）|
| `RMT_MAX_FIELDS` / `RMT_MAX_HDRS` | 160 / 40 | PHV 字段 / 头实例上限 |
| `RMT_MAX_CACHES` / `RMT_MAX_DNNS` | 4 / 2 | 每流水线 cache / dnn extern 数 |

### 1.3 多队列并行引擎 (mq.c)

单条流水线之上的横向扩展层，模拟多队列网卡 + DPDK run-to-completion
模型：分发线程按流哈希把报文指针散列到 Q 条无锁环，每条环由一个
绑核 worker 独占消费（队列:线程 = 1:1）：

```
                    ┌─ ring 0 (SPSC 无锁环) ─► worker 0 (绑核) ─┬─ rmt_process_burst()
 pkt* ─► 分发线程   ├─ ring 1              ─► worker 1        ─┤   → 逐包回调 on_done()
  rmt_rss_flow_hash ├─ ...                                     ─┤
  (5 元组 hash % Q)  └─ ring Q-1            ─► worker Q-1      ─┘
```

- **同流同队列**：`rmt_rss_flow_hash()` 对 5 元组散列，同一条流永远
  落在同一 worker 上 —— 无锁即可保证流内包序；
- **SPSC 环零拷贝**：环内只传 8 字节指针，head/tail 分离缓存行，
  只用 acquire/release，无 CAS；
- **规模**：最多 `MQ_MAX_WORKERS=128` 队列/线程（mq.c），环容量 2^order
  可配；worker 数超过核数时按 `id % nproc` 取模共享核，配合自适应
  退让（spin→yield）不会饿死分发线程；
- 流水线配置本身只读，所有 worker 共享同一条 `rmt_pipeline_t`；
  cache/dnn extern 内部自带锁，多队列下也是安全的。

详细优化手段与性能数据见 §9 第三轮；API 用法见 §9 / §10。

### 1.4 目录结构

```
rmt-pipeline/
├── include/rmt.h         全部核心数据结构与模块接口
├── src/
│   ├── phv.c             PHV 字段值 (128-bit 大端) 位级操作
│   ├── parser.c          可编程解析器 FSM + 逆解析器
│   ├── match.c           tree 查找引擎 (级联 bit-trie)
│   ├── alu.c             16-ALU VLIW 执行器 (读旧写新语义 + extern 指令)
│   ├── cache.c           in-network cache 引擎 (CS/PIT, 数据面可写)
│   ├── dnn.c             DNN 推理引擎 (Q16.16 定点 MLP)
│   ├── pipeline.c        流水线编排: parse → stages → deparse
│   ├── mq.c              多队列并行引擎 (RSS 分发 + SPSC 无锁环 + 绑核 worker)
│   ├── p4c.c             P4 编译器 (词法/语法/代码生成/VLIW 调度)
│   ├── runtime.c         控制面: 表项文件解析与下发
│   ├── main.c            独立 SRv6 演示 (bin/rmt_demo)
│   ├── tests.c           三套端到端测试 + 批量/并发/多队列测试 (bin/rmt_tests)
│   ├── int_demo.c        INT 带内遥测 demo (bin/int_demo)
│   ├── ndn_demo.c        NDN 缓存 demo (bin/ndn_demo)
│   ├── ids_demo.c        DNN 异常检测 demo (bin/ids_demo)
│   ├── telescope_demo.c  动态网络望远镜 demo (bin/telescope_demo)
│   ├── enc_classify_demo.c  MLP 加密流量分类 demo (bin/enc_classify_demo)
│   └── test_match.c     match 表单元测试 (bin/test_match)
├── p4/                   示例 P4 程序 (mpls/vxlan/srv6/int/ndn/ids/telescope/enc_classify)
├── entries/              对应的控制面表项文件
├── Makefile
└── run.sh                一键构建/测试脚本
```

---

## 2. 数据流

一个报文经过 `rmt_process()` 的完整路径：

```
 in: rmt_pkt_t {data[], len, in_port}
  │
  ▼
 ①解析  rmt_parse()
  │  从 start 状态开始沿解析图走:
  │    extract(hdr)           按字段位宽提取到 PHV (字节对齐走 memcpy 快路径), 置 hvalid 位=1
  │    extract_array(stk, n)  头栈批量提取, n = PHV 字段值 + 常数 (运行时决定)
  │    select(field){...}     按已提取字段值选择下一状态 (default 兜底)
  │  返回已消费的头部字节数 hdr_bytes, 剩余为 payload
  │  (payload 指针暴露给 phv.pl, 供 cache extern 的 cs_insert 取内容)
  ▼
 ②PHV  phv_t { fields[160]: fv_t(128-bit 大端右对齐), hvalid: uint64_t 位图 }
  │      = 流水线各级共享的"寄存器堆"
  │      intrinsic metadata: meta.ingress_port / meta.timestamp 自动填充
  ▼
 ③逐级 Match-Action  (stage 0..N-1, 每级依次应用分配到该级的表)
  │   ┌─ 门控:  table.pred_hdr 无效 → 跳过该表 (来自 P4 的 if(valid(h)))
  │   ├─ Match: rmt_table_lookup() 在级联 bit-trie 中查 PHV key 字段
  │   │         命中 → 表项 entry {action_id, params[]}
  │   │         未命中 → default_action
  │   └─ Action: rmt_action_exec() 按 VLIW word 顺序执行微码
  │             每 word ≤16 条指令: 先全部读源、算结果, 再统一写回
  │             (模拟 16 个硬件 ALU 同拍锁存, WAR 天然安全)
  │   任一时刻 meta.drop == 1 → 丢包, 提前结束
  ▼
 ④逆解析  rmt_deparse()
  │  按头声明顺序串接所有 hvalid==1 且非 metadata 的头 (bit 级拼接)
  │  → 支持 pop(INVALID 后自动消失)/push(VALID 后自动出现)
  │  payload: 默认原样追加; 若 cs_lookup 命中 (phv.has_inject),
  │  则改为追加缓存注入内容 (NDN 本地 Data 应答)
  ▼
 out: rmt_pkt_t {data[], len, out_port = meta.egress_port, dropped}
```

### PHV 字段值格式

```
fv_t: v[16] = 128-bit 大端整数, 右对齐; bitlen = 字段位宽
  例: ipv6.dst (128b) 占满 v[0..15]
      mpls.label (20b) 位于 v[13] 低 4 位 + v[14] + v[15]
```

所有 ALU 运算按 128-bit 大端语义进行，写回时用 `fv_mask()` 截断到字段
位宽 —— 与硬件中定宽字段总线的行为一致。

---

## 3. Match 的实现 (match.c)

### 3.1 Match 选项

本实现支持两种匹配类型（每个 key 字段独立选择）：

| 匹配类型 | P4 写法 | 语义 | 典型用途 |
|---|---|---|---|
| `exact` | `h.f : exact;` | 表项前缀长度必须 == 字段全宽 | VNI、端口号、协议号、pkt_type |
| `lpm` | `h.f : lpm;` | 最长前缀匹配，`/0` 即通配 | IPv4/IPv6 路由、NDN 名字前缀 |

- 一张表最多 **8 个 key 字段**级联，`exact` 与 `lpm` 可混用 ——
  5 元组 ACL 即 `lpm + lpm + exact + exact + exact`；
- `ternary`（任意掩码）未实现，可用 lpm+exact 组合近似；
- 每表还有一个**门控条件**（`if (valid(h))` 编译为 `pred_hdr`），
  在查表之前判定，头无效则整表跳过 —— 这是方向/角色分离的关键机制
  （VXLAN 两个方向、NDN Interest/Data 共用一条流水线）。

### 3.2 数据结构：级联二叉 bit-trie

```
      key0 的 trie (如 ipv6.src)          key1 的 trie (如 ipv6.dst)
            root ────────────────────┐
           /    \                    │
          0      1                   ▼
         / \      \  next_root →   root ─ ... ─→  keyN 的 trie
        ...  ●(prefix end)                          叶: entry 链表
```

节点 `trie_node_t { b[0], b[1], next_root, entry }`：
- `b[0]/b[1]`：按 key 值的下一 bit 走向子节点；
- `next_root`：本字段前缀在此结束，进入**下一个 key 字段**的树根；
- `entry`：最后一个字段的前缀终点，挂表项链表（同 key 先插入者优先）。

### 3.3 插入 (rmt_table_insert)

对每个 key 字段：取表项的前缀长 `plen`（exact 强制 = 字段宽），沿
key 值高位起逐 bit 走/建节点共 `plen` 层；非最后字段则经 `next_root`
切换到下一字段的树；最后字段处把 `entry{action_id, params}` 挂到
终点节点。

### 3.4 查找 (rmt_table_lookup)：最长前缀优先回溯

```
trie_walk(root, kidx):
  沿 PHV 中该字段值的 bit 序列从 root 下走, 记录路径上所有
  "可行点" (挂有 next_root 或 entry 的节点) 到 cand[]
  从最深的可行点开始回溯:
     - 最后一个字段: 直接返回 entry  (最深 = 最长前缀)
     - 否则: 递归进入 cand[i]->next_root 匹配下一字段,
             失败则退到更短前缀的可行点再试
```

- 单字段即标准 LPM；多字段时语义为**字典序的最长前缀优先**
  （前面字段的更长前缀优先于后面字段）；
- exact 字段只有"全宽命中"一个可行点，天然退化为哈希表语义；
- 复杂度：O(∑ 字段位宽)，与表项数无关（对应硬件 TCAM/SRAM 的
  确定性查找延迟）。

### 3.5 Match 结果

命中返回 `rmt_entry_t*`，其中 `action_id` 选择动作微码、
`params[8]` 作为 action data 接到 ALU 立即数端口；未命中则用表的
`default_action / def_params`。

---

## 4. Action 的实现 (alu.c + p4c 调度)

### 4.1 指令集（三地址 RISC，PHV 即寄存器堆）

| 指令 | 语义 | P4 原语来源 |
|---|---|---|
| `MOVI / MOV` | fd = imm / fs1 | `set(dst, 常量/参数)` / `set(dst, 字段)` |
| `ADD/SUB/AND/OR/XOR/SHL/SHR` (+`*I` 立即数变体) | fd = fs1 op fs2/imm | `add/sub/and/or/xor/shl/shr` |
| `MOVX` | fd = phv[fbase + phv[fs1]×stride]（带界检查）| `copy_indexed(dst, stk.f, idx)` —— SRv6 `dst=seg[SL]` |
| `VALID / INVALID` | 置/清头有效位 | `add_header / remove_header` —— push/pop 头 |
| `CS_LOOKUP` | 查 cache[imm]，命中: fd(cs_hit)=1 + 内容→inject | `cs_lookup(cs, name)` |
| `CS_INSERT` | payload 按 key 存入 cache[imm] | `cs_insert(cs, name)` |
| `PIT_INSERT` | key→fs2(入端口)，重复: fd(drop)=1（兴趣聚合）| `pit_insert(pit, name)` |
| `PIT_CONSUME` | 取出并删除: fd(egress)=端口，无: drop=1 | `pit_consume(pit, name)` |
| `UTIL_MARK` | 地址 key 标记"在用"@当前 epoch（插入/刷新）| `util_mark(util, ipv4.src)` |
| `UTIL_PROBE` | 非破坏探测: 在用且未老化 fd(used)=1 | `util_probe(util, ipv4.dst)` |
| `DNN_INFER` | 收集绑定特征字段→定点 MLP→fd=argmax 类别 | `dnn_infer(net, meta.class)` |

`use_param/pidx`：立即数端口可改接**表项 action data**，同一份微码
被不同表项以不同参数复用（如 `fwd(port)` 的端口号）。

### 4.2 VLIW 并行执行模型（两相提交）

```
一个 VLIW word (≤16 条指令) 的执行:
  相位 1 execute: 16 个 ALU 全部读源操作数、计算, 结果暂存 wb[]
  相位 2 commit : 所有结果统一写回 PHV (同拍锁存)
```

- **读旧写新**：同 word 内后写的字段不影响先读 —— 与硬件 ALU 在同一
  时钟沿锁存操作数完全一致；WAR 天然安全；
- 例：`set(udp.len, ipv4.len)` 与 `add(ipv4.len, 50)` 同 word，
  udp.len 拿到旧值（VXLAN 封装依赖此语义）；
- MAC 交换必须 3 条 mov 经 scratch 字段、跨 2 个 word 完成
  （NDN `cs_reply`），因为 RAW 依赖会被调度器推迟。

### 4.3 编译期 VLIW 调度 (p4c.c sched())

对 action 中每条新指令做依赖分析后装入最早可行的 word：

| 依赖 | 判定 | 处理 |
|---|---|---|
| RAW | 读到更早指令写的字段 | 排到该指令的下一 word |
| WAW | 与更早指令同目的字段 | 排到下一 word |
| WAR | 同 word 读旧写新语义 | **无需处理**，可同 word |

word 内 16 槽满则顺延；extern 无写回指令（fd<0）不产生伪依赖。
调度效果示例（`vxlan_encap`，34 条原语 → 3 个 word）：

```
w0: 16 槽全满  4×validate + 12×字段拷贝 (相互独立, 16 ALU 并行)
w1: 16 槽全满  其余拷贝 + 外层字段改写
w2: 2 槽       addi udp.len (RAW 依赖 w1 的 mov udp.len) 等
```

### 4.4 有状态 extern

普通 match 表只能控制面写；两类 extern 是**数据面指令直接读写**的
有状态引擎，挂在 pipeline 上、由 P4 声明：

- **cache 引擎 (cache.c)**：`cache cs(64);` —— 名字键值存储，
  FNV-1a 哈希 + 线性探测(8) + 最老序号淘汰 + 每实例自旋锁；
  支撑 NDN 的 Content Store 与 PIT（见 §6.3）；同一引擎的
  **利用率视图**（mark/probe + epoch 老化）支撑动态网络望远镜
  （见 §6.5）；
- **dnn 引擎 (dnn.c)**：`dnn ids(f1, f2, ...);` —— 声明时绑定
  输入特征字段；Q16.16 定点 MLP（1 隐层 ReLU + argmax），前向纯
  整数运算；控制面浮点训练后 `rmt_dnn_load()` 量化下发
  （详细架构见 §6.4）。

---

## 5. P4 编译器架构 (p4c.c)

单遍（single-pass）编译器：手写词法器 + 递归下降语法分析，边分析边
直接生成流水线配置（无独立 IR）。

```
 *.p4 源文件
   │
   ▼
 ┌──────────┐  token 流  ┌───────────────────────────────────────────┐
 │ 词法器    ├──────────►│ 递归下降语法分析 + 直接代码生成              │
 │ next()   │            │                                           │
 │ id/num/  │            │ header/metadata ──► 符号表                 │
 │ punct/   │            │   hdr_def_t / field_def_t / arr_def_t     │
 │ 注释跳过  │            │   (头栈 h[N] 展开为 N 个实例 h[0..N-1])    │
 └──────────┘            │                                           │
                         │ cache/dnn 声明 ──► extern 实例 + 字段绑定   │
                         │                                           │
                         │ parser 块 ──────► 解析图 parse_state_t     │
                         │   extract / extract_array / select / goto │
                         │                                           │
                         │ action 块 ──────► ALU 微码 + VLIW 调度      │
                         │   原语→指令映射见 §4.1                     │
                         │   sched(): RAW/WAW 列表调度 (§4.3)         │
                         │                                           │
                         │ table 块 ──────► rmt_table_t              │
                         │   key {field: exact|lpm} / actions /      │
                         │   default_action                          │
                         │                                           │
                         │ control ingress ► 物理级分配 + 门控        │
                         │   apply(t) 按出现顺序分配 stage 0,1,2...   │
                         │   if (valid(h)) { apply(t) } → t.pred_hdr │
                         └───────────────────┬───────────────────────┘
                                             ▼
                         rmt_finalize(): 缓存 meta.drop/egress_port/
                         ingress_port/timestamp 字段 id, 校验级数 ≥ 4
```

### 支持的 P4 子集语法

```p4
header h { f1 : 16; f2 : 8; }        // 任意位宽字段
header stk[4] { ... }                // 头栈 (实例 stk[0..3])
metadata meta { ... }                // 元数据, 不参与逆解析

cache cs(64);                        // in-network cache extern
dnn net(h.f1, h.f2);                 // DNN extern, 绑定特征字段

parser st {
    extract(h);                      // 也支持 extract(stk[0])
    extract_array(stk, f + 1);       // 运行时数量
    select(h.f1) { 0x86dd: st2; default: accept; }
    goto accept;                     // 无条件跳转
}

action a(p1, p2) {                   // 参数 = 表项 action data
    set(h.f1, p1);   set(h.f1, h2.f);
    add(h.f1, 10);   sub(h.f1, h2.f);      // and/or/xor/shl/shr 同
    copy_indexed(ipv6.dst, seg.sid, srh.segments_left);
    add_header(h);   remove_header(stk[0]);
    cs_lookup(cs, h.f1);  pit_consume(pit, h.f1);
    dnn_infer(net, meta.class);
}

table t {
    key     { h.f1 : exact; h.f2 : lpm; }  // 最多 8 个 key 字段
    actions { a; nop; }
    default_action nop();
}

control ingress {
    apply(t0);                       // 依次分配 stage 0,1,2,...
    if (valid(h)) { apply(t1); }     // 编译为表门控
}
```

约定字段：`meta.drop`（置 1 即丢包）、`meta.egress_port`（出端口）、
`meta.ingress_port` / `meta.timestamp`（声明即由流水线入口自动填充）。

---

## 6. 转发处理与 Match-Action 映射

本节给出全部七个数据平面的 **每一级功能、match key
与匹配选项、动作微码** 的完整映射以及报文逐级处理过程。前五个为完整
报文走查，MPLS / INT 为简表形式。

### 6.1 SRv6 End (`p4/srv6.p4`)

**级映射总表**

| stage | 表 | 门控 | Match key (选项) | 动作 | 本级功能 |
|---|---|---|---|---|---|
| 0 | `local_sid` | `valid(srh)` | `ipv6.dst : lpm` | `srv6_end` / nop | "My SID" 判定：命中本机 SID 则执行 End 行为 |
| 1 | `route` | — | `ipv6.dst : lpm` | `forward(port)` / drop | 对**新 dst** 做 IPv6 路由，得出端口 |
| 2 | `acl` | `valid(udp)` | `ipv6.src : lpm`<br>`ipv6.dst : lpm`<br>`ipv6.next_hdr : exact`<br>`udp.sport : exact`<br>`udp.dport : exact` | permit / deny | **5 元组分类**（五棵 trie 级联匹配）|
| 3 | `mac_rewrite` | — | `meta.egress_port : exact` | `set_macs` | 出端口 → 下一跳 MAC 改写 |

**报文逐级处理**

```
in:  eth │ ipv6(dst=SID1, hlim=64) │ srh(SL=2) │ seg[0..2] │ udp │ payload
      │
 parser: extract eth → ipv6 (next_hdr=43) → srh
         extract_array(seg, srh.last_entry+1)   ← 栈深度运行时决定
         → udp
      ▼
 stage0 local_sid: lpm(ipv6.dst) 命中 fd00::a1/128 → srv6_end
         w0: │sub SL,1│sub hlim,1│          ← 2 ALU 并行 (无依赖)
         w1: │MOVX ipv6.dst = seg[SL].sid│  ← RAW 依赖 w0 的新 SL
      ▼
 stage1 route: lpm(新 dst=SID2) → forward(2): egress_port=2
      ▼
 stage2 acl: 5 棵 trie 依次匹配 (src,dst,proto,sport,dport)
         命中 deny 规则 → meta.drop=1 → 流水线终止
         否则 permit (default) → 继续
      ▼
 stage3 mac_rewrite: exact(port=2) → set_macs(下一跳 MAC)
      ▼
 deparse: 头字段回写 (seg 栈原样保留), payload 追加
out: eth'│ ipv6(dst=SID2, hlim=63) │ srh(SL=1) │ seg │ udp │ payload
```

要点：End 行为的"先 SL-1、再取 seg[新 SL]"由 VLIW 调度器自动拆成
两拍；`MOVX` 的栈索引 `fbase + idx*stride` 带界检查（SL 越界则不写）。

### 6.2 VXLAN 网关 (`p4/vxlan.p4`)

**级映射总表**

| stage | 表 | 门控 | Match key (选项) | 动作 | 本级功能 |
|---|---|---|---|---|---|
| 0 | `vxlan_term` | `valid(vxlan)` | `vxlan.vni : exact` | `vxlan_decap` / drop / nop | VTEP 终结：本地 VNI → 剥外层 4 个头 |
| 1 | `tenant` | `valid(ipv4)` | `ipv4.dst : lpm` | `vxlan_encap(vni,sip,dip)` / nop | 租户前缀 → 封装（构建外层 + 原头搬入内层）|
| 2 | `l2` | `valid(ieth)` | `ieth.dmac : exact` | `set_port` / nop | overlay 二层转发（解封装后的内层帧）|
| 3 | `underlay` | `valid(ipv4)` | `ipv4.dst : lpm` | `route_underlay(port,dmac,smac)` / drop | underlay 路由（封装后的外层报文）|

方向分离完全靠**头有效位门控**，无显式方向标志：

**解封装方向（overlay 进）**

```
in:  eth │ ipv4 │ udp(dport=4789) │ vxlan(vni) │ ieth │ iipv4 │ payload   (88B)
 parser: 一路解析到内层 (udp.dport=4789 → parse_vxlan → 内层)
 stage0 vxlan_term: exact(vni=5001) HIT → vxlan_decap
         w0: │INVALID eth│INVALID ipv4│INVALID udp│INVALID vxlan│ (4 ALU 并行)
 stage1 tenant:   gate valid(ipv4)=0 (刚失效) → 跳过
 stage2 l2:       exact(ieth.dmac) → set_port(3)
 stage3 underlay: gate valid(ipv4)=0 → 跳过
 deparse: 只有 ieth/iipv4 有效 → 输出内层帧
out: ieth │ iipv4 │ payload   (38B)
```

**封装方向（tenant 进）**

```
in:  eth │ ipv4(dst=10.1.x.x) │ payload   (42B, 无 vxlan)
 stage0 vxlan_term: gate valid(vxlan)=0 → 跳过
 stage1 tenant: lpm(ipv4.dst=10.1.0.0/16) HIT → vxlan_encap(vni,sip,dip)
         w0: 16 槽全满 = 4×VALID(udp/vxlan/ieth/iipv4)
             + 12×mov (原 eth/ipv4 字段 → 内层槽, 读旧值安全)
         w1: 16 槽全满 = 其余拷贝 + 外层改写 (src/dst/ttl/proto=17,
             udp.sport/dport=4789, vxlan.flags/vni, mov udp.len←ipv4.len,
             addi ipv4.len,50)
         w2: addi udp.len,30 (RAW 依赖 w1 的 mov)
 stage2 l2: gate valid(ieth)=1 (刚生效) 但 dmac 无表项 → nop
 stage3 underlay: lpm(外层新 dst=dip) → route_underlay(2, 外层 MAC)
 deparse: eth│ipv4│udp│vxlan│ieth│iipv4 全部有效 → 串接
out: eth'│ ipv4(len+50) │ udp(4789) │ vxlan │ ieth │ iipv4 │ payload   (92B)
```

要点：同一张表在两个方向自动"消失"（门控），无需两条流水线；
"原头搬内层 + 原槽改外层"依赖同 word 读旧写新语义（§4.2）。

### 6.3 NDN 命名数据网络 (`p4/ndn.p4`)

按**内容名字**而非主机地址转发；两个 cache extern 是数据面可写的
有状态存储：

```
cache cs(64);    Content Store : name → 内容字节   (在途缓存)
cache pit(64);   Pending Interest Table : name → 入端口 (面包屑)
```

**级映射总表**

| stage | 表 | 门控 | Match key (选项) | 动作 | 本级功能 |
|---|---|---|---|---|---|
| 0 | `cs_t` | `valid(ndn)` | `ndn.pkt_type : exact` | `cs_check`(Interest) / `cs_store`(Data) | Interest 探缓存；Data 途经即缓存 |
| 1 | `cs_hit_t` | `valid(ndn)` | `ndn.pkt_type : exact`<br>`meta.cs_hit : exact` | `cs_reply` / nop | 缓存命中的 Interest → 本地转为 Data 应答 |
| 2 | `pit_t` | `valid(ndn)` | `ndn.pkt_type : exact`<br>`meta.cs_hit : exact` | `pit_add`(Interest) / `pit_serve`(Data) | Interest 留面包屑（重复→聚合丢弃）；Data 反向路径 |
| 3 | `fib_t` | `valid(ndn)` | `ndn.pkt_type : exact`<br>`meta.cs_hit : exact`<br>`ndn.name : lpm` | `fwd(port)` / drop / nop | **名字 LPM 转发**：128 位名字（4×32 位分量），/32 匹配首分量 |

动作没有分支能力，Interest/Data、命中/未命中的判断全部拆解为
**级间靠 `meta.cs_hit` + `pkt_type` 做 match key** —— 这正是
match-action 架构表达条件逻辑的标准方式。

**三种报文的逐级处理**

```
① Interest 未命中 (type=1)
 stage0 cs_t(1)→cs_check: CS_LOOKUP miss, cs_hit 保持 0
 stage1 cs_hit_t(1,0) 无表项 → nop
 stage2 pit_t(1,0)→pit_add: PIT_INSERT name→ingress_port
        (同名已挂起 → meta.drop=1, 兴趣聚合, 不再上行)
 stage3 fib_t(1,0,name)→ lpm /video··/32 → fwd(2): hop_limit-1
out: Interest 沿 FIB 上行; 无路由 → /0 兜底 drop

② Data 返回 (type=2)
 stage0 cs_t(2)→cs_store: CS_INSERT name→payload  ← 途经节点全部缓存
 stage1 nop      stage3 nop (Data 不查 FIB)
 stage2 pit_t(2,0)→pit_serve: PIT_CONSUME
        命中面包屑 → meta.egress_port=记录的入端口, 删除表项
        无面包屑   → meta.drop=1 (未经请求的 Data 丢弃)
out: Data 沿 PIT 反向路径逐跳回到消费者

③ Interest 命中缓存 (type=1, 边缘节点)
 stage0 cs_check: CS_LOOKUP HIT → meta.cs_hit=1, 内容→phv.inject
 stage1 cs_hit_t(1,1)→cs_reply:
        w0: │movi pkt_type,2│mov tmp←dmac│mov dmac←smac│mov egress←ingress│
        w1: │mov smac←tmp│                 ← 3-mov MAC 交换跨 2 word
 stage2/3: key(1,1) 无表项 → nop (不再进 PIT/FIB)
 deparse: 头重建 + 用 inject 缓存内容替换 payload
out: 本地生成的 Data 应答原路返回, 上游/生产者完全无感知
```

### 6.4 DNN 异常检测 IDS (`p4/ids.p4`)

表项里**零攻击签名**——`entries/ids.txt` 只有 "TCP 就送去推理"、
"class=1 就隔离" 两条通用规则，SYN flood / Xmas scan / NULL scan
的检出完全来自网络内的神经网络。

**级映射总表**

| stage | 表 | 门控 | Match key (选项) | 动作 | 本级功能 |
|---|---|---|---|---|---|
| 0 | `ids_t` | `valid(tcp)` | `ipv4.proto : exact` | `classify` / `nop` | TCP → `dnn_infer(ids, meta.class)` 写判决 |
| 1 | `verdict_t` | `valid(tcp)` | `meta.class : exact` | `quarantine` / `nop` | class=1 → drop（隔离），class=0 → 放行 |
| 2 | `route` | `valid(ipv4)` | `ipv4.dst : lpm` | `forward(port)` / `drop_pkt` | 幸存报文正常 L3 转发（ttl-1） |

非 TCP 报文两级门控都不命中，直接落到 stage 2 纯转发——IDS 对
无关流量零干扰。

**网络模型架构（dnn extern, dnn.c）**

```
            特征绑定 (P4 声明: dnn ids(ipv4.len, ipv4.ttl,
                                       tcp.flags, tcp.window);)
 PHV ──gather──►  x[4] (原始整数值)
                   │  归一化: xq[i] = (x[i] << 16) / scale[i]   Q16.16
                   ▼
         ┌───────────────────────┐
         │ 隐层  16 神经元        │   h[j] = ReLU(Σ w1[j][i]·xq[i] + b1[j])
         │ w1: 16×4  b1: 16      │   乘累加在 Q32.32, >>16 回 Q16.16
         ├───────────────────────┤
         │ 输出层  2 类 (线性)    │   z[k] = Σ w2[k][j]·h[j] + b2[k]
         │ w2: 2×16  b2: 2       │   无 softmax —— argmax 只需比大小
         └───────────┬───────────┘
                     ▼
            meta.class = argmax(z)     0 = 正常, 1 = 异常
```

- **拓扑 4-16-2**（demo 训练所得）；引擎上限 `RMT_DNN_MAX_IN=8` 输入、
  `HID=32` 隐层、`OUT=4` 类，每流水线至多 `RMT_MAX_DNNS=2` 个实例；
- **纯整数前向**：权重/偏置量化为 Q16.16 定点 (`int32_t`)，乘累加用
  64 位整数——即硬件中挂在 match-action 级旁边的 MAC 阵列所做的运算，
  数据面不出现任何浮点；
- **归一化进模型**：`scale[i]` 是每个特征的原始除数
  （len/1500、ttl/256、flags/256、window/65536），随权重一起下发；
- **并发安全**：推理只读模型，多线程报文可同时 infer，仅统计计数
  （infers / class_cnt）加互斥锁。

**训练→量化→下发→在线推理 生命周期**

```
控制面 (ids_demo.c, float)                 数据面 (流水线, 整数)
──────────────────────────                ────────────────────────
合成带标签流量                              
 (正常: web/握手/挥手                       
  攻击: SYN flood/Xmas/NULL)                
   │ SGD + softmax 交叉熵, 30 epoch          
   ▼                                        
 float MLP  (留出集精度 > 97%)               
   │ rmt_dnn_load(): 逐权重 ×2^16 四舍五入    
   ▼                                        
 Q16.16 参数 ──────── 量化下发 ────────►  dnn extern (float/定点判决
                                          一致率 > 99%)
                                            │ 每 TCP 报文 stage 0
                                            ▼
                                          DNN_INFER → meta.class
```

**`DNN_INFER` 指令数据通路（alu.c）**

```
OP_DNN_INFER  imm = extern 编号, fd = meta.class
  1. 按声明时绑定的 in_fid[] 从 PHV 收集 4 个特征字段 → x[4]
  2. rmt_dnn_infer(dnn, x): 定点前向 + argmax
  3. 返回类别经两相提交写回 fd —— 与普通 ALU 指令同一 word 语义
```

**报文过程（SYN flood 报文 len=40, ttl=47, flags=0x02, win=128）**

```
in: eth|ipv4(proto=6)|tcp    54B
 parse : eth → ipv4 → tcp, 三头 valid
 stage0: ids_t 命中 proto=6 → classify
         [alu0  w0] dnn_infer meta.class = 0x01     ← 网络判定异常
 stage1: verdict_t 命中 class=1 → quarantine: meta.drop=1
 stage2: meta.drop 短路, 整级不再进入
out: dropped  (同特征的合法握手 SYN win=64240 → class=0, 正常转发)
```

验证结果（`./run.sh ids`）：逐类攻击单包检出、合法握手无误报、
1000 包混合流检出率 ≥ 97%、误报率 ≤ 2%。

### 6.5 动态网络望远镜 (`p4/telescope.p4`)

MORP4 风格的动态网络望远镜：部署在组织网络唯一出入口
（端口 1 = 上联/Internet，端口 2 = 内部 LAN，端口 9 = 捕获采集器），
在数据面内**在线推断哪些地址未被使用**，只捕获打向未用地址的
不请自来流量（IBR）：

- 出向报文：源地址活着 → `util_mark` 盖 epoch 戳；
- 入向报文：目的地址 `util_probe` —— 在用→正常转发（机密流量，
  绝不捕获），未用→重定向到采集器。

**级映射总表**

| stage | 表 | 门控 | Match key (选项) | 动作 | 本级功能 |
|---|---|---|---|---|---|
| 0 | `dir_t` | — | `meta.ingress_port : exact` | `set_dir(d)` | 入链路定方向（1=出向, 0=入向） |
| 1 | `mark4_t` | `valid(ipv4)` | `meta.dir : exact` | `mark4` | 出向：`util_mark(util4, ipv4.src)` |
| 2 | `probe4_t` | `valid(ipv4)` | `meta.dir : exact` + `ipv4.dst : lpm` | `probe4` | 入向且 dst 在监测空间：scoped=1 + `util_probe(util4, ipv4.dst)` |
| 3 | `mark6_t` | `valid(ipv6)` | `meta.dir : exact` | `mark6` | 同 stage 1，IPv6 |
| 4 | `probe6_t` | `valid(ipv6)` | `meta.dir : exact` + `ipv6.dst : lpm` | `probe6` | 同 stage 2，IPv6（静态望远镜在 IPv6 下不可行，动态推断可行） |
| 5 | `decide_t` | — | `meta.dir + meta.scoped + meta.used (全 exact)` | `capture(9)` / `fwd(port)` | (0,1,0)→捕获；(0,1,1)/(0,0,0)→LAN；(1,0,0)→上联 |

**正确性关键**：利用率推断（stage 1/3）与捕获决策（stage 2/4/5）
耦合在**同一条流水线**里：某地址的第一个出向包在数据面内瞬时把它
置为"在用"，后续任何入向包查到的已是最新状态 —— 新激活地址的
机密流量在任何情况下都不会被误捕（无跨设备同步窗口）。

**控制面旋钮**（时间/空间粒度）：

- 空间粒度：监测前缀在表项里（`10.9.0.0/16`、`2001:db8:9::/48`），
  探测到单地址 /32、/128 粒度 —— 未用地址与在用地址混杂也能回收；
- 时间粒度：`rmt_cache_set_aging(util, ttl)` + `rmt_cache_tick(util)`
  —— 连续 ttl 个 epoch 无出向流量的地址自动老化回望远镜；反拉黑：
  望远镜面积随主机开关机每时每刻伸缩，地址集无法被静态拉黑。

**报文过程（扫描 → 激活 → 保护 → 老化）**

```
① 扫描器→10.9.1.7 (从未活跃)   dir=0, scoped=1, probe UNUSED
   → decide(0,1,0) → capture → 端口 9 (望远镜)
② 10.9.1.7 首个出向包          dir=1 → util_mark @epoch e
   → decide(1,0,0) → 上联端口 1
③ 外部回复→10.9.1.7             probe 命中 (未过 ttl) → used=1
   → decide(0,1,1) → LAN 端口 2, 绝不捕获        ← 正确性
④ 静默 > ttl 个 epoch 后         probe 发现过期 → 重新变回望远镜
```

验证结果（`./run.sh telescope`）：未用地址捕获/监测外旁路、激活即
受保护、老化后回收、复活再保护、IPv6 同源语义、/24 扫描 249 捕获
+ 5 活跃主机零误捕。

### 6.6 加密流量分类 (`p4/enc_classify.p4`)

**目标**：不解密负载，仅用 4 个可观测特征将 TLS 加密流量分为 4 类（web / video /
voip / bulk），并联动 QoS DSCP 标记实现差异化服务。

**模型架构**：

```
特征: ipv4.len ─┐
        tcp.window ─┼─→ [4×24 ReLU] ─→ [24×4 softmax/argmax] ─→ class ∈ {0,1,2,3}
        tls.length ─┼        (Q16.16 定点实现)
        tls.type   ─┘
```

**流水线过程**：

```
parser: eth → ipv4 → tcp → tls (dport=443 时解析 TLS 头 5 字节)

stage 0: classify_t (tls.type == 23 触发 DNN，其他旁路)
            └→ DNN extern 前向: 归一化 → 隐藏层 ReLU → argmax → meta.class
stage 1: qos_t    (meta.class → DSCP：0/32/46/10)
stage 2: route    (ipv4.dst LPM → egress_port)
stage 3: mac_rewrite (MAC 重写)
```

**特征归一化**：`scales[4] = {1500, 65536, 16384, 256}`，各特征除以其
理论最大值，归入 [0,1] 后转 Q16.16。

**测试结果** (`./run.sh enc`)：
- 训练保持浮点精度 98%+，量化后与浮点 100% 一致
- 单包逐类校验: web(tos=0) / video(tos=0x20) / voip(tos=0x2e) / bulk(tos=0x0a)
- 批量 4000 随机报文 99.1% 准确率
- 非 TLS 流量旁路不推理、不标记

### 6.7 其余数据平面（简表）

**MPLS (`p4/mpls.p4`)** —— 头栈逐层匹配：

| stage | 表 / key (选项) | 功能 |
|---|---|---|
| 0-2 | `mplsK` / `mpls[K].label : exact`，gate `valid(mpls[K])` | 第 K 层标签 pop / swap |
| 3 | `mac_rewrite` / `meta.egress_port : exact` | MAC 改写 |

**INT 遥测 (`p4/int.p4`)** —— 角色全由 match key 决定，同一程序跑全部交换机：

| stage | 表 / key (选项) | 功能 |
|---|---|---|
| 0 | `route` / `ipv4.dst : lpm` | 路由定出端口 |
| 1 | `int_source_t` / `dst lpm + dport exact + tos exact` | 源角色：push shim+md[0]，打 tos=0x17 标记 |
| 2 | `int_transit_t` / `tos + count + int_done (全 exact)` | 中转角色：按 count 追加 md[k] |
| 3 | `mac_rewrite` / `egress_port : exact` | MAC 改写 |

**加密流量分类 (`p4/enc_classify.p4`)** —— 不解密、统计特征 MLP 4 分类 + QoS 标记：

| stage | 表 / key | 功能 |
|---|---|---|
| 0 | `classify_t` / `tls.type : exact` | 仅对 TLS application_data (type=23) 触发 DNN 推理，结果写 meta.class |
| 1 | `qos_t` / `meta.class : exact` | 按类别打 DSCP：web=0, video=AF41, voip=EF, bulk=AF11 |
| 2 | `route` / `ipv4.dst : lpm` | LPM 路由、定出端口 |
| 3 | `mac_rewrite` / `meta.egress_port : exact` | MAC 改写 |

DNN extern 绑定 4 个特征: `ipv4.len`、`tcp.window`、`tls.length`、`tls.type`。
网络拓扑 4-24-4 (ReLU + argmax)，测试准确率 99%。

---

## 7. 控制面 (runtime.c)

表项文件格式（`entries/*.txt`，`#` 注释）：

```
table_add <表名> <动作名> <key...> => <动作参数...>
table_set_default <表名> <动作名> <参数...>
```

key / 参数自动识别多种字面量：

| 形式 | 示例 | 说明 |
|---|---|---|
| IPv6 (+前缀) | `fd00::2/128` | 128-bit key, lpm/exact |
| IPv4 (+前缀) | `192.168.1.0/24` | key 或 32-bit 参数均可 |
| MAC | `0a:0b:0c:0d:0e:02` | 48-bit |
| 十六进制/十进制 | `0x8847` / `300` | 任意宽度 |
| 长十六进制 (+前缀) | `0x76696465··/32` (32 个 nibble) | >64bit key，如 128 位 NDN 名字 |

示例（NDN 边缘节点 `entries/ndn_edge.txt`）：

```
table_add cs_t cs_check 1 =>                 # Interest: 查缓存
table_add cs_hit_t cs_reply 1 1 =>           # 命中: 本地应答
table_add fib_t fwd 1 0 0x76696465000000000000000000000000/32 => 2
table_add fib_t drop_pkt 1 0 0x000···0/0 =>  # 无路由兜底
```

DNN 模型的下发走 C API（demo 内训练→量化→`rmt_dnn_load()`），
不经表项文件。

---

## 8. 构建与测试脚本

### 8.1 构建

```bash
make            # 全部二进制 (gcc -O3 -flto -Wall -Wextra -std=c11, 0 警告)
make test       # 构建并运行 bin/rmt_tests
make clean
```

### 8.2 run.sh 一键脚本

```bash
./run.sh [target]

  all        构建 + 运行全部基础测试 (默认)
  build      仅构建
  test       基础测试 (MPLS/VXLAN/SRv6, 简洁输出)
  test-v     基础测试 + 详细流水线轨迹
  mpls/vxlan/srv6   只跑单个示例 (详细轨迹)
  bulk [N]   SRv6 批量转发测试, 默认 1000 报文
  par [T]    SRv6 并发测试: T 线程共享一条流水线, 默认 16
  mq [Q]     SRv6 多队列测试: 流哈希分发到 Q 个绑核 worker 队列, 默认 4
  tcpmq [Q]  TCP 按流分派测试: 5 元组哈希到 Q 队列, 默认 8, 最大 128
  int / int-v       INT 带内遥测 demo (3 交换机路径 + collector)
  ndn / ndn-v       NDN demo (名字转发 + 缓存命中本地应答)
  ids / ids-v       DNN 异常流量检测 demo (训练→量化→在线检出)
  telescope / telescope-v   动态网络望远镜 demo (推断未用空间→捕获 IBR)
  enc / enc-v     MLP 加密流量分类 demo (TLS 4 类: web/video/voip/bulk)
  demo       独立 SRv6 演示 (bin/rmt_demo)
  clean      清理
```

### 8.3 测试内容

每套测试均执行完整链路：**建流水线 → p4c 编译 → 加载表项 → 注入手工
构造报文 → 逐字节校验输出**。

| 套件 | 用例 | 校验点 |
|---|---|---|
| mpls | 流 A 嵌套 pop+pop+swap / 流 B 中转 swap | 7 + 6 |
| vxlan | 解封装 (88→38B) / 封装 (42→92B, 外层逐字段) | 6 + 12 |
| srv6 | End 行为 (dst/SL/hlim/port/MAC) / 5 元组 ACL 丢弃 | 7 + 1 |
| bulk | N 个变化报文 (SL 交替, 源地址/端口/载荷逐包不同) | 逐包 7 项 |
| par | T 线程并发打入同一条流水线 | 全部正确 + 加速比 |
| mq | RSS 散列到 Q 条无锁环, 绑核 worker 回调内逐包校验 | 全部正确 + 队列均衡 + 加速比 |
| tcpmq | 2048 条 TCP 流按 5 元组哈希分派 (ids.p4, 最大 128 队列) | 全部正确 + 同流同队列不变式 + 负载均衡 |
| int | 3 跳路径逐跳插桩、collector 解码、非监控流不受扰 | 11 |
| ndn | FIB 转发/PIT 反向/CS 命中应答/聚合/兜底丢弃 | 13 |
| ids | 浮点精度/量化一致性/逐类攻击检出/批量检出率与误报率 | 11 |
| telescope | 未用捕获/激活保护/老化回收/复活/IPv6/批量扫描零误捕 | 14 |
| enc_classify | 模型训练/量化一致性/逐类 QoS 标记/非 TLS 旁路/路由+MAC | 20 |

### 8.4 详细轨迹 (-v)

`./run.sh test-v` / `./run.sh ndn-v` 等可观察每一步内部执行：

```
  [parser] state parse_ndn
    [parser] extract ndn    (176 bits)
  -- stage 0 --
    table cs_t         HIT  entry#0 -> action cs_check
      [alu0  w0] cs_lookup meta.cs_hit           = 0x01
  -- stage 1 --
    table cs_hit_t     HIT  entry#0 -> action cs_reply
      [alu0  w0] movi  ndn.pkt_type              = 0x02
      [alu1  w0] mov   meta.tmp_mac              = 0xaa000000ee01
      ...
```

`[aluK wN]` 表示该指令由第 N 个 VLIW word 的第 K 号 ALU 执行 ——
可直接看到 16 ALU 的并行占用情况。

---

## 9. 性能

软件模型的热路径做了以下优化（功能语义不变，全部测试逐字节校验通过）：

**第一轮（基础优化）**

| 优化 | 位置 | 说明 |
|---|---|---|
| PHV 访问函数内联 | `rmt.h` | `fv_get_bit/fv_set_bit/fv_get_u64` 等移入头文件 `static inline`（每包数千次调用），`fv_get_u64` 改用 `bswap64` |
| 字节对齐快路径 | `parser.c` | 字段按字节对齐时（真实协议绝大多数字段）解析/逆解析直接 `memcpy`，仅 4/20-bit 等奇数宽度字段走逐 bit 路径 |
| per-stage 表索引 | `pipeline.c` | `rmt_finalize()` 预建每级表列表，去掉每包每级全表扫描 |
| 轻量 PHV 复位 | `pipeline.c` | 模板 `memcpy` 只复位实际使用的字段，不再每包清零整个 phv_t |
| 输出包免预清零 | `pipeline.c/parser.c` | 逆解析完整覆盖头部区域，仅需清理末尾非整字节位 |
| 编译选项 | `Makefile` | `-O2` → `-O3 -flto -march=native -funroll-loops` |

**第二轮（面向 100 Gbps 目标的深度优化）**

| 优化 | 位置 | 说明 |
|---|---|---|
| computed goto 分发 | `alu.c` | GCC labels-as-values 分发表替代 27 分支 switch，消除分支预测失败 |
| 原子自旋锁 | `cache.c` | `atomic_flag` + x86 `pause` 替代 pthread_mutex（临界区 <200ns 时快 10-50x） |
| hvalid 位图 | `rmt.h` | `bool[40]` → `uint64_t` 位图，头有效性检查从内存加载变为单条位测试 |
| inject 缓冲分离 | `rmt.h/pipeline.c` | 2KB 注入缓冲移出 phv_t（线程局部静态区），phv_t 栈占用 ~5KB → ~3KB，减轻 L1 压力 |
| 整头部批量提取 | `parser.c` | 头部字节对齐时全部字段一趟指针递推 + memcpy，去掉逐字段位偏移计算 |
| 迭代式 trie 查找 | `match.c` | 多 key 回溯查找由递归改为两阶段迭代（显式候选栈），去掉调用帧开销；trie 下行带硬件预取 |
| 批处理 API | `pipeline.c` | `rmt_process_batch()` 一批 64 包共享一次 `clock_gettime`，消除每包系统调用 |
| 分支提示 + restrict | 全部热路径 | `RMT_LIKELY/UNLIKELY` 优化代码布局；`restrict` 指针允许编译器更激进调度 |
| PGO 两遍构建 | `Makefile` | `make pgo`：插桩→训练→按真实分支频率重编译，额外 10-20% 提升 |

**第三轮（多队列多线程横向扩展）**

模拟多队列网卡 + DPDK run-to-completion 模型：分发线程按 RSS 哈希把报文
散列到 N 条无锁环，每条环由一个绑核 worker 独占消费。

| 优化 | 位置 | 说明 |
|---|---|---|
| SPSC 无锁环 | `mq.c` | 每队列单生产者/单消费者环，head/tail 分离缓存行，无 CAS 只用 acquire/release |
| 零拷贝交接 | `mq.c` | 环内只传 8 字节报文指针，省掉每包 ~2KB memcpy |
| RSS 流保序 | `mq.c` | `rmt_rss_hash()`（FNV-1a）对 IPv6 源地址散列：同流→同队列，天然无锁保序 |
| worker 绑核 | `mq.c` | `pthread_setaffinity_np` 每 worker 独占一核（最多 128 队列/线程，超核数时取模共享），缓存稳定、不迁移 |
| burst 处理 | `pipeline.c` | `rmt_process_burst()` 指针数组批处理，直接消费环中指针，一批共享一次时间戳 |
| 自适应退让 | `mq.c` | 空转/背压先 spin 256 次再 `sched_yield()`：核超订阅时不饿死分发线程 |
| 5 元组流哈希 | `mq.c` | `rmt_rss_flow_hash()`：解析 Eth/VLAN → IPv4\|IPv6(含 SRH 等扩展头) → TCP\|UDP，对 {源址, 宿址, 协议, 源端口, 宿端口} 散列；TCP 报文严格按流分派，非首片分片退化 3 元组保持稳定 |
| 雪崩终混 | `mq.c` | FNV-1a 后接 murmur3 fmix32 终混：消除 `hash % nqueues` 下结构化元组的队列倾斜（实测从 min 0/max 150k 修复到 45k/53k） |

使用方式：

```c
rmt_mq_t *mq = rmt_mq_create(pipe, 8 /*队列数,最大128*/, 12 /*环 2^12 槽*/, on_done, arg);
rmt_mq_start(mq);                              /* 拉起并绑核 8 个 worker */
rmt_mq_submit(mq, pkt, rmt_rss_flow_hash(pkt->data, pkt->len)); /* 按流分派 */
rmt_mq_flush(mq);                              /* 等全部处理完(含回调) */
rmt_mq_stop(mq); rmt_mq_destroy(mq);
```

SRv6 批量测试（126B 报文，VirtualBox 8 核环境，`make bench` 复测）：

| 阶段 | 单线程 per-pkt | 批处理模式 | 多线程 |
|---|---|---|---|
| 未优化 | ~6300 ns/pkt | – | 3.0x (8 线程) |
| 第一轮后 | ~2000 ns/pkt | ~1123 ns/pkt | 1.2x (VM 受限) |
| 第二轮后 (PGO) | ~933 ns/pkt | ~911 ns/pkt (≈1.1 Gbps) | 2.0x (8 线程) |
| 第三轮后 (8 队列 mq) | – | **~227 ns/pkt** | **4.6-5.2x, 2.6-4.4 Mpps (最高 ≈4.4 Gbps)** |

TCP 按流分派测试（ids.p4 数据面，54B TCP，2048 条流，400000 包，逐包校验 +
“同流只落同队列”不变式断言）：

| 队列数 | 多队列吞吐 | 队列负载均衡 |
|---|---|---|
| 8 | **125 ns/pkt, 7.97 Mpps (≈3.4 Gbps @54B), 2.97x** | min 45k / max 54k |
| 64（8 核超订阅） | 154 ns/pkt, 6.48 Mpps, 3.93x | min 3.9k / max 8.4k |
| 128（8 核超订阅） | 3.52x | min 1.4k / max 5.1k |

队列数是纯运行时参数（8/16/32/64/128 均可，上限 `MQ_MAX_WORKERS=128`）；
8 核环境下 64/128 队列为超订阅验证正确性，吞吐收益需同等核数支撑。

> 100 Gbps @ 126B ≈ 99 Mpps（≈10 ns/pkt），纯软件单线程无法达到。三轮优化
> 将软件模型推至单线程 ~1.1 Mpps、8 队列最高 ~4.4 Mpps（VM 内与 IDE 共享 8 核，
> 裸机独占核数更多时按队列数近线性扩展）；生产级 100 Gbps 需叠加：
> DPDK/AF_XDP 内核旁路零拷贝收发（本模型的 mq 分发/环/绑核结构可直接对接
> NIC 硬件 RSS 多队列）、SIMD 化 PHV（AVX-512 一次处理 4 个 128-bit 字段）、
> 以及最终落到 P4 可编程交换芯片（Tofino）或 SmartNIC 上执行——本模型的
> P4 程序与表项格式可直接迁移。

---

## 10. C 嵌入 API 速查

全部公开接口均在 `include/rmt.h`，以下是将流水线嵌入自定义程序的最小调用序列：

```c
#include "rmt.h"

/* 1. 创建 + 编译 */
rmt_pipeline_t *sw = rmt_create();
p4c_compile_file(sw, "p4/srv6.p4");   // 布局全部头/表/动作/解析图
rmt_load_entries(sw, "entries/srv6.txt"); // 下发控制面表项

/* 2. 处理报文 */
rmt_pkt_t in  = { .data = pkt_bytes, .len = 126, .in_port = 1 };
rmt_pkt_t out = {0};
rmt_process(sw, &in, &out);           // parse → stages → deparse
// out.data[]/out.len/out.out_port/out.dropped 即结果

/* 3. 销毁 */
rmt_destroy(sw);
```

**常用 API 一览**

| 功能 | 函数 | 说明 |
|---|---|---|
| 流水线生命周期 | `rmt_create / rmt_destroy` | 创建/释放全部资源（含 extern） |
| P4 编译 | `p4c_compile_file(p, path)` | 从 .p4 源码配置整条流水线 |
| 控制面下发 | `rmt_load_entries(p, path)` | 从 .txt 文件加载表项 |
| 报文处理 | `rmt_process(p, &in, &out)` | 完整 parse→stage→deparse |
| 批量处理 | `rmt_process_batch / rmt_process_burst` | 数组/指针数组批处理，一批共享时间戳 |
| 多队列并行 | `rmt_mq_create / start / submit / flush / stop / destroy` | RSS 分发 + 绑核 worker（§1.3/§9） |
| 流哈希 | `rmt_rss_flow_hash(data, len)` / `rmt_rss_hash` | 5 元组流哈希（含 VLAN/IPv6 扩展头）/ 字节段哈希 |
| Cache extern | `rmt_cache_create / put / get / take / mark / probe` | CS/PIT/利用率视图 |
| DNN extern | `rmt_dnn_create / rmt_dnn_load / rmt_dnn_infer` | 训练→量化→在线推理 |
| 符号查询 | `rmt_hdr_id / rmt_field_id / rmt_arr_id` | 按名字查头/字段 id |
| 详细轨迹 | `sw->verbose = 1` | 打印每级 match + ALU 执行细节 |

线程安全：`rmt_process` 本身只读流水线配置，多线程共享同一
`rmt_pipeline_t` 无锁并发处理是安全的（cache extern 内部有自旋锁、
dnn 统计计数有互斥锁）。
需要现成的多线程横向扩展时直接用 mq 引擎（§1.3，用法见 §9 第三轮），
它额外提供同流保序与绑核亲和性。

---

## 11. 扩展：添加新数据平面

只需 3 个文件即可新增一个完整的可编程数据平面：

```
① p4/foo.p4          P4 数据面程序
② entries/foo.txt     控制面表项
③ src/foo_demo.c      端到端验证 demo
```

**Step 1: 编写 P4 程序**

```p4
// p4/foo.p4
header eth  { dmac:48; smac:48; type:16; }
header ipv4 { ... }
metadata meta { ingress_port:16; egress_port:16; drop:1; }

parser start { extract(eth); select(eth.type) { 0x0800: p_ip; default: accept; } }
parser p_ip  { extract(ipv4); goto accept; }

action fwd(port) { set(meta.egress_port, port); }
action drop_pkt() { set(meta.drop, 1); }

table route {
    key { ipv4.dst : lpm; }
    actions { fwd; drop_pkt; }
    default_action drop_pkt();
}

control ingress { apply(route); }
```

规则：
- metadata **必须**声明 `egress_port` 和 `drop`（流水线输出依赖）；
- `ingress_port` / `timestamp` 可选，声明即自动填充；
- 每个 `apply(t)` 占用一个物理 stage，总数 ≤ `RMT_MAX_STAGES`（8）；
- `if (valid(h))` 可门控表（头无效则整表跳过）。

**Step 2: 编写表项文件**

```
# entries/foo.txt
table_add route fwd 10.0.0.0/8 => 2
table_add route fwd 192.168.0.0/16 => 3
```

Key 格式自动识别：IPv4/IPv6（+前缀长）、MAC、十进制/十六进制、长 hex。

**Step 3: 编写 demo 并接入构建**

```c
// src/foo_demo.c  (skeleton)
#include "rmt.h"
int main(int argc, char **argv) {
    rmt_pipeline_t *sw = rmt_create();
    sw->verbose = (argc > 1 && !strcmp(argv[1], "-v"));
    p4c_compile_file(sw, "p4/foo.p4");
    rmt_load_entries(sw, "entries/foo.txt");

    // 构造测试报文
    uint8_t pkt[64];
    /* ... 填充 eth + ipv4 ... */
    rmt_pkt_t in = { .data = pkt, .len = 42, .in_port = 1 };
    rmt_pkt_t out = {0};
    rmt_process(sw, &in, &out);

    // 逐字节校验
    int fail = 0;
    fail += CHECK("egress port", out.out_port == 2);
    /* ... */
    printf("%s (%d failures)\n", fail ? "FAIL" : "ALL PASS", fail);
    rmt_destroy(sw);
    return fail;
}
```

Makefile 增加一行：
```makefile
FOODEMO := bin/foo_demo
$(FOODEMO): $(COBJS) build/foo_demo.o | bin
	$(CC) $(CFLAGS) -o $@ $^
foo: $(FOODEMO)
	./$(FOODEMO)
```

run.sh 增加 `foo)` case，即可通过 `./run.sh foo` 一键构建并运行。

---

## 12. 健壮性与防御性编程

数据面运行在不可信输入（任意报文）上，且 extern 索引由编译器/控制面下发，
因此各模块均内建运行时防护：

| 模块 | 防护措施 | 防御场景 |
|---|---|---|
| `alu.c` MOVX | `idx >= imm`（栈深度）+ `src ∈ [0, MAX_FIELDS)` 双重界检查 | 恶意 SL 值越界访问 PHV |
| `alu.c` extern 指令 | `safe_cache(p, imm)` / `safe_dnn(p, imm)` NULL 保护 | extern 编号越界（配置错误） |
| `alu.c` PIT_CONSUME | `fid_drop >= 0` 前置检查：缺 drop 字段时不写 | P4 程序未声明 meta.drop |
| `parser.c` extract_array | `cnt` 受 `adefs[].count` 和 `nhdrs - hdr` 双重上限 | 恶意包头声明巨大栈深 |
| `pipeline.c` parse failure | 解析失败立即 `out->dropped = true` 返回 | 畸形报文不进入 match-action |
| `dnn.c` 归一化 | `xi > INT64_MAX >> Q` 时饱和截断 | 极端特征值溢出定点乘法 |
| `cache.c` 统计 | `_Atomic uint64_t` 无锁计数 + print 时加锁快照 | 多 worker 并发统计撕裂 |
| `dnn.c` 统计 | `pthread_mutex` 保护 `print_stats` 读取 | 推理/打印并发竞态 |

构建全程 `-Wall -Wextra`，0 警告；`-fsanitize=thread` 下无数据竞争报告。

---

## 13. 已知限制

- 软件功能模型，非周期精确；非字节对齐字段仍逐 bit 模拟，查找/ALU
  为行为级模拟；
- P4 为 P4-14 风格自定义子集（见 §5 语法），不含 checksum 单元，
  示例中 IPv4/UDP 校验和置 0；
- 表在 control 中按 `apply` 顺序分配独立 stage（一表一级），
  无依赖表打包同级并行查找未实现；
- 三值匹配 (ternary) 未实现，5 元组 ACL 用 lpm+exact 组合表达；
- VXLAN 示例未定义内层 UDP 槽位，封装方向的租户 UDP 报文其 L4 会占用
  外层 udp 槽（测试用 TCP 载荷规避）；
- NDN：PIT 单端口（不做多端口复制）；未经请求的 Data 会先入 CS 再被
  PIT 丢弃（真实 NDN 同样存在该缓存污染面，需上层校验）；
- DNN extern 输入特征须为动作执行前已就绪的字段（同 action 内仅对
  首特征字段建立调度依赖）。

---

## 14. 参考文献

1. Bosshart P, Gibb G, Kim H S, et al. **Forwarding Metamorphosis: Fast
   Programmable Match-Action Processing in Hardware for SDN.** SIGCOMM 2013.
   —— RMT 架构原始论文，本项目的设计基础。

2. Zhang L, Alexander A, Jacobson V, et al. **Named Data Networking.** ACM
   SIGCOMM CCR, 2014. —— NDN 数据面（CS/PIT/FIB）参考。

3. Kim C, Sivaraman A, Katta N, et al. **In-band Network Telemetry via
   Programmable Dataplanes.** SOSR 2015. —— INT 带内遥测参考。

4. Barradas D, Santos N, Rodrigues L. **MORP4: A Dynamic Network
   Telescope on Steroids.** IEEE S&P 2025. —— 动态网络望远镜参考。

5. Filsfils C, Nainar N K, Pignataro C, et al. **Segment Routing with the
   MPLS Data Plane.** RFC 8660, 2019 / SRv6: RFC 8986, 2021.
   —— SRv6 End 行为参考。

6. Mahalingam M, Dutt D, et al. **VXLAN: A Framework for Overlaying
   Virtualized Layer 2 Networks over Layer 3 Networks.** RFC 7348, 2014.

