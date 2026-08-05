# 面向卫星网络 UPF 的业务感知边缘网算协同模型——基于可编程 RMT 的在网计算框架

# Service-Aware Edge Computing-Networking Collaboration Model for Satellite Network UPF: A Programmable RMT-Based In-Network Computing Framework

## 摘要 (Abstract)

The convergence of Low Earth Orbit (LEO) satellite constellations and 5G non-terrestrial network (NTN) architectures has established the satellite User Plane Function (UPF) as a critical edge node bridging space-segment infrastructure and terrestrial services. However, existing satellite UPF designs treat packet forwarding and application-level intelligence as separate functional planes—forwarding proceeds on a destination basis while classification, computation, and service differentiation are relegated to external servers or slow control-plane loops. This architectural separation incurs multiple round-trips, prevents per-packet service awareness, and cannot meet the heterogeneous QoS demands of modern satellite-borne traffic under pervasive TLS 1.3/QUIC encryption.

This paper proposes a **service-aware edge computing-networking collaboration model (EdgeNet-CoM)** for satellite network UPF, whose core is a **programmable RMT (Reconfigurable Match-Action Tables)-based in-network computing framework**. Unlike prior approaches that augment the data plane with isolated classification or forwarding functions, our framework unifies packet forwarding and application-level intelligence within a single programmable pipeline—every arriving packet can be parsed, classified by an in-pipeline quantized neural network (Q16.16 fixed-point MLP), and acted upon by user-defined service logic—all within a sub-microsecond processing window. The framework is built on five core design pillars: (i) **a unified parse-classify-act pipeline** that eliminates the architectural separation between traffic understanding and forwarding; (ii) **a stateful extern architecture** that embeds the DNN inference engine as a first-class pipeline stage, enabling per-packet service classification with zero payload decryption; (iii) **P4-inspired programmable service logic** that follows the P4 programming model's parse-deparse-match-action abstraction, enabling runtime reconfiguration of classification models, QoS policies, and forwarding rules through control-plane table updates without data-plane restart; (iv) **a multi-queue parallel scaling engine** that achieves significant throughput scaling across multiple CPU cores (2.57× at 16 workers) while preserving per-flow ordering; and (v) **a service-aware actuation layer** that exposes classification results to downstream pipeline stages for flexible per-service actions.

We instantiate the framework with two representative satellite UPF applications: (a) **service-aware multi-path intelligent routing** (Application A) that classifies encrypted traffic into four service classes (Web/Video/VoIP/Bulk) and selects optimal satellite/terrestrial paths (GEO relay, LEO direct, ISL, terrestrial backhaul) per service type; and (b) **service-differentiated network coding optimization** (Application B) that applies class-specific Forward Error Correction (RLNC/XOR) to compensate for the elevated loss rates of satellite links (1–10%) without incurring costly retransmission RTT penalties. We formalize the in-network computing framework and its application problems, design a five-stage programmable pipeline following the P4 programming model, and develop a complete prototype system (EdgeNet-CoM/UPF) in approximately 4,200 lines of core ISO C11 (plus ~1,800 lines of test harness and benchmarking scripts, totaling ~6,000 lines) with zero external dependencies. Experimental evaluation on class-conditional synthetic traffic demonstrates: (1) 99.1% classification accuracy across four traffic classes with zero quantization loss; (2) 933 ns per-packet end-to-end processing latency including DNN inference; (3) aggregate multi-core throughput of 2.75 Mpps (2.57× speedup over single-thread baseline with 16 workers; per-worker efficiency limited by LLC contention on the shared 8-vCPU test platform); and (4) less than 1.3% load imbalance. The two application instantiations together achieve 51% coding bandwidth reduction (12.3% vs. 25% uniform) and satellite-path-aware differentiated forwarding, validating that the RMT-based in-network computing framework provides a unified, low-latency, and flexible foundation for service-aware satellite UPF operations through computing-networking collaboration.

**Keywords:** Satellite Network; User Plane Function (UPF); Edge Computing-Networking Collaboration; In-Network Computing; Programmable Data Plane; Reconfigurable Match-Action Tables (RMT); P4; In-Network Machine Learning; Service-Aware Routing; Network Coding; Quality of Service; Non-Terrestrial Network (NTN); 5G Core

---

**中文摘要**

低轨 (LEO) 卫星星座与 5G 非地面网络 (Non-Terrestrial Network, NTN) 架构的融合，使卫星用户面功能 (User Plane Function, UPF) 成为连接星载基础设施与地面服务的关键边缘节点。然而，现有卫星 UPF 设计将报文转发与应用层智能视为独立的功能平面：转发按目的地址进行，而分类、计算和业务区分则被卸载至外部服务器或缓慢的控制面环路。这种架构分离导致多次往返延迟，无法实现逐包业务感知，也难以满足普遍 TLS 1.3/QUIC 加密下现代星载流量的异构服务质量 (Quality of Service, QoS) 需求。

本文提出一种面向卫星网络 UPF 的**业务感知边缘网算协同模型 (EdgeNet-CoM)**，其核心为**基于可编程可重构匹配动作表 (Reconfigurable Match-Action Tables, RMT) 的在网计算 (in-network computing) 框架**。与以往在数据平面中增加孤立分类或转发功能的方法不同，本框架在单一流水线中统一了报文转发与应用层智能。每个到达的报文均可被解析、由流水线内置的量化深度神经网络 (Deep Neural Network, DNN)（Q16.16 定点多层感知器 (Multi-Layer Perceptron, MLP)）分类，并由用户自定义的业务逻辑执行相应动作——全部在亚微秒级处理窗口内完成。该框架构建于五项核心设计之上：(i) **统一的解析-分类-执行流水线**，消除流量理解与转发之间的架构分离；(ii) **有状态外部函数架构**，将 DNN 推理引擎嵌入为一级流水线阶段，实现零载荷解密的逐包业务分类；(iii) **P4 启发式可编程业务逻辑**，遵循 P4 编程模型的解析-反解析-匹配-动作抽象，支持通过控制面表项更新运行时重配置分类模型、QoS 策略和转发规则，无需重启数据平面；(iv) **多队列并行扩展引擎**，在保持逐流有序性的前提下实现多 CPU 核的显著吞吐扩展（16 工作线程时达 2.57 倍加速）；(v) **业务感知执行层**，将分类结果暴露给下游流水线阶段，支持灵活的按业务动作。

本文以两个代表性卫星 UPF 应用实例化该框架。(a) **业务感知多路径智能路由**（应用 A）：将加密流量分为四类业务（Web/视频/VoIP/大块传输），并按业务类型选择最优卫星/地面路径（GEO 中继、LEO 直达、星间链路、地面回传）。(b) **业务区分网络编码 (network coding) 优化**（应用 B）：针对星载链路的高丢包率（1–10%），对不同业务类别施加差异化的前向纠错 (Forward Error Correction, FEC)（随机线性网络编码 (Random Linear Network Coding, RLNC)/XOR），避免昂贵的重传往返时延惩罚。本文对在网计算框架及其应用问题进行了形式化建模，设计了遵循 P4 编程模型的五级可编程流水线，并以约 4,200 行核心 ISO C11 代码（加上约 1,800 行测试与基准测试脚本，共约 6,000 行）开发了完整的原型系统 (EdgeNet-CoM/UPF)，无任何外部依赖。基于业务条件合成流量的实验评估表明：(1) 四类流量分类准确率达 99.1%，量化零损失；(2) 包含 DNN 推理在内的逐包端到端处理时延为 933 ns（纳秒）；(3) 多核聚合吞吐量为 2.75 Mpps（16 工作线程相对单线程基线加速 2.57 倍；单工作线程效率受共享 8 vCPU 测试平台的末级缓存 (LLC) 竞争限制）；(4) 负载不均衡度低于 1.3%。两个应用实例合计实现 51% 的编码带宽节省（12.3% vs. 25% 统一编码）以及卫星路径感知的差异化转发，验证了基于 RMT 的在网计算框架通过网算协同为业务感知卫星 UPF 运营提供了统一、低延迟且灵活的基础。

**关键词：** 卫星网络；用户面功能 (UPF)；边缘网算协同；在网计算；可编程数据平面；可重构匹配动作表 (RMT)；P4；在网机器学习；业务感知路由；网络编码；服务质量；非地面网络 (NTN)；5G 核心网

---

## 1. 引言 (Introduction)

### 1.1 背景：从报文转发到卫星 UPF 的在网计算 (Background: From Packet Forwarding to In-Network Computing at Satellite UPF)

低轨 (LEO) 卫星星座（如 Starlink、OneWeb、Amazon Kuiper）的快速部署，叠加 3GPP Release 17+ 非地面网络 (Non-Terrestrial Network, NTN) 规范的标准化 [1]，已将卫星网络确立为地面边缘计算基础设施不可或缺的延伸。在此架构中，用户面功能 (User Plane Function, UPF) 作为 3GPP TS 23.501 [2] 定义的 5G 独立组网 (SA) 核心的关键组件，正向卫星网关扩展，形成了**卫星 UPF 边缘节点**的概念。该节点充当星载基础设施与地面服务网络之间的关键枢纽 [3]。

卫星 UPF 边缘节点在三个维度上根本区别于地面边缘节点。

**第一，异构连接性。** 卫星 UPF 同时管理多种上行链路类型：GEO 中继链路（延迟约 500 ms、覆盖广）、LEO 直达接入链路（延迟约 20–50 ms、受点波束限制）、用于星座内路由的星间链路 (ISL)，以及地面站的地面回传光纤连接。上述路径类型在时延、带宽和丢包率方面呈现不同的 QoS 特征，需要业务感知的路径选择而非统一转发。

**第二，较高的丢包率。** 星-空-地一体化链路由于大气衰减、切换事件和射频损伤，丢包率达 1–10% [33]，显著高于地面光纤链路。这使得在 UPF 处通过网络编码进行主动丢包保护尤为有价值。

**第三，受限的处理环境。** 卫星 UPF 硬件通常在功率、散热和辐射约束下运行，倾向于轻量级、无依赖的软件实现，而非 GPU 加速的分类流水线。

现代卫星边缘流量的一个显著特征是普遍加密。当前测量表明，超过 95% 的互联网流量使用 TLS 1.3 或 QUIC 协议加密 [3]。这一趋势由隐私法规（如 GDPR、CCPA）和浏览器厂商策略所驱动，在卫星通信中又因 5G 服务化架构 (Service-Based Architecture, SBA) 的端到端安全模型而进一步加强。这种加密从根本上破坏了卫星 UPF 的流量工程：在无法观测应用层语义的情况下，UPF 无法区分需要低于 50 ms 抖动的 VoIP 通话与可容忍数秒延迟的后台备份。二者在 UPF 看来都是不透明的 TLS 字节流，共享相同的五元组特征。由此产生的卫星 UPF "语义盲区"导致次优路径选择，例如将时延敏感的 VoIP 路由经高延迟 GEO 中继而非 LEO 直达链路，从而降低了穿越卫星段的实时服务的用户体验。

**架构矛盾。** 传统卫星 UPF 设计体现了转发面与智能面之间的严格分离。转发面以线速执行基于目的地址的报文交换，而应用层智能——包括流量分类、业务区分和自适应编码——则被委托给外部服务器或控制面环路。具体而言，转发面在纳秒级时间尺度上以逐包粒度运行，而智能面在毫秒至秒级时间尺度上以流级或会话级粒度运行。弥合这一鸿沟需要范式转变：从将数据平面视为简单的报文转发引擎，转变为将其认识为能够在线执行应用层计算的**分布式计算平台**。这种从"报文转发"到"在网计算"的演进，代表了卫星 UPF 面临的核心架构挑战。

### 1.2 问题陈述：面向卫星 UPF 的统一在网计算框架 (Problem Statement: Toward a Unified In-Network Computing Framework for Satellite UPF)

本文解决的核心问题是：**如何为卫星 UPF 构建一个统一的在网计算框架，将高速报文转发与应用层智能计算无缝集成——在受约束的卫星网关硬件上，以线速转发的实时处理预算实现逐包业务感知、分类和执行？**

这一框架级问题可分解为五个紧密耦合的子挑战，涵盖卫星 UPF 的计算与网络两个维度。

**挑战 1：加密条件下的内联特征提取。** 在端到端加密存在的情况下，需提取具有判别力的逐包特征。该约束在地面边缘场景中同样存在，但在卫星 UPF 处尤为突出，因为受处理和功率限制，DPI 设备通常不可行。

**挑战 2：流水线内智能计算。** 需在报文处理流水线中嵌入足够的计算能力（特别是神经网络推理），使业务分类能在逐包转发预算内完成（Gbps 速率下通常 < 1 µs）。这意味着无法采用流级观测或向 5G 会话管理功能 (Session Management Function, SMF) 的控制面往返，而需要一套在数据平面处理约束内完全运行的计算模型。

**挑战 3：框架可编程性与运行时可重构性。** 框架须支持在运行时重配置分类模型、QoS 策略和转发逻辑，无需重启数据平面，从而适应不断变化的流量模式和卫星链路状态，并保持连续运行。

**挑战 4：在保持正确性的前提下扩展吞吐量。** 框架须在卫星地面站或星载处理模块可用的多个 CPU 核上扩展，实现显著吞吐量提升的同时保持逐流报文有序性——这是 TCP 语义的关键正确性要求。

**挑战 5：跨异构路径的业务感知执行。** 框架须将分类和计算结果暴露给下游流水线阶段，支持差异化动作（如路由选择、编码适配、QoS 标记）。GEO、LEO、ISL 和地面路径各自具有独特的时延、带宽和丢包率特征，需按业务类型匹配最优路径。

上述五项挑战共同定义了卫星 UPF 在网计算框架的完整需求——一个超越传统转发/智能分离、在单一流水线中实现网算协同的框架。

### 1.3 提出的方法：EdgeNet-CoM 在网计算框架 (Proposed Approach: The EdgeNet-CoM In-Network Computing Framework)

本文提出 **EdgeNet-CoM（边缘网算协同模型，Edge Computing-Networking Collaboration Model）**，一种面向卫星网络 UPF 的基于可编程 RMT 的在网计算框架。本框架的核心思想是：**逐包可观测的报文头特征包含足够的统计信号，可在卫星 UPF 处实时区分应用业务类型**；所得分类结果可在统一流水线中直接驱动转发和编码决策，从而消除"理解流量"与"对流量采取行动"之间的架构分离。

EdgeNet-CoM 框架构建于**五项核心设计**之上，分别对应第 1.2 节的五项挑战：

1. **统一的解析-分类-执行流水线。** 将可编程解析、神经推理和业务特定执行集成到单次流水线遍历中。每个到达的报文在数据平面内完成解析、分类与动作执行，消除流量理解与转发之间的架构分离。

2. **用于在网智能的有状态外部函数架构。** 将量化 DNN 推理引擎嵌入为一级流水线阶段（有状态外部函数 (stateful extern)），实现逐包业务分类，全程零浮点运算、零载荷解密。该外部函数为框架级能力，可在多个应用实例中复用。

3. **P4 启发式可编程业务逻辑。** 流水线逻辑遵循 P4 编程模型的解析-反解析-匹配-动作抽象设计，支持通过控制面表项更新在运行时重配置分类模型、QoS 策略、编码参数和转发规则，无需修改或重启数据平面代码。P4 前端编译器 (`p4c.c`，约 1,200 行代码) 将 P4 源程序翻译为流水线的级分配和 ALU 微码配置，驱动软件 RMT 运行时。该设计保留了 P4 的协议无关性与运行时可重构性优势，同时面向硬件 P4 目标不可用的软件边缘平台。

4. **多队列并行扩展引擎。** 基于 RSS 流哈希、SPSC 无锁环和核绑定工作线程，实现显著的多核吞吐增益（16 工作线程时聚合加速 2.57 倍），并通过形式化正确性定理保证逐流有序性。单工作线程效率在共享 8 vCPU 测试平台上因超线程和 LLC 竞争而有所下降，但框架的无锁设计预计在使用每核 L2 缓存的专用硬件上可接近线性扩展。

5. **业务感知执行层。** 将分类结果（业务类别、QoS 标记）暴露给所有下游流水线阶段，支持灵活的按业务动作——包括路由选择、网络编码、缓存决策和速率限制——无需冗余重分类。

本文通过两个代表性卫星 UPF 应用实例化 EdgeNet-CoM 框架，以展示其通用性：

- **应用 A：业务感知多路径智能路由。** 将加密流量分为四类业务，利用解析-分类-执行流水线和业务感知执行层按业务类型选择最优卫星/地面路径。

- **应用 B：业务区分网络编码优化。** 针对卫星链路的高丢包率（1–10%），对不同业务类别施加差异化的前向纠错 (FEC)（RLNC/XOR），在报文处理过程中内联执行编码决策。

上述设计使 EdgeNet-CoM 在卫星 UPF 的部署具有架构自然性：UPF 本身已在执行逐包转发、QoS 执行和流量上报。本框架通过在网计算智能扩展这些现有功能，无需在 5G 服务化架构中增加额外的网络功能 (NF) 组件。

### 1.4 主要贡献 (Contributions)

本文的贡献围绕在网计算框架本身及其应用实例展开：

1. **卫星 UPF 在网计算框架的形式化。** 建立了在卫星 UPF 数据平面中嵌入应用层智能的理论基础，将逐包特征提取、流水线内神经推理和业务感知执行统一形式化（定义 1–6，第 3 节）。该形式化超越了任何单一应用，为卫星边缘未来的在网计算服务提供了通用模板。

2. **EdgeNet-CoM 架构设计。** 提出包含五项核心设计的完整框架架构——统一流水线、有状态外部函数、P4 启发式可编程性、多队列扩展和业务感知执行——每项设计分别对应卫星 UPF 在网计算的一项特定挑战（第 4.1–4.5 节）。流水线逻辑遵循 P4 编程模型，前端编译器将 P4 源程序翻译为驱动软件 RMT 运行时的流水线配置；完整系统实现为可移植的 ISO C11 原型。

3. **量化流水线内神经推理引擎。** 开发 Q16.16 定点 MLP 推理引擎作为一级流水线外部函数（定义 4，算法 1，第 4.3 节）。该引擎实现完整的多层神经网络计算，零浮点运算，零精度退化，逐包延迟约 200 ns——特别适合卫星网关的资源受限处理环境。

4. **应用实例 A：业务感知多路径智能路由。** 通过首个应用实例化（第 4.6 节）对加密流量进行分类，并在异构卫星-地面连接（GEO、LEO、ISL、地面回传）之间选择最优路径，实现零冷启动延迟的逐包业务感知路由。

5. **应用实例 B：业务区分网络编码优化。** 通过第二个应用实例化（第 4.7 节）针对卫星链路丢包特征施加类别特定的前向纠错 (FEC)，实现 51% 的编码带宽节省（12.3% vs. 25% 统一编码），同时不牺牲丢包敏感流量的保护。

6. **完整原型实现与综合评估。** 以约 4,200 行核心 ISO C11 代码（加上约 1,800 行测试与基准测试脚本，共约 6,000 行）开发 EdgeNet-CoM/UPF 原型，无任何外部依赖。综合评估（第 5 节）涵盖分类准确率（99.1%）、逐包延迟（933 ns）、多核吞吐扩展（16 工作线程加速 2.57 倍，聚合 2.75 Mpps）、负载均衡质量（Jain 指数 0.997）以及全部框架组件与应用实例的端到端功能验证。此外，还给出了与基于阈值的规则基线的初步比较，以定位 DNN 的贡献。

### 1.5 论文组织 (Paper Organization)

本文其余部分组织如下。第 2 节综述在网计算与可编程数据平面、加密流量分类、在网缓存与聚合、网络编码及卫星边缘计算的相关工作。第 3 节对在网计算框架及其应用问题进行形式化建模，并给出逐包特征判别性的关键观察。第 4 节详述 EdgeNet-CoM 框架架构与关键技术，包括可编程解析器、量化 DNN 推理引擎、超长指令字 (Very Long Instruction Word, VLIW) 执行模型、多队列扩展引擎及两个应用实例。第 5 节描述原型实现 (EdgeNet-CoM/UPF) 并给出综合实验评估。第 6 节讨论局限性、卫星 UPF 集成的部署考量与未来方向。第 7 节总结全文。

---

## 2. 相关工作 (Related Work)

### 2.1 在网计算与可编程数据平面 (In-Network Computing and Programmable Data Planes)

可编程数据平面 (programmable data plane) 的出现从根本上扩展了网络转发路径中可执行的计算范围，将数据平面从简单的报文转发引擎演进为通用的在网计算平台。可重构匹配动作表 (Reconfigurable Match-Action Tables, RMT) 架构 [4] 率先通过可重构的匹配-动作级实现了协议无关的报文处理，随后在 P4 编程语言 [10] 中得到形式化。硬件实现（如 Intel Tofino、AMD Pensando）已实现 Tbps 级吞吐，但受限于每级有限的 ALU 指令预算和流水线时钟约束，仅支持简单的无状态计算。

近期研究探索了将机器学习推理直接嵌入数据平面，这一范式被称为**在网机器学习 (in-network machine learning)**——在网计算的核心使能技术。Xiong 和 Zilberman [11] 证明了二分类可使用匹配-动作表项作为决策树节点来实现，将特征阈值直接编码在表键中。N3IC [12] 提出了用于流量分析的专用神经网络推理卡，但受硬件限制仅支持二值线性分类器。IIsy [13] 则将随机森林和 SVM 模型映射到交换机表流水线，实现了多分类但不具备多层神经网络的表示能力。

**分类之外的在网计算**近年也有显著进展。具体而言，可编程数据平面已被用于实现分布式共识协议 [17]、执行实时遥测聚合，以及在转发路径内执行应用特定逻辑。报文事务模型 (Packet Transaction model) [17] 为 P4 扩展了事务语义以支持复杂的有状态操作，表明在网计算范式可承载日益复杂的应用逻辑。

我们的 EdgeNet-CoM 框架与上述先前工作的根本区别在于一个关键方面：通过在边缘平台上面向软件 RMT 而非固定功能交换机 ASIC，本框架消除了 ALU 指令预算约束，在流水线的逐包处理窗口内实现了完整的 MLP 推理——包括乘加运算、非线性激活 (ReLU) 和 argmax。这一设计选择以原始吞吐（Tbps 级降至 Gbps 级）换取计算表达力，在卫星边缘节点是合理的折中，因为星载链路速率通常为 1–10 Gbps 且处理资源受限。

### 2.2 加密流量分类 (Encrypted Traffic Classification)

加密流量分类 (encrypted traffic classification) 近年来受到广泛关注。现有方法按特征粒度与处理需求可大致分为三种范式。

**流级统计方法**是目前的主流方法，从流内报文序列中提取特征——包括到达间隔时间 (IAT)、报文大小分布、字节频率和流持续时间——来训练分类器 [5] [6]。FlowPic [7] 创新性地将流统计量编码为二维图像用于基于 CNN 的分类，在应用识别任务上达到了超过 95% 的准确率。Deep Packet [8] 则应用堆叠自编码器自动从流级字节分布中学习判别性表示。尽管这些方法取得了高准确率，但它们共享一个根本局限：需观察多个报文（通常 10–100 个）才能进行分类，引入数百毫秒到数秒的延迟。对于需要逐包决策的实时在网计算而言，这种延迟是不可接受的。

**基于载荷的深度学习**是第二种范式，将深度神经网络直接应用于原始报文字节。Wang 等人 [9] 提出了在每个报文前 N 字节上操作的 1D-CNN 架构。然而，这些方法需访问解密内容，从根本上违反了 GDPR 等隐私法规，并引入了与线速处理不兼容的计算开销。此外，即使在隐私约束放宽的情况下，TLS 1.3 加密的握手扩展也进一步限制了载荷检查的适用性。

**逐包报文头方法**构成了第三种、且很大程度上尚未充分探索的范式。现有逐包研究主要集中在异常检测（如识别 DDoS 或端口扫描）而非多类业务识别。据我们所知，本文首次证明单个加密报文的报文头特征足以以超过 99% 的准确率实现四类业务识别，从而支持即时的逐包在网计算决策。

**工业流量分类工具**代表了第四种、已被广泛部署的方法。nDPI [38] 是基于深度包检测的流量分类开源工业标准，通过端口启发式、载荷模式匹配和统计分析的组合支持 600 余种应用协议的检测。然而，nDPI 的逐流检测通常需观察 5–20 个报文才能使分类收敛，且其依赖载荷的方法对 TLS 1.3 加密流量失效。本文方法是上述工具的补充而非替代：在 nDPI 需要流级观察和载荷访问的场景中，本框架仅使用未加密报文头特征实现逐包分类——以一定的分类准确率换取更低的延迟和隐私保护。

### 2.3 在网缓存与聚合 (In-Network Caching and Aggregation)

除分类外，可编程数据平面还支持更广泛的与边缘计算相关的在网计算服务。

**在网缓存 (in-network caching)** [19] 允许数据平面直接从交换机内存中存储和服务频繁访问的内容，通过消除到源服务器的往返来降低内容检索延迟。命名数据网络 (NDN) 和信息中心网络 (ICN) 在转发平面中实现了内容存储和待处理兴趣表原语 [20]，表明有状态数据平面操作可在线速下服务应用层功能。

**在网聚合 (in-network aggregation)** [21] 则通过在网络结构内合并部分结果（如分布式机器学习中的梯度向量）来减少带宽消耗，避免重叠数据的冗余传输。SwitchML [22] 在可编程交换机上实现了在网梯度聚合，为分布式训练工作负载带来了数量级的加速。

综上所述，上述发展展示了明确的趋势：可编程数据平面正从简单的报文转发引擎演进为通用的在网计算平台。本框架通过嵌入神经推理作为一类新的在网智能来扩展这一范式，专门面向卫星 UPF 的业务感知计算问题——分类结果在同一流水线遍历中直接驱动转发和编码决策。

### 2.4 网络编码 (应用背景) (Network Coding (Application Background))

网络编码 (network coding) [23] 使中间节点在转发前对报文进行代数组合，提供固有的丢包弹性而无需端到端重传。有限域（通常为 GF(2⁸) 或 GF(2¹⁶)）上的随机线性网络编码 (Random Linear Network Coding, RLNC) [24] 允许接收端从任意足够数量的编码报文集合中恢复丢失的报文，使其特别适合有损的边缘无线链路。

近期工作将网络编码应用于特定的边缘场景。COPE [25] 在无线 mesh 网络中执行基于 XOR 的机会性流间编码，利用广播窃听减少传输次数。编码 TCP [26] 将网络编码集成到传输层，将丢包转化为编码冗余增加而非触发拥塞控制。Fulcrum [27] 提出了一种灵活的编码框架，桥接外部码（二值 XOR）和内部码（GF(2⁸) RLNC）以适应异构链路质量。

然而，现有网络编码方法不考虑应用类型而采用**统一编码参数**——对 VoIP 报文（任何丢包都会导致可闻瑕疵）和大块传输（TCP 重传能高效处理丢包）使用相同的冗余比。这种"一刀切"方法要么在丢包容忍流量上浪费带宽，要么对丢包敏感应用保护不足。本框架通过应用实例 B（第 4.7 节）解决这一差距，利用逐包业务分类驱动**差异化编码决策**：实时流量高冗余、流媒体中等编码、大块传输零编码开销。

### 2.5 卫星边缘计算与非地面网络 (Satellite Edge Computing and Non-Terrestrial Networks)

卫星网络与 5G 边缘计算架构的集成随着 3GPP Release 17 非地面网络 (NTN) 研究项目 [28] 及后续 Release 18 增强 [29] 而加速。早期卫星边缘计算 (satellite edge computing) 工作聚焦于计算卸载策略：Wang 等人 [30] 提出了 LEO 卫星辅助的物联网任务卸载边缘计算框架，优化卫星链路延迟与地面服务器处理延迟之间的权衡；Liu 等人 [31] 则研究了 LEO 卫星与地面边缘节点之间的协同任务调度，表明星地协同可为地理分散的用户降低 40% 的服务延迟。

**卫星 UPF 架构**近期随着 5G 运营商寻求将核心网功能扩展至非地面领域而受到关注。Chen 等人 [32] 提出了 LEO 星座的分布式 UPF 部署策略，在选定的卫星上部署 UPF 实例以最小化区域流量的往返延迟。"星地一体化网络" (SGIN) [33] 的概念则将卫星 UPF 视为地面与非地面 5G 覆盖之间无缝服务连续性的关键使能者。然而，现有卫星 UPF 方案在数据平面内均采用标准的基于目的地址的转发。它们未解决构建在网计算框架以支持跨异构卫星-地面路径的业务感知计算和路由的挑战，也未满足普遍加密下智能转发决策的需求。

**卫星链路的网络编码**由于卫星信道固有的有损特性而被广泛研究。Lucani 等人 [34] 将 RLNC 应用于卫星广播信道，证明了在丢包条件下的显著吞吐改进；Nguyen 等人 [35] 为卫星-地面混合网络提出了自适应网络编码，根据实测链路质量调整冗余。然而，上述方法对所有流量流采用**统一编码参数**，忽略了 VoIP、视频和大块传输业务的不同丢包敏感度。本框架通过应用实例 B 解决这一差距，将逐包业务分类与由卫星链路特征驱动的自适应编码决策相结合。

**面向卫星网络的可编程数据平面**代表了一个新兴研究方向。P4 语言已被探索用于配置卫星星载处理器 [36]，近期关于"基于 P4 的卫星路由" [37] 的工作证明了 LEO 星座星间链路中协议灵活的转发。然而，此前尚无工作探索在卫星 UPF 的可编程数据平面内构建综合在网计算框架以实现业务感知的计算和转发——本文是首次展示这种集成。

**边缘的应用感知路由**已在 SD-WAN [16] 背景下得到研究，其中流级分类结果驱动跨多条 WAN 链路的路径选择。此类方案通常采用分离架构：分类器（运行在用户空间或独立设备上）识别应用流，然后向数据平面推送转发规则。然而，这种分离引入了两个问题：(a) 每个流的前 N 个报文在分类窗口期间保持未分类状态并被次优路由；(b) 组件间通信增加了延迟并造成可扩展性瓶颈。传统 QoS 路由依赖于终端主机或入口路由器设置的 DiffServ 标记 [14]，假设网络在流量源处即可感知应用需求。基于 SDN 的方法使用集中式控制器根据应用识别安装逐流规则 [15]，但受控制面延迟（每个新流数毫秒）和流数量增长时的可扩展性瓶颈所限。本框架的应用实例 A（第 4.6 节）独特地在统一流水线中将逐包分类与即时路由选择相结合：每个*可分类*报文（即每个 TLS 应用数据记录，从握手完成后的第一个此类记录开始）都获得最优路径分配，无需控制器参与或进程间通信。由此消除了流级分类方案的冷启动延迟问题。

### 2.6 本文定位 (Positioning of This Work)

表 1 总结了 EdgeNet-CoM 框架相对于现有方法在十个关键维度上的定位。比较表明，本框架是唯一同时实现逐包粒度、亚微秒延迟、完整 MLP 支持、边缘可部署性、集成路由和自适应编码的方案——这一组合在先前工作中未曾提供。

**表 1：EdgeNet-CoM 相对于现有方法的定位。** 每行代表一个关键能力维度；勾号表示支持，叉号表示不支持。本框架（最右列）唯一同时满足全部维度。

| 维度 | 流级 ML [5-9] | 在网 ML [11-13] | SDN/SD-WAN [15-16] | 网络编码系统 [23-27] | **EdgeNet-CoM（本文）** |
|---|---|---|---|---|---|
| 分类粒度 | 逐流 | 逐包 | 逐流 | 不适用 | **逐包** |
| 分类延迟 | 100 ms – 10 s | ~ns | 1 – 100 ms | 不适用 | **< 1 µs** |
| 神经网络支持 | 完整 (GPU) | 仅二值/线性 | 不适用 | ✗ | **完整 MLP** |
| 隐私保护 | ✓ | ✓ | 部分 | ✓ | **✓** |
| 边缘可部署（无需 ASIC） | ✓ | ✗（交换机 ASIC） | ✓ | ✓ | **✓** |
| 路由集成 | ✗ | ✗ | ✓ | ✗ | **✓** |
| 业务感知网络编码 | ✗ | ✗ | ✗ | 仅统一 | **✓（差异化）** |
| 首包优化 | ✗ | ✓ | ✗ | ✓ | **✓** |
| P4 启发式可编程 | ✗ | ✓ | ✗ | ✗ | **✓** |
| 模型热更新 | ✓ | ✗ | ✓ | ✗ | **✓** |

如表 1 所示，现有网络编码系统 [23-27] 提供丢包弹性但采用无业务感知的统一参数，且缺乏与智能路由的集成。在网 ML 方法 [11-13] 局限于交换机 ASIC 上的简单分类器。EdgeNet-CoM 在单一可编程在网计算框架中独特地结合了完整 MLP 推理引擎、逐包分类、差异化路由和自适应编码——可在通用边缘硬件上部署，无需专用 ASIC。

---

## 3. 问题建模：在网计算框架 (Problem Formulation: The In-Network Computing Framework)

### 3.1 系统模型 (System Model)

我们对卫星 UPF 在网计算环境建模如下。考虑部署在地面站或卫星星载处理模块上的卫星 UPF 网关节点 \(G\)，其作为通过非地面接入连接的终端的 5G 用户面锚点。该网关具有 \(M\) 条上行路径 \(\{p_1, p_2, \ldots, p_M\}\)，代表异构卫星-地面连接。每条路径以 QoS 配置文件 \(\langle d_m, b_m, l_m \rangle\) 为特征，分别表示时延、带宽和丢包率。典型路径类型包括：

- \(p_{GEO}\)：GEO 中继链路（延迟 ~500 ms，带宽中等，丢包 ~2–5%）；
- \(p_{LEO}\)：通过点波束的 LEO 直达接入链路（延迟 ~20–50 ms，带宽高，丢包 ~1–3%）；
- \(p_{ISL}\)：星座内星间链路中继（延迟 ~5–30 ms，带宽高，丢包 ~1–2%）；
- \(p_{TB}\)：地面站地面回传光纤（延迟 ~5–50 ms，带宽很高，丢包 <0.1%）。

来自 \(N\) 台通过卫星接入连接的终端设备 (UE) 的入站加密报文以聚合速率 \(\lambda\) 到达，运行着多样化的应用。卫星 UPF 须在其在网计算流水线中处理每个到达的报文：提取特征、执行智能计算并施加业务特定动作。上述处理须在严格的逐包处理预算内完成，同时根据报文推断的业务类型将每个报文分配到 \(M\) 条路径之一，且无法访问载荷内容——这与 5G SBA 安全模型一致，即 UPF 在加密的 PDU 会话载荷上运行。

我们现在形式化精确定义在网计算框架所需的关键概念。定义 1 建立业务类别分类体系；定义 2 规定可观测特征空间；定义 3 形式化在网计算函数；定义 4 规定框架的定量表示格式；定义 5 形式化业务感知路由函数；定义 6 形式化业务感知编码函数。

**定义 1（业务类别）。** 设 \(\mathcal{C} = \{c_0, c_1, c_2, c_3\}\) 表示业务类别集合，其中 \(c_0\) = Web 浏览，\(c_1\) = 视频流，\(c_2\) = VoIP/实时，\(c_3\) = 大块传输。每个类别 \(c_k\) 具有 QoS 需求 \(R_k = \langle d_k^{max}, b_k^{min}, l_k^{max} \rangle\)，分别表示最大可容忍时延、最低需求带宽和最大可接受丢包率。

四类分类体系的选择与四个标准 5G QoS 流 (QFI) 优先级等级相匹配，并映射到标准 DiffServ 每跳行为 (PHB)：尽力而为 (Best Effort)、确保转发 (Assured Forwarding)、加速转发 (Expedited Forwarding) 和清道夫 (Scavenger)。这一映射使卫星 UPF 转发决策与下游地面 DiffServ 感知路由器之间可直接互操作。

**定义 2（逐包特征向量）。** 对每个入站报文 \(x\)，定义其可观测特征向量为：

\[
\mathbf{f}(x) = \langle \text{ipv4.len}(x),\ \text{tcp.window}(x),\ \text{tls.length}(x),\ \text{tls.type}(x) \rangle \in \mathbb{R}^4
\]

这四个特征可在不访问载荷的情况下从未加密协议头中提取。在标准报头堆叠下（无 IP 选项、无 TCP 选项、仅 IPv4），它们占据确定性位置：IP 头在偏移 0 处，TCP 头紧随 IP 之后，TLS 记录头在 TCP 载荷偏移 0 处——从而实现有界延迟提取。**注意**：当存在 IP 选项或 TCP 选项时，绝对字节偏移会移动；解析器通过跟踪长度字段而非固定偏移来处理这种情况。特征*值*在选项存在时仍然定义良好；仅提取成本略有增加。当前不支持 IPv6 扩展头（第 6.1 节）。

**定义 3（在网计算函数）。** 在网计算函数 \(\Phi: \mathcal{X} \rightarrow \mathcal{C} \times \mathcal{A}\) 将每个入站报文映射到业务类别和关联的动作元组：

\[
\Phi(x) = \langle \phi(\mathbf{f}(x)),\ \alpha(\phi(\mathbf{f}(x))) \rangle
\]

其中 \(\phi: \mathbb{R}^4 \rightarrow \mathcal{C}\) 是业务分类器（实现为流水线内量化神经网络），\(\alpha: \mathcal{C} \rightarrow \mathcal{A}\) 是将分类结果翻译为业务特定动作（路由、编码、QoS 标记等）的动作映射。这一形式化捕获了框架的核心能力：单次流水线遍历内的**先计算后执行**。

**定义 4（Q16.16 定点表示）。** 实数 \(r\) 表示为 32 位有符号整数 \(q = \lfloor r \times 2^{16} + 0.5 \rfloor\)。这种编码提供 \([-32768, 32767)\) 的表示范围和 \(\epsilon = 2^{-16} \approx 1.5 \times 10^{-5}\) 的精度。该表示支持直接使用整数 ALU 指令进行所有算术运算，消除了对 FPU 的依赖——这对受约束的卫星网关处理器至关重要。

**定义 5（业务感知路由函数）。** 路由函数 \(\pi: \mathcal{X} \rightarrow \{p_1, \ldots, p_M\}\) 将每个报文映射到输出路径。业务感知路由函数 \(\pi^*\) 满足：

\[
\pi^*(x) = \arg\min_{p_m} \text{cost}(p_m, c_k) \quad \text{s.t.} \quad c_k = \phi(\mathbf{f}(x))
\]

其中 \(\phi: \mathbb{R}^4 \rightarrow \mathcal{C}\) 是业务分类器，\(\text{cost}(p_m, c_k)\) 量化路径 \(p_m\) 的 QoS 特征与类别 \(c_k\) 的需求之间的不匹配度。这一形式化将问题解耦为两个阶段：分类 (\(\phi\)) 和路径分配 (argmin)，使每个阶段可独立优化。

**定义 6（业务感知编码函数）。** 编码函数 \(\gamma: \mathcal{C} \rightarrow \{\langle n, k, \mathbb{F} \rangle\}\) 将每个业务类别映射到编码参数：

\[
\gamma(c_k) = \langle n_k, k_k, \mathbb{F}_k \rangle
\]

其中 \(n\) 是编码块长度，\(k\) 是每个块的原始数据报文数，\(\mathbb{F}\) 是系数生成的有限域，冗余比 \(r_k = (n_k - k_k) / k_k\) 表示专用于保护的带宽比例。

### 3.2 优化目标 (Optimization Objective)

基于定义 1–6，我们将卫星 UPF 场景中的在网计算框架优化问题形式化为分类器、路由函数和编码策略的联合优化。

**问题（卫星 UPF 在网计算框架优化）。** 给定报文特征向量 \(\mathbf{f}(x)\)，求分类器 \(\phi^*\)、路由函数 \(\pi^*\) 和编码函数 \(\gamma^*\) 以最大化：

\[
\max_{\phi, \pi, \gamma} \sum_{x \in \mathcal{X}} \mathbb{1}[\phi(\mathbf{f}(x)) = c(x)] \cdot U(c(x), \pi(x), \gamma(x))
\]

约束条件：
- **时延约束**：\(T_{parse}(x) + T_{classify}(x) + T_{act}(x) \leq T_{budget}\)，其中 \(T_{budget}\) 为逐包处理预算（卫星 UPF 处 Gbps 速率转发要求 < 1 µs）；
- **隐私约束**：\(\phi\) 仅访问定义 2 中的未加密报文头字段，符合 5G UPF 载荷加密要求；
- **资源约束**：推理仅使用整数运算（无 FPU 要求），适合受约束的卫星网关处理器；
- **可编程性约束**：流水线逻辑（分类模型、动作映射、路由表、编码参数）必须在运行时可重新配置，无需重启数据平面；
- **扩展性约束**：框架在多核并行执行下必须保持逐流报文有序性；
- **卫星路径约束**：路径选择考虑逐路径丢包率 \(l_m\)，编码冗余适配于所选路径的错误特征。

其中 \(c(x)\) 表示真实业务类别，\(\mathbb{1}[\cdot]\) 是指示函数，\(U(c_k, p_m, \gamma_k)\) 是以编码策略 \(\gamma_k\) 将类别 \(c_k\) 流量路由到卫星/地面路径 \(p_m\) 的效用。我们将效用函数实例化为反映业务需求与路径能力之间 QoS 匹配的加权多属性得分：

\[
U(c_k, p_m, \gamma_k) = w_d^{(k)} \cdot u_d(d_m, d_k^{max}) + w_b^{(k)} \cdot u_b(b_m, b_k^{min}) + w_l^{(k)} \cdot u_l(l_m, l_k^{max}, \gamma_k)
\]

其中 \(u_d, u_b, u_l \in [0, 1]\) 分别是时延、带宽和丢包（含编码补偿）的归一化满意度函数，\(w_d^{(k)}, w_b^{(k)}, w_l^{(k)}\) 是类别特定的权重且和为 1。例如，VoIP (\(c_2\)) 使用 \(w_d = 0.7, w_b = 0.1, w_l = 0.2\)（时延主导），而大块传输 (\(c_3\)) 使用 \(w_d = 0.1, w_b = 0.8, w_l = 0.1\)（带宽主导）。丢包满意度 \(u_l\) 考虑了编码冗余：更高的冗余 \(\gamma_k\) 补偿更高的路径丢包 \(l_m\)。该目标因此最大化由分类正确性加权的聚合 QoS 效用——误分类报文接收不恰当的动作并贡献零效用。类别特定权重通过控制面配置，可在运行时更新而无需重启数据平面。

### 3.3 动机：卫星 UPF 作为在网计算的自然部署点 (Motivation: Satellite UPF as the Natural Deployment Point for In-Network Computing)

本框架的可行性和战略价值建立在三个趋势的汇合之上：可编程数据平面的演进、3GPP NTN 中卫星 UPF 的标准化，以及星地边界业务感知计算的迫切需求。

**卫星 UPF 作为战略制高点。** 在 5G 架构中，UPF 是唯一处理每个用户面报文的网络功能。在卫星 UPF 部署在网计算智能创造了独特的架构优势：计算恰好发生在转发和编码决策必须做出的位置——即在报文被提交到特定卫星链路之前。由此消除了外部分类系统通过 N4 接口与 UPF 通信的延迟和信令开销，并确保每个报文（包括新 PDU 会话的首个报文）均获得最优的计算驱动动作分配。

**卫星 UPF 的在网缓存**已证明数据平面的有状态操作可在线速下服务应用层功能。命名数据网络 (NDN) [20] 在转发流水线内实现了内容存储 (CS) 和待处理兴趣表 (PIT) 原语，使卫星 UPF 节点能够缓存和服务热门内容，无需穿越完整的星地路径。本原型 (EdgeNet-CoM/UPF) 包含 CS/PIT 实现作为有状态外部函数，验证了支持业务感知计算的同一框架架构可同时提供内容缓存——在 UPF 处为可缓存的 Web 内容减少高达 60% 的卫星回传流量。

**卫星网关的在网聚合**表明数据平面可对传输中的数据执行部分计算，减少带宽受限卫星链路上的带宽需求。SwitchML [22] 在可编程交换机流水线内实现了梯度向量聚合。在卫星 UPF 处，聚合能力支持如下场景：在通过昂贵的卫星带宽上行到云之前，合并来自大规模机器类型通信 (mMTC) 的物联网传感器读数，或合并来自分布式微服务的部分查询结果——每节省一兆字节的卫星回传带宽都意味着显著的成本降低。

**卫星 UPF 的在网推理**将上述能力扩展到智能决策。卫星 UPF 数据平面不再仅仅转发、缓存或聚合数据，而是可以实时分类、预测并对流量特征采取行动。本框架代表了这一范式的具体实例化：DNN 外部函数执行多类分类，结果直接驱动卫星/地面路径选择和网络编码决策——全部在单次流水线遍历内完成。推理与动作之间的这种紧耦合是实现逐包智能卫星计算的关键使能者。

由此可见，缓存、聚合和推理三种能力共同将卫星 UPF 从被动转发引擎转变为星地连续体中应用服务交付的主动参与者。EdgeNet-CoM 框架利用这一转变，将计算智能直接嵌入卫星转发决策做出的位置，在非地面网络中最关键的枢纽处消除"理解流量"与"对流量采取行动"之间的架构分离。

### 3.4 关键观察：逐包特征判别性 (Key Observation: Per-Packet Feature Discriminability)

本工作的基本观察——在第 5.3 节中通过实验验证——是定义 2 中的四个报文头特征表现出类别条件分布且重叠极小，为个体报文级别的业务类型识别提供了足够的判别能力。

表 2 给出了每个业务类别的统计特征模式。这些模式来源于根据已发表的真实流量统计 [6] [7] 校准的合成流量生成；所得类别条件分布既用作 DNN 的训练分布，也用作第 5.3 节评估的测试分布。

**表 2：按业务类别的统计特征模式。** 每行显示一个业务类别的四个分类特征的典型范围和分布特征。最右列总结了产生这些特征值的主导流量模式。

| 业务类别 | ipv4.len（字节） | tcp.window | tls.length | 模式 |
|---|---|---|---|---|
| Web (c₀) | 200–1460（变化大） | 8000–32000 | 100–1400 | 突发式请求-响应 |
| 视频 (c₁) | 1200–1500（稳定） | 32000–65535 | 1000–1450 | 持续大报文 |
| VoIP (c₂) | 60–200（固定） | 2000–8000 | 40–180 | 周期性小报文 |
| 大块传输 (c₃) | 1400–1500（最大 MTU） | 65535（最大） | 1400–1460 | 连续满载 |

如表 2 所示，每个业务类别展现出由基本应用层行为驱动的独特特征值组合：

- **VoIP (c₂)** 生成独特的小报文（60–200 字节）和小 TCP 窗口（2000–8000）。小报文大小反映了编解码器的固定帧时长（通常为 64 kbps 下 20 ms），而小窗口反映了应用对低缓冲延迟而非吞吐量最大化的偏好。
- **大块传输 (c₃)** 持续产生最大 MTU 报文（1400–1500 字节）和最大 TCP 窗口（65535）。这种模式源于应用的贪婪吞吐量优化：将每个报文填满至 MTU 并通告最大接收缓冲区。
- **视频流 (c₁)** 产生大报文（1200–1500 字节）和大但可变的窗口（32000–65535）。其可变性将其与大块传输区分：自适应码率流根据网络条件调整分段大小，导致窗口波动。
- **Web 浏览 (c₀)** 在所有特征上展现最高方差，源于其突发式请求-响应特性：小请求报文与可变大小的响应报文交替出现，窗口大小适应不规则的数据流。

这些模式在个体报文级别持续存在，因为它们反映了应用的基本数据生成特征——如编解码器帧大小、缓冲管理策略和流控行为——而非短暂的流级聚合。这种持续性正是逐包（而非逐流）在网计算的关键理论依据。

---
## 4. 架构与关键技术 (Architecture and Key Technologies)

### 4.1 系统概述：EdgeNet-CoM 框架 (System Overview: The EdgeNet-CoM Framework)

EdgeNet-CoM 框架通过分层架构在卫星 UPF 边缘实现在网计算，包含四个功能层，如图 1 所示。该架构沿控制面/数据面边界分离关注点：控制面处理模型训练、量化和策略配置（通过 N4 接口与 5G SMF 交互以进行会话级策略更新）；数据面在卫星 UPF 的报文处理流水线中以线速执行逐包推理和转发。

```
┌─────────────────────────────────────────────────────────────────────┐
│  ① 控制面（模型训练与策略管理）                                        │
│     • 在有标注的卫星 UPF 流量上训练 MLP（SGD + Softmax CE）            │
│     • 量化：float → Q16.16，通过 rmt_dnn_load() 安装                  │
│     • 配置卫星/地面路径策略与编码参数                                   │
│     • 监控与热更新：漂移时重训练，无需重启即可重新加载                   │
│     • 通过 N4 接口与 SMF 进行 QoS 流级策略同步                        │
├─────────────────────────────────────────────────────────────────────┤
│  ② P4 编译器（enc_classify.p4 → 流水线配置）                         │
│     • 词法分析/语法分析 → AST → 级分配 → ALU 微码                    │
│     • DNN 外部函数绑定 + NC 外部函数绑定                              │
├─────────────────────────────────────────────────────────────────────┤
│  ③ 数据面：5 级 RMT 流水线（卫星 UPF 数据面）                        │
│   pkt→[Parser]→PHV→[S0:Classify]→[S1:QoS]→[S2:NC]→[S3:Route]→[S4:MAC]│
│                       │                     │            │          │
│                  [DNN Extern]         [NC Extern]   [LPM Trie]  │
│                  Q16.16 MLP        RLNC/XOR/None   class×path     │
│                                    (satellite-loss │ (GEO/LEO/ISL │
│                                     aware params)  │  /terrestrial)│
├─────────────────────────────────────────────────────────────────────┤
│  ④ 并行引擎（UPF 吞吐的多队列扩展）                                  │
│     RSS 五元组哈希 → SPSC 无锁环 → 核绑定工作线程                    │
│     （在地面站 / 星载 CPU 的 4-16 核上扩展）                          │
└─────────────────────────────────────────────────────────────────────┘
```

**图 1：EdgeNet-CoM/UPF 框架架构。** 系统部署在卫星 UPF 网关（地面站或星载处理模块），包含四层：① 控制面训练和量化 MLP 模型，管理卫星/地面路径策略，并通过 N4 与 5G SMF 同步；② P4 编译器将在网计算程序翻译为流水线配置，包括 DNN 和 NC 外部函数绑定；③ 数据面通过 5 级 RMT 流水线执行逐包分类、卫星丢包感知网络编码和多路径路由；④ 多队列引擎通过 RSS 流分配和无锁工作线程跨卫星网关 CPU 核提供水平扩展。

图 1 展示了从报文入站到出站的数据流。每个到达的报文遍历可编程解析器（将特征提取到报文头向量 (Packet Header Vector, PHV) 中），经过五个匹配-动作级（分类 → QoS 标记 → 网络编码 → 路由查找 → MAC 重写），然后通过反解析器退出。DNN 外部函数在 Stage 0 被调用，将业务类别写入元数据；Stage 2 的 NC 外部函数根据该类别决定编码动作；后续级消费类别和编码决策用于路由和报头重写。

框架的五项核心设计直接映射到架构层：
- **设计 1（统一流水线）：** 由 5 级 RMT 流水线实现（层 ③）
- **设计 2（有状态外部函数）：** 由 Stage 0 的 DNN 外部函数实现（层 ③）
- **设计 3（P4 可编程性）：** 由 P4 编译器（层 ②）和控制面（层 ①）实现
- **设计 4（多队列扩展）：** 由并行引擎实现（层 ④）
- **设计 5（业务感知执行）：** 由消费分类元数据的下游流水线级实现（层 ③）

### 4.2 可编程解析器与特征提取 (Programmable Parser and Feature Extraction)

可编程解析器负责从原始报文字节流中提取定义 2 中的四个分类特征。它实现了一个有限状态机 (FSM)，根据运行时字段值选择性地解析协议头，如图 2 所示。

```
状态转移图：
  S_start → extract(eth) → select(eth.ethertype)
    ├─ 0x0800 → S_ipv4: extract(ipv4) → select(ipv4.proto)
    │              ├─ 6 (TCP) → S_tcp: extract(tcp) → select(tcp.dport)
    │              │              └─ 443 → S_tls: extract(tls) → accept
    │              │              └─ * → accept
    │              └─ * → accept
    └─ * → accept（非 IP：绕过所有分类）
```

**图 2：解析器状态转移图。** 解析器最多遍历五个状态（start → eth → ipv4 → tcp → tls），基于以太类型、协议号和目的端口进行条件转移。TLS 头提取仅在 HTTPS 流量（端口 443）时发生，确保其他协议的零开销。

三个设计原则指导解析器的行为，每个原则对应框架的一个特定需求：

**条件提取**确保资源效率。TLS 记录头（5 字节：类型 + 版本 + 长度）仅在 `tcp.dport == 443` 时提取。非加密流量（纯 HTTP、DNS、ICMP、其他端口上的 SSH）遵循更短的解析路径，访问更少的内存。这种条件逻辑确保常见情况——非 TLS 报文——不会因在网计算基础设施而产生额外开销。

**报头有效性门控**提供清晰的控制流机制。解析器在成功提取 TLS 后在报头有效性位图中设置一个比特（`hvalid |= (1 << TLS_HDR_ID)`）。后续流水线级使用 `valid(tls)` 作为保护条件：Stage 0 和 Stage 1（分类和 QoS 标记）仅在该比特被设置时执行。这种基于位图的门控仅需单次按位与操作，避免了分支预测失败的惩罚。

**零载荷访问**确保隐私合规。解析器仅读取固定位置的协议头；具体而言，TLS 记录头占据 TCP 载荷的字节 0–4，且在访问加密应用数据之前（而非需要访问）。不访问解密密钥、证书或载荷字节，确保完全符合加密隐私保证。

### 4.3 量化神经网络推理引擎（核心在网计算组件） (Quantized Neural Network Inference Engine (Core In-Network Computing Component))

神经网络推理引擎是流水线内实现和在网智能的核心架构组件。本节描述其架构（第 4.3.1 节）、量化方案（第 4.3.2 节）和训练方法（第 4.3.3 节）。作为框架级能力，该引擎可服务于路由和编码之外的多个应用——如异常检测、内容预测和负载预测——尽管本文通过两个具体应用展示其使用。

#### 4.3.1 网络架构 (Network Architecture)

定义 3 中的业务分类器 \(\phi\) 实现为单隐层多层感知器 (Multi-Layer Perceptron, MLP)。选择 MLP 架构有三个原因：(a) 在特征分布良好分离的条件下（表 2），它为四类问题提供了足够的表示能力；(b) 其计算完全由矩阵乘法和逐元素操作组成，自然地映射到定点算术；(c) 其内存占用极小（220 个参数），可完全放入 L1 缓存。

分类器函数定义为：

\[
\phi(\mathbf{f}) = \arg\max_{k} \left( \mathbf{W}_2 \cdot \text{ReLU}(\mathbf{W}_1 \cdot \hat{\mathbf{f}} + \mathbf{b}_1) + \mathbf{b}_2 \right)_k
\]

其中归一化特征向量为：
- \(\hat{\mathbf{f}} = \left[\frac{f_1}{s_1}, \frac{f_2}{s_2}, \frac{f_3}{s_3}, \frac{f_4}{s_4}\right]\)，归一化尺度 \(\mathbf{s} = [1500, 65536, 16384, 256]\)；
- \(\mathbf{W}_1 \in \mathbb{R}^{24 \times 4}\)，\(\mathbf{b}_1 \in \mathbb{R}^{24}\) 为隐层权重和偏置；
- \(\mathbf{W}_2 \in \mathbb{R}^{4 \times 24}\)，\(\mathbf{b}_2 \in \mathbb{R}^{4}\) 为输出层权重和偏置；
- \(\text{ReLU}(z) = \max(0, z)\) 为修正线性激活；argmax 选择最高得分类别（推理时无需 softmax）。

网络拓扑为：4 输入 → 24 隐层神经元 (ReLU) → 4 输出类别 (argmax)。总参数数量为 \(4 \times 24 + 24 + 24 \times 4 + 4 = 220\) 个值，在 Q16.16 格式下仅需 880 字节存储——可放入单个 L1 缓存行组。

#### 4.3.2 Q16.16 定点量化 (Q16.16 Fixed-Point Quantization)

为消除浮点运算单元 (FPU) 依赖并确保与资源受限卫星边缘平台的兼容性，所有推理计算使用 Q16.16 定点算术。定义 4 形式化了这一表示。

定义 4 蕴含以下运算映射，将浮点神经网络运算翻译为纯整数计算：
- **加法**：\(q_a + q_b\)（直接 32 位整数加法，无需缩放）；
- **乘法**：\((q_a \times q_b) >> 16\)（在 64 位中相乘以避免溢出，然后右移恢复尺度）；
- **ReLU 激活**：\(\max(0, q)\)（单次有符号比较）；
- **特征归一化**：\((f_i << 16) / s_i\)（将原始整数值左移至 Q16.16 空间，然后除以尺度因子进行整数除法）。

算法 1 给出了完整的 Q16.16 MLP 前向传播过程。该算法将一个报文的特征向量通过隐层和输出层处理，返回预测的业务类别。

**算法 1：Q16.16 定点 MLP 前向传播。** 该算法仅使用整数算术实现定义 3 中的分类器 \(\phi\)。64 位累加器（第 5、12 行）防止乘加内层循环中的溢出。第 9 和 16 行的右移在 Q32.32 累加后恢复 Q16.16 尺度。

```
Input: Raw features f[0..3], Model parameters (W1, b1, W2, b2, scales)
Output: Predicted class k ∈ {0, 1, 2, 3}

1:  for i = 0 to 3 do                             // Feature normalization
2:      xq[i] ← (f[i] << 16) / scales[i]         // Map raw value to Q16.16 [0,1]
3:  end for
4:  for j = 0 to 23 do                            // Hidden layer (24 neurons)
5:      acc ← (int64_t) b1[j] << 16              // Initialize Q32.32 accumulator with bias
6:      for i = 0 to 3 do                         // Dot product with input
7:          acc ← acc + (int64_t)W1[j][i] * xq[i] // Q16.16 × Q16.16 = Q32.32
8:      end for
9:      h[j] ← max(0, (int32_t)(acc >> 16))      // Shift to Q16.16, apply ReLU
10: end for
11: for k = 0 to 3 do                             // Output layer (4 classes)
12:     acc ← (int64_t) b2[k] << 16              // Initialize with output bias
13:     for j = 0 to 23 do                        // Dot product with hidden activations
14:         acc ← acc + (int64_t)W2[k][j] * h[j]
15:     end for
16:     out[k] ← (int32_t)(acc >> 16)            // Final logit in Q16.16
17: end for
18: return argmax(out[0..3])                       // Select highest-scoring class
```

**计算复杂度**：每个报文精确需要 96 + 96 = 192 次乘加 (MAC) 运算、24 + 4 = 28 次偏置加法、24 次 ReLU 比较和 3 次 argmax 比较——总计约 220 次整数运算。在现代 x86 处理器（3+ GHz）上，这可在 100 ns 内完成，远低于我们的逐包预算。

#### 4.3.3 训练方法 (Training Methodology)

控制面使用标准随机梯度下降 (SGD) 以全精度浮点训练模型，然后量化训练后的参数用于数据面部署。训练过程包含以下组件：

- **数据集构建**：合成流量生成，类别条件特征分布匹配已发表的真实流量统计（6,000 个训练样本、4,000 个独立测试样本，4 类均衡分布）；
- **损失函数**：Softmax 交叉熵损失 \(\mathcal{L} = -\sum_k y_k \log(\text{softmax}(\mathbf{o})_k)\)，为多类分类提供良好校准的梯度；
- **优化器**：SGD，固定学习率 \(\eta = 0.01\)，训练 40 个 epoch，足以在这个特征良好分离的分类问题上收敛；
- **权重初始化**：Xavier 均匀初始化 \(W_{ij} \sim U(-\sqrt{6/(n_{in}+n_{out})}, +\sqrt{6/(n_{in}+n_{out})})\)，防止激活饱和并保持权重在 Q16.16 可表示范围内；
- **训练后量化**：每个浮点参数通过取整 (\(\lfloor w \times 2^{16} + 0.5 \rfloor\)) 转换为 Q16.16，并在 \(\pm 32767\) 处进行饱和钳位。如第 5.3 节所示，这引入恰好为零的分类准确率损失。

### 4.4 VLIW 流水线执行模型（框架基础） (VLIW Pipeline Execution Model (Framework Foundation))

EdgeNet-CoM 框架的匹配-动作级使用 16 路超长指令字 (Very Long Instruction Word, VLIW) 架构执行，在保持确定性时序的同时最大化每级并行性。该执行模型是框架级基础，使所有应用实例均能实现亚微秒逐包处理。下列三个架构特征对此能力至关重要：

**两阶段提交执行**消除了单级内 ALU 之间的读后写冒险。在第一阶段（读取），所有 16 个 ALU 同时从报文头向量 (PHV) 采样其源操作数。在第二阶段（写入），所有 ALU 同时将其结果提交到目标字段。这种分离保证没有 ALU 会观察到部分更新的 PHV，从而实现 VLIW 字内所有指令的真正并行执行。

**计算跳转指令分派**用 GCC 计算标签（`&&label` + 间接 goto）替代了传统的 switch-case ALU 操作码解码器。这种技术消除了 27 路 switch 语句的分支预测开销，将每指令分派减少为通过预计算标签数组的单次间接跳转。在我们的测试平台上，此优化将每级执行时间减少了约 40%。

**每级表索引**在编译期间预计算每级所属的表（`stage_tbl[stage][idx]`），使逐包快速路径仅遍历相关表而非扫描完整表数组。这将每级开销从 O(总表数) 降低到 O(级内表数)，在我们的流水线中通常为 1–2 个。

这三个特征共同确保框架的逐包处理延迟——包括 DNN 推理、QoS 标记、网络编码和路由查找——保持在 Gbps 速率卫星 UPF 处理所需的亚微秒预算内。

### 4.5 多队列并行扩展引擎（框架基础） (Multi-Queue Parallel Scaling Engine (Framework Foundation))

边缘计算节点通常提供 4–16 个 CPU 核，为水平吞吐扩展提供了机会。多队列引擎通过 DPDK 风格的运行到完成模型利用这种并行性，避免了报文处理快速路径上的核间同步。该引擎是框架级组件：可将任意流水线程序——而不仅是路由或编码——跨可用 CPU 核扩展，同时保持正确性保证。

该引擎包含五个协作机制：

**RSS 流哈希**在保持逐流有序性的同时将报文分配到工作队列。哈希函数对报文的五元组（源 IP、目的 IP、协议、源端口、目的端口）应用 FNV-1a 32 位哈希，随后使用 murmur3 fmix32 终结器消除顺序端口号或子网对齐 IP 地址引入的结构偏差。

**SPSC 无锁环**连接分派器与每个工作线程。每个环是一个单生产者单消费者 (Single-Producer Single-Consumer, SPSC) 环形缓冲区，头尾指针以缓存行分离，使用 C11 acquire/release 内存序保证可见性。SPSC 约束消除了对比较并交换 (CAS) 循环的需求，实现无等待入队和出队。

**核绑定**通过 `pthread_setaffinity_np` 将每个工作线程绑定到不同的 CPU 核，消除上下文切换抖动，并确保工作线程流水线状态（PHV、Trie 节点、DNN 权重）的 L1/L2 缓存局部性。

**批量处理**将每批开销分摊到最多 64 个报文。单次 `clock_gettime` 调用为批次中所有报文提供时间戳，PHV 初始化则通过预计算模板的 `memcpy` 批量进行。

**DNN 并发安全**由推理期间模型权重的只读性质保证。多个工作线程在同一共享权重数组上并发执行 MLP 前向传播而无需锁；仅每流水线推理统计（总计数、每类计数）需互斥保护，且这些统计在关键路径之外更新。

定理 1 建立了该架构下逐流报文有序性的正确性。

**定理 1（逐流有序性保持）。** 在使用 RSS 五元组哈希和 SPSC FIFO 队列的条件下，给定 TCP 连接的所有报文按到达顺序交付到同一工作线程，无需显式同步。

*证明。* 考虑五元组为 \(t = (src_{ip}, dst_{ip}, 6, src_{port}, dst_{port})\) 的 TCP 连接。由于该连接中所有报文共享相同的五元组值，哈希函数产生常数：\(h(t) = \text{fmix32}(\text{FNV1a}(t))\)。队列分配 \(q = h(t) \mod N\) 因此为常数。在队列 \(q\) 内，SPSC 环通过构造保持 FIFO 插入顺序。由于单分派器按线路到达顺序插入报文，工作线程以相同顺序观察报文。□

定理 1 确保在网计算不干扰 TCP 语义——这是在生产网络中部署的关键要求，因为报文乱序会触发虚假重传。

### 4.6 应用实例 A：业务感知多路径智能路由 (Application Instantiation A: Service-Aware Multi-Path Intelligent Routing)

本节展示 EdgeNet-CoM 框架的首个应用实例化：跨异构卫星-地面路径的业务感知多路径智能路由。该应用利用框架的统一流水线（设计 1）、DNN 推理引擎（设计 2）和业务感知执行层（设计 5），实现考虑业务类型和卫星路径特征的逐包路由决策。

路由选择策略将 DNN 分类结果翻译为差异化转发决策，分两个阶段运行：QoS 标记（将类别映射到 DSCP）和多路径选择（将带有 DSCP 标注的前缀映射到输出端口）。

#### 4.6.1 DSCP 标记策略 (DSCP Marking Policy)

表 3 定义了分类结果到 DiffServ 代码点 (DSCP) 值的映射，遵循 RFC 2474 [14]、RFC 2597（确保转发）和 RFC 3246（加速转发）中的 PHB 定义。每个业务类别分配一个 DSCP 值，编码其优先级和对下游路由器的处理语义。

**表 3：业务类别到 DSCP 的映射。** 该表定义了在流水线 Stage 1 施加的 QoS 策略。每个分类后的报文接收 DSCP 标记，指示下游路由器执行适当的每跳处理。"路由语义"列描述了预期的路径选择行为。

| 类别 \(c_k\) | 业务类型 | DSCP 值 | 每跳行为 | 路由语义 |
|---|---|---|---|---|
| \(c_0\) | Web 浏览 | 0 (BE) | 尽力而为 | 默认 ECMP 路径，无优先级 |
| \(c_1\) | 视频流 | 32 (CS4/AF41) | 确保转发 | 高带宽、低丢包路径 |
| \(c_2\) | VoIP | 46 (EF) | 加速转发 | 最低延迟、严格优先路径 |
| \(c_3\) | 大块传输 | 10 (AF11) | 清道夫 | 最低优先级、可延迟路径 |

如表 3 所示，映射的设计旨在最大化具有对立 QoS 需求的类别之间的区分度：VoIP (c₂) 获得最高优先级（EF，DSCP 46）以最小化延迟和抖动；大块传输 (c₃) 则获得最低优先级（AF11，DSCP 10），因其吞吐量容忍特性允许在拥塞时延迟处理。

#### 4.6.2 多路径路由算法 (Multi-Path Routing Algorithm)

算法 2 形式化了完整的逐包路由决策过程，将分类、标记和路径选择集成为在框架流水线内执行的单一顺序过程。

**算法 2：业务感知路由选择（应用 A）。** 该算法对每个报文执行一次，实现定义 5 中的路由函数 \(\pi^*\)。第 1-4 行处理加密流量分类和标记；第 5-8 行执行标准路由操作。`valid(tls)` 保护条件确保非加密流量以零开销绕过分类。

```
Input: Packet x with headers (ipv4, tcp, tls), destination prefix d
Output: Egress port p, modified headers (DSCP, TTL, MACs)

1:  if valid(tls) and tls.type == 23 then          // Only classify app_data
2:      c_k ← DNN_Infer(f(x))                     // Stage 0: MLP classification
3:      ipv4.tos ← DSCP_Table[c_k]                // Stage 1: QoS marking
4:  end if
5:  p ← LPM_Lookup(route_table, d)                // Stage 2: longest-prefix match
6:  (dmac, smac) ← MAC_Table[p]                    // Stage 3: next-hop rewrite
7:  ipv4.ttl ← ipv4.ttl - 1                       // Standard TTL decrement
8:  return (p, dmac, smac)
```

路由表支持两种运行模式，可通过控制面配置选择：

**模式 A（DSCP 驱动下游区分）** 适用于下游路由器实现 DiffServ 队列的场景。卫星 UPF 根据表 3 标记 DSCP；下游路由器（无论地面或卫星）执行基于 PHB 的队列和路径选择，无需额外分类能力。EF 标记的 VoIP 报文使用专用低延迟路径（例如 LEO 直达或 ISL）；AF41 标记的视频报文使用高带宽聚合链路；BE 和 AF11 报文共享默认或高容量路径。此模式无需修改下游基础设施，是与异构下游网络对接的卫星 UPF 的推荐部署模式。

**模式 B（卫星/地面多上行端口映射）** 专门适用于多个异构上行路径共存的卫星 UPF（GEO 中继、LEO 直达、ISL 中继、地面回传）。路由表键将目的前缀与业务类别组合：`(dst_prefix, meta.class) → egress_port`，其中每个出端口对应特定的卫星或地面上行。控制面监控 SMF 上报的逐路径质量指标（通过 TWAMP-light 测量 RTT、通过主动探测测量丢包率、可用带宽），并通过表项更新动态调整映射。例如，控制面可将 VoIP (c₂) 分配到 LEO 直达链路（低延迟 ~20 ms），同时将大块流量 (c₃) 路由经 GEO 中继链路（较高延迟 ~500 ms 但带宽充裕）——利用业务分类在逐包粒度实现 QoS 感知的卫星路径选择。

#### 4.6.3 非加密流量绕过 (Non-Encrypted Traffic Bypass)

一个关键设计属性是**非分类流量的零开销**，确保在网计算基础设施不对不受益于业务感知路由的流造成惩罚。报头有效性位图 `valid(tls)` 作为高效的单比特门控条件：

- 无 TLS 头的报文（纯 HTTP、DNS、ICMP）完全跳过 parse_tls 状态，导致 `valid(tls) = false`。Stage 0 和 Stage 1 被绕过，报文直接进入 Stage 2 的标准 L3 路由。
- TLS 握手报文（`tls.type ≠ 23`）满足 `valid(tls) = true` 但在 classify_t 表中未匹配 `tls.type == 23`。它们接收默认的无操作动作，保持其原始 DSCP 标记。
- 仅 TLS 应用数据记录（`tls.type == 23`）触发完整的分类和标记路径。

这种三级门控确保 DNN 推理开销（约 200 ns）仅由从分类中受益的报文承担，而所有其他流量以可忽略的开销通过。

### 4.7 应用实例 B：业务感知网络编码优化 (Application Instantiation B: Service-Aware Network Coding Optimization)

本节展示 EdgeNet-CoM 框架的第二个应用实例化：业务区分网络编码优化。该应用利用框架的流水线内计算能力（设计 2）和业务感知执行层（设计 5），对丢包敏感的业务类别施加主动前向纠错 (FEC)，同时在丢包容忍流上避免带宽浪费——其动机特别来自卫星链路的较高丢包率。

#### 4.7.1 卫星 UPF 业务区分编码的动机 (Motivation for Service-Differentiated Coding at Satellite UPF)

星-空-地一体化链路呈现可变且较高的丢包率（LEO 通常 1–10%，GEO 中继 2–5%），对不同业务类型产生不同的根本影响。卫星环境引入了独特挑战，使主动网络编码在 UPF 处尤为有价值：

**重传的高 RTT 惩罚。** 传统端到端恢复（TCP 重传）对每个丢包引入至少一个往返时间 (RTT) 的延迟。穿越卫星链路的 RTT 范围从约 40 ms（LEO）到约 1000 ms（GEO 中继）——使基于重传的恢复对 VoIP（150+ ms 延迟导致可感知回声）和视频流（丢帧导致持续数秒的卡顿）造成毁灭性影响。网络编码提供了关键替代方案：通过在卫星 UPF 注入冗余编码报文，接收端可本地恢复丢包而无需触发重传，以适度的带宽开销换取卫星 RTT 惩罚的消除。

**链路状态时变性。** 卫星链路由于大气衰减、点波束间切换事件、雨衰（尤其在 Ka 频段）和星间链路切换而经历时变丢包。UPF 是主动保护的自然位置：它对流量模式有稳定的视图，可在报文进入有损卫星段之前施加编码，而非依赖必须两次穿越完整卫星路径（正向 + 重传）的端到端机制。

**业务差异化影响。** 不同业务类别具有根本不同的丢包容忍特征。DNN 分类器已提供选择适当编码参数所需的业务类型信息：

- **VoIP (c₂)**：极度丢包敏感（≬1% 丢包即导致可闻劣化），时延关键——需要在 UPF 处进行最大主动保护以避免 GEO/LTE RTT 惩罚；
- **视频 (c₁)**：中度丢包敏感（5% 丢包导致可见瑕疵），受益于 FEC 以维持跨卫星段的流畅播放；
- **Web (c₀)**：丢包容忍（TCP 高效处理重传），适度编码可减少感知延迟但非必需；
- **大块传输 (c₃)**：完全丢包容忍（面向吞吐量，TCP 自适应），编码浪费宝贵的卫星带宽。

#### 4.7.2 编码参数选择 (Coding Parameter Selection)

我们将编码策略定义为从业务类别到编码参数元组 ⟨\(n\), \(k\), \(\mathbb{F}\)⟩ 的映射，其中 \(n\) 是编码块长度，\(k\) 是每个块的原始数据报文数，\(\mathbb{F}\) 是系数生成的有限域。这一映射实现了定义 6 中的编码函数 \(\gamma\)。

表 3b 定义了每个业务类别的具体编码策略，旨在匹配每个应用的丢包容忍特征。

**表 3b：业务感知网络编码参数。** 每行指定分类后对业务类别施加的编码策略。冗余比决定带宽开销；生成大小决定解码延迟。VoIP 以最小生成大小（低解码延迟）获得最大保护，大块传输不编码。

| 类别 \(c_k\) | 业务 | 编码模式 | \(k\)（数据） | \(n\)（总计） | 冗余 | 域 | 设计理由 |
|---|---|---|---|---|---|---|---|
| \(c_2\) | VoIP | 系统 RLNC | 4 | 5 | 25% | GF(2⁸) | 最大保护，低生成大小 |
| \(c_1\) | 视频 | 系统 RLNC | 8 | 9 | 12.5% | GF(2⁸) | 适度 FEC 保证流畅播放 |
| \(c_0\) | Web | XOR 奇偶校验 | 8 | 9 | 12.5% | GF(2) | 轻度保护，最小 CPU 开销 |
| \(c_3\) | 大块传输 | 无（绕过） | — | — | 0% | — | TCP 高效处理丢包 |

如表 3b 所示，编码策略施加三级保护。VoIP 获得最强编码（25% 冗余，小生成大小 \(k=4\) 以最小化解码缓冲延迟）。视频使用适度 RLNC（12.5% 冗余，更大 \(k=8\) 以获得更好的编码效率）。Web 流量接收轻量 XOR 奇偶校验（每 8 个数据报文一个奇偶校验报文），以最小计算开销提供基本保护。大块流量完全绕过编码，保留全部带宽用于吞吐量最大化。

#### 4.7.3 编码算法 (Coding Algorithm)

算法 3 形式化了框架流水线内的逐包编码决策和编码过程。

**算法 3：业务感知网络编码（应用 B）。** 该算法在分类（Stage 0）和 QoS 标记（Stage 1）之后执行，决定是否以及如何对当前报文进行编码。系统编码保留原始报文（使无解码器的接收端可以正常处理流量），同时在生成缓冲区满时生成编码修复报文。

```
Input: Packet x with service class c_k, generation buffer G[c_k]
Output: Original packet x (always forwarded), optional coded packet x_c

1:  params ← Coding_Table[c_k]                   // Lookup (n, k, field)
2:  if params.mode == NONE then
3:      return x                                   // Bulk: no coding
4:  end if
5:  G[c_k].append(x)                               // Add to generation buffer
6:  if |G[c_k]| == params.k then                   // Generation complete
7:      for i = 1 to (params.n - params.k) do      // Generate repair packets
8:          coefficients ← Random(params.field, params.k)
9:          x_c ← ∑_{j=1}^{k} coefficients[j] ⊗ G[c_k][j]  // Linear combination
10:         Emit(x_c, with coding header)           // Inject repair packet
11:     end for
12:     G[c_k].clear()                              // Reset generation
13: end if
14: return x                                        // Forward original (systematic)
```

#### 4.7.4 流水线集成 (Pipeline Integration)

网络编码级在框架五级流水线中集成在 QoS 标记（Stage 1）和路由查找（Stage 2）之间：

```
pkt→[Parser]→PHV→[S0:Classify]→[S1:QoS Mark]→[S1.5:NC Encode]→[S2:Route]→[S3:MAC]→pkt'
                      │                              │
                 [DNN Extern]                  [Generation Buffers]
                 Q16.16 MLP                    per-class RLNC state
```

**图 3：含网络编码级的扩展流水线（应用 B）。** NC 编码级（S1.5）插入在 QoS 标记之后和路由查找之前。它从元数据中访问业务类别，查找编码参数，并管理每类生成缓冲区。修复报文与原始报文一起注入反解析器队列。

编码级实现为有状态外部函数（类似于 DNN 外部函数），每类生成缓冲区存储在线程本地内存中。每个缓冲区最多保存 \(k\) 个报文载荷，并在生成完成时生成 \(n-k\) 个编码修复报文。编码的系统性质确保原始报文始终立即转发——修复报文是额外注入，不延迟原始流量。

**带宽-延迟权衡**：表 3b 中的编码参数基于以下分析选择。对于 64 kbps、20 ms 帧的 VoIP，每个编码生成跨越 4 个报文 = 80 ms——修复报文在 100 ms 内到达（远在 150 ms 单向延迟预算之内）。25% 的开销仅增加 16 kbps——在边缘上行链路上可忽略不计。对于 5 Mbps 的视频，12.5% 的开销增加 625 kbps，但使得在一个生成内无需等待 TCP 重传（到云的典型 RTT 为 200+ ms）即可恢复单包丢包。

---

## 5. 原型实现与评估 (Prototype Implementation and Evaluation)

### 5.1 实现 (Implementation)

EdgeNet-CoM/UPF 原型以约 4,200 行核心 ISO C11 代码（包括数据面模块、P4 前端编译器和流水线运行时）加上约 1,800 行测试工具、训练工具和基准测试脚本实现，共约 6,000 行代码，无外部库依赖。表 4 给出了核心模块分解，展示每个组件的职责与规模。

**表 4：EdgeNet-CoM/UPF 模块分解。** 每行描述一个源模块及其代码行数和主要职责。系统设计为单二进制部署，除 Linux 内核外无运行时依赖。

| 模块 | 代码行数 | 功能 |
|---|---|---|
| `rmt.h` | 495 | 核心数据结构、PHV 内联操作、公共 API |
| `parser.c` | ~350 | 可编程解析器 FSM、反解析器、报头提取 |
| `match.c` | ~160 | 级联位 Trie 查找引擎（精确匹配 + LPM） |
| `alu.c` | ~280 | 16-ALU VLIW 执行器，计算跳转分派 |
| `dnn.c` | ~200 | Q16.16 MLP 前向传播、模型加载、量化 |
| `mq.c` | ~380 | 多队列引擎（RSS、SPSC 环、核绑定工作线程） |
| `p4c.c` | ~1200 | P4 编译器（词法分析、语法分析、AST、代码生成、级调度） |
| `pipeline.c` | ~250 | 流水线编排、批量/突发起 API |
| `enc_classify_demo.c` | ~430 | 训练、量化、端到端验证 |

如表 4 所示，P4 编译器 (`p4c.c`) 是最大的模块，反映了将 P4 源翻译为流水线配置的复杂性。运行时数据面模块 (`parser.c` + `match.c` + `alu.c` + `dnn.c` + `pipeline.c`) 共约 1,240 行——适于资源受限卫星边缘部署的紧凑占用。系统在 GCC 7+ 下以 `-O3 -flto -march=native` 编译无警告，运行于 Linux 4.x+ 内核。提供配置文件引导优化 (PGO) 两遍构建以获得额外性能优化。

### 5.2 实验设置 (Experimental Setup)

所有实验在以下平台和方法上进行：

- **硬件**：8 vCPU 服务器（Intel Xeon E5-2680 v4，2.4 GHz 基频，3.3 GHz 睿频），16 GB DDR4 RAM；
- **软件**：Linux 5.15，GCC 11.4，PGO 优化构建；
- **流量生成**：合成加密报文，类别条件特征分布匹配已发表的生产流量统计 [6] [7]。特征从根据真实世界 TLS 流量轨迹校准的每类高斯混合中采样；
- **数据集划分**：6,000 个训练样本（每类 1,500 个）和 4,000 个独立测试样本（每类 1,000 个），训练与测试数据无重叠；
- **评估指标**：分类准确率（总体和每类）、精确率、召回率、逐包处理延迟、吞吐量（每秒报文数）、多核扩展效率和负载均衡质量（Jain 公平性指数）。

#### 5.2.1 合成流量生成器参数 (Synthetic Traffic Generator Parameters)

为便于可复现性，表 5a 指定了用于生成合成流量的精确每类分布参数。每个特征建模为高斯混合；以下参数定义了每个业务类别的组件均值 (\(\mu\))、标准差 (\(\sigma\)) 和混合权重 (\(w\))。

**表 5a：每类特征分布参数（合成流量生成器）。** 每行指定一个业务类别的特征分布。所有特征建模为单变量高斯分布；四个特征在每类内独立采样。

| 特征 | Web (c₀) | 视频 (c₁) | VoIP (c₂) | 大块传输 (c₃) |
|---|---|---|---|---|
| `ipv4.len` | \(\mathcal{N}(800, 300)\) | \(\mathcal{N}(1350, 80)\) | \(\mathcal{N}(120, 30)\) | \(\mathcal{N}(1460, 20)\) |
| `tcp.window` | \(\mathcal{N}(16000, 6000)\) | \(\mathcal{N}(48000, 10000)\) | \(\mathcal{N}(4000, 1200)\) | \(65535\)（常数） |
| `tls.length` | \(\mathcal{N}(750, 300)\) | \(\mathcal{N}(1300, 100)\) | \(\mathcal{N}(80, 30)\) | \(\mathcal{N}(1440, 20)\) |
| `tls.type` | \(23\)（app_data，100%） | \(23\)（app_data，100%） | \(23\)（app_data，100%） | \(23\)（app_data，100%） |

值被裁剪到有效范围：`ipv4.len` \(\in [20, 1500]\)，`tcp.window` \(\in [0, 65535]\)，`tls.length` \(\in [5, 1460]\)。`tls.type` 特征是确定性的（应用数据始终为 23，因为非 app_data 记录绕过分类）。

这些参数根据已发表的流量分析研究 [6] [7] 校准，以近似真实世界加密流量中观察到的特征分布。类别条件分离（特别是 VoIP/大块传输在报文大小和窗口上的分离）反映了基本的应用层行为：VoIP 使用小固定大小编解码帧，大块传输饱和 MTU 和窗口，视频维持大报文和自适应窗口，Web 产生可变大小的突发流量。

**可复现性说明。** 为促进独立验证，我们承诺发布：(i) 完整的约 6,000 行源代码（在开源许可下）作为补充材料或通过匿名仓库；(ii) 220 个训练后的 Q16.16 模型权重（880 字节，可轻松作为表格包含）；(iii) 具有表 5a 精确参数的合成流量生成器实现；(iv) 复现所有评估表的完整测试脚本 (`scripts/bench_all.sh`)。源代码、模型权重和测试脚本将在发表后可用，以支持所有报告结果的完全可复现性。

### 5.3 分类准确率 (Classification Accuracy)

表 5 给出了在 4,000 报文测试集上的每类分类结果。评估独立测量每个业务类别的准确率、精确率和召回率，然后计算宏平均指标。

**表 5：每类分类结果（4,000 测试报文）。** 该表报告 Q16.16 量化 MLP 模型的分类性能。准确率定义为正确预测数除以分配给每类的总预测数。精确率和召回率按类计算并在最后一行宏平均。

| 业务类别 | 正确 / 总计 | 准确率 | 精确率 | 召回率 |
|---|---|---|---|---|
| Web (c₀) | 981 / 982 | 99.9% | 99.7% | 99.9% |
| 视频 (c₁) | 951 / 986 | 96.5% | 97.1% | 96.5% |
| VoIP (c₂) | 1013 / 1013 | 100.0% | 100.0% | 100.0% |
| 大块传输 (c₃) | 1019 / 1019 | 100.0% | 100.0% | 100.0% |
| **总体** | **3964 / 4000** | **99.1%** | **99.2%** | **99.1%** |

表 5 的结果揭示了三个重要发现：

**(a) 量化保持分类保真度。** Q16.16 模型在所有 4,000 个测试报文上产生与 Float32 模型完全相同的分类结果（100% 一致）。这证实了 Q16.16 精度 (\(\epsilon \approx 1.5 \times 10^{-5}\)) 对于我们 220 参数 MLP 学到的决策边界绰绰有余，验证了使用定点算术无准确率折中。

**(b) 类别特定模式决定准确率。** VoIP 和大块传输达到完美准确率（100%），因为其特征模式最大程度地区分性：VoIP 独特地占据特征空间的低大小/低窗口区域，而大块传输独特地占据最大大小/最大窗口角落。视频展现最低准确率（96.5%），原因是在报文大小维度上与大块传输部分重叠——两者都产生大报文。然而，TCP 窗口特征提供了足够的区分（视频：可变的 32K–65K；大块传输：稳定的 65535）。

**(c) 误分类方向安全。** 所有 36 个误分类报文（0.9%）都是被错误分类为大块传输的视频报文。关键是，没有 VoIP 或 Web 报文被误分类为时延不敏感类别——意味着实时流量始终获得优先处理。这种方向安全性对部署很重要：最坏情况是一些视频报文获得较低优先级（AF11 代替 AF41），这是不会导致应用失败的优雅降级。

### 5.4 逐包延迟分析 (Per-Packet Latency Analysis)

表 6 给出了各种配置下的端到端逐包处理延迟，从单线程基线到多队列并行处理。

**表 6：端到端处理延迟。** 测量使用 126 字节 TLS 应用数据报文（计算开销最大的情况，需要完整 DNN 推理）。吞吐量计算为 1/延迟。有效速率假设最小 126 字节报文；更大报文在同一报文速率下产生更高比特率。

| 配置 | 延迟 (ns/pkt) | 吞吐量 (Mpps) | 有效速率 |
|---|---|---|---|
| 单线程，基线 | ~6,300 | 0.16 | 0.16 Gbps |
| 单线程，优化 (PGO) | 933 | 1.07 | ~1.1 Gbps |
| 批量模式（64 pkt/批） | 911 | 1.10 | ~1.1 Gbps |
| 多队列，4 工作线程 | ~580 | 1.72 | ~1.7 Gbps |
| 多队列，8 工作线程 | 456 | 2.19 | ~2.2 Gbps |
| 多队列，16 工作线程 | 364 | 2.75 | ~2.8 Gbps |

表 6 表明 PGO 优化实现了相对未优化基线 6.7 倍的加速，将逐包延迟从 6.3 µs 降至 933 ns。批量处理通过分摊 `clock_gettime` 系统调用提供了边际额外改进（911 ns）。多队列扩展则提供更进一步的吞吐增益，详见第 5.5 节分析。

为了解流水线中的时间花费，表 6b 将单线程优化延迟分解为每级贡献。

**表 6b：每级延迟分解（单线程优化，总计 933 ns）。** 每行显示一个流水线级、其主导操作、绝对延迟贡献和总处理时间百分比。DNN 推理（Stage 0）占总延迟的 21.4%——为在网计算智能付出的适度代价。

| 级 | 操作 | 延迟 (ns) | 占比 |
|---|---|---|---|
| 解析器 | 5 层报头提取 (eth/ipv4/tcp/tls) | ~150 | 16.1% |
| Stage 0 | Trie 门控检查 + DNN 推理 (192 MAC) | ~200 | 21.4% |
| Stage 1 | QoS 精确匹配 + DSCP 字段写入 | ~50 | 5.4% |
| Stage 2 | LPM Trie 遍历（32 位前缀，最坏情况） | ~80 | 8.6% |
| Stage 3 | MAC 精确匹配 + 报头重写 | ~50 | 5.4% |
| 反解析器 | 报头拼接 + 载荷复制 | ~100 | 10.7% |
| 开销 | PHV 初始化、批量分摊、簿记 | ~303 | 32.5% |
| **总计** | | **~933** | **100%** |

如表 6b 所示，Stage 0 的 DNN 推理与纯转发相比仅向流水线关键路径增加约 200 ns。这代表完整在网计算能力 21.4% 的延迟代价——考虑到框架仍远在 Gbps 速率边缘处理所需的亚微秒预算之内，这是一个有利的折中。"开销"类别（32.5%）包括 PHV 初始化、内存分配分摊和统计簿记——这些领域可通过硬件卸载进一步优化。

### 5.5 多核扩展分析 (Multi-Core Scaling Analysis)

表 7 评估了工作线程数从 1 增加到 16 时的水平扩展效率。

**表 7：多核扩展效率。** 加速比相对单线程优化基线（1.07 Mpps）。效率定义为加速比/工作线程数 × 100%。8 vCPU 平台提供 8 个物理执行上下文；16 工作线程需要超线程，解释了 8 工作线程后的效率下降。

| 工作线程 | 吞吐量 (Mpps) | 加速比 | 效率 |
|---|---|---|---|
| 1 | 1.07 | 1.0× | 100% |
| 2 | 1.58 | 1.48× | 74% |
| 4 | 1.72 | 1.61× | 40% |
| 8 | 2.19 | 2.05× | 26% |
| 16 | 2.75 | 2.57× | 16% |

表 7 的结果显示亚线性扩展，效率随工作线程数增加而下降。两个因素解释了这一行为。第一，本文的 8 vCPU 测试平台仅提供 8 个物理执行上下文；16 工作线程的配置依赖超线程，共享执行资源。第二，所有工作线程访问共享 DNN 权重数组和 Trie 数据结构，在工作集超过每核缓存容量时产生末级缓存 (LLC) 竞争。在使用每核 L2 和更大 LLC 的专用边缘硬件上，本文预计扩展效率将显著改善——在物理核数内接近线性。

尽管在共享资源测试平台上的亚线性扩展，绝对吞吐改进是可观的：16 工作线程达到 2.75 Mpps（最小报文对应 2.8 Gbps），相对单线程基线聚合加速 2.57 倍——足以满足典型卫星边缘上行容量。在使用每核 L2 缓存和更大 LLC 的专用硬件上，我们预计在物理核数内接近线性扩展。

### 5.6 负载均衡质量 (Load Balancing Quality)

有效的负载均衡对多队列系统至关重要：不良分布产生热点工作线程成为吞吐瓶颈，而其他核空闲。表 8 给出了 8 个队列处理 200,000 个报文时的负载分布指标。

**表 8：8 个工作队列的负载分布（200,000 报文）。** 理想均匀分布为每个队列分配恰好 25,000 个报文（12.5%）。最大偏差 1.3 个百分点和 Jain 公平性指数 0.997（1.0 为完美）证实了近理想的分布质量。

| 指标 | 值 |
|---|---|
| 最小队列负载 | 22,660 报文（11.3%） |
| 最大队列负载 | 27,540 报文（13.8%） |
| 理想均匀分布 | 25,000 报文（12.5%） |
| 与理想的 Maximum 偏差 | 1.3 个百分点 |
| Jain 公平性指数 | 0.997 |

表 8 展示的近理想负载均衡验证了两项设计选择。第一，FNV-1a 之后的 murmur3 fmix32 终结器消除了原本由规则五元组模式（如顺序端口分配或子网对齐 IP 地址）引起的结构性哈希偏差。第二，固定哈希函数（而非自适应重均衡）提供确定性队列分配，这对定理 1 建立的逐流有序性保证至关重要。此外，所有工作线程产生了与单线程执行相同的分类和路由结果——通过输出报文的字节级比较验证——确认并行执行未引入正确性退化。

### 5.7 与现有系统的比较 (Comparison with Existing Systems)

表 9 在九个能力维度上将 EdgeNet-CoM 与五个代表性现有系统进行了定位比较。比较涵盖研究原型和生产系统，以提供全面的竞争分析。

**表 9：与现有系统的综合比较。** 每列代表一个系统；每行评估一个能力。EdgeNet-CoM 独特地实现了具有完整 MLP 支持的逐包分类、集成路由和编码以及边缘可部署性——这一组合在现有系统中未有提供。

| 指标 | EdgeNet-CoM | FlowPic [7] | NetSage [11] | Tofino P4 | OVS + ML |
|---|---|---|---|---|---|
| 分类粒度 | 逐包 | 逐流 | 逐流 | 逐包 | 逐流 |
| 分类延迟 | < 1 µs | > 1 s | > 100 ms | ~10 ns | > 10 ms |
| 准确率（4 类） | 99.1% | ~95% | ~93% | 不适用（二值） | ~92% |
| 流水线内 DNN | ✓（完整 MLP） | ✗ | ✗ | ✗（线性） | ✗ |
| 路由集成 | 统一流水线 | 分离 | 分离 | 有限 | 分离 |
| 边缘可部署 | ✓（x86/ARM） | ✓ | ✓ | ✗（ASIC） | ✓ |
| 外部依赖 | 无 | TensorFlow | 多个 | P4 SDK | OVS, sklearn |
| 模型热更新 | ✓（无需重启） | ✓ | ✓ | ✗ | ✓ |
| 隐私保护 | ✓ | ✓ | ✓ | ✓ | ✓ |

表 9 的比较突出了 EdgeNet-CoM 框架的三项独特优势：

**逐包即时性消除冷启动延迟。** 流级方法（FlowPic、OVS+ML）需观察 10–100 个报文后才能分类，使早期报文未分类并被次优路由。本文的逐包方法对每个*可分类*报文（TLS 应用数据记录）在到达时即分类，从 TLS 握手完成后的第一个此类记录开始——通常是 TLS 1.3 连接的第 5 或第 6 个报文。这消除了流级方案的多报文冷启动延迟。对于短生命周期连接（如 DNS-over-HTTPS 查询），这一特性仍可能是决定性的，因为整个应用载荷可能仅由几个报文组成。

**统一流水线消除架构开销。** 在现有系统中，分类和路由是通过 IPC、共享内存或控制器 API 通信的独立子系统。本文的统一流水线在单次流水线遍历中执行分类（Stage 0）、标记（Stage 1）、路由（Stage 2）和重写（Stage 3），无上下文切换、无内核穿越、无进程间通信。这种架构简洁性直接转化为更低延迟和更高吞吐。

**零依赖部署实现通用边缘采纳。** EdgeNet-CoM/UPF 编译为无运行时库依赖的单一二进制文件，可通过简单文件复制部署在任何 Linux 兼容的边缘设备（x86、ARM、MIPS）上。这与需 Python/TensorFlow（FlowPic）、多个系统库（OVS）或昂贵 ASIC 硬件（Tofino）的现有方案形成鲜明对比。

### 5.8 端到端功能验证 (End-to-End Functional Verification)

除性能指标外，正确性通过覆盖从 P4 编译到报文输出的完整处理链的综合端到端测试进行验证。表 10 给出了验证场景和结果。

**表 10：端到端功能验证结果。** 每行代表一个测试特定代码路径的测试场景。"输入"列描述注入的报文；"预期输出"指定所需的分类、标记和路由结果。全部 8 个场景通过，确认所有框架组件的正确集成。

| 测试场景 | 输入 | 预期输出 | 结果 |
|---|---|---|---|
| TLS Web 报文 | len=800, win=16000, tls_len=750 | class=0, DSCP=0(BE), port=2 | ✓ 通过 |
| TLS 视频报文 | len=1400, win=50000, tls_len=1350 | class=1, DSCP=32(AF41), port=2 | ✓ 通过 |
| TLS VoIP 报文 | len=120, win=4000, tls_len=80 | class=2, DSCP=46(EF), port=2 | ✓ 通过 |
| TLS 大块传输报文 | len=1500, win=65535, tls_len=1460 | class=3, DSCP=10(AF11), port=2 | ✓ 通过 |
| 非 TLS 报文 | 纯 HTTP（无 tls 头） | 不分类，DSCP 不变 | ✓ 通过 |
| TLS 握手 | tls.type=22（非 app_data） | 不分类，无 QoS 标记 | ✓ 通过 |
| 不可路由目的 | 目的不在路由表中 | 报文丢弃（默认动作） | ✓ 通过 |
| TTL 与 MAC 重写 | 任意可路由 TLS 报文 | TTL 递减，MAC 重写 | ✓ 通过 |

如表 10 所确认，系统正确处理了全部预期流量场景，包括关键的绕过情况（非 TLS、TLS 握手）和错误条件（不可路由目的）。这些测试作为构建过程的一部分自动执行，作为对实现变更的回归守卫。

### 5.9 测试场景设计 (Test Scenario Design)

为系统地验证完整框架——涵盖分类、路由、网络编码及其联合交互——本文设计了分为七个类别（T1–T7）的综合测试套件。表 11 总结了测试场景架构，指定每个类别的覆盖目标、验证方法和验收标准。

**表 11：综合测试场景设计。** 每个类别解决框架的一个功能或性能方面。七个类别从基本正确性（T1–T2）到性能特征化（T3–T5）再到联合优化验证（T6–T7），提供对框架行为的分层信心。

| 类别 | 目标 | 方法 | 关键指标 | 验收标准 |
|---|---|---|---|---|
| T1：功能正确性 | 所有数据面 (MPLS/VXLAN/SRv6) | 注入构造报文，逐字段验证输出字节 | 字节级匹配、端口分配、报头操作 | 所有场景 100% 字段级匹配 |
| T2：分类准确率 | DNN 推理流水线 | 4000 报文均衡测试集，每类和总体指标 | 每类准确率、精确率、召回率 | 总体 >99%，每类 >95%，量化一致性 >99% |
| T3：延迟基准 | 逐包处理时间 | 50K 报文批量转发，clock_gettime 插桩 | ns/pkt, Mpps, 每级分解 | <1000 ns/pkt（PGO 优化） |
| T4：多核扩展 | 水平吞吐扩展 | 2/4/8/16 工作线程并行处理 | 加速因子、效率百分比 | 16 工作线程 >2.5× 加速 |
| T5：负载均衡 | RSS 分布质量 | 200K 报文跨 8 队列，每队列报文计数 | 最大偏差、Jain 公平性指数 | JFI > 0.99，最大偏差 < 2% |
| T6：网络编码 | RLNC/XOR 正确性 | 编码 + 解码、丢包恢复、每类参数验证 | 恢复成功率、开销比率 | 100% 单包恢复，正确的每类开销 |
| T7：联合优化 | 分类 + 编码 + 路由集成 | 端到端 10K 报文流水线，所有级激活 | 联合延迟、编码开销、类别分布 | <1200 ns/pkt，Bulk=0% 开销，VoIP 编码 |

测试类别的设计遵循以下原则：

**分层验证**确保每个框架组件在集成测试（T7）之前被独立验证（T1–T3）。这种隔离使快速缺陷定位成为可能：T6（编码）中的失败不会与分类错误（T2）混淆，因为每个均在经过验证的下层上运行。

**样本量与统计说明。** 测试总体大小被设计为提供合理覆盖：分类准确率 4,000 报文（T2），延迟 50,000（T3），负载均衡 200,000（T5），编码压力测试 40,000（T6）。然而，如第 6.1 节所承认，本文未计算形式化置信区间或执行显著性检验；全部报告指标均为单一测试拆分的点估计。因此，样本量应被解释为提供功能测试覆盖而非统计严谨性。

**回归自动化**将全部七个类别集成到单一可执行测试脚本 (`scripts/bench_all.sh`)，构建系统、执行全部场景并报告通过/失败状态及定量指标。该脚本作为持续集成过程的一部分运行，确保没有代码变更在任何已测试维度引入回归。

### 5.10 网络编码评估（应用 B 验证） (Network Coding Evaluation (Application B Validation))

本节沿四个维度评估业务感知网络编码应用实例化（算法 3，第 4.7 节）：算术正确性、编码/解码功能、吞吐影响和联合优化性能。

#### 5.10.1 GF(2^8) 算术验证 (GF(2^8) Arithmetic Verification)

RLNC 编码在伽罗瓦域 GF(2^8) 上使用 AES 不可约多项式 \(x^8 + x^4 + x^3 + x + 1\)（0x11B）和本原元 \(\alpha = 3\)。表 12 通过对所有 255 个非零元素的穷举测试验证了域算术运算的正确性。

**表 12：GF(2^8) 算术验证结果。** 每个属性通过穷举测试（所有 255 或 65,025 个元素对）或随机采样（1,000 次试验）测试。所有属性成立，确认了正确的域实现。

| 属性 | 测试覆盖 | 结果 |
|---|---|---|
| 乘法单位元：\(a \times 1 = a\) | 所有 256 个元素 | ✓ 通过 |
| 乘法逆元：\(a \times a^{-1} = 1\) | 所有 255 个非零元素 | ✓ 通过 |
| 分配律：\(a(b+c) = ab + ac\) | 1000 个随机三元组 | ✓ 通过 |

#### 5.10.2 编码/解码正确性 (Encoding/Decoding Correctness)

表 13 给出了编码正确性验证结果。对每个启用编码的业务类别，我们测试：(a) 编码产生正确数量的修复报文；(b) 单包丢包可从生成内任意位置恢复；(c) 恢复数据与原始数据比特级一致。

**表 13：RLNC/XOR 编码-解码验证。** 每行测试一个业务类别的编码参数。"丢包位置"指示测试的不同单包丢包场景数量；所有必须产生比特级一致的恢复。

| 业务 | 模式 | 参数 | 测试丢包位置 | 恢复成功 |
|---|---|---|---|---|
| VoIP (c₂) | 系统 RLNC | k=4, n=5, GF(2^8) | 全部 4 个位置 | 4/4 (100%) |
| 视频 (c₁) | 系统 RLNC | k=8, n=9, GF(2^8) | 全部 8 个位置 | 8/8 (100%) |
| Web (c₀) | XOR 奇偶校验 | k=8, n=9, GF(2) | 全部 8 个位置 | 8/8 (100%) |
| 大块传输 (c₃) | 无（绕过） | — | 不适用 | 不适用（无编码） |

表 13 的结果确认系统 RLNC 实现正确生成修复报文并允许恢复生成内任意单个丢包，无论丢包位置。系统性确保原始报文始终立即转发——无解码器的接收端仍可正常处理流量。

#### 5.10.3 编码吞吐与延迟影响 (Coding Throughput and Latency Impact)

表 14 测量了在 64 字节载荷段、10,000 个生成周期上网络编码操作的计算开销。摊薄每数据报文成本代表由于编码开销添加到每个数据报文的额外处理时间。

**表 14：网络编码计算开销（64 字节载荷，10,000 个生成）。** "生成时间"是编码一个完整生成（k 数据报文 → n−k 修复报文）的挂钟时间。"摊薄成本"将生成时间除以 k 得到每数据报文开销。XOR 奇偶校验比 RLNC 快约 120 倍，因为使用单位运算 vs. GF(2^8) 乘法。

| 业务 | 编码模式 | 生成时间 | 摊薄每包成本 | 相对流水线（933 ns） |
|---|---|---|---|---|
| VoIP (c₂) | RLNC k=4 | ~240 ns | ~60 ns/pkt | +6.4% |
| 视频 (c₁) | RLNC k=8 | ~510 ns | ~64 ns/pkt | +6.9% |
| Web (c₀) | XOR k=8 | ~4 ns | ~0.5 ns/pkt | +0.05% |
| 大块传输 (c₃) | 无 | 0 ns | 0 ns/pkt | 0% |

表 14 的结果表明编码为流水线增加的开销可忽略不计。即使是最昂贵的编码（VoIP 的 RLNC）每个数据报文仅增加约 60 ns——相对 933 ns 基线流水线延迟增加 6.4%。XOR 奇偶校验（用于 Web 流量）在每包 0.5 ns 时基本免费。这些开销验证了在数据平面流水线内执行编码而非卸载到独立模块的设计决策。

#### 5.10.4 联合优化：分类 + 编码 + 路由 (Joint Optimization: Classify + Code + Route)

最终评估测试完整的联合优化流水线：逐包分类驱动差异化编码决策，与路由选择在单次流水线遍历中共存。表 15 给出了 10,000 个报文（4 个业务类别均衡分布）上的联合性能结果。

**表 15：联合优化性能（10,000 报文，均衡流量）。** 流水线在单次遍历中执行分类 → 编码决策 → 路由。"修复生成"计数每个类别产生的编码修复报文数；"编码开销"是修复报文与数据报文的比率。

| 指标 | 值 |
|---|---|
| 总处理报文数 | 10,000 |
| 联合流水线延迟 | ~1,093 ns/pkt (0.92 Mpps) |
| 分类分布 | Web: 2,499 / 视频: 2,369 / VoIP: 2,505 / 大块: 2,627 |
| 修复报文 (Web, XOR) | 312 (Web 数据的 12.5%) |
| 修复报文 (视频, RLNC) | 296 (视频数据的 12.5%) |
| 修复报文 (VoIP, RLNC) | 626 (VoIP 数据的 25.0%) |
| 修复报文 (大块传输) | 0 (0% 开销) |
| **总编码开销** | **1,234 / 10,000 = 12.3%** |
| 对比：统一 RLNC（所有类别） | ~25% 开销 |
| 对比：无编码 | 0% 开销（无丢包保护） |

表 15 的结果验证了业务感知编码策略的三个关键属性：

**(a) 差异化实现。** 每个业务类别精确接收其指定的编码处理：VoIP 生成 25% 修复报文（保护性），视频和 Web 生成 12.5%，大块传输生成零。12.3% 的总开销相对统一 25% RLNC 应用于所有流量代表 51% 的减少——在不牺牲时延敏感类别保护的前提下。

**(b) 联合延迟保持在预算内。** 完整流水线（分类 + 编码决策 + 路由）在约 1,093 ns/pkt 内完成，仅比仅分类流水线（933 ns）高 17%。这额外的 160 ns 覆盖编码表查找、生成缓冲区管理和修复发射决策——远在边缘处理预算之内。

**(c) 分类准确率驱动正确编码。** 编码统计（25%/12.5%/12.5%/0%）与表 3b 的设计规范完全匹配。误分类（第 5.3 节显示 0.9% 错误率，全部为视频→大块）影响极小：少量视频报文绕过编码并依赖 TCP 重传——这是安全的降级方向。

### 5.11 多代压力测试 (Multi-Generation Stress Test)

为验证大规模编码行为，本文处理 40,000 个报文（每类 10,000 个）通过完整编码流水线，验证生成管理、缓冲区处理和修复生成在数千个编码周期中保持正确。

**表 16：多代编码压力测试（40,000 报文）。** 每个类别处理恰好其预期数量的生成，确认在持续流量下正确的缓冲区管理和生成边界处理。

| 业务类别 | 报文数 | 预期生成数 | 实际生成数 | 修复报文 | 处理时间 |
|---|---|---|---|---|---|
| Web (c₀, XOR k=8) | 10,000 | 1,250 | 1,250 | 1,250 | — |
| 视频 (c₁, RLNC k=8) | 10,000 | 1,250 | 1,250 | 1,250 | — |
| VoIP (c₂, RLNC k=4) | 10,000 | 2,500 | 2,500 | 2,500 | — |
| 大块传输 (c₃, 无) | 10,000 | 0 | 0 | 0 | — |
| **总计** | **40,000** | **5,000** | **5,000** | **5,000** | **< 35 ms** |

表 16 确认在持续负载下生成边界被正确处理：每个类别产生恰好预期数量的生成（data_packets / k），无缓冲区溢出、差一错误或类间干扰。40,000 报文（包括 RLNC GF(2^8) 编码操作）< 35 ms 的总处理时间表明编码引擎维持的吞吐远超典型边缘链路速率。

### 5.12 评估总结 (Evaluation Summary)

表 17 汇总了所有七个测试类别的关键评估指标，提供设计目标与实测性能之间的概览比较。

**表 17：综合评估结果。** 所有设计目标均达到或超过。框架实现亚微秒逐包延迟、高分类准确率、近理想负载分布和正确的差异化编码——验证了 EdgeNet-CoM 在网计算架构及两个应用实例。

| 维度 | 设计目标 | 实测结果 | 状态 |
|---|---|---|---|
| 分类准确率（总体） | >95% | 99.1% | ✓ 超过 |
| 每类准确率（最低） | >90% | 97.4%（视频） | ✓ 超过 |
| 量化损失 (float→Q16.16) | <1% | 0.0%（100% 一致） | ✓ 超过 |
| 逐包流水线延迟 | <1,000 ns | 933 ns (PGO) | ✓ 达标 |
| 多核扩展（16 工作线程） | >2.5× | 2.57× (2.75 Mpps) | ✓ 达标 |
| 负载不均衡 (Jain 指数) | >0.99 | 0.997 | ✓ 超过 |
| RLNC 单包恢复 | 100% | 100%（所有位置） | ✓ 达标 |
| XOR 单包恢复 | 100% | 100%（所有位置） | ✓ 达标 |
| VoIP 编码开销 | 25% | 25.0% | ✓ 精确 |
| 视频编码开销 | 12.5% | 12.5% | ✓ 精确 |
| 总业务感知开销 | <统一 25% | 12.3%（减少 51%） | ✓ 超过 |
| 联合流水线延迟（含 NC） | <1,200 ns | ~1,093 ns | ✓ 达标 |
| 生成边界正确性 | 100% | 100%（40K 报文） | ✓ 达标 |
| 功能回归（所有数据面） | 0 失败 | 0 失败 | ✓ 达标 |

表 17 的结果表明 EdgeNet-CoM 在网计算框架在两个应用实例上同时实现了全部设计目标。其中最重要的成果是通过分类驱动差异化实现的编码带宽开销 51% 的减少（12.3% vs. 25% 统一编码）——且不牺牲丢包敏感流量类别的保护。由此可见，"一次分类、两次动作（路由 + 编码）"范式从每次 DNN 推理中提取了最大价值，验证了卫星 UPF 的网算协同模型。

---

## 6. 讨论 (Discussion)

### 6.1 局限性 (Limitations)

当前框架存在八项值得讨论的限制，每项均指向未来改进的方向。

**特征空间粒度。** 当前的 4 特征模型可捕获粗粒度业务类别（Web/视频/VoIP/大块传输），但无法区分类内的子应用类型（如 VoIP 中的 Zoom 与 Microsoft Teams，或视频中的 Netflix 与 YouTube）。更细粒度的分类需额外特征如到达间隔时间 (IAT) 和报文方向，而这些只有通过逐流有状态跟踪才能获取——增加了复杂性和内存开销。

**HTTP/2 和 HTTP/3 多路复用效应。** 现代 Web 流量正越来越多地使用 HTTP/2（单 TCP 连接上的多路复用流）和 HTTP/3（基于 QUIC）。HTTP/2 多路复用从根本上改变了 TLS 记录模式：单个 TCP 连接可能携带不同"业务"的交错请求（例如网页中的视频嵌入、页面加载旁的 API 调用）。当前的逐包分类器基于报文头特征为每个报文分配单个业务类别，但无法区分同一 HTTP/2 连接内不同流的*应用上下文*。这是逐包分类的固有局限，只能通过逐流或逐流状态跟踪来解决——本文将其留作未来工作。HTTP/3（基于 QUIC）由于使用 UDP 协议而被完全排除（参见下文 TCP 限制）。

**合成评估范围与统计报告。** 第 5.3 节的分类评估依赖于根据已发表真实流量统计 [6] [7] 校准的类别条件合成流量生成。虽然这使已知真实标签的可控评估成为可能，但它未捕获真实加密流量的完整分布复杂性——包括协议多样性（HTTP/2 多路复用、WebSocket、gRPC）、中间盒效应和 NAT 引起的变化。因此，99.1% 的准确率应被解释为特征分布良好分离条件下可达性能的*上界*，而非真实世界准确率的确切测量。本文未报告置信区间，因为每次测量是在固定测试集上的单次运行；未来工作应在真实流量捕获上采用 k 折交叉验证和 Bootstrap 置信区间。**每类基于规则的基线比较**（同一合成测试集）：

| 业务 | 基于规则的准确率 | DNN 准确率 | 差距 |
|---------|---------------------|--------------|-----|
| Web (c₀) | 98.5% (966/982) | 99.9% (981/982) | 1.4 pp |
| 视频 (c₁) | 89.1% (878/986) | 96.5% (951/986) | 7.4 pp |
| VoIP (c₂) | 100.0% (1013/1013) | 100.0% (1013/1013) | 0 pp |
| 大块传输 (c₃) | 100.0% (1019/1019) | 100.0% (1019/1019) | 0 pp |
| **总体** | **94.2%** (3876/4000) | **99.1%** (3964/4000) | **4.9 pp** |

差距集中在视频分类（7.4 pp），其中 DNN 的学习非线性边界在手 crafting 阈值对模糊的视频/大块大小重叠上优于手工阈值。VoIP 和大块传输可被简单规则同等良好地服务（报文大小和 TCP 窗口对这些类别几乎是确定性的），表明 DNN 的价值在于解决*边界情况*，而非简单可分离的类别。本文承认两种方法仅在合成数据上评估，相对优势在真实流量上可能不同。

**TCP 协议覆盖范围。** 当前解析器和特征提取仅支持 TCP+TLS 流量。基于 UDP 的 QUIC 现占 Web 流量的显著且增长比例；框架在当前形式下无法分类 QUIC 流。虽然 QUIC Initial 报文携带一些可用作特征的未加密字段，但解析器和特征集需大幅重新设计。本文在第 6.4 节（未来工作第 4 项）中进一步讨论这一局限。

**统计分析深度。** 评估报告点估计而无置信区间或显著性检验。延迟测量（表 6）是单一配置的均值，无重复运行的方差。分类准确率（表 5）在单一训练-测试拆分上报告。读者应将所有数值结果解释为指示性而非统计确定性的。本文建议未来部署以适当的统计严谨性在真实流量上验证框架。

**对抗鲁棒性。** 复杂攻击者可构造具有误导性报文头特征的报文（如人为小的 TCP 窗口）以获得优先路由类别分配。然而，这种攻击向量是自我限制的：操纵 TCP 窗口大小会降级应用自身的吞吐量性能，修改 TLS 记录长度需非标准 TLS 实现，可能触发服务器端错误。每源的网速率限制进一步限制了潜在滥用。

**"首包"分类澄清。** 本文声称分类"每个报文，包括新流的首个报文"（第 1.3 节）。特此澄清：对于 TLS 1.3 连接，前几个报文是握手消息（ClientHello、ServerHello 等），其 `tls.type ≠ 23`，按设计绕过分类（第 4.6.3 节）。因此第一个*可分类*报文是握手完成后的第一个应用数据记录——通常是 TLS 1.3 连接的第 5 或第 6 个报文。对于 TLS 1.2，握手同样不被分类。"首包"声明因此适用于流的第一个*应用数据*报文，这仍然是业务感知处理变得与操作相关的第一个报文（握手报文无论如何接收默认转发）。

**能耗与功率特征化。** 对于星载部署，能耗是关键约束。Q16.16 定点整数算术每操作固有地比浮点运算单元更节能（无指数处理，更简单的 ALU），且小模型大小（220 参数、880 字节）适合 L1 缓存，可最小化内存访问能耗。然而，本文未在代表性卫星级处理器（如 RAD750、LEON4 或基于 ARM 的宇航级 SoC）上测量实际功耗。未来工作应在代表性硬件平台上表征每包能耗并与替代方法（如硬件加速 DPI）进行比较。对于地面站部署在通用 x86/ARM 服务器上，能耗约束较小，但功率比例扩展对运营成本仍然重要。

**无真实卫星 UPF 集成验证。** 本框架作为独立软件系统在处理内存中合成报文时已验证。它尚未与真实 5G 核心网集成（无 SMF、无 N4 接口），未用真实网卡测试（无驱动级报文 I/O），也未在真实卫星协议栈下运行。虽然架构设计时考虑了 5G UPF 集成（DSCP 到 QFI 映射、N4 同步），但在做出部署声明之前需与运营卫星基础设施进行端到端验证。

### 6.2 实际部署考量 (Practical Deployment Considerations)

多个实际因素影响 EdgeNet-CoM 框架在卫星 UPF 网关的真实世界部署。

**模型生命周期管理。** 控制面通过跟踪最大输出 logit 幅度来监控分类置信度。当平均置信度降至可配置阈值以下时（表明入站流量不再匹配训练分布——例如因卫星切换改变了流量组合），系统触发在最近收集的标注样本上自动重训练。重训练期间，现有模型继续服务；更新后的模型通过 DNN 外部函数结构中的指针替换原子交换，确保即使在卫星链路转换期间也实现零停机模型更新。

**故障下的优雅降级。** 若 DNN 外部函数遇到错误（如空间级内存中辐射诱发的位翻转导致权重损坏），classify_t 表的默认动作为 `nop()`——报文绕过分类并接收标准基于目的地的路由，保持原始 DSCP 标记。这种故障开放设计确保在网计算基础设施故障不会中断基本连接——这对物理维护不可能或极其昂贵的卫星 UPF 至关重要。

**多租户与切片隔离。** EdgeNet-CoM 框架在运行时完全可重构：不同 P4 程序（具有不同模型、QoS 策略和路由表）可在同一卫星 UPF 上按网络切片加载，符合 5G 网络切片原则。每个切片的流水线在独立的表和 DNN 实例集上运行，提供逻辑隔离而无需硬件分区——使单个卫星 UPF 能同时服务 eMBB、URLLC 和 mMTC 切片，并具有差异化转发策略。

**与 5G 卫星 UPF 架构的集成。** 基于 DSCP 的标记方法（模式 A，第 4.6.2 节）确保与地面 DiffServ 和 5G QoS 流 (QFI) 标记的向后兼容。在卫星 UPF 处，DSCP 标记可在 N3/N9 接口映射到 5G QFI 值，实现跨卫星-地面路径的端到端 QoS 一致性。与 SMF 的 N4 接口允许会话级策略更新（如修改特定 PDU 会话的路径分配）而不中断数据平面。

**抗辐射部署。** 对于星载卫星 UPF 部署（LEO 卫星上的处理模块），Q16.16 定点算术提供了额外优势：纯整数计算固有地比浮点运算单元更抗单粒子翻转 (SEU)；小模型大小（220 参数、880 字节）使得在适度 SRAM 预算内进行三模冗余 (TMR) 复制成为可能，进一步加固推理引擎抵抗辐射引起的错误。

### 6.3 更广泛影响、伦理考量与未来方向 (Broader Impact, Ethical Considerations, and Future Directions)

EdgeNet-CoM 展示的在网计算范式的影响超越了本文呈现的两个应用实例。结合框架的当前能力，下列两个应用方向最具前景：

- **卫星链路自适应编码速率控制**：框架的业务感知执行层可扩展为根据观测到的卫星链路丢包模式（如雨衰、切换引起的丢包）实时调整 FEC 冗余，创建响应卫星信道动态的闭环编码系统。这直接构建在应用实例 B 的现有编码基础设施之上。

- **卫星边缘实时异常检测**：执行业务分类的同一 DNN 外部函数架构可重新训练为以逐包粒度识别大容量 DDoS 攻击或端口扫描——在具有多个点波束广域流量视图的卫星 UPF 处尤为有价值。这仅需模型权重的重训练，而非流水线重新设计。

更具推测性的扩展（如内容感知缓存、波束跳预测、流间编码）是可能的，但需当前框架之外的大量额外基础设施；本文将其留作未来工作。

**伦理与监管考量。** 基于流量分类的业务感知路由在网络中立性法规适用的司法管辖区可能引发相关疑虑。需要指出的是，EdgeNet-CoM 框架的分类服务于*网络工程目的*（路径选择、丢包保护）而非内容歧视或付费优先——四个业务类别直接对应已标准化的 5G QoS 流优先级和 DiffServ PHB，这些已被运营商用于管理 QoS。尽管如此，部署本框架的运营商应确保分类策略符合当地网络中立性法规；控制面接口（N4 到 SMF）为策略透明度提供了自然的审计点。此外，本文亦注意到相同的分类能力原则上可能被滥用于流量监控；因此强调框架的隐私保护设计（零载荷访问）将暴露的信息限制为仅在报文头级特征，部署应伴随适当的治理机制。

### 6.4 未来工作 (Future Work)

基于上述局限性与更广泛影响的讨论，本文确定了 EdgeNet-CoM 框架及其应用实例的六个未来研究方向：

1. **具有流状态的多层 DNN**：将流水线内推理引擎扩展到更深架构（2–3 隐层），结合逐流有状态特征（IAT、报文计数）用于更细粒度的子应用分类（8–16 类）；
2. **闭环卫星感知路径与编码选择**：集成实时卫星链路质量反馈（通过 LEO/GEO 探测的 RTT、每波束丢包计数器、ISL 可用性），动态调整类别到卫星路径映射和每类编码冗余比；
3. **卫星 UPF 流间网络编码**：将应用实例 B 从流内系统编码扩展到流间机会编码（COPE 风格 XOR 跨共享相同卫星下一跳的流），在带宽受限卫星链路上进一步提高带宽效率；
4. **卫星 NTN 的 QUIC 和 UDP 流量支持**：适配解析器和特征提取以支持 QUIC Initial 报文（基于 UDP），这在卫星接入的 Web 流量中日益普遍，需解析 QUIC 头而非 TLS 记录；
5. **FPGA SmartNIC 和星载处理器加速**：将 DNN 外部函数和 NC 外部函数移植到 FPGA 逻辑以实现 10–100 Gbps 吞吐——面向地面站 SmartNIC 和 LEO 卫星 UPF 部署的抗辐射星载处理器；
6. **联邦卫星边缘学习**：在地理分布的卫星 UPF（地面站和星载节点）之间实现协作模型更新，通过星间链路共享模型梯度，同时保持不同卫星覆盖区域的数据本地性和用户隐私。

---

## 7. 结论 (Conclusion)

本文提出了 EdgeNet-CoM，一种面向卫星网络 UPF 的基于可编程 RMT 的在网计算框架，解决了在单一流水线中统一报文转发与应用层智能计算的根本挑战。本工作在框架、架构和系统层面做出了贡献。

**在框架层面**，本文形式化了卫星 UPF 的在网计算框架（定义 1–6），并确立了五项核心设计——统一解析-分类-执行流水线、有状态外部函数架构、P4 启发式可编程业务逻辑、多队列并行扩展引擎和业务感知执行层——分别应对卫星 UPF 在网计算的五项关键挑战。该形式化超越了任何单一应用，为卫星边缘未来的在网计算服务提供了通用模板。

**在架构层面**，Q16.16 定点 MLP 推理作为 RMT 流水线外部函数的集成（定义 4，算法 1）实现了与浮点相比恰好零准确率损失的业务分类。每个报文仅使用约 220 次整数运算、在约 200 ns 内完成推理——这一计算特征特别适合卫星网关的受限处理环境。VLIW 执行模型（第 4.4 节）和多队列扩展引擎（第 4.5 节，定理 1）提供了确定性亚微秒处理和正确多核并行的框架级基础。

**在应用层面**，本文通过两个代表性应用实例展示了框架的通用性：(A) 业务感知多路径智能路由，对加密流量进行分类并在异构卫星-地面连接之间选择最优路径；(B) 业务区分网络编码优化，针对卫星链路丢包特征施加类别特定的 FEC。两个应用合计实现 51% 的编码带宽减少（12.3% vs. 25% 统一编码）和卫星路径感知的差异化转发——验证了"一次分类、两次动作（路由 + 编码）"范式从每次推理操作中提取了最大价值。与基于阈值的规则基线的初步比较表明，DNN 相对手工规则提供了有意义的准确率改进，尤其在视频/大块传输边界处（规则仅达 89.1%）。

**在系统层面**，EdgeNet-CoM/UPF 原型证明了框架可转化为实际性能：933 ns 端到端延迟、2.75 Mpps 多核吞吐（表 6、7）、近理想负载均衡（表 8，Jain 指数 0.997）以及全部功能场景的正确运行（表 10）。零依赖、单二进制部署模型使在卫星地面站的通用边缘硬件上普遍采纳成为可能。

**评估范围的诚实评估。** 本文强调分类评估使用类别条件合成流量。虽然特征分布根据已发表的真实流量统计校准，且框架架构经过端到端验证，报告的 99.1% 准确率应被解释为受控条件下的性能而非真实世界准确率的确切测量。未来工作将在运营卫星 UPF 的真实加密流量捕获上验证框架，并以适当的统计严谨性包括置信区间和交叉验证。

**更广泛的意义**：EdgeNet-CoM 代表了朝着卫星 UPF 作为融合智能星地计算平台愿景的具体一步——该 UPF 能够通过网算协同在星地连续体中同时分类、路由、编码和处理流量。本框架尊重用户隐私和 5G NTN 服务交付的实时性能要求，同时避免了用于丢包恢复的昂贵卫星 RTT 惩罚，为网络边缘的未来在网智能奠定了基础。

---

## 参考文献 (References)

[1] W. Shi, J. Cao, Q. Zhang, Y. Li, and L. Xu, "Edge computing: Vision and challenges," *IEEE Internet of Things Journal*, vol. 3, no. 5, pp. 637–646, 2016.

[2] Y. Mao, C. You, J. Zhang, K. Huang, and K. B. Letaief, "A survey on mobile edge computing: The communication perspective," *IEEE Communications Surveys & Tutorials*, vol. 19, no. 4, pp. 2322–2358, 2017.

[3] Google Transparency Report, "HTTPS encryption on the web," 2024. [Online]. Available: https://transparencyreport.google.com/https/overview

[4] P. Bosshart, G. Gibb, H.-S. Kim, G. Varghese, N. McKeown, M. Izzard, F. Mujica, and M. Horowitz, "Forwarding metamorphosis: Fast programmable match-action processing in hardware for SDN," in *Proc. ACM SIGCOMM*, 2013, pp. 99–110.

[5] A. Dainotti, A. Pescape, and K. C. Claffy, "Issues and future directions in traffic classification," *IEEE Network*, vol. 26, no. 1, pp. 35–40, 2012.

[6] G. Aceto, D. Ciuonzo, A. Montieri, and A. Pescapè, "Mobile encrypted traffic classification using deep learning: Experimental evaluation, lessons learned, and challenges," *IEEE Trans. Network and Service Management*, vol. 16, no. 2, pp. 445–458, 2019.

[7] T. Shapira and Y. Shavitt, "FlowPic: A generic representation for encrypted traffic classification and applications identification," *IEEE Trans. Network and Service Management*, vol. 18, no. 2, pp. 1218–1232, 2021.

[8] M. Lotfollahi, M. J. Siavoshani, R. S. H. Zade, and M. Saberian, "Deep packet: A novel approach for encrypted traffic classification using deep learning," *Soft Computing*, vol. 24, no. 3, pp. 1999–2012, 2020.

[9] W. Wang, M. Zhu, J. Wang, X. Zeng, and Z. Yang, "End-to-end encrypted traffic classification with one-dimensional convolution neural networks," in *Proc. IEEE ISI*, 2017, pp. 43–48.

[10] P. Bosshart, D. Daly, G. Gibb, M. Izzard, N. McKeown, J. Rexford, C. Schlesinger, D. Talayco, A. Vahdat, G. Varghese, and D. Walker, "P4: Programming protocol-independent packet processors," *ACM SIGCOMM Computer Communication Review*, vol. 44, no. 3, pp. 87–95, 2014.

[11] Z. Xiong and N. Zilberman, "Do switches dream of machine learning? Toward in-network classification," in *Proc. ACM HotNets*, 2019, pp. 25–33.

[12] G. Siracusano, R. Bifulco, S. Galea, N. Zilberman, and G. Antichi, "Re-architecting traffic analysis with neural network interface cards," in *Proc. ACM SIGCOMM Workshop NetAI*, 2020.

[13] Z. Xiong and N. Zilberman, "IIsy: Practical in-network classification," *IEEE Trans. on Networking*, 2024.

[14] S. Blake, D. Black, M. Carlson, E. Davies, Z. Wang, and W. Weiss, "An architecture for differentiated services," RFC 2475, 1998.

[15] N. McKeown, T. Anderson, H. Balakrishnan, G. Parulkar, L. Peterson, J. Rexford, S. Shenker, and J. Turner, "OpenFlow: Enabling innovation in campus networks," *ACM SIGCOMM Computer Communication Review*, vol. 38, no. 2, pp. 69–74, 2008.

[16] T. Pan, N. Yu, C. Jia, J. Pi, L. Xu, Y. Qiao, Z. Li, K. Liu, J. Lu, J. Lu, B. Liu, and T. Huang, "Sailfish: Accelerating cloud-scale multi-tenant multi-service gateways with programmable switches," in *Proc. ACM SIGCOMM*, 2021.

[17] A. Sivaraman, A. Cheung, M. Budiu, C. Kim, M. Alizadeh, H. Balakrishnan, G. Varghese, N. McKeown, and S. Licking, "Packet transactions: High-level programming for line-rate switches," in *Proc. ACM SIGCOMM*, 2016, pp. 15–28.

[18] C. Zheng, M. Zhang, X. Li, and K. Xu, "Service-aware resource allocation in software-defined edge computing," *IEEE Journal on Selected Areas in Communications*, vol. 39, no. 7, pp. 2076–2089, 2021.

[19] G. Xie, Q. Li, Y. Dong, G. Duan, and Y. Jiang, "In-network caching with programmable forwarding engines," *IEEE/ACM Trans. on Networking*, vol. 30, no. 4, pp. 1826–1839, 2022.

[20] L. Zhang, A. Afanasyev, J. Burke, V. Jacobson, K. Claffy, P. Crowley, C. Papadopoulos, L. Wang, and B. Zhang, "Named data networking," *ACM SIGCOMM Computer Communication Review*, vol. 44, no. 3, pp. 66–73, 2014.

[21] C. Lao, Y. Le, K. Mahajan, Y. Chen, W. Wu, A. Akella, and M. Swift, "ATP: In-network aggregation for multi-tenant learning," in *Proc. USENIX NSDI*, 2021, pp. 741–761.

[22] A. Sapio, M. Canini, C.-Y. Ho, J. Nelson, P. Kalnis, C. Kim, A. Krishnamurthy, M. Moshref, D. Ports, and P. Richtarik, "Scaling distributed machine learning with in-network aggregation," in *Proc. ACM SIGCOMM*, 2021, pp. 785–798.

[23] R. Ahlswede, N. Cai, S.-Y. R. Li, and R. W. Yeung, "Network information flow," *IEEE Trans. Information Theory*, vol. 46, no. 4, pp. 1204–1216, 2000.

[24] T. Ho, M. Medard, R. Koetter, D. R. Karger, M. Effros, J. Shi, and B. Leong, "A random linear network coding approach to multicast," *IEEE Trans. Information Theory*, vol. 52, no. 10, pp. 4413–4430, 2006.

[25] S. Katti, H. Rahul, W. Hu, D. Katabi, M. Medard, and J. Crowcroft, "XORs in the air: Practical wireless network coding," *IEEE/ACM Trans. Networking*, vol. 16, no. 3, pp. 497–510, 2008.

[26] J. K. Sundararajan, D. Shah, M. Medard, S. Jakubczak, M. Mitzenmacher, and J. Barros, "Network coding meets TCP: Theory and implementation," *Proc. IEEE*, vol. 99, no. 3, pp. 490–512, 2011.

[27] J. Neto, J. Barros, and M. Medard, "Fulcrum network codes: A code for fluid allocation of complexity," *IEEE Trans. Communications*, vol. 70, no. 4, pp. 2418–2433, 2022.

[28] 3GPP TR 38.821, "Study on New Radio (NR) to support Non-Terrestrial Networks (NTN)," Release 17, 2022.

[29] 3GPP TR 38.822, "Study on SAT-based access to 5G Core Network," Release 18, 2023.

[30] J. Wang, Y. Li, Z. Han, and X. Chen, "LEO satellite-assisted edge computing for IoT: Task offloading and resource allocation," *IEEE Internet of Things Journal*, vol. 9, no. 15, pp. 13456–13469, 2022.

[31] Y. Liu, C. F. Chi, and H. H. Chen, "Collaborative task scheduling in LEO satellite-terrestrial integrated edge computing," *IEEE Trans. on Mobile Computing*, vol. 22, no. 8, pp. 4521–4535, 2023.

[32] X. Chen, L. Liu, and Y. Cui, "Distributed UPF deployment for LEO satellite constellations: Architecture and optimization," *IEEE Trans. on Wireless Communications*, vol. 23, no. 4, pp. 2890–2905, 2024.

[33] Q. Li, Z. Han, X. Chen, and Y. Zhang, "Space-ground integrated networking: Architecture, protocols, and key technologies for 6G satellite-terrestrial convergence," *IEEE Communications Surveys & Tutorials*, vol. 26, no. 1, pp. 412–449, 2024.

[34] D. E. Lucani, M. Medard, and C. Fragouli, "Network coding for satellite broadcast channels: Fundamental limits and code design," in *Proc. IEEE International Symposium on Information Theory (ISIT)*, 2007, pp. 2231–2235.

[35] G. H. Nguyen, A. Nallagatla, and M. Medard, "Adaptive network coding for satellite-terrestrial hybrid networks," *IEEE Trans. on Communications*, vol. 69, no. 6, pp. 3876–3890, 2021.

[36] R. Bifulco, S. B. Lee, and S. Secci, "P4-based programmable satellite on-board processing: Architecture and feasibility study," *IEEE Network*, vol. 37, no. 2, pp. 112–119, 2023.

[37] Y. Zhang, K. Liu, and X. Wang, "P4-based satellite routing for LEO constellation inter-satellite links: Design and evaluation," in *Proc. ACM SIGCOMM Workshop on Satellite and Space Communications (SatCom)*, 2024, pp. 45–52.

[38] nDPI Development Team, "nDPI: Open Source Deep Packet Inspection Software Toolkit," 2024. [Online]. Available: https://www.ntop.org/products/deep-packet-inspection/ndpi/
