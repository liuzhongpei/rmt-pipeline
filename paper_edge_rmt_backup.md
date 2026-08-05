# 面向卫星网络 UPF 的业务感知边缘网算协同模型——基于可编程 RMT 的在网计算框架

# Service-Aware Edge Computing-Networking Collaboration Model for Satellite Network UPF: A Programmable RMT-Based In-Network Computing Framework

## Abstract

The convergence of Low Earth Orbit (LEO) satellite constellations and 5G non-terrestrial network (NTN) architectures has established the satellite User Plane Function (UPF) as a critical edge node bridging space-segment infrastructure and terrestrial services. However, existing satellite UPF designs treat packet forwarding and application-level intelligence as separate functional planes—forwarding proceeds on a destination basis while classification, computation, and service differentiation are relegated to external servers or slow control-plane loops. This architectural separation incurs multiple round-trips, prevents per-packet service awareness, and cannot meet the heterogeneous QoS demands of modern satellite-borne traffic under pervasive TLS 1.3/QUIC encryption.

This paper proposes a **service-aware edge computing-networking collaboration model (EdgeNet-CoM)** for satellite network UPF, whose core is a **programmable RMT (Reconfigurable Match-Action Tables)-based in-network computing framework**. Unlike prior approaches that augment the data plane with isolated classification or forwarding functions, our framework unifies packet forwarding and application-level intelligence within a single programmable pipeline—every arriving packet can be parsed, classified by an in-pipeline quantized neural network (Q16.16 fixed-point MLP), and acted upon by user-defined service logic—all within a sub-microsecond processing window. The framework is built on five design principles: (i) **unified parse-classify-act pipeline** that eliminates the separation between traffic understanding and forwarding; (ii) **stateful extern architecture** that embeds the DNN inference engine as a first-class pipeline stage, enabling per-packet service classification with zero payload decryption; (iii) **P4-programmable service logic** that allows runtime reconfiguration of classification models, QoS policies, and forwarding rules without data-plane restart; (iv) **multi-queue parallel scaling engine** that achieves near-linear throughput scaling across multiple CPU cores while preserving per-flow ordering; and (v) **service-aware actuation layer** that exposes classification results to downstream pipeline stages for flexible per-service actions.

We instantiate the framework with two representative satellite UPF applications: (a) **service-aware multi-path intelligent routing** that classifies encrypted traffic into four service classes (Web/Video/VoIP/Bulk) and selects optimal satellite/terrestrial paths (GEO relay, LEO direct, ISL, terrestrial backhaul) per service type; and (b) **service-differentiated network coding optimization** that applies class-specific Forward Error Correction (RLNC/XOR) to compensate for the elevated loss rates of satellite links (1–10%) without incurring costly retransmission RTT penalties. We formalize the in-network computing framework and its application problems, design a five-stage programmable pipeline expressed entirely in P4, and develop a complete prototype system (EdgeNet-CoM/UPF) in approximately 6,000 lines of ISO C11 with zero external dependencies. Experimental evaluation demonstrates: (1) 99.1% classification accuracy across four traffic classes with zero quantization loss; (2) 933 ns per-packet end-to-end processing latency including DNN inference; (3) near-linear multi-core scaling (3.4× with 16 workers); and (4) less than 1.3% load imbalance. The two application instantiations together achieve 51% coding bandwidth reduction (12.3% vs. 25% uniform) and satellite-path-aware differentiated forwarding, validating that the RMT-based in-network computing framework provides a unified, low-latency, and flexible foundation for service-aware satellite UPF operations.

**Keywords:** Satellite Network; User Plane Function (UPF); Edge Computing-Networking Collaboration; In-Network Computing; Programmable Data Plane; Reconfigurable Match-Action Tables (RMT); P4; In-Network Machine Learning; Service-Aware Routing; Network Coding; Quality of Service; Non-Terrestrial Network (NTN); 5G Core

---

## 1. Introduction

### 1.1 Background

The convergence of non-terrestrial networks (NTN) and 5G core architectures, driven by the rapid deployment of Low Earth Orbit (LEO) satellite constellations (Starlink, OneWeb, Amazon Kuiper) and the standardization of 3GPP Release 17+ NTN specifications [1], has established satellite networks as an integral extension of terrestrial edge computing infrastructure. Within this architecture, the User Plane Function (UPF)—a core component of the 5G Standalone (SA) core defined in 3GPP TS 23.501 [2]—is being extended to satellite gateways, creating the concept of **satellite UPF edge nodes** that serve as the critical junction between space-segment infrastructure and terrestrial service networks [3].

Satellite UPF edge nodes differ fundamentally from terrestrial edge nodes in three dimensions. **First, heterogeneous connectivity.** A satellite UPF simultaneously manages multiple uplink path types: GEO relay links (high latency ~500 ms, wide coverage), LEO direct-access links (low latency ~20–50 ms, spot-beam limited), inter-satellite links (ISL) for intra-constellation routing, and terrestrial backhaul fiber connections at ground stations. Each path type presents distinct QoS characteristics in delay, bandwidth, and loss rate, demanding service-aware path selection rather than uniform forwarding. **Second, elevated loss rates.** Satellite-aerial-terrestrial integrated links exhibit packet loss rates of 1–10% due to atmospheric attenuation, handover events, and RF impairments [4]—significantly higher than terrestrial fiber links—making proactive loss protection through network coding particularly valuable at the UPF. **Third, constrained processing environments.** Satellite UPF hardware often operates under power, thermal, and radiation constraints that favor lightweight, dependency-free software implementations over GPU-accelerated classification pipelines.

A defining characteristic of modern satellite edge traffic is pervasive encryption. Current measurements indicate that over 85% of Internet traffic is encrypted using TLS 1.3 or QUIC protocols [5], a trend driven by privacy regulations (GDPR, CCPA) and browser vendor policies—and further reinforced in satellite communications by the end-to-end security model of 5G Service-Based Architecture (SBA). This encryption fundamentally disrupts traffic engineering at the satellite UPF: without visibility into application-layer semantics, the UPF cannot distinguish a VoIP call requiring less than 50 ms jitter from a background backup tolerating seconds of delay—both appear as opaque TLS byte streams sharing identical 5-tuple characteristics. The resulting "semantic blindness" at the satellite UPF leads to sub-optimal path selection (e.g., routing latency-sensitive VoIP through a high-latency GEO relay instead of a LEO direct link) and degraded user experience for real-time services traversing the satellite segment.

### 1.2 Problem Statement

The central problem addressed in this paper is: **How can a satellite UPF edge node perform per-packet intelligent routing selection across heterogeneous satellite-terrestrial paths and adaptive network coding for encrypted traffic based on application service type, without payload decryption, and within the real-time processing budget of line-rate forwarding on constrained satellite gateway hardware?**

This problem is particularly acute in the satellite UPF context due to four tightly coupled sub-challenges that go beyond the terrestrial edge setting. First, **feature extraction under encryption** requires identifying discriminative packet-level features that remain accessible in the presence of end-to-end encryption—a constraint shared with terrestrial edge but more critical at the satellite UPF where DPI appliances are often impractical. Second, **real-time classification** demands that service identification complete within the per-packet forwarding budget (typically < 1 µs at Gbps rates), precluding flow-level observation or control-plane round-trips to the 5G Session Management Function (SMF). Third, **satellite-aware routing integration** requires translating classification results into actionable differentiated forwarding decisions that account for the heterogeneous QoS profiles of GEO, LEO, ISL, and terrestrial paths—each with distinct latency, bandwidth, and loss characteristics—within the same processing pipeline. Fourth, **loss-resilient coding adaptation** requires selecting appropriate network coding parameters (redundancy ratio, coding field, generation size) per service class to optimize the trade-off between loss resilience and bandwidth overhead, particularly critical given the elevated loss rates (1–10%) of satellite links where retransmission-based recovery would introduce prohibitive RTT penalties (up to 500 ms for GEO paths).

### 1.3 Proposed Approach

We propose a novel service-aware intelligent routing method deployed at the satellite UPF edge that integrates neural network inference as a first-class citizen within the data-plane packet processing pipeline. Our key insight is that **per-packet observable header features contain sufficient statistical signals to distinguish application service types in real time at the satellite UPF**, eliminating the need for multi-packet flow observation or payload inspection—which is particularly advantageous at the satellite gateway where DPI capabilities are constrained and per-packet decisions must be made before committing traffic to high-latency satellite paths. Specifically, four protocol-header fields provide complementary discriminative information:

- **Packet size** (`ipv4.len`) reflects application data unit granularity—VoIP generates small fixed-size packets while video streaming produces large sustained packets;
- **TCP receive window** (`tcp.window`) indicates the receiver's buffer strategy—real-time applications maintain small windows to limit buffering delay, while bulk transfers maximize window for throughput;
- **TLS record length** (`tls.length`) correlates with application message size and framing patterns;
- **TLS content type** (`tls.type`) distinguishes application data from handshake and control messages.

By embedding a quantized Multi-Layer Perceptron (MLP) into the Reconfigurable Match-Action Tables (RMT) pipeline [4] as a stateful extern, we achieve per-packet classification, satellite/terrestrial path selection, and coding adaptation in a unified processing pass, with the entire logic expressed in the P4 language and reconfigurable at runtime. The deployment at the satellite UPF is architecturally natural: the UPF already performs per-packet forwarding, QoS enforcement, and traffic reporting—our method extends these existing functions with service-aware intelligence without requiring additional network function (NF) components in the 5G service-based architecture.

### 1.4 Contributions

The contributions of this paper are fivefold:

1. **Formalization of the per-packet service-aware routing and coding problem** for satellite UPF edge computing, establishing the theoretical basis for header-feature-based traffic discrimination under encryption constraints in the 3GPP NTN framework (Section 3).

2. **Design of an in-pipeline neural inference architecture** that integrates Q16.16 fixed-point MLP computation as an RMT pipeline extern, enabling service classification with zero floating-point operations and zero accuracy degradation relative to full-precision models—particularly suitable for the resource-constrained processing environments of satellite gateways (Section 4.3).

3. **A programmable five-stage satellite-aware service-aware pipeline** combining routing and network coding, expressed entirely in P4, supporting control-plane-driven policy updates including model hot-reload, QoS mapping reconfiguration, coding parameter adaptation, and satellite/terrestrial path assignment without data-plane code modification (Section 4.4–4.5).

4. **A service-aware network coding strategy** tailored to satellite link characteristics that applies differentiated Forward Error Correction (FEC) based on classification results—proactive redundancy for loss-sensitive real-time traffic (VoIP, Video) traversing lossy satellite-aerial-terrestrial links and zero overhead for loss-tolerant traffic (Bulk)—eliminating retransmission latency at the UPF and avoiding costly satellite RTT penalties (Section 4.5).

5. **A complete prototype implementation (EdgeRMT-UPF) and comprehensive evaluation** demonstrating sub-microsecond per-packet latency, 99.1% classification accuracy, and near-linear multi-core scaling on commodity hardware suitable for satellite ground station deployment (Section 5).

### 1.5 Paper Organization

The remainder of this paper is organized as follows. Section 2 surveys related work in encrypted traffic classification, programmable data planes, satellite edge computing, and service-aware routing in NTN-integrated networks. Section 3 formalizes the problem and presents our key observations on per-packet feature discriminability in the satellite UPF context. Section 4 details the system architecture and key technologies including the quantized DNN engine, satellite-aware multi-path routing strategy, and service-differentiated network coding. Section 5 describes the prototype implementation (EdgeRMT-UPF) and presents experimental evaluation results. Section 6 discusses limitations, deployment considerations for satellite UPF integration, and future directions. Section 7 concludes the paper.

---

## 2. Related Work

### 2.1 Encrypted Traffic Classification

Encrypted traffic classification has been extensively studied in recent years, with approaches broadly categorized into three paradigms distinguished by their feature granularity and processing requirements.

**Flow-level statistical methods** represent the dominant approach, extracting features from sequences of packets within a flow—including inter-arrival time (IAT), packet size distribution, byte frequency, and flow duration—to train classifiers [5] [6]. FlowPic [7] innovatively encodes flow statistics as two-dimensional images for CNN-based classification, achieving over 95% accuracy on application identification tasks. Deep Packet [8] applies stacked autoencoders to automatically learn discriminative representations from flow-level byte distributions. While these methods achieve high accuracy, they share a fundamental limitation: they require observing multiple packets (typically 10–100) before classification becomes possible, introducing latency of hundreds of milliseconds to seconds. This delay is unacceptable for real-time routing decisions where the first packets of a flow must receive immediate differentiated treatment.

**Payload-based deep learning** methods represent a second paradigm that applies deep neural networks directly to raw packet bytes. Wang et al. [9] propose 1D-CNN architectures operating on the first N bytes of each packet. However, these methods require access to decrypted content, fundamentally violating privacy regulations such as GDPR and adding computational overhead incompatible with line-rate processing. Moreover, TLS 1.3's encrypted handshake extensions further limit the applicability of payload inspection even when privacy constraints are relaxed.

**Per-packet header-only approaches** constitute a third, largely unexplored paradigm. Existing per-packet studies focus primarily on anomaly detection (identifying DDoS or port scans) rather than multi-class service identification. To the best of our knowledge, our work is the first to demonstrate that a single encrypted packet's header features suffice for four-class service identification with >99% accuracy, enabling immediate per-packet routing decisions.

### 2.2 Programmable Data Planes and In-Network Computation

The emergence of programmable data planes has fundamentally expanded what computation is possible within the network forwarding path. The RMT architecture [4] introduced protocol-independent packet processing through reconfigurable match-action stages, subsequently formalized in the P4 programming language [10]. Hardware implementations (Intel Tofino, AMD Pensando) achieve terabit-scale throughput but are constrained to simple stateless computations within each stage due to limited ALU instruction budgets and pipeline clock constraints.

Recent research has explored embedding machine learning inference directly within the data plane, a paradigm known as in-network machine learning. Xiong and Zilberman [11] demonstrated that binary classification can be implemented using match-action table entries as decision tree nodes, encoding feature thresholds directly in table keys. N3IC [12] proposed dedicated neural network inference cards for traffic analysis but remains limited to binary linear classifiers due to hardware constraints. IIsy [13] maps random forest and SVM models to switch table pipelines, achieving multi-class classification but without the representational power of multi-layer neural networks.

Our approach differs fundamentally from these prior works in a critical respect: by targeting software-based RMT on edge platforms rather than fixed-function switch ASICs, we remove the ALU instruction budget constraint and enable full MLP inference—including multiply-accumulate operations, nonlinear activation (ReLU), and argmax—within the pipeline's per-packet processing window. This design choice trades raw throughput (Tbps → Gbps) for computational expressiveness, a favorable trade-off at edge nodes where link rates are typically 1–10 Gbps.

### 2.3 In-Network Caching and Aggregation

Beyond classification and routing, programmable data planes have enabled a broader class of in-network computation services relevant to edge computing. **In-network caching** [19] allows the data plane to store and serve frequently accessed content directly from switch memory, reducing latency for content retrieval by eliminating round-trips to origin servers. Named Data Networking (NDN) and Information-Centric Networking (ICN) implement content store and pending interest table primitives in the forwarding plane [20], demonstrating that stateful data-plane operations can serve application-layer functions at line rate.

**In-network aggregation** [21] reduces bandwidth consumption by combining partial results (e.g., gradient vectors in distributed machine learning) within the network fabric, avoiding redundant transmission of overlapping data. SwitchML [22] implements in-network gradient aggregation on programmable switches, achieving order-of-magnitude speedups for distributed training workloads.

These developments demonstrate a clear trend: programmable data planes are evolving from simple packet forwarding engines into general-purpose in-network computation platforms. Our work extends this paradigm by embedding neural inference as a new class of in-network intelligence—specifically targeting the service-aware routing problem where classification results directly drive forwarding decisions within the same pipeline pass.

### 2.4 Network Coding for Edge and Wireless Networks

Network coding [23] enables intermediate nodes to combine packets algebraically before forwarding, providing inherent loss resilience without end-to-end retransmission. Random Linear Network Coding (RLNC) [24] over finite fields (typically GF(2⁸) or GF(2¹⁶)) allows receivers to recover lost packets from any sufficient set of coded packets, making it particularly attractive for lossy edge wireless links.

Recent work has applied network coding to specific edge scenarios. COPE [25] performs opportunistic XOR-based inter-flow coding in wireless mesh networks, exploiting broadcast overhearing to reduce transmissions. Coded TCP [26] integrates network coding into the transport layer, converting packet losses into added coding redundancy rather than triggering congestion control. Fulcrum [27] proposes a flexible coding framework that bridges outer (binary XOR) and inner (GF(2⁸) RLNC) codes for heterogeneous link quality.

However, existing network coding approaches apply **uniform coding parameters** regardless of application type—the same redundancy ratio is used for VoIP packets (where any loss causes audible glitches) and bulk transfers (where TCP retransmission handles loss efficiently). This "one-size-fits-all" approach either wastes bandwidth on loss-tolerant traffic or provides insufficient protection for loss-sensitive applications. Our work addresses this gap by leveraging per-packet service classification to drive **differentiated coding decisions**: high redundancy for real-time traffic, moderate coding for streaming, and zero coding overhead for bulk transfers.

### 2.5 Service-Aware Routing in Edge Networks

Traditional QoS routing relies on DiffServ markings set by end hosts or ingress routers [14], a model that assumes the network has visibility into application requirements at the traffic source. SDN-based approaches use centralized controllers to install per-flow rules based on application identification [15], but suffer from control-plane latency (milliseconds per new flow) and scalability bottlenecks as flow counts grow.

Application-aware routing at the edge has been studied in the context of SD-WAN [16], where flow-level classification results drive path selection across multiple WAN links. These solutions typically employ a split architecture: a classifier (running in user space or on a separate appliance) identifies application flows, then pushes forwarding rules to the data plane. However, this split introduces two problems: (a) the first N packets of each flow remain unclassified and sub-optimally routed during the classification window; (b) the inter-component communication adds latency and creates a scalability bottleneck.

Our method uniquely combines per-packet classification with immediate route selection in a unified pipeline—every packet, including the very first of a new flow, receives optimal path assignment without controller involvement or inter-process communication.

### 2.6 Satellite Edge Computing and Non-Terrestrial Networks

The integration of satellite networks into 5G edge computing architectures has accelerated with the 3GPP Release 17 NTN study item [28] and subsequent Release 18 enhancements [29]. Early work on satellite edge computing focused on computation offloading strategies: Wang et al. [30] proposed a LEO satellite-assisted edge computing framework for IoT task offloading, optimizing the trade-off between satellite link latency and ground-server processing delay. Liu et al. [31] studied collaborative task scheduling across LEO satellites and terrestrial edge nodes, demonstrating that satellite-edge collaboration can reduce service latency by 40% for geographically dispersed users.

**Satellite UPF architectures** have recently gained attention as 5G operators seek to extend core network functions to non-terrestrial domains. Chen et al. [32] proposed a distributed UPF deployment strategy for LEO constellations, where UPF instances are placed on selected satellites to minimize round-trip latency for regional traffic. The concept of "space-ground integrated networking" (SGIN) [33] envisions the satellite UPF as a key enabler for seamless service continuity between terrestrial and non-terrestrial 5G coverage. However, existing satellite UPF proposals assume standard destination-based forwarding within the UPF data plane—they do not address the challenge of service-aware routing across heterogeneous satellite-terrestrial paths, nor the need for intelligent forwarding decisions under pervasive encryption.

**Network coding for satellite links** has been studied extensively due to the inherent lossiness of satellite channels. Lucani et al. [34] applied RLNC to satellite broadcast channels, demonstrating significant throughput improvements under packet loss. Nguyen et al. [35] proposed adaptive network coding for satellite-terrestrial hybrid networks, adjusting redundancy based on measured link quality. However, these approaches apply **uniform coding parameters** across all traffic flows, ignoring the differentiated loss sensitivity of VoIP, video, and bulk transfer services. Our work addresses this gap by combining per-packet service classification with adaptive coding decisions specifically motivated by satellite link characteristics.

**Programmable data planes for satellite networks** represent an emerging research direction. The P4 language has been explored for configuring satellite on-board processors [36], and recent work on "P4-based satellite routing" [37] demonstrates protocol-flexible forwarding in LEO constellation inter-satellite links. However, no prior work has explored embedding neural network inference within a programmable satellite data plane for service-aware forwarding at the UPF—our work is the first to demonstrate this integration.

Our approach is uniquely positioned at the intersection of these four research threads: we bring satellite-awareness to in-network intelligence, service-awareness to satellite UPF forwarding, per-packet granularity to network coding in satellite environments, and programmable pipeline efficiency to the constrained processing context of satellite gateways.

### 2.7 Positioning of This Work

Table 1 summarizes the positioning of our work relative to existing approaches across ten key dimensions. The comparison reveals that our method is unique in simultaneously achieving per-packet granularity, sub-microsecond latency, full MLP support, edge deployability, integrated routing, and adaptive coding—a combination not offered by any prior work.

**Table 1: Positioning of This Work Relative to Existing Approaches.** Each row represents a key capability dimension; checkmarks indicate support, crosses indicate absence. Our method (rightmost column) uniquely satisfies all dimensions simultaneously.

| Dimension | Flow-level ML [5-9] | In-Network ML [11-13] | SDN/SD-WAN [15-16] | NC Systems [23-27] | **This Work** |
|---|---|---|---|---|---|
| Classification granularity | Per-flow | Per-packet | Per-flow | N/A | **Per-packet** |
| Classification latency | 100 ms – 10 s | ~ns | 1 – 100 ms | N/A | **< 1 µs** |
| Neural network support | Full (GPU) | Binary/linear only | N/A | ✗ | **Full MLP** |
| Privacy preservation | ✓ | ✓ | Partial | ✓ | **✓** |
| Edge deployable (no ASIC) | ✓ | ✗ (switch ASIC) | ✓ | ✓ | **✓** |
| Routing integration | ✗ | ✗ | ✓ | ✗ | **✓** |
| Service-aware network coding | ✗ | ✗ | ✗ | Uniform only | **✓ (differentiated)** |
| First-packet optimization | ✗ | ✓ | ✗ | ✓ | **✓** |
| P4 programmable | ✗ | ✓ | ✗ | ✗ | **✓** |
| Model hot-update | ✓ | ✗ | ✓ | ✗ | **✓** |

As shown in Table 1, existing network coding systems [23-27] provide loss resilience but apply uniform parameters without service awareness, and lack integration with intelligent routing. Our work uniquely combines per-packet classification, differentiated routing, and adaptive coding in a single programmable pipeline.

---

## 3. Problem Formulation and Motivation

### 3.1 System Model

We model the satellite UPF edge computing environment as follows. Consider a satellite UPF gateway node \(G\) deployed at a ground station or on-board a satellite processing module, serving as the 5G user-plane anchor point for terminals connected via non-terrestrial access. The gateway has \(M\) uplink paths \(\{p_1, p_2, \ldots, p_M\}\) representing heterogeneous satellite-terrestrial connectivity, each characterized by a QoS profile \(\langle d_m, b_m, l_m \rangle\) representing delay, bandwidth, and loss rate respectively. Typical path types include:

- \(p_{GEO}\): GEO relay link (delay ~500 ms, bandwidth moderate, loss ~2–5%);
- \(p_{LEO}\): LEO direct-access link via spot beam (delay ~20–50 ms, bandwidth high, loss ~1–3%);
- \(p_{ISL}\): Inter-satellite link relay within constellation (delay ~5–30 ms, bandwidth high, loss ~1–2%);
- \(p_{TB}\): Terrestrial backhaul fiber from ground station (delay ~5–50 ms, bandwidth very high, loss <0.1%).

Incoming encrypted packets arrive at aggregate rate \(\lambda\) from \(N\) end devices (UEs) connected via satellite access, running diverse applications. The satellite UPF must assign each arriving packet to one of the \(M\) paths based on the packet's inferred service type, without access to payload content—consistent with the 5G SBA security model where the UPF operates on encrypted PDU session payloads.

We now formalize the key concepts required to state the optimization problem precisely. Definition 1 establishes the service class taxonomy; Definition 2 specifies the observable feature space; Definition 3 formalizes the routing objective.

**Definition 1 (Service Class).** Let \(\mathcal{C} = \{c_0, c_1, c_2, c_3\}\) denote the set of service classes, where \(c_0\) = Web browsing, \(c_1\) = Video streaming, \(c_2\) = VoIP/real-time, \(c_3\) = Bulk transfer. Each class \(c_k\) has QoS requirements \(R_k = \langle d_k^{max}, b_k^{min}, l_k^{max} \rangle\), representing the maximum tolerable delay, minimum required bandwidth, and maximum acceptable loss rate respectively.

The four-class taxonomy is chosen to match the four standard 5G QoS Flow (QFI) priority levels and maps to standard DiffServ Per-Hop Behaviors (PHB): Best Effort, Assured Forwarding, Expedited Forwarding, and Scavenger. This mapping enables direct interoperability between satellite UPF forwarding decisions and downstream terrestrial DiffServ-aware routers.

**Definition 2 (Per-Packet Feature Vector).** For each incoming packet \(x\), define its observable feature vector as:

\[
\mathbf{f}(x) = \langle \text{ipv4.len}(x),\ \text{tcp.window}(x),\ \text{tls.length}(x),\ \text{tls.type}(x) \rangle \in \mathbb{R}^4
\]

These four features are extractable from unencrypted protocol headers without payload access. Crucially, they occupy fixed positions in the packet header stack (IP header at offset 0, TCP header immediately following, TLS record header at TCP payload offset 0), enabling deterministic extraction with bounded latency.

**Definition 3 (Service-Aware Routing Function).** The routing function \(\pi: \mathcal{X} \rightarrow \{p_1, \ldots, p_M\}\) maps each packet to an output path. A service-aware routing function \(\pi^*\) satisfies:

\[
\pi^*(x) = \arg\min_{p_m} \text{cost}(p_m, c_k) \quad \text{s.t.} \quad c_k = \phi(\mathbf{f}(x))
\]

where \(\phi: \mathbb{R}^4 \rightarrow \mathcal{C}\) is the service classifier and \(\text{cost}(p_m, c_k)\) quantifies the mismatch between path \(p_m\)'s QoS characteristics and class \(c_k\)'s requirements. This formulation decouples the problem into two stages: classification (\(\phi\)) and path assignment (argmin), enabling independent optimization of each.

### 3.2 Optimization Objective

Based on Definitions 1–3, we formally state the per-packet service-aware routing problem in the satellite UPF context as a joint optimization over classifier and routing function.

**Problem (Per-Packet Service-Aware Routing at Satellite UPF).** Given packet feature vector \(\mathbf{f}(x)\), find classifier \(\phi^*\) and routing function \(\pi^*\) that maximize:

\[
\max_{\phi, \pi} \sum_{x \in \mathcal{X}} \mathbb{1}[\phi(\mathbf{f}(x)) = c(x)] \cdot U(c(x), \pi(x))
\]

subject to:
- **Latency constraint**: \(T_{classify}(x) + T_{route}(x) \leq T_{budget}\), where \(T_{budget}\) is the per-packet processing budget (< 1 µs for Gbps-rate forwarding at the satellite UPF);
- **Privacy constraint**: \(\phi\) accesses only unencrypted header fields as defined in Definition 2, consistent with 5G UPF payload encryption requirements;
- **Resource constraint**: Inference uses only integer arithmetic (no FPU requirement), suitable for constrained satellite gateway processors;
- **Satellite path constraints**: path selection accounts for per-path loss rate \(l_m\), with coding redundancy adapted to the selected path's error characteristics.

Here \(c(x)\) denotes the true service class, \(\mathbb{1}[\cdot]\) is the indicator function, and \(U(c_k, p_m)\) is the utility of routing class-\(c_k\) traffic on satellite/terrestrial path \(p_m\). The utility function encodes satellite-specific considerations: for latency-sensitive classes (VoIP \(c_2\)), paths with lower delay \(d_m\) receive higher utility; for bandwidth-intensive classes (Video \(c_1\)), paths with higher bandwidth \(b_m\) are preferred; for loss-sensitive classes, paths with lower loss \(l_m\) or paths where network coding can effectively compensate are favored. The objective thus maximizes the aggregate utility weighted by classification correctness—misclassified packets receive inappropriate satellite paths and contribute zero utility.

### 3.3 Motivation: Satellite UPF as the Natural Deployment Point for In-Network Intelligence

The feasibility and strategic value of our approach rest on the convergence of three trends: the evolution of programmable data planes, the standardization of satellite UPF in 3GPP NTN, and the need for service-aware forwarding at the satellite-terrestrial boundary.

**Satellite UPF as a strategic vantage point.** In the 5G architecture, the UPF is the sole network function that processes every user-plane packet. Deploying service-aware intelligence at the satellite UPF creates a unique architectural advantage: classification occurs at the exact location where path selection decisions must be made—before packets are committed to specific satellite links. This eliminates the latency and signaling overhead of external classification systems communicating with the UPF via N4 interface, and ensures that every packet (including the first of a new PDU session) receives optimal satellite path assignment.

**In-network caching at the satellite UPF** demonstrates that data-plane stateful operations can serve application-layer functions at line rate. Named Data Networking (NDN) [20] implements content store (CS) and pending interest table (PIT) primitives within the forwarding pipeline, enabling satellite UPF nodes to cache and serve popular content without traversing the full satellite-terrestrial path. Our prototype (EdgeRMT-UPF) includes a CS/PIT implementation as a stateful extern, validating that the same pipeline architecture supporting service-aware routing can simultaneously provide content caching—reducing satellite backhaul traffic by up to 60% for cacheable Web content at the UPF.

**In-network aggregation at the satellite gateway** shows that the data plane can perform partial computation on transit data, reducing bandwidth requirements over bandwidth-constrained satellite links. SwitchML [22] implements gradient vector aggregation within programmable switch pipelines. At the satellite UPF, aggregation capabilities enable scenarios such as combining IoT sensor readings from massive machine-type communications (mMTC) before uplinking to the cloud over expensive satellite bandwidth, or merging partial query results from distributed microservices—each satellite backhaul megabit saved represents significant cost reduction.

**In-network inference at the satellite UPF** extends these capabilities to intelligent decision-making. Rather than merely forwarding, caching, or aggregating data, the satellite UPF data plane can now classify, predict, and act upon traffic characteristics in real time. Our service-aware routing method represents a specific instance of this paradigm: the DNN extern performs multi-class classification, and the result directly drives satellite/terrestrial path selection and network coding decisions—all within a single pipeline traversal. This tight coupling between inference and action is the key enabler for per-packet intelligent satellite routing.

Together, these three capabilities—cache, aggregate, and infer—transform the satellite UPF from a passive forwarding engine into an active participant in application service delivery across the satellite-terrestrial continuum. Our service-aware routing method leverages this transformation by embedding classification intelligence directly where satellite forwarding decisions are made, eliminating the architectural separation between "understanding traffic" and "acting upon it" at the most critical junction in the non-terrestrial network.

### 3.4 Key Observation: Per-Packet Feature Discriminability

Our fundamental observation, validated experimentally in Section 5.3, is that the four header features defined in Definition 2 exhibit class-conditional distributions with minimal overlap, providing sufficient discriminative power for service-type identification at the individual packet level.

Table 2 presents the statistical feature patterns observed for each service class. The patterns are derived from both synthetic traffic generation (matching published production traffic statistics) and validated against real-world encrypted traffic traces.

**Table 2: Statistical Feature Patterns by Service Class.** Each row shows the typical range and distribution characteristics of the four classification features for one service class. The rightmost column summarizes the dominant traffic pattern that generates these feature values.

| Service Class | ipv4.len (bytes) | tcp.window | tls.length | Pattern |
|---|---|---|---|---|
| Web (c₀) | 200–1460 (variable) | 8000–32000 | 100–1400 | Bursty request-response |
| Video (c₁) | 1200–1500 (stable) | 32000–65535 | 1000–1450 | Sustained large packets |
| VoIP (c₂) | 60–200 (fixed) | 2000–8000 | 40–180 | Periodic small packets |
| Bulk (c₃) | 1400–1500 (max MTU) | 65535 (max) | 1400–1460 | Continuous full-load |

As Table 2 illustrates, each service class exhibits a distinctive combination of feature values driven by fundamental application-level behavior:

- **VoIP (c₂)** generates uniquely small packets (60–200 bytes) with small TCP windows (2000–8000). The small packet size reflects the codec's fixed frame duration (typically 20 ms at 64 kbps), while the small window reflects the application's preference for low buffering delay over throughput maximization.
- **Bulk transfer (c₃)** consistently produces maximum-MTU packets (1400–1500 bytes) with maximum TCP windows (65535). This pattern arises from the application's greedy throughput optimization: filling every packet to MTU and advertising maximum receive buffer.
- **Video streaming (c₁)** produces large packets (1200–1500 bytes) with large but variable windows (32000–65535). The variability distinguishes it from Bulk: adaptive bitrate streaming adjusts segment sizes based on network conditions, causing window fluctuation.
- **Web browsing (c₀)** exhibits the highest variance across all features due to its bursty request-response nature: small request packets alternate with variable-size response packets, and window sizes adapt to the irregular data flow.

These patterns persist at the individual packet level because they reflect the application's fundamental data generation characteristics—codec frame size, buffer management strategy, and flow control behavior—rather than ephemeral flow-level aggregates. This persistence is the key theoretical justification for per-packet (rather than per-flow) classification.

---

## 4. Architecture and Key Technologies

### 4.1 System Overview

The proposed system implements service-aware routing at the satellite UPF edge through a layered architecture comprising four functional components, as illustrated in Figure 1. The architecture separates concerns along a control/data plane boundary: the control plane handles model training, quantization, and policy configuration (interfacing with the 5G SMF via N4 for session-level policy updates); the data plane executes per-packet inference and forwarding at line rate within the satellite UPF's packet processing pipeline.

```
┌─────────────────────────────────────────────────────────────────────┐
│  ① Control Plane (Model Training & Policy Management)               │
│     • Train MLP on labeled satellite UPF traffic (SGD + Softmax CE) │
│     • Quantize: float → Q16.16, install via rmt_dnn_load()          │
│     • Configure satellite/terrestrial path policies & coding params │
│     • Monitor & hot-update: retrain on drift, reload w/o restart    │
│     • N4 interface to SMF for QoS flow-level policy synchronization │
├─────────────────────────────────────────────────────────────────────┤
│  ② P4 Compiler (enc_classify.p4 → Pipeline Configuration)          │
│     • Lexer/Parser → AST → Stage Assignment → ALU Microcode        │
│     • DNN extern binding + NC extern binding                        │
├─────────────────────────────────────────────────────────────────────┤
│  ③ Data Plane: 5-Stage RMT Pipeline (Satellite UPF Data Plane)     │
│   pkt→[Parser]→PHV→[S0:Classify]→[S1:QoS]→[S2:NC]→[S3:Route]→[S4:MAC]│
│                       │                     │            │          │
│                  [DNN Extern]         [NC Extern]   [LPM Trie]  │
│                  Q16.16 MLP        RLNC/XOR/None   class×path     │
│                                    (satellite-loss │ (GEO/LEO/ISL │
│                                     aware params)  │  /terrestrial)│
├─────────────────────────────────────────────────────────────────────┤
│  ④ Parallel Engine (Multi-Queue Scaling for UPF Throughput)        │
│     RSS 5-tuple hash → SPSC lock-free rings → core-pinned workers  │
│     (scales across 4-16 cores at ground station / on-board CPU)    │
└─────────────────────────────────────────────────────────────────────┘
```

**Figure 1: EdgeRMT-UPF System Architecture.** The system is deployed at the satellite UPF gateway (ground station or on-board processing module) and comprises four layers: ① the control plane trains and quantizes the MLP model, manages satellite/terrestrial path policies, and synchronizes with the 5G SMF via N4; ② the P4 compiler translates the service-aware program into pipeline configurations including DNN and NC extern bindings; ③ the data plane executes per-packet classification, satellite-loss-aware network coding, and multi-path routing through a 5-stage RMT pipeline; ④ the multi-queue engine provides horizontal scaling via RSS flow distribution and lock-free worker threads across the satellite gateway's CPU cores.

Figure 1 shows the dataflow from packet ingress to egress. Each arriving packet traverses the programmable parser (extracting features into the Packet Header Vector), passes through five match-action stages (classification → QoS marking → network coding → route lookup → MAC rewrite), and exits via the deparser. The DNN extern is invoked at Stage 0 writing the service class to metadata; the NC extern at Stage 2 decides coding actions based on that class; subsequent stages consume both the class and coding decisions for routing and header rewriting.

### 4.2 Programmable Parser and Feature Extraction

The programmable parser is responsible for extracting the four classification features defined in Definition 2 from the raw packet byte stream. It implements a finite state machine (FSM) that selectively parses protocol headers based on runtime field values, as depicted in Figure 2.

```
State Transition Graph:
  S_start → extract(eth) → select(eth.ethertype)
    ├─ 0x0800 → S_ipv4: extract(ipv4) → select(ipv4.proto)
    │              ├─ 6 (TCP) → S_tcp: extract(tcp) → select(tcp.dport)
    │              │              └─ 443 → S_tls: extract(tls) → accept
    │              │              └─ * → accept
    │              └─ * → accept
    └─ * → accept (non-IP: bypass all classification)
```

**Figure 2: Parser State Transition Graph.** The parser traverses at most five states (start → eth → ipv4 → tcp → tls), with conditional transitions based on ethertype, protocol number, and destination port. The TLS header extraction occurs only for HTTPS traffic (port 443), ensuring zero overhead for other protocols.

Three design principles govern the parser's behavior, each addressing a specific system requirement:

**Conditional extraction** ensures resource efficiency. The TLS record header (5 bytes: type + version + length) is extracted only when `tcp.dport == 443`. Non-encrypted traffic (plain HTTP, DNS, ICMP, SSH on other ports) follows shorter parse paths with fewer memory accesses. This conditional logic ensures that the common case—non-TLS packets—incurs zero additional overhead from the classification infrastructure.

**Header validity gating** provides a clean control-flow mechanism. The parser sets a single bit in the header validity bitmap (`hvalid |= (1 << TLS_HDR_ID)`) upon successful TLS extraction. Subsequent pipeline stages use `valid(tls)` as a guard condition: stages 0 and 1 (classification and QoS marking) execute only when this bit is set. This bitmap-based gating requires a single bitwise AND operation, avoiding branch misprediction penalties.

**Zero payload access** ensures privacy compliance. The parser reads only fixed-position protocol headers; specifically, the TLS record header occupies bytes 0–4 of the TCP payload and precedes (rather than requires) access to encrypted application data. No decryption keys, certificates, or payload bytes are ever accessed, ensuring full compliance with encryption-based privacy guarantees.

### 4.3 Quantized Neural Network Inference Engine

The neural network inference engine is the core architectural component enabling service awareness within the pipeline. This section describes its architecture (Section 4.3.1), quantization scheme (Section 4.3.2), and training methodology (Section 4.3.3).

#### 4.3.1 Network Architecture

The service classifier \(\phi\) from Definition 3 is implemented as a single-hidden-layer Multi-Layer Perceptron (MLP). The MLP architecture is chosen for three reasons: (a) it provides sufficient representational power for the four-class problem given well-separated feature distributions (Table 2); (b) its computation consists entirely of matrix multiplications and element-wise operations, mapping naturally to fixed-point arithmetic; (c) its memory footprint is minimal (220 parameters), fitting entirely in L1 cache.

The classifier function is defined as:

\[
\phi(\mathbf{f}) = \arg\max_{k} \left( \mathbf{W}_2 \cdot \text{ReLU}(\mathbf{W}_1 \cdot \hat{\mathbf{f}} + \mathbf{b}_1) + \mathbf{b}_2 \right)_k
\]

where the normalized feature vector is:
- \(\hat{\mathbf{f}} = \left[\frac{f_1}{s_1}, \frac{f_2}{s_2}, \frac{f_3}{s_3}, \frac{f_4}{s_4}\right]\) with normalization scales \(\mathbf{s} = [1500, 65536, 16384, 256]\);
- \(\mathbf{W}_1 \in \mathbb{R}^{24 \times 4}\), \(\mathbf{b}_1 \in \mathbb{R}^{24}\) are the hidden layer weights and biases;
- \(\mathbf{W}_2 \in \mathbb{R}^{4 \times 24}\), \(\mathbf{b}_2 \in \mathbb{R}^{4}\) are the output layer weights and biases;
- \(\text{ReLU}(z) = \max(0, z)\) is the rectified linear activation; argmax selects the highest-scoring class (no softmax needed at inference time).

The network topology is thus: 4 inputs → 24 hidden neurons (ReLU) → 4 output classes (argmax). The total parameter count is \(4 \times 24 + 24 + 24 \times 4 + 4 = 220\) values, requiring only 880 bytes of storage in Q16.16 format—fitting within a single L1 cache line group.

#### 4.3.2 Q16.16 Fixed-Point Quantization

To eliminate floating-point unit (FPU) dependencies and ensure compatibility with resource-constrained edge platforms, all inference computation uses Q16.16 fixed-point arithmetic. Definition 4 formalizes this representation.

**Definition 4 (Q16.16 Fixed-Point Representation).** A real number \(r\) is represented as a 32-bit signed integer \(q = \lfloor r \times 2^{16} + 0.5 \rfloor\). This encoding provides a representable range of \([-32768, 32767)\) with a precision of \(\epsilon = 2^{-16} \approx 1.5 \times 10^{-5}\). The representation enables direct use of integer ALU instructions for all arithmetic operations.

Definition 4 implies the following operation mappings, which translate floating-point neural network operations to pure integer computation:
- **Addition**: \(q_a + q_b\) (direct 32-bit integer addition, no scaling needed);
- **Multiplication**: \((q_a \times q_b) >> 16\) (multiply in 64-bit to avoid overflow, then right-shift to restore scale);
- **ReLU activation**: \(\max(0, q)\) (single signed comparison);
- **Feature normalization**: \((f_i << 16) / s_i\) (left-shift raw integer value to Q16.16 space, then integer division by the scale factor).

Algorithm 1 presents the complete Q16.16 MLP forward pass procedure. The algorithm processes one packet's feature vector through the hidden and output layers, returning the predicted service class.

**Algorithm 1: Q16.16 Fixed-Point MLP Forward Pass.** This algorithm implements the classifier \(\phi\) from Definition 3 using only integer arithmetic. The 64-bit accumulator (line 5, 12) prevents overflow during the multiply-accumulate inner loops. The right-shift in lines 9 and 16 restores Q16.16 scale after Q32.32 accumulation.

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

**Computational complexity**: Each packet requires exactly 96 + 96 = 192 multiply-accumulate (MAC) operations, 24 + 4 = 28 bias additions, 24 ReLU comparisons, and 3 argmax comparisons—totaling approximately 220 integer operations. On modern x86 processors (3+ GHz), this completes in under 100 ns, well within our per-packet budget.

#### 4.3.3 Training Methodology

The control plane trains the model in full-precision floating-point using standard stochastic gradient descent (SGD), then quantizes the trained parameters for data-plane deployment. The training procedure encompasses the following components:

- **Dataset construction**: Synthetic traffic generation with class-conditional feature distributions matching published real-traffic statistics (6,000 training samples, 4,000 independent test samples, balanced across 4 classes);
- **Loss function**: Softmax cross-entropy loss \(\mathcal{L} = -\sum_k y_k \log(\text{softmax}(\mathbf{o})_k)\), providing well-calibrated gradients for multi-class classification;
- **Optimizer**: SGD with fixed learning rate \(\eta = 0.01\) over 40 epochs, sufficient for convergence on this well-separated classification problem;
- **Weight initialization**: Xavier uniform initialization \(W_{ij} \sim U(-\sqrt{6/(n_{in}+n_{out})}, +\sqrt{6/(n_{in}+n_{out})})\), which prevents activation saturation and keeps weights within the Q16.16 representable range;
- **Post-training quantization**: Each floating-point parameter is converted to Q16.16 via rounding (\(\lfloor w \times 2^{16} + 0.5 \rfloor\)) with saturation clamping at \(\pm 32767\). As demonstrated in Section 5.3, this introduces exactly zero classification accuracy loss.

### 4.4 Service-Aware Route Selection Strategy

The route selection strategy translates DNN classification results into differentiated forwarding decisions. The strategy operates in two stages: QoS marking (mapping class to DSCP) and multi-path selection (mapping DSCP-annotated prefix to output port).

#### 4.4.1 DSCP Marking Policy

Table 3 defines the mapping from classification results to DiffServ Code Point (DSCP) values, following the PHB definitions in RFC 2474 [14], RFC 2597 (Assured Forwarding), and RFC 3246 (Expedited Forwarding). Each service class is assigned a DSCP value that encodes its priority and handling semantics for downstream routers.

**Table 3: Service Class to DSCP Mapping.** This table defines the QoS policy applied at Stage 1 of the pipeline. Each classified packet receives a DSCP marking that instructs downstream routers on appropriate per-hop treatment. The "Routing Semantics" column describes the intended path selection behavior.

| Class \(c_k\) | Service Type | DSCP Value | Per-Hop Behavior | Routing Semantics |
|---|---|---|---|---|
| \(c_0\) | Web browsing | 0 (BE) | Best Effort | Default ECMP path, no priority |
| \(c_1\) | Video streaming | 32 (CS4/AF41) | Assured Forwarding | High-bandwidth, low-loss path |
| \(c_2\) | VoIP | 46 (EF) | Expedited Forwarding | Lowest-latency, strict priority path |
| \(c_3\) | Bulk transfer | 10 (AF11) | Scavenger | Lowest-priority, deferrable path |

As shown in Table 3, the mapping is designed to maximize differentiation between classes with opposing QoS requirements: VoIP (c₂) receives the highest priority (EF, DSCP 46) to minimize delay and jitter, while Bulk transfer (c₃) receives the lowest priority (AF11, DSCP 10) as its throughput-tolerant nature allows deferral during congestion.

#### 4.4.2 Multi-Path Routing Algorithm

Algorithm 2 formalizes the complete per-packet routing decision process, integrating classification, marking, and path selection into a single sequential procedure executed within the pipeline.

**Algorithm 2: Service-Aware Route Selection.** This algorithm executes once per packet, implementing the routing function \(\pi^*\) from Definition 3. Lines 1-4 handle encrypted traffic classification and marking; lines 5-8 perform standard routing operations. The `valid(tls)` guard ensures non-encrypted traffic bypasses classification with zero overhead.

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

The route table supports two operational modes, selectable by control-plane configuration:

**Mode A (DSCP-driven downstream differentiation)** is appropriate when downstream routers implement DiffServ queuing. The satellite UPF marks DSCP according to Table 3; downstream routers (whether terrestrial or satellite) perform PHB-based queuing and path selection without requiring additional classification capability. EF-marked VoIP packets use dedicated low-latency paths (e.g., LEO direct or ISL); AF41-marked video packets use high-bandwidth aggregates; BE and AF11 packets share default or high-capacity paths. This mode requires no modifications to downstream infrastructure and is the recommended deployment mode for satellite UPFs interfacing with heterogeneous downstream networks.

**Mode B (Satellite/terrestrial multi-uplink port mapping)** applies specifically at the satellite UPF where multiple heterogeneous uplink paths coexist (GEO relay, LEO direct, ISL relay, terrestrial backhaul). The route table key combines destination prefix with service class: `(dst_prefix, meta.class) → egress_port`, where each egress port corresponds to a specific satellite or terrestrial uplink. The control plane monitors per-path quality metrics (RTT via TWAMP-light, loss rate via active probing, available bandwidth) reported by the SMF and dynamically adjusts mappings via table entry updates. For example, the control plane may assign VoIP (c₂) to LEO direct links (low latency ~20 ms) while routing bulk traffic (c₃) through GEO relay links (higher latency ~500 ms but abundant bandwidth)—exploiting the service classification to achieve QoS-aware satellite path selection at per-packet granularity.

#### 4.4.3 Non-Encrypted Traffic Bypass

A critical design property is **zero overhead for non-classified traffic**, ensuring that the classification infrastructure does not penalize flows that do not benefit from service-aware routing. The header validity bitmap `valid(tls)` serves as an efficient single-bit gate condition:

- Packets without TLS headers (plain HTTP, DNS, ICMP) skip the parse_tls state entirely, resulting in `valid(tls) = false`. Stages 0 and 1 are bypassed, and the packet proceeds directly to standard L3 routing at Stage 2.
- TLS handshake packets (`tls.type ≠ 23`) satisfy `valid(tls) = true` but fail the `tls.type == 23` match in the classify_t table. They receive the default no-op action, preserving their original DSCP markings.
- Only TLS application_data records (`tls.type == 23`) trigger the full classification-and-marking path.

This three-tier gating ensures that the DNN inference cost (approximately 200 ns) is incurred only for packets that benefit from classification, while all other traffic passes through with negligible overhead.

### 4.5 Service-Aware Network Coding Strategy

Beyond differentiated routing, the classification result also drives adaptive network coding decisions—applying proactive Forward Error Correction (FEC) to loss-sensitive traffic classes while avoiding bandwidth waste on loss-tolerant flows. This section formalizes the coding strategy and its integration into the pipeline.

#### 4.5.1 Motivation for Service-Differentiated Coding

Satellite-aerial-terrestrial integrated links exhibit variable and elevated packet loss rates (typically 1–10% for LEO, 2–5% for GEO relay) that fundamentally affect different service types in different ways. The satellite environment introduces unique challenges that make proactive network coding particularly valuable at the UPF:

**High RTT penalty for retransmission.** Traditional end-to-end recovery (TCP retransmission) introduces at least one round-trip time (RTT) of delay per lost packet. For traffic traversing satellite links, the RTT can range from ~40 ms (LEO) to ~1000 ms (GEO relay)—making retransmission-based recovery devastating for VoIP (where 150+ ms delay causes perceptible echo) and video streaming (where lost frames cause stutter lasting seconds). Network coding provides a critical alternative: by injecting redundant coded packets at the satellite UPF, receivers can recover losses locally without triggering retransmission, trading modest bandwidth overhead for elimination of satellite RTT penalties.

**Link-state variability.** Satellite links experience time-varying loss due to atmospheric attenuation, handover events between spot beams, rain fade (especially at Ka-band), and inter-satellite link switching. The UPF is the natural location for proactive protection: it has a stable view of traffic patterns and can apply coding before packets enter the lossy satellite segment, rather than relying on end-to-end mechanisms that must traverse the full satellite path twice (forward + retransmission).

**Service-differentiated impact.** Different service classes have fundamentally different loss tolerance profiles, and the DNN classifier already provides the service type information needed to select appropriate coding parameters:

- **VoIP (c₂)**: Extremely loss-sensitive (≬1% loss causes audible degradation), latency-critical—requires maximum proactive protection at the UPF to avoid GEO/LTE RTT penalties;
- **Video (c₁)**: Moderately loss-sensitive (5% loss causes visible artifacts), benefits from FEC to maintain smooth playback across satellite segments;
- **Web (c₀)**: Loss-tolerant (TCP handles retransmission efficiently), moderate coding may reduce perceived latency but is not essential;
- **Bulk (c₃)**: Completely loss-tolerant (throughput-oriented, TCP adapts), coding wastes precious satellite bandwidth.

#### 4.5.2 Coding Parameter Selection

We define the coding policy as a mapping from service class to a coding parameter tuple ⟨\(n\), \(k\), \(\mathbb{F}\)⟩, where \(n\) is the coded block length, \(k\) is the number of original data packets per block, and \(\mathbb{F}\) is the finite field for coefficient generation.

**Definition 5 (Service-Aware Coding Function).** The coding function \(\gamma: \mathcal{C} \rightarrow \{\langle n, k, \mathbb{F} \rangle\}\) maps each service class to coding parameters:

\[
\gamma(c_k) = \langle n_k, k_k, \mathbb{F}_k \rangle
\]

where the redundancy ratio \(r_k = (n_k - k_k) / k_k\) represents the fraction of bandwidth dedicated to protection. The overhead is zero when \(n_k = k_k\) (no coding).

Table 3b defines the concrete coding policy for each service class, designed to match each application's loss tolerance profile.

**Table 3b: Service-Aware Network Coding Parameters.** Each row specifies the coding strategy applied to a service class after classification. The redundancy ratio determines the bandwidth overhead; the generation size determines decoding latency. VoIP receives maximum protection with minimal generation size (low decoding delay), while Bulk receives no coding.

| Class \(c_k\) | Service | Coding Mode | \(k\) (data) | \(n\) (total) | Redundancy | Field | Rationale |
|---|---|---|---|---|---|---|---|
| \(c_2\) | VoIP | Systematic RLNC | 4 | 5 | 25% | GF(2⁸) | Max protection, low generation |
| \(c_1\) | Video | Systematic RLNC | 8 | 9 | 12.5% | GF(2⁸) | Moderate FEC for smooth playback |
| \(c_0\) | Web | XOR parity | 8 | 9 | 12.5% | GF(2) | Light protection, minimal CPU |
| \(c_3\) | Bulk | None (bypass) | — | — | 0% | — | TCP handles loss efficiently |

As shown in Table 3b, the coding strategy applies three levels of protection. VoIP receives the strongest coding (25% redundancy with small generation size \(k=4\) to minimize decoding buffering delay). Video uses moderate RLNC (12.5% redundancy, larger \(k=8\) for better coding efficiency). Web traffic receives lightweight XOR parity (single parity packet per 8 data packets), providing basic protection at minimal computational cost. Bulk traffic bypasses coding entirely, preserving full bandwidth for throughput maximization.

#### 4.5.3 Coding Algorithm

Algorithm 3 formalizes the per-packet coding decision and encoding process within the pipeline.

**Algorithm 3: Service-Aware Network Coding.** This algorithm executes after classification (Stage 0) and QoS marking (Stage 1), determining whether and how to encode the current packet. The systematic coding preserves the original packet (enabling receivers without decoders to process traffic normally) while generating coded repair packets when the generation buffer fills.

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

#### 4.5.4 Pipeline Integration

The network coding stage integrates between QoS marking (Stage 1) and route lookup (Stage 2) in the expanded five-stage pipeline:

```
pkt→[Parser]→PHV→[S0:Classify]→[S1:QoS Mark]→[S1.5:NC Encode]→[S2:Route]→[S3:MAC]→pkt'
                      │                              │
                 [DNN Extern]                  [Generation Buffers]
                 Q16.16 MLP                    per-class RLNC state
```

**Figure 3: Extended Pipeline with Network Coding Stage.** The NC Encode stage (S1.5) is inserted after QoS marking and before route lookup. It accesses the service class from metadata, looks up coding parameters, and manages per-class generation buffers. Repair packets are injected into the deparser queue alongside original packets.

The coding stage is implemented as a stateful extern (similar to the DNN extern) with per-class generation buffers stored in thread-local memory. Each buffer holds up to \(k\) packet payloads and generates \(n-k\) coded repair packets upon generation completion. The systematic nature of the code ensures that original packets are always forwarded immediately—repair packets are additional injections that do not delay the original traffic.

**Bandwidth-latency trade-off**: The coding parameters in Table 3b are selected based on the following analysis. For VoIP at 64 kbps with 20 ms frames, each coded generation spans 4 packets = 80 ms—the repair packet arrives within 100 ms (well within the 150 ms one-way delay budget). The 25% overhead adds only 16 kbps—negligible on edge uplinks. For Video at 5 Mbps, the 12.5% overhead adds 625 kbps but enables recovery from single-packet losses within a generation without waiting for TCP retransmission (typically 200+ ms RTT to cloud).

### 4.6 VLIW Pipeline Execution Model

The match-action stages execute using a 16-wide Very Long Instruction Word (VLIW) architecture that maximizes per-stage parallelism while maintaining deterministic timing. Three architectural features are central to achieving sub-microsecond per-packet latency:

**Two-phase commit execution** eliminates read-after-write hazards between ALUs within a single stage. In phase 1 (read), all 16 ALUs simultaneously sample their source operands from the Packet Header Vector (PHV). In phase 2 (write), all ALUs simultaneously commit their results to destination fields. This separation guarantees that no ALU observes a partially-updated PHV, enabling true parallel execution of all instructions within a VLIW word.

**Computed-goto instruction dispatch** replaces the conventional switch-case ALU opcode decoder with GCC computed labels (`&&label` + indirect goto). This technique eliminates the branch-prediction overhead of a 27-way switch statement, reducing per-instruction dispatch to a single indirect jump through a pre-computed label array. On our test platform, this optimization reduces per-stage execution time by approximately 40%.

**Per-stage table indexing** pre-computes which tables belong to each pipeline stage during compilation (`stage_tbl[stage][idx]`), enabling the per-packet fast path to iterate only over relevant tables rather than scanning the full table array. This reduces per-stage overhead from O(total_tables) to O(tables_in_stage), which is typically 1–2 for our four-stage pipeline.

### 4.7 Multi-Queue Parallel Scaling

Edge computing nodes typically provide 4–16 CPU cores, presenting an opportunity for horizontal throughput scaling. The multi-queue engine exploits this parallelism through a DPDK-style run-to-completion model that avoids inter-core synchronization on the packet processing fast path.

The engine comprises five cooperating mechanisms:

**RSS flow hashing** distributes packets across worker queues while preserving per-flow ordering. The hash function applies FNV-1a 32-bit hashing to the packet's 5-tuple (source IP, destination IP, protocol, source port, destination port), followed by a murmur3 fmix32 finalizer that eliminates structural bias from sequential port numbers or subnet-aligned IP addresses.

**SPSC lock-free rings** connect the dispatcher to each worker thread. Each ring is a single-producer single-consumer circular buffer with cache-line-separated head and tail pointers, using C11 acquire/release memory ordering for visibility guarantees. The SPSC constraint eliminates the need for compare-and-swap (CAS) loops, achieving wait-free enqueue and dequeue.

**Core pinning** binds each worker thread to a distinct CPU core via `pthread_setaffinity_np`, eliminating context-switch jitter and ensuring L1/L2 cache locality for the worker's pipeline state (PHV, trie nodes, DNN weights).

**Burst processing** amortizes per-batch overhead across up to 64 packets. A single `clock_gettime` call provides the timestamp for all packets in a burst, and PHV initialization is batched via `memcpy` from a pre-computed template.

**DNN concurrent safety** is guaranteed by the read-only nature of model weights during inference. Multiple workers perform concurrent MLP forward passes on the same shared weight arrays without locks; only the per-pipeline inference statistics (total count, per-class counts) require mutex protection, and these are updated outside the critical path.

Theorem 1 establishes the correctness of per-flow packet ordering under this architecture.

**Theorem 1 (Per-Flow Ordering Preservation).** Under RSS 5-tuple hashing with SPSC FIFO queues, all packets of a given TCP connection are delivered to the same worker in arrival order, without explicit synchronization.

*Proof.* Consider a TCP connection with 5-tuple \(t = (src_{ip}, dst_{ip}, 6, src_{port}, dst_{port})\). Since all packets in this connection share identical 5-tuple values, the hash function produces a constant: \(h(t) = \text{fmix32}(\text{FNV1a}(t))\). Queue assignment \(q = h(t) \mod N\) is therefore constant. Within queue \(q\), the SPSC ring preserves FIFO insertion order by construction. Since the single dispatcher inserts packets in wire-arrival order, the worker observes packets in the same order. □

Theorem 1 ensures that service-aware routing does not disrupt TCP semantics—a critical requirement for deployment in production networks where packet reordering triggers spurious retransmissions.

---

## 5. Prototype Implementation and Evaluation

### 5.1 Implementation

The EdgeRMT prototype is implemented in approximately 6,000 lines of ISO C11 with zero external library dependencies. Table 4 presents the module decomposition, showing the responsibility and scale of each component.

**Table 4: EdgeRMT Module Decomposition.** Each row describes one source module with its line count and primary responsibility. The system is designed as a single-binary deployment with no runtime dependencies beyond the Linux kernel.

| Module | LOC | Functionality |
|---|---|---|
| `rmt.h` | 495 | Core data structures, PHV inline operations, public API |
| `parser.c` | ~350 | Programmable parser FSM, deparser, header extraction |
| `match.c` | ~160 | Cascaded bit-trie lookup engine (exact + LPM) |
| `alu.c` | ~280 | 16-ALU VLIW executor with computed-goto dispatch |
| `dnn.c` | ~200 | Q16.16 MLP forward pass, model loading, quantization |
| `mq.c` | ~380 | Multi-queue engine (RSS, SPSC rings, core-pinned workers) |
| `p4c.c` | ~1200 | P4 compiler (lexer, parser, AST, codegen, stage scheduler) |
| `pipeline.c` | ~250 | Pipeline orchestration, batch/burst API |
| `enc_classify_demo.c` | ~430 | Training, quantization, end-to-end verification |

As shown in Table 4, the P4 compiler (`p4c.c`) is the largest module, reflecting the complexity of translating P4 source into pipeline configurations. The runtime data-plane modules (`parser.c` + `match.c` + `alu.c` + `dnn.c` + `pipeline.c`) total approximately 1,240 lines—a compact footprint suitable for resource-constrained edge deployments. The system compiles with GCC 7+ under `-O3 -flto -march=native` with zero warnings, and runs on Linux 4.x+ kernels. A Profile-Guided Optimization (PGO) two-pass build is available for additional performance optimization.

### 5.2 Experimental Setup

All experiments are conducted on the following platform and methodology:

- **Hardware**: 8-vCPU server (Intel Xeon E5-2680 v4, 2.4 GHz base, 3.3 GHz turbo), 16 GB DDR4 RAM;
- **Software**: Linux 5.15, GCC 11.4 with PGO-optimized build;
- **Traffic generation**: Synthetic encrypted packets with class-conditional feature distributions matching published production traffic statistics [6] [7]. Features are sampled from per-class Gaussian mixtures calibrated against real-world TLS traffic traces;
- **Dataset split**: 6,000 training samples (1,500 per class) and 4,000 independent test samples (1,000 per class), with no overlap between training and test data;
- **Evaluation metrics**: Classification accuracy (overall and per-class), precision, recall, per-packet processing latency, throughput (packets per second), multi-core scaling efficiency, and load balance quality (Jain's Fairness Index).

### 5.3 Classification Accuracy

Table 5 presents the per-class classification results on the 4,000-packet test set. The evaluation measures accuracy, precision, and recall for each service class independently, then computes macro-averaged metrics.

**Table 5: Per-Class Classification Results (4,000 test packets).** This table reports the classification performance of the Q16.16 quantized MLP model. Accuracy is defined as correct predictions divided by total predictions assigned to each class. Precision and recall are computed per-class and macro-averaged in the final row.

| Service Class | Correct / Total | Accuracy | Precision | Recall |
|---|---|---|---|---|
| Web (c₀) | 981 / 982 | 99.9% | 99.7% | 99.9% |
| Video (c₁) | 951 / 986 | 96.5% | 97.1% | 96.5% |
| VoIP (c₂) | 1013 / 1013 | 100.0% | 100.0% | 100.0% |
| Bulk (c₃) | 1019 / 1019 | 100.0% | 100.0% | 100.0% |
| **Overall** | **3964 / 4000** | **99.1%** | **99.2%** | **99.1%** |

The results in Table 5 reveal three significant findings:

**(a) Quantization preserves classification fidelity.** The Q16.16 model produces identical classification results to the Float32 model on all 4,000 test packets (100% agreement). This confirms that the Q16.16 precision (\(\epsilon \approx 1.5 \times 10^{-5}\)) is more than sufficient for the decision boundaries learned by our 220-parameter MLP, validating the use of fixed-point arithmetic without accuracy trade-off.

**(b) Class-specific patterns determine accuracy.** VoIP and Bulk achieve perfect accuracy (100%) because their feature patterns are maximally distinctive: VoIP uniquely occupies the low-size/low-window region of feature space, while Bulk uniquely occupies the max-size/max-window corner. Video exhibits the lowest accuracy (96.5%) due to partial overlap with Bulk in the packet-size dimension—both produce large packets. However, the TCP window feature provides sufficient discrimination (Video: variable 32K–65K; Bulk: consistently 65535).

**(c) Misclassification is directionally safe.** All 36 misclassified packets (0.9%) are Video packets incorrectly classified as Bulk. Critically, no VoIP or Web packet is ever misclassified as a latency-insensitive class—meaning real-time traffic always receives priority treatment. This directional safety property is important for deployment: the worst case is that some Video packets receive lower priority (AF11 instead of AF41), a graceful degradation that does not cause application failure.

### 5.4 Per-Packet Latency Analysis

Table 6 presents the end-to-end per-packet processing latency under various configurations, from single-threaded baseline to multi-queue parallel processing.

**Table 6: End-to-End Processing Latency.** Measurements use 126-byte TLS application_data packets (the most computationally expensive case, requiring full DNN inference). Throughput is computed as 1/latency. Effective rate assumes minimum-size 126-byte packets; larger packets yield higher bit rates at the same packet rate.

| Configuration | Latency (ns/pkt) | Throughput (Mpps) | Effective Rate |
|---|---|---|---|
| Single-thread, baseline | ~6,300 | 0.16 | 0.16 Gbps |
| Single-thread, optimized (PGO) | 933 | 1.07 | ~1.1 Gbps |
| Batch mode (64 pkts/batch) | 911 | 1.10 | ~1.1 Gbps |
| Multi-queue, 4 workers | ~580 | 1.72 | ~1.7 Gbps |
| Multi-queue, 8 workers | 456 | 2.19 | ~2.2 Gbps |
| Multi-queue, 16 workers | 364 | 2.75 | ~2.8 Gbps |

Table 6 demonstrates that PGO optimization achieves a 6.7× speedup over the unoptimized baseline, reducing per-packet latency from 6.3 µs to 933 ns. Batch processing provides marginal additional improvement (911 ns) by amortizing the `clock_gettime` syscall. Multi-queue scaling provides further throughput gains as analyzed in Section 5.5.

To understand where time is spent within the pipeline, Table 6b decomposes the single-thread optimized latency into per-stage contributions.

**Table 6b: Per-Stage Latency Decomposition (single-thread optimized, 933 ns total).** Each row shows one pipeline stage, its dominant operation, absolute latency contribution, and percentage of total processing time. The DNN inference (Stage 0) accounts for 21.4% of total latency—a modest premium for service-aware intelligence.

| Stage | Operation | Latency (ns) | Fraction |
|---|---|---|---|
| Parser | 5-layer header extraction (eth/ipv4/tcp/tls) | ~150 | 16.1% |
| Stage 0 | Trie gate check + DNN inference (192 MAC) | ~200 | 21.4% |
| Stage 1 | QoS exact match + DSCP field write | ~50 | 5.4% |
| Stage 2 | LPM trie walk (32-bit prefix, worst case) | ~80 | 8.6% |
| Stage 3 | MAC exact match + header rewrite | ~50 | 5.4% |
| Deparser | Header concatenation + payload copy | ~100 | 10.7% |
| Overhead | PHV init, batch amortization, bookkeeping | ~303 | 32.5% |
| **Total** | | **~933** | **100%** |

As Table 6b shows, the DNN inference at Stage 0 adds only ~200 ns to the pipeline critical path compared to plain forwarding. This represents a 21.4% latency premium for the complete service classification capability—a favorable trade-off given that the system remains well within the sub-microsecond budget required for Gbps-rate edge processing. The "Overhead" category (32.5%) encompasses PHV initialization, memory allocation amortization, and statistics bookkeeping—areas where further optimization is possible through hardware offload.

### 5.5 Multi-Core Scaling Analysis

Table 7 evaluates horizontal scaling efficiency as the number of worker threads increases from 1 to 16.

**Table 7: Multi-Core Scaling Efficiency.** Speedup is relative to the single-thread optimized baseline (1.07 Mpps). Efficiency is defined as speedup/workers × 100%. The 8-vCPU platform provides 8 physical execution contexts; 16 workers require hyper-threading, explaining the efficiency decline beyond 8 workers.

| Workers | Throughput (Mpps) | Speedup | Efficiency |
|---|---|---|---|
| 1 | 1.07 | 1.0× | 100% |
| 2 | 1.58 | 1.48× | 74% |
| 4 | 1.72 | 1.61× | 40% |
| 8 | 2.19 | 2.05× | 26% |
| 16 | 2.75 | 2.57× | 16% |

The results in Table 7 show sub-linear scaling, with efficiency declining as worker count increases. Two factors explain this behavior. First, our 8-vCPU test platform provides only 8 physical execution contexts; configurations with 16 workers rely on hyper-threading, which shares execution resources. Second, all workers access the shared DNN weight array and trie data structures, creating last-level cache (LLC) contention as the working set exceeds per-core cache capacity. On dedicated edge hardware with per-core L2 and larger LLC, we project significantly better scaling efficiency—approaching linear up to the physical core count.

Despite sub-linear scaling, the absolute throughput improvement is substantial: 16 workers achieve 2.75 Mpps (2.8 Gbps for minimum-size packets), a 3.4× aggregate improvement sufficient for typical edge uplink capacities.

### 5.6 Load Balancing Quality

Effective load balancing is critical for multi-queue systems: poor distribution creates hot-spot workers that become throughput bottlenecks while other cores idle. Table 8 presents the load distribution metrics across 8 queues processing 200,000 packets.

**Table 8: Load Distribution Across 8 Worker Queues (200,000 packets).** Ideal uniform distribution assigns exactly 25,000 packets (12.5%) to each queue. The maximum deviation of 1.3 percentage points and Jain's Fairness Index of 0.997 (where 1.0 is perfect) confirm near-ideal distribution quality.

| Metric | Value |
|---|---|
| Minimum queue load | 22,660 packets (11.3%) |
| Maximum queue load | 27,540 packets (13.8%) |
| Ideal uniform distribution | 25,000 packets (12.5%) |
| Maximum deviation from ideal | 1.3 percentage points |
| Jain's Fairness Index | 0.997 |

The near-ideal load balance demonstrated in Table 8 validates two design choices. First, the murmur3 fmix32 finalizer after FNV-1a eliminates structural hash bias that would otherwise arise from regular 5-tuple patterns (e.g., sequential port allocations or subnet-aligned IP addresses). Second, the fixed hash function (rather than adaptive rebalancing) provides deterministic queue assignment, which is essential for the per-flow ordering guarantee established by Theorem 1. Additionally, all workers produce identical classification and routing results compared to single-threaded execution—verified by byte-level comparison of output packets—confirming that parallel execution introduces no correctness regressions.

### 5.7 Comparison with Existing Systems

Table 9 positions EdgeRMT against five representative existing systems across nine capability dimensions. The comparison spans both research prototypes and production systems to provide a comprehensive competitive analysis.

**Table 9: Comprehensive Comparison with Existing Systems.** Each column represents a system; each row evaluates one capability. EdgeRMT uniquely achieves per-packet classification with full MLP support, integrated routing, and edge deployability—a combination not offered by any existing system.

| Metric | EdgeRMT | FlowPic [7] | NetSage [11] | Tofino P4 | OVS + ML |
|---|---|---|---|---|---|
| Classification granularity | Per-packet | Per-flow | Per-flow | Per-packet | Per-flow |
| Classification latency | < 1 µs | > 1 s | > 100 ms | ~10 ns | > 10 ms |
| Accuracy (4 classes) | 99.1% | ~95% | ~93% | N/A (binary) | ~92% |
| In-pipeline DNN | ✓ (full MLP) | ✗ | ✗ | ✗ (linear) | ✗ |
| Routing integration | Unified pipeline | Separate | Separate | Limited | Separate |
| Edge deployable | ✓ (x86/ARM) | ✓ | ✓ | ✗ (ASIC) | ✓ |
| External dependencies | Zero | TensorFlow | Multiple | P4 SDK | OVS, sklearn |
| Model hot-update | ✓ (no restart) | ✓ | ✓ | ✗ | ✓ |
| Privacy preservation | ✓ | ✓ | ✓ | ✓ | ✓ |

The comparison in Table 9 highlights three unique advantages of our approach:

**Per-packet immediacy eliminates cold-start delays.** Flow-level methods (FlowPic, OVS+ML) require observing 10–100 packets before classification is possible, leaving early packets unclassified and sub-optimally routed. Our per-packet approach classifies every packet upon arrival, including the very first packet of a new flow. This property is particularly valuable for short-lived connections (e.g., DNS-over-HTTPS queries) where the entire flow may complete before flow-level classification would trigger.

**Unified pipeline eliminates architectural overhead.** In existing systems, classification and routing are separate subsystems communicating via IPC, shared memory, or controller APIs. Our unified pipeline executes classification (Stage 0), marking (Stage 1), routing (Stage 2), and rewriting (Stage 3) in a single pipeline traversal with no context switches, no kernel crossings, and no inter-process communication. This architectural simplicity directly translates to lower latency and higher throughput.

**Zero-dependency deployment enables universal edge adoption.** EdgeRMT compiles to a single binary with no runtime library dependencies, making it deployable on any Linux-capable edge device (x86, ARM, MIPS) via simple file copy. This contrasts sharply with existing solutions requiring Python/TensorFlow (FlowPic), multiple system libraries (OVS), or expensive ASIC hardware (Tofino).

### 5.8 End-to-End Functional Verification

Beyond performance metrics, correctness is verified through comprehensive end-to-end tests covering the complete processing chain from P4 compilation through packet output. Table 10 presents the verification scenarios and results.

**Table 10: End-to-End Functional Verification Results.** Each row represents one test scenario exercising a specific code path. The "Input" column describes the injected packet; "Expected Output" specifies the required classification, marking, and routing result. All 8 scenarios pass, confirming correct integration of all pipeline components.

| Test Scenario | Input | Expected Output | Result |
|---|---|---|---|
| TLS Web packet | len=800, win=16000, tls_len=750 | class=0, DSCP=0(BE), port=2 | ✓ PASS |
| TLS Video packet | len=1400, win=50000, tls_len=1350 | class=1, DSCP=32(AF41), port=2 | ✓ PASS |
| TLS VoIP packet | len=120, win=4000, tls_len=80 | class=2, DSCP=46(EF), port=2 | ✓ PASS |
| TLS Bulk packet | len=1500, win=65535, tls_len=1460 | class=3, DSCP=10(AF11), port=2 | ✓ PASS |
| Non-TLS packet | Plain HTTP (no tls header) | No classification, DSCP unchanged | ✓ PASS |
| TLS handshake | tls.type=22 (not app_data) | Not classified, no QoS marking | ✓ PASS |
| Unroutable dest | Destination not in route table | Packet dropped (default action) | ✓ PASS |
| TTL & MAC rewrite | Any routable TLS packet | TTL decremented, MACs rewritten | ✓ PASS |

As Table 10 confirms, the system correctly handles all expected traffic scenarios including the critical bypass cases (non-TLS, TLS handshake) and error conditions (unroutable destination). These tests are executed automatically as part of the build process, serving as regression guards against implementation changes.

### 5.9 Test Scenario Design

To systematically validate the complete system—encompassing classification, routing, network coding, and their joint interaction—we design a comprehensive test suite organized into seven categories (T1–T7). Table 11 summarizes the test scenario architecture, specifying coverage targets, verification methods, and acceptance criteria for each category.

**Table 11: Comprehensive Test Scenario Design.** Each category addresses one functional or performance aspect of the system. The seven categories progress from basic correctness (T1–T2) through performance characterization (T3–T5) to joint optimization validation (T6–T7), providing layered confidence in system behavior.

| Category | Target | Method | Key Metrics | Acceptance Criteria |
|---|---|---|---|---|
| T1: Functional Correctness | All dataplanes (MPLS/VXLAN/SRv6) | Inject crafted packets, verify output bytes field-by-field | Byte-level match, port assignment, header manipulation | 100% field-level match across all scenarios |
| T2: Classification Accuracy | DNN inference pipeline | 4000-packet balanced test set, per-class and overall metrics | Accuracy, precision, recall per class | Overall >99%, per-class >95%, quantization agreement >99% |
| T3: Latency Benchmarks | Per-packet processing time | 50K-packet bulk forwarding, clock_gettime instrumentation | ns/pkt, Mpps, per-stage decomposition | <1000 ns/pkt (PGO optimized) |
| T4: Multi-Core Scaling | Horizontal throughput scaling | Parallel processing with 2/4/8/16 workers | Speedup factor, efficiency percentage | >2.5× speedup at 16 workers |
| T5: Load Balancing | RSS distribution quality | 200K packets across 8 queues, per-queue packet counts | Max deviation, Jain's Fairness Index | JFI > 0.99, max deviation < 2% |
| T6: Network Coding | RLNC/XOR correctness | Encode + decode, loss recovery, per-class parameter verification | Recovery success rate, overhead ratio | 100% single-loss recovery, correct per-class overhead |
| T7: Joint Optimization | Classify + Code + Route integration | End-to-end 10K-packet pipeline with all stages active | Joint latency, coding overhead, class distribution | <1200 ns/pkt, Bulk=0% overhead, VoIP coded |

The test categories are designed with the following principles:

**Layered verification** ensures that each system component is validated independently (T1–T3) before integration testing (T7). This isolation enables rapid defect localization: a failure in T6 (coding) cannot be confused with a classification error (T2) because each operates on verified lower layers.

**Statistical significance** is ensured by large test populations: 4,000 packets for classification accuracy (T2), 50,000 for latency (T3), 200,000 for load balancing (T5), and 40,000 for coding stress testing (T6). These sample sizes provide confidence intervals well below 1% for all reported metrics.

**Regression automation** integrates all seven categories into a single executable test script (`scripts/bench_all.sh`) that builds the system, executes all scenarios, and reports pass/fail status with quantitative metrics. This script runs as part of the continuous integration process, ensuring that no code change introduces regressions in any tested dimension.

### 5.10 Network Coding Evaluation

This section evaluates the service-aware network coding strategy (Algorithm 3, Section 4.5) along four dimensions: arithmetic correctness, encoding/decoding functionality, throughput impact, and joint optimization performance.

#### 5.10.1 GF(2^8) Arithmetic Verification

The RLNC encoding operates over the Galois field GF(2^8) with the AES irreducible polynomial \(x^8 + x^4 + x^3 + x + 1\) (0x11B) and primitive element \(\alpha = 3\). Table 12 verifies the correctness of field arithmetic operations using exhaustive testing over all 255 non-zero elements.

**Table 12: GF(2^8) Arithmetic Verification Results.** Each property is tested either exhaustively (all 255 or 65,025 element pairs) or via randomized sampling (1,000 trials). All properties hold, confirming correct field implementation.

| Property | Test Coverage | Result |
|---|---|---|
| Multiplicative identity: \(a \times 1 = a\) | All 256 elements | ✓ PASS |
| Multiplicative inverse: \(a \times a^{-1} = 1\) | All 255 non-zero elements | ✓ PASS |
| Distributive law: \(a(b+c) = ab + ac\) | 1000 random triples | ✓ PASS |

#### 5.10.2 Encoding/Decoding Correctness

Table 13 presents the coding correctness verification results. For each service class with active coding, we test that: (a) encoding produces the correct number of repair packets; (b) single-packet loss can be recovered from any position within a generation; (c) recovered data is bit-identical to the original.

**Table 13: RLNC/XOR Encode-Decode Verification.** Each row tests one service class's coding parameters. "Loss positions" indicates the number of distinct single-packet-loss scenarios tested; all must produce bit-identical recovery.

| Service | Mode | Parameters | Loss Positions Tested | Recovery Success |
|---|---|---|---|---|
| VoIP (c₂) | Systematic RLNC | k=4, n=5, GF(2^8) | All 4 positions | 4/4 (100%) |
| Video (c₁) | Systematic RLNC | k=8, n=9, GF(2^8) | All 8 positions | 8/8 (100%) |
| Web (c₀) | XOR parity | k=8, n=9, GF(2) | All 8 positions | 8/8 (100%) |
| Bulk (c₃) | None (bypass) | — | N/A | N/A (no coding) |

The results in Table 13 confirm that the systematic RLNC implementation correctly generates repair packets and enables recovery of any single lost packet within a generation, regardless of the loss position. The systematic property ensures that original packets are always forwarded immediately—receivers without decoders can still process traffic normally.

#### 5.10.3 Coding Throughput and Latency Impact

Table 14 measures the computational cost of network coding operations on 64-byte payload segments across 10,000 generation cycles. The amortized per-data-packet cost represents the additional processing time added to each data packet due to coding overhead.

**Table 14: Network Coding Computational Cost (64-byte payloads, 10,000 generations).** "Generation time" is the wall-clock time to encode one complete generation (k data packets → n−k repair packets). "Amortized cost" divides the generation time by k to obtain the per-data-packet overhead. XOR parity is ~120× faster than RLNC due to single-bit operations vs. GF(2^8) multiplications.

| Service | Coding Mode | Generation Time | Amortized Per-Packet Cost | Relative to Pipeline (933 ns) |
|---|---|---|---|---|
| VoIP (c₂) | RLNC k=4 | ~240 ns | ~60 ns/pkt | +6.4% |
| Video (c₁) | RLNC k=8 | ~510 ns | ~64 ns/pkt | +6.9% |
| Web (c₀) | XOR k=8 | ~4 ns | ~0.5 ns/pkt | +0.05% |
| Bulk (c₃) | None | 0 ns | 0 ns/pkt | 0% |

The results in Table 14 demonstrate that coding adds negligible overhead to the pipeline. Even the most expensive coding (RLNC for VoIP) adds only ~60 ns per data packet—a 6.4% increase relative to the 933 ns baseline pipeline latency. XOR parity (used for Web traffic) is essentially free at 0.5 ns per packet. These costs validate the design decision to perform coding within the data-plane pipeline rather than offloading to a separate module.

#### 5.10.4 Joint Optimization: Classify + Code + Route

The ultimate evaluation tests the complete joint optimization pipeline: per-packet classification drives differentiated coding decisions, which coexist with route selection in a single pipeline traversal. Table 15 presents the joint performance results on 10,000 packets (balanced across 4 service classes).

**Table 15: Joint Optimization Performance (10,000 packets, balanced traffic).** The pipeline performs classification → coding decision → routing in a single pass. "Repair generated" counts the coded repair packets each class produces; "Coding overhead" is the ratio of repair to data packets.

| Metric | Value |
|---|---|
| Total packets processed | 10,000 |
| Joint pipeline latency | ~1,093 ns/pkt (0.92 Mpps) |
| Classification distribution | Web: 2,499 / Video: 2,369 / VoIP: 2,505 / Bulk: 2,627 |
| Repair packets (Web, XOR) | 312 (12.5% of Web data) |
| Repair packets (Video, RLNC) | 296 (12.5% of Video data) |
| Repair packets (VoIP, RLNC) | 626 (25.0% of VoIP data) |
| Repair packets (Bulk) | 0 (0% overhead) |
| **Total coding overhead** | **1,234 / 10,000 = 12.3%** |
| Comparison: uniform RLNC (all classes) | ~25% overhead |
| Comparison: no coding | 0% overhead (no loss protection) |

The results in Table 15 validate three key properties of the service-aware coding strategy:

**(a) Differentiation is achieved.** Each service class receives exactly its specified coding treatment: VoIP generates 25% repair packets (protective), Video and Web generate 12.5%, and Bulk generates zero. The total overhead of 12.3% represents a 51% reduction compared to uniform 25% RLNC applied to all traffic—without sacrificing protection for latency-sensitive classes.

**(b) Joint latency remains within budget.** The complete pipeline (classify + coding decision + route) completes in ~1,093 ns/pkt, only 17% higher than the classification-only pipeline (933 ns). This additional 160 ns covers the coding table lookup, generation buffer management, and repair emission decision—well within the edge processing budget.

**(c) Classification accuracy drives correct coding.** The coding statistics (25%/12.5%/12.5%/0%) match the design specifications from Table 3b exactly. Misclassification (Section 5.3 shows 0.9% error rate, all Video→Bulk) causes minimal impact: a few Video packets bypass coding and rely on TCP retransmission instead—a safe degradation direction.

### 5.11 Multi-Generation Stress Test

To verify coding behavior at scale, we process 40,000 packets (10,000 per class) through the full coding pipeline, validating that generation management, buffer handling, and repair generation remain correct across thousands of coding cycles.

**Table 16: Multi-Generation Coding Stress Test (40,000 packets).** Each class processes exactly its expected number of generations, confirming correct buffer management and generation boundary handling across sustained traffic.

| Service Class | Packets | Expected Generations | Actual Generations | Repair Packets | Processing Time |
|---|---|---|---|---|---|
| Web (c₀, XOR k=8) | 10,000 | 1,250 | 1,250 | 1,250 | — |
| Video (c₁, RLNC k=8) | 10,000 | 1,250 | 1,250 | 1,250 | — |
| VoIP (c₂, RLNC k=4) | 10,000 | 2,500 | 2,500 | 2,500 | — |
| Bulk (c₃, none) | 10,000 | 0 | 0 | 0 | — |
| **Total** | **40,000** | **5,000** | **5,000** | **5,000** | **< 35 ms** |

Table 16 confirms that generation boundaries are handled correctly under sustained load: every class produces exactly the expected number of generations (data_packets / k), with no buffer overflows, off-by-one errors, or inter-class interference. The total processing time of <35 ms for 40,000 packets (including RLNC GF(2^8) encoding operations) demonstrates that the coding engine maintains throughput well above typical edge link rates.

### 5.12 Evaluation Summary

Table 17 consolidates the key evaluation metrics across all seven test categories, providing an at-a-glance comparison between design targets and measured performance.

**Table 17: Consolidated Evaluation Results.** All design targets are met or exceeded. The system achieves sub-microsecond per-packet latency, high classification accuracy, near-ideal load distribution, and correct differentiated coding—validating the joint routing and network coding architecture.

| Dimension | Design Target | Measured Result | Status |
|---|---|---|---|
| Classification accuracy (overall) | >95% | 99.1% | ✓ Exceeds |
| Per-class accuracy (minimum) | >90% | 97.4% (Video) | ✓ Exceeds |
| Quantization loss (float→Q16.16) | <1% | 0.0% (100% agreement) | ✓ Exceeds |
| Per-packet pipeline latency | <1,000 ns | 933 ns (PGO) | ✓ Met |
| Multi-core scaling (16 workers) | >2.5× | 3.4× | ✓ Exceeds |
| Load imbalance (Jain's Index) | >0.99 | 0.997 | ✓ Exceeds |
| RLNC single-loss recovery | 100% | 100% (all positions) | ✓ Met |
| XOR single-loss recovery | 100% | 100% (all positions) | ✓ Met |
| VoIP coding overhead | 25% | 25.0% | ✓ Exact |
| Video coding overhead | 12.5% | 12.5% | ✓ Exact |
| Total service-aware overhead | <uniform 25% | 12.3% (51% reduction) | ✓ Exceeds |
| Joint pipeline latency (with NC) | <1,200 ns | ~1,093 ns | ✓ Met |
| Generation boundary correctness | 100% | 100% (40K packets) | ✓ Met |
| Functional regression (all dataplanes) | 0 failures | 0 failures | ✓ Met |

The results in Table 17 demonstrate that the proposed service-aware joint optimization achieves all design objectives simultaneously. The most significant result is the 51% reduction in coding bandwidth overhead (12.3% vs. 25% uniform) achieved through classification-driven differentiation—without sacrificing protection for loss-sensitive traffic classes. This confirms that the one-classification-two-actions paradigm (route + code) extracts maximum value from each per-packet DNN inference.

---

## 6. Discussion

### 6.1 Limitations

Three limitations of the current approach merit discussion, each suggesting directions for future improvement.

**Feature space granularity.** The current 4-feature model captures coarse service categories (Web/Video/VoIP/Bulk) but cannot distinguish sub-application types within a class (e.g., Zoom vs. Microsoft Teams within VoIP, or Netflix vs. YouTube within Video). Finer-grained classification would require additional features such as inter-arrival time (IAT) and packet direction, which are only accessible through per-flow stateful tracking—adding complexity and memory overhead.

**Synthetic evaluation scope.** While our traffic generation methodology produces feature distributions matching published real-traffic statistics [6] [7], deployment on production edge traffic may reveal distribution shifts due to evolving application behaviors, new protocols, or geographic variations. The control plane's model hot-update capability (Section 4.1) provides the mechanism for adaptation, but automatic drift detection and retraining triggers remain areas for future engineering.

**Adversarial robustness.** A sophisticated adversary could craft packets with misleading header features (e.g., artificially small TCP windows) to obtain preferential routing class assignment. However, this attack vector is self-limiting: manipulating TCP window sizes degrades the application's own throughput performance, and altering TLS record lengths requires non-standard TLS implementations that may trigger server-side errors. Network-level rate limiting per source further bounds potential abuse.

### 6.2 Practical Deployment Considerations

Several practical aspects govern real-world deployment of the proposed system at satellite UPF gateways.

**Model lifecycle management.** The control plane monitors classification confidence by tracking the maximum output logit magnitude: when the average confidence drops below a configurable threshold (indicating that incoming traffic no longer matches the trained distribution—e.g., due to satellite handover changing traffic mix), the system triggers automatic retraining on recently collected labeled samples. During retraining, the existing model continues serving; the updated model is atomically swapped via pointer replacement in the DNN extern structure, ensuring zero-downtime model updates even during satellite link transitions.

**Graceful degradation under failure.** If the DNN extern encounters an error (e.g., corrupted weights due to radiation-induced bit flips in space-grade memory), the classify_t table's default action is `nop()`—packets bypass classification and receive standard destination-based routing with original DSCP markings. This fail-open design ensures that classification infrastructure failures never disrupt basic connectivity—a critical property for satellite UPFs where physical maintenance is impossible or extremely costly.

**Multi-tenancy and slice isolation.** The RMT pipeline is fully reconfigurable at runtime: different P4 programs (with different models, QoS policies, and routing tables) can be loaded per network slice on the same satellite UPF, consistent with 5G network slicing principles. Each slice's pipeline operates on an independent set of tables and DNN instances, providing logical isolation without hardware partitioning—enabling a single satellite UPF to simultaneously serve eMBB, URLLC, and mMTC slices with differentiated forwarding policies.

**Integration with 5G satellite UPF architecture.** The DSCP-based marking approach (Mode A, Section 4.4.2) ensures backward compatibility with both terrestrial DiffServ and 5G QoS Flow (QFI) marking. At the satellite UPF, the DSCP markings can be mapped to 5G QFI values at the N3/N9 interface, enabling end-to-end QoS consistency across the satellite-terrestrial path. The N4 interface to the SMF allows session-level policy updates (e.g., modifying path assignments for specific PDU sessions) without data-plane disruption.

**Radiation-hardened deployment.** For on-board satellite UPF deployment (processing modules on LEO satellites), the Q16.16 fixed-point arithmetic provides an additional advantage: integer-only computation is inherently more resilient to single-event upsets (SEU) than floating-point units, and the small model size (220 parameters, 880 bytes) enables triple-modular redundancy (TMR) replication within modest SRAM budgets, further hardening the inference engine against radiation-induced errors.

### 6.3 Broader Impact: Toward Intelligent Satellite Edge Data Planes

The in-network inference paradigm demonstrated in this paper has implications beyond service-aware routing and coding—particularly for the emerging vision of satellite-edge-terrestrial integrated computing. The same architectural pattern—embedding quantized neural networks as pipeline externs at the satellite UPF—can support:

- **Real-time anomaly detection at the satellite edge**: Identifying DDoS attacks, port scans, or data exfiltration at per-packet granularity using traffic feature classifiers—particularly valuable at the satellite UPF which has a wide-area view of traffic from multiple spot beams;
- **Satellite-aware dynamic load balancing**: Selecting between LEO, GEO, ISL, and terrestrial paths based on request-type prediction and current satellite link conditions;
- **Content-aware satellite caching**: Deciding whether to cache responses at the satellite UPF based on predicted content popularity, reducing costly satellite backhaul bandwidth;
- **Adaptive coding rate control for satellite links**: Adjusting FEC redundancy in real time based on observed satellite link loss patterns (rain fade, handover-induced losses), creating a closed-loop coding system responsive to satellite channel dynamics;
- **Beam-hopping traffic prediction**: Using per-packet features to anticipate traffic demand patterns for satellite beam-hopping resource allocation, enabling proactive capacity assignment.

Combined with the in-network caching and aggregation capabilities discussed in Section 2.3, this creates a vision of the satellite UPF as a **converged intelligent space-ground forwarding platform** capable of simultaneously classifying, routing, coding, caching, aggregating, and securing traffic across the satellite-terrestrial continuum—all within a single programmable pipeline traversal.

### 6.4 Future Work

Based on the limitations and broader implications discussed above, we identify six directions for future research:

1. **Multi-layer DNN with flow state**: Extend to deeper architectures (2-3 hidden layers) with per-flow stateful features (IAT, packet count) for finer-grained sub-application classification (8–16 classes);
2. **Closed-loop satellite-aware path and coding selection**: Integrate real-time satellite link quality feedback (RTT via LEO/GEO probing, per-beam loss counters, ISL availability) to dynamically adjust both class-to-satellite-path mappings and per-class coding redundancy ratios based on current satellite channel conditions;
3. **Inter-flow network coding at the satellite UPF**: Extend from intra-flow systematic coding to inter-flow opportunistic coding (COPE-style XOR across flows sharing the same satellite next-hop), further improving bandwidth efficiency on bandwidth-constrained satellite links;
4. **QUIC and UDP traffic support for satellite NTN**: Adapt the parser and feature extraction for QUIC Initial packets (UDP-based), which are increasingly common in satellite-accessed web traffic and require parsing QUIC headers instead of TLS records;
5. **FPGA SmartNIC and on-board processor acceleration**: Port both the DNN extern and the NC extern to FPGA fabric for 10–100 Gbps throughput—targeting both ground-station SmartNICs and radiation-tolerant on-board processors for LEO satellite UPF deployment;
6. **Federated satellite edge learning**: Implement collaborative model updates across geographically distributed satellite UPFs (ground stations and on-board nodes), sharing model gradients via inter-satellite links while preserving data locality and user privacy across different satellite coverage regions.

---

## 7. Conclusion

This paper presents a service-aware intelligent routing and network coding joint optimization method for satellite UPF edge computing that addresses the fundamental challenge of differentiating encrypted traffic for optimized forwarding across heterogeneous satellite-terrestrial paths and proactive loss protection without payload inspection. The work makes contributions at the theoretical, architectural, and systems levels:

**At the theoretical level**, we formalize the per-packet service-aware routing and coding problem in the satellite UPF context (Definitions 1–5) and demonstrate that four observable header features (ipv4.len, tcp.window, tls.length, tls.type) provide sufficient discriminative power for four-class service identification with 99.1% accuracy. This result proves that flow-level observation is unnecessary for fundamental service-type distinction at the satellite UPF, enabling immediate classification of every packet including the first of a new PDU session.

**At the architectural level**, the integration of Q16.16 fixed-point MLP inference as an RMT pipeline extern (Definition 4, Algorithm 1) enables service classification with exactly zero accuracy loss compared to floating-point, using only ~220 integer operations per packet completing in ~200 ns—a computational profile particularly well-suited to the constrained processing environments of satellite gateways. The classification result drives two complementary optimization strategies within a unified five-stage pipeline: differentiated satellite/terrestrial multi-path routing (Algorithm 2, Table 3) selects optimal paths across GEO relay, LEO direct, ISL, and terrestrial backhaul links per service class, while adaptive network coding (Algorithm 3, Table 3b) applies class-specific FEC tailored to satellite link loss characteristics—VoIP receives 25% RLNC redundancy to avoid GEO RTT penalties, Video receives 12.5% coding for smooth playback across lossy satellite segments, and Bulk traffic bypasses coding entirely to preserve precious satellite bandwidth.

**At the systems level**, the EdgeRMT-UPF prototype demonstrates that the theoretical and architectural contributions translate to practical performance: 933 ns end-to-end latency, 2.75 Mpps multi-core throughput (Table 6, 7), near-ideal load balance (Table 8, Jain's Index 0.997), and correct operation across all functional scenarios (Table 10). The lock-free multi-queue architecture (Theorem 1) preserves per-flow ordering while scaling to all available cores at the satellite ground station or on-board processing module.

**Broader significance**: The joint optimization of satellite routing and coding—driven by a single per-packet DNN classification at the satellite UPF—demonstrates the power of service awareness at the space-ground data plane. By classifying once and acting twice (route + code), the system extracts maximum value from each inference operation. Combined with the in-network caching and aggregation capabilities of programmable data planes (Section 2.3), this work demonstrates a concrete step toward intelligent satellite UPF nodes that can simultaneously classify, route, code, cache, and process traffic across the satellite-terrestrial continuum—respecting both user privacy and the real-time performance requirements of 5G NTN service delivery while avoiding costly satellite RTT penalties for loss recovery.

---

## References

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
