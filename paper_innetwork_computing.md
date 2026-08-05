# EdgeNet-CoM: Service-Aware In-Network Computing toward Satellite User Plane Functions

## Abstract

Satellite user-plane functions (UPFs) must make service decisions on encrypted packets while traffic traverses paths with different delay, capacity, and loss. Conventional UPF pipelines expose packet headers to forwarding logic but usually place service classification and coding outside the per-packet path. This paper evaluates EdgeNet-CoM, a software RMT framework that extracts four header features, performs Q16.16 MLP inference, writes a local QoS mark, and applies destination-based routing in one pipeline traversal. A connected coding harness reuses the inferred class to select RLNC/XOR profiles; it is not a pipeline-resident coding extern.

The prototype is implemented in ISO C11 and uses a P4-inspired compiler, integer-only inference, and RSS flow hashing with core-pinned workers. On the stated synthetic workloads, it reaches 99.1% accuracy on one 4,000-packet split and 98.53% ± 0.39% across five independent test streams; a threshold cascade reaches 97.68% on the same split, and a Gaussian-calibrated workload reaches 97.0% with a sibling trainer. On the 8-vCPU host, the in-memory pipeline records 592 ns median service time for a 126-byte packet under `-O3 -flto -march=native -funroll-loops` with PGO and 4.87 Mpps with 16 oversubscribed workers. The coding harness reports a 12.3% expected weighted repair ratio for the observed inferred-class mix.

These results establish an internally verified software-pipeline mechanism under controlled workloads. They do not establish accuracy on operational traffic, satellite-link performance, NIC throughput, in-pipeline repair emission, or 3GPP UPF conformance. The motivating deployment point is the post-decapsulation N6 reference point; N3/N9 GTP-U parsing and satellite-parameterized experiments remain future work.

**Index Terms:** In-network computing, programmable data planes, satellite networks, user-plane function, encrypted traffic classification, network coding.

---

## 1. Introduction

### 1.1 Motivation and Context

The motivating deployment is a *satellite UPF*: a user-plane function at the boundary between space and terrestrial networks. Edge computing moves computation toward the access network to reduce service delay and backhaul demand [1], [2], while 3GPP NTN work extends the 5G core to satellite access [28], [29]. The artifact evaluated in this paper is a standalone software data plane, not a 3GPP-conformant UPF. Thus, the satellite UPF is the deployment context and design target, not an experimental platform.

The scenario creates three design pressures. First, a gateway may choose among GEO, LEO, inter-satellite, and terrestrial paths whose delay, capacity, and loss differ substantially. Second, link-layer protection does not remove burst losses during rain fade, beam transitions, or handovers; the residual loss regime therefore matters when coding is placed before a satellite hop [28], [33]. Third, gateway and on-board processors may be more constrained than data-center servers. The experiments use a commodity x86 host, so they quantify software behavior rather than space-grade efficiency or radiation tolerance (Section 6.1).

Encryption removes the application signal that a forwarding policy would ideally use. HTTPS carries most web traffic [3], yet two flows with similar transport headers may have different delay and loss requirements. A UPF that waits for a flow-level classifier or an external controller can therefore add decision delay or lose packet-level context.

The resulting gap is architectural: forwarding is per-packet, whereas classification and service adaptation are often flow-level or control-plane functions. EdgeNet-CoM asks whether a compact classifier can run inside a software match-action pipeline and expose its result to the next actions without inspecting encrypted payload bytes. The question is one of mechanism feasibility, not a claim that the prototype replaces a production UPF or controller.

The parser imposes a concrete placement boundary. N3 and N9 carry user packets inside GTP-U tunnels on UDP port 2152 [45], [47], whereas the prototype expects decapsulated Ethernet/IPv4/TCP/TLS headers. We therefore evaluate the classifier at the N6 reference point, after decapsulation, and treat N3/N9 support as future work (Sections 4.2 and 6.2).

### 1.2 Design Requirements

The design question is whether a software UPF data path can combine header-only classification with immediate actuation without violating a per-packet processing budget. We express that question as five requirements and use them to delimit the evaluation.

**General requirements.**
- **R1 (Inline feature extraction under encryption).** Extract discriminative packet-level features that remain accessible under end-to-end encryption, without payload inspection.
- **R2 (Programmability and runtime reconfiguration).** Support reloading of classification models, QoS policies, and forwarding tables without restarting the data plane.

**Requirements sharpened by the satellite context.**
- **R3 (In-pipeline inference budget).** Complete classification within the per-packet forwarding budget (target < 1 µs at Gbps rates), thereby precluding flow-level observation windows or control-plane round-trips to the Session Management Function (SMF).
- **R4 (Throughput scaling with ordering guarantees).** Scale across the CPU cores available at a ground-station gateway while preserving per-flow packet ordering, a correctness requirement for TCP semantics.
- **R5 (Service-aware actuation across heterogeneous paths).** Expose classification results to downstream stages so that routing, QoS marking, and coding decisions can differ per service class, matching the heterogeneous QoS profiles of GEO, LEO, ISL, and terrestrial paths.

The prototype evaluates R1–R4 and the implemented portion of R5 (local QoS marking with a DSCP policy mapping, plus destination-based routing). Class-aware multi-path selection, dynamic link monitoring, and coding actuation inside the RMT pipeline are proposed extensions rather than evaluated capabilities.

### 1.3 EdgeNet-CoM Overview

EdgeNet-CoM is a software RMT framework for this placement point. Its central mechanism is a parse–classify–act traversal: the parser extracts four visible fields, a Q16.16 MLP writes a service class, and later tables apply QoS marking and destination lookup. A P4-inspired front end supplies parser and table configuration; RSS hashing, SPSC rings, and core-pinned workers provide parallel execution while preserving flow affinity. The coding harness is connected to the pipeline output but remains outside the RMT fast path.

The architecture therefore has two evidence paths. Application A is pipeline-resident and evaluates classification, local QoS marking, destination routing, and header rewrite. Application B is harness-level and evaluates whether the same inferred class can select an RLNC/XOR profile and generation state. Keeping these paths separate is essential: the paper validates shared decision metadata, not an integrated coding extern.

The intended placement is a gateway UPF at N6. A production deployment would still require GTP-U handling for N3/N9, SMF/PFCP authorization, atomic policy updates, real packet I/O, and satellite-link feedback (Section 6.2).

### 1.4 Contributions

This paper makes four bounded contributions:

1. **A software RMT architecture.** The framework combines header parsing, fixed-point inference, match-action processing, and service-aware forwarding, with a P4-inspired configuration front end (Sections 3 and 4).
2. **An integer-only classifier path.** The Q16.16 MLP agrees with its floating-point counterpart on the 2,000-sample quantization test and exceeds the threshold cascade by 1.42 percentage points on the headline split (Section 5.3).
3. **A scoped coupling of decisions.** The inferred class drives a local QoS mark governed by a DSCP policy in the pipeline and coding-profile selection in a connected harness; the latter is not presented as in-pipeline packet generation (Sections 4.6 and 4.7).
4. **An artifact-level evaluation.** Functional tests, repeated timing runs, multicore scaling, load balance, and standalone coding tests quantify what the implementation does under controlled workloads. They do not establish operational-traffic accuracy or 5G/satellite deployment readiness.

The remainder of this paper reviews related work (Section 2), defines the system model and optimization problem (Section 3), presents the design (Section 4), evaluates the prototype (Section 5), discusses limitations and deployment considerations (Section 6), and concludes (Section 7).

---

## 2. Related Work

Prior work relevant to EdgeNet-CoM falls into four groups: programmable data planes and software packet processing; encrypted-traffic classification; in-network caching and aggregation; and network coding with satellite edge computing. The final subsection positions the present artifact within these groups.

### 2.1 In-Network Computing and Programmable Data Planes

The RMT architecture [4] established reconfigurable match-action stages for protocol-independent packet processing, and P4 provided a portable programming abstraction for those stages [10]. Hardware pipelines deliver high throughput, but their per-stage instruction and state budgets limit the computations that can run inline. This constraint motivates a distinction between hardware-oriented in-network processing and software pipelines that can spend more cycles on each packet.

In-network machine learning explores the former direction. Xiong and Zilberman [11] encode binary decision trees in match-action tables; N3IC [12] uses a neural-network interface card for traffic analysis; and IIsy [13] maps random forests and SVMs to switch pipelines. The Packet Transaction model [17] extends the same substrate to stateful operations with transactional semantics. These systems establish that inference and state updates can be exposed to the forwarding path, but their model families and targets differ from a software RMT that executes a multi-class MLP.

Software data-plane systems provide the closest execution context. NetBricks [39] uses per-core run-to-completion NFV, ClickOS [40] provides isolated virtual middleboxes, and eBPF/XDP [41] places packet filtering in the kernel. P4-to-software targets apply the match-action abstraction to commodity processors, while FlowRadar [43], Sonata [42], and Marple [44] demonstrate stateful telemetry in programmable pipelines. These systems motivate the execution model used here, but they do not evaluate a header-only MLP whose output immediately drives QoS and routing in the same pipeline.

EdgeNet-CoM therefore trades hardware-level throughput for model expressiveness and configurability. Its contribution is not a new switch target; it is an artifact-level study of how a compact integer MLP can be inserted into a software RMT path and how its output can be consumed by subsequent actions.

### 2.2 Encrypted Traffic Classification

Encrypted-traffic classifiers trade decision latency, feature visibility, and model capacity. Flow-level methods use statistics such as inter-arrival times, packet-size distributions, and flow duration [5], [6]; FlowPic [7] turns flow observations into images, and Deep Packet [8] learns from byte sequences. These methods can improve context, but they must wait for an observation window and therefore do not provide an immediate packet-level decision.

Byte-sequence models inspect a larger ciphertext prefix [8], [9]. They avoid decryption but incur greater computation and retain more packet content than a four-field header model. The resulting privacy and deployment properties depend on the exact observation window and retention policy.

Header-only, per-packet classification is the relevant alternative for an inline UPF. It sacrifices flow context for bounded work and can act on an individual supported application-data record without waiting for a flow aggregate. EdgeNet-CoM evaluates this tradeoff with a four-class service taxonomy and controlled synthetic workloads; the resulting accuracy is evidence about the mechanism under those distributions, not about general application identification.

Industrial tools such as nDPI [38] use protocol metadata, signatures when available, and flow state to recognize a much broader protocol set. EdgeNet-CoM has a narrower objective: it avoids payload inspection and sends its class directly to forwarding actions. A fair comparison with nDPI would require the same captured traffic, labels, and measurement boundary; the present paper instead includes a threshold baseline on the identical generated workload.

### 2.3 In-Network Caching and Aggregation

Programmable data planes also execute stateful application functions. In-network caching [19] and NDN/ICN forwarding primitives [20] reduce retrieval work by keeping state near the forwarding path. In-network aggregation [21], including SwitchML [22], reduces redundant traffic by combining partial results in the network.

These systems establish the feasibility of application-specific state, but they address storage or reduction rather than encrypted service classification. EdgeNet-CoM uses the same principle of placing a small computation beside forwarding, then measures whether the resulting class can be reused for QoS and coding policy.

### 2.4 Network Coding

Network coding combines packets algebraically so that a receiver can recover losses without an immediate retransmission [23], [24]. COPE [25] exploits overhearing for XOR coding, Coded TCP [26] integrates redundancy with transport behavior, and Fulcrum [27] combines binary and GF(2⁸) codes for heterogeneous links. Each design chooses coding parameters from channel, transport, or complexity information.

Application B adds a different policy input: a packet-level service label. The harness assigns higher configured redundancy to the real-time class and bypasses coding for Bulk. This is a policy-coupling experiment, not a comparison with link-adaptive coding; the paper does not model a live loss trace or receiver deployment.

### 2.5 Satellite Edge Computing and Non-Terrestrial Networks

3GPP Release 17 and Release 18 work formalizes NTN support in the 5G system [28], [29]. Satellite-edge studies then address computation offloading and scheduling [30], [31], while UPF proposals place user-plane functions across LEO constellations [32] and use space-ground integration to preserve service continuity [33]. These studies motivate the deployment context but generally assume conventional forwarding inside the UPF.

Two additional threads bound the proposed applications. Satellite coding work addresses long RTTs and variable loss [34], [35], but the present harness does not compare against a shared link-adaptive trace or model residual link-layer FEC. P4-oriented satellite processing and routing [36], [37] show that programmable packet handling is a plausible direction; EdgeNet-CoM studies the complementary question of fixed-point inference in a software RMT prototype. Application-aware SDN and multi-service gateways [15], [16], [18] commonly classify flows or sessions before installing policy, whereas Application A acts on supported client-to-server TLS application-data packets in the same process as forwarding.

### 2.6 Positioning of This Work

Table 1 summarizes the resulting gap. It compares architectural scope, not cross-paper performance: the cited systems use different datasets, labels, targets, and hardware. The comparison therefore identifies what is combined in EdgeNet-CoM without implying a controlled head-to-head ranking.

**Table 1: Architectural Positioning Relative to Existing Approaches.** "Typical" describes the dominant design in each literature category; individual systems may differ.

| Dimension | Flow-Level ML [5-9] | In-Network ML [11-13] | SDN/SD-WAN [15-16] | Software NFV + ML [39-41] | Network Coding [23-27] | **EdgeNet-CoM** |
|---|---|---|---|---|---|---|
| Decision granularity | Flow | Packet | Flow/session | Packet | Packet/block | **TLS application-data packet** |
| Classifier placement | Host/server | Switch/NIC pipeline | External or controller-assisted | NF process / kernel (XDP) | None | **Software RMT pipeline (extern)** |
| Classifier family | General ML | Target-dependent restricted models | Tool-dependent | Arbitrary runtime (e.g., TFLite) | Not applicable | **Q16.16 MLP** |
| Routing coupled to classification | Typically no | System-dependent | Yes | Application-dependent | No | **Yes (local QoS mark + destination LPM)** |
| Coding coupled to classification | No | No | No | No | Typically no | **Yes (harness-level profile selection)** |
| Payload inspection required | Method-dependent | Usually no | Method-dependent | Application-dependent | No | **No** |
| Runtime policy updates | Model-dependent | Target-dependent | Yes | Application-dependent | System-dependent | **Tables and model parameters (stop-the-world reload)** |

The gap is the reuse of one packet-level classification result at two explicit boundaries: route-related actuation inside a software match-action pipeline, and coding-policy selection in a connected harness. Section 5 evaluates these boundaries on the same artifact and workload; it does not claim quantitative superiority over the cited systems.

---

## 3. System Model and Problem Formulation

The model separates the ideal policy from the implemented path. Section 3.1 specifies classes, features, paths, and coding profiles; Section 3.2 states the policy objective; Section 3.3 justifies N6 placement; and Section 3.4 defines the evaluation generator.

### 3.1 System Model

We model one gateway with heterogeneous uplinks. Let G denote a prospective satellite UPF at a ground station or satellite processing module, and let {p₁, …, p_M} be its available paths. Each path has a QoS profile ⟨d_m, b_m, l_m⟩ for delay, bandwidth, and loss rate. The profiles used in the motivating scenario are:

- p_GEO: GEO relay link (round-trip delay ≈ 500 ms, moderate bandwidth, burst loss during fade/handover);
- p_LEO: LEO direct-access link via spot beam (one-way delay 10–25 ms, high bandwidth, loss dominated by beam transition);
- p_ISL: inter-satellite relay within the constellation (one-way delay 5–30 ms, high bandwidth, low loss);
- p_TB: terrestrial backhaul fiber from the ground station (delay 5–50 ms, very high bandwidth, loss < 0.1%).

Encrypted packets arrive at aggregate rate λ from N devices. On N3/N9 they are GTP-U encapsulated [45], [47]; the evaluated path begins after decapsulation, where the pipeline sees the inner packet and assigns an output according to the supported policy. Here, "supported" means an Ethernet/IPv4/TCP packet whose TLS application-data header appears at the parser's expected offset. All other traffic follows the bypass path in Section 4.6.3.

The following definitions make the implementation and evaluation refer to the same objects. Definitions 1–2 specify classes and features; Definitions 3–4 specify inference and numeric representation; Definitions 5–6 specify the ideal routing and coding policies.

**Definition 1 (Service Class).** Let C = {c₀, c₁, c₂, c₃} denote the service classes: c₀ = Web browsing, c₁ = Video streaming, c₂ = VoIP/real-time, c₃ = Bulk transfer. Each class c_k has QoS requirements R_k = ⟨d_k^max, b_k^min, l_k^max⟩ (maximum tolerable delay, minimum required bandwidth, maximum acceptable loss rate). The four-class taxonomy is an application-level abstraction used by the prototype; it is not a one-to-one representation of standardized 5QI values or QFI identifiers. Operators may configure a mapping from inferred classes to local DiffServ per-hop behaviors and, in a complete UPF, to policy-defined 5G QoS flows.

**Definition 2 (Per-Packet Feature Vector).** For each incoming packet x, the observable feature vector is

f(x) = ⟨ipv4.len(x), tcp.window(x), tls.length(x), tls.type(x)⟩ ∈ ℝ⁴.

These features are visible without reading encrypted record contents. The implemented parser assumes Ethernet followed by a 20-byte IPv4 header, a 20-byte TCP header, and a TLS record header at the start of the TCP payload. IPv4 options, TCP options, fragmentation, IPv6, server-to-client traffic, and TLS records that do not begin at the segment boundary are unsupported and must either bypass classification or be handled by an extended parser. On GTP-U-encapsulated interfaces (N3/N9), the parser requires prior decapsulation; the prototype targets the N6 reference point, where user packets are already decapsulated (Section 4.2).

**Definition 3 (In-Network Computing Function).** The in-network computing function Φ: X → C × A maps each incoming packet to a service class and an associated action tuple Φ(x) = ⟨φ(f(x)), α(φ(f(x)))⟩, where φ: ℝ⁴ → C is the service classifier and α: C → A maps the class to actions. QoS and routing actions execute in the RMT pipeline; the evaluated coding action executes in the connected harness. The common property is that both consume the same inference result.

**Definition 4 (Q16.16 Fixed-Point Representation).** A real number r is stored as a signed 32-bit integer obtained by scaling by 2¹⁶, rounding half away from zero, and saturating at ±2,147,483,000 (the saturation bound used by the implementation's `quant()` function to leave headroom for subsequent multiply-accumulate). The representable range is approximately [−32768, 32767.99998], with a step of 2⁻¹⁶. Inference uses integer arithmetic; floating point remains confined to the control-plane training code.

**Definition 5 (Service-Aware Routing Function).** The routing function π: X → {p₁, …, p_M} maps each packet to an output path. A service-aware routing function π* satisfies π*(x) = argmin_{p_m} cost(p_m, c_k) subject to c_k = φ(f(x)), where cost(p_m, c_k) quantifies the mismatch between the QoS characteristics of path p_m and the requirements of class c_k. This formulation decouples classification (φ) from path assignment (argmin), enabling each to be optimized independently. The prototype implements destination-based LPM (Mode A); the composite key (dst_prefix, meta.class) required for π* is specified in Mode B but not implemented.

**Definition 6 (Service-Aware Coding Function).** The coding function γ: C → {⟨n, k, F⟩} maps each service class to coding parameters γ(c_k) = ⟨n_k, k_k, F_k⟩, where n is the coded block length, k is the number of original data packets per block, F is the finite field for coefficient generation, and the redundancy ratio r_k = (n_k − k_k)/k_k represents the fraction of bandwidth dedicated to protection.

### 3.2 Optimization Objective

Definitions 1–6 describe an ideal joint policy over classification, routing, and coding. The prototype does not solve this optimization; it implements the classifier and a fixed subset of the action tables.

**Policy objective (not an implemented optimizer).** Given f(x), an ideal policy would choose a classifier φ*, routing function π*, and coding function γ* that maximize

max_{φ,π,γ} Σ_{x∈X} 1[φ(f(x)) = c(x)] · U(c(x), π(x), γ(x))

subject to:
- **Latency constraint**: T_parse(x) + T_classify(x) + T_act(x) ≤ T_budget, with T_budget the per-packet processing budget (< 1 µs for Gbps-rate forwarding);
- **Data-access constraint**: φ accesses only the header fields in Definition 2, not encrypted TLS record contents;
- **Resource constraint**: inference uses only integer arithmetic (no FPU requirement);
- **Programmability constraint**: pipeline logic (classification model, action mappings, routing tables, coding parameters) is reloadable at runtime without data-plane restart;
- **Scaling constraint**: per-flow packet ordering is preserved under multi-core parallel execution;
- **Satellite path constraints**: path selection accounts for the per-path loss rate l_m, with coding redundancy adapted to the error characteristics of the selected path.

Here c(x) denotes the true service class, 1[·] is the indicator function, and U(c_k, p_m, γ_k) is the utility of routing class-c_k traffic on path p_m with coding strategy γ_k. We instantiate the utility as a weighted multi-attribute score reflecting the QoS match between service requirements and path capabilities:

U(c_k, p_m, γ_k) = w_{d,k} · u_d(d_m, d_k^max) + w_{b,k} · u_b(b_m, b_k^min) + w_{l,k} · u_l(l_m, l_k^max, γ_k)

where u_d, u_b, u_l ∈ [0, 1] are normalized satisfaction functions and the class-specific weights sum to 1. This expression is a design target. The experiments use static class-to-DSCP and class-to-coding tables; they do not estimate u_d, u_b, or u_l from live links and do not implement the argmin in Definition 5.

### 3.3 Why Place Inference at the UPF?

A gateway UPF is a natural insertion point because packets cross it before path selection and QoS enforcement. At N6, the parser observes the decapsulated IPv4/TCP/TLS headers, and the inferred class can be written into packet metadata before later match-action stages execute. TLS handshakes and unsupported protocols retain the default forwarding behavior.

The placement claim is deliberately narrow. EdgeNet-CoM tests whether a compact classifier can run as a software-pipeline extern and control two downstream actions; it does not claim that the prototype supplies a complete UPF, a GTP-U terminator, or a general stateful application platform.

### 3.4 Synthetic Feature Model

The evaluation tests one hypothesis: whether the four header features in Definition 2 can separate the four configured labels under a stated generator. Table 2 gives the generator ranges. Section 5.3 uses separate training and test draws from these distributions, then adds a Gaussian-calibrated workload and five independent seeds.

**Table 2: Configured Synthetic Feature Ranges by Service Class.** The rightmost column summarizes the generator profile; it is not a measured production-traffic statistic.

| Service Class | ipv4.len (bytes) | tcp.window | tls.length | Pattern |
|---|---|---|---|---|
| Web (c₀) | 200–1460 | 16384–45000 | 100–1400 | Broad ranges |
| Video (c₁) | 1200–1500 | 40000–65535 | 1200–16000 | Large packets and records |
| VoIP (c₂) | 60–200 | 4096–16384 | 20–160 | Small packets and records |
| Bulk (c₃) | 1400–1500 | 60000–65535 | 14000–16384 | Near-maximum values |

The ranges overlap in selected dimensions, so the generator contains both easy and ambiguous boundaries. VoIP and Bulk are separable from the other labels by combinations of ranges, whereas Web/Video and Video/Bulk share packet-size, window, and TLS-length intervals. The latter boundaries therefore determine most classification errors (Section 5.3).

The ranges are a reproducible mechanism test, not application invariants. VoIP may use RTP over UDP, and HTTP/2 or QUIC multiplexing can mix application contexts within one transport flow. The calibrated workload in Section 5.3 broadens the test but does not replace captured-traffic validation, which remains future work.

---

## 4. EdgeNet-CoM Design

One packet traverses the prototype from parsing to actuation. Sections 4.1–4.2 describe the architecture and parser; Section 4.3 describes inference; Sections 4.4–4.5 describe stage execution and parallelism; Sections 4.6–4.7 show the forwarding and coding-policy applications.

### 4.1 System Overview

Figure 1 separates the prototype into four functional layers. The control plane trains and quantizes the model and stores local policy; the compiler converts the supported P4-like input into runtime tables and stage metadata; the data plane parses, classifies, marks, routes, and rewrites each packet; and the multi-queue engine distributes flow-affine work across workers. SMF/N4 integration is a deployment interface, not part of the artifact.

```
       Model training and policy tables
                      |
             P4-inspired compiler
                      |
Pkt -> Parse -> Classify -> QoS -> Route -> MAC
                  |                 |
             Q16.16 MLP          LPM trie
                  |
          Marked output packet
                  |
          Coding test harness

RSS hash -> SPSC rings -> flow-pinned workers
```

**Figure 1: EdgeNet-CoM/UPF Framework Architecture.** The prototype comprises four layers: ① the control plane trains and quantizes the MLP model and configures table entries; ② the P4-inspired compiler translates the supported input subset into parser and pipeline configurations with a DNN extern binding; ③ the four-stage data plane executes classification, QoS marking, routing, and header rewrite, while the coding harness evaluates class-driven RLNC/XOR selection; and ④ the multi-queue engine distributes flows across worker threads. An integrated coding extern, N4/SMF integration, GTP-U parsing for N3/N9 placement, and on-board deployment are not implemented.

The per-packet data path is linear. A supported packet traverses the parser, four match-action stages (classification, QoS marking, route lookup, and MAC rewrite), and the deparser; the DNN extern writes the service class at Stage 0. The network-coding experiments use that class in a separate C harness after `rmt_process()`; they validate coding operations and selection logic, but not a compiled NC extern or repair-packet injection in the RMT deparser.

This separation also fixes the evidence boundary. The RMT path is evaluated for parsing, inference, marking, routing, rewriting, and parallel execution. The coding harness is evaluated for profile selection and standalone encode-decode behavior after `rmt_process()`; it is not evidence of an in-pipeline coding extern.

### 4.2 Programmable Parser and Feature Extraction

The programmable parser extracts the four classification features of Definition 2 from the raw packet byte stream. It implements a finite state machine (FSM) that selectively parses protocol headers based on runtime field values, as depicted in Figure 2.

```
start -> Ethernet
  non-IPv4 -> accept
  IPv4 -> TCP protocol?
    no -> accept
    yes -> destination port 443?
      no -> accept
      yes -> TLS header -> accept
```

**Figure 2: Parser State Transition Graph.** The parser traverses at most five states (start → eth → ipv4 → tcp → tls), with conditional transitions based on ethertype, protocol number, and destination port. TLS-header extraction occurs only for destination port 443; other traffic incurs the ordinary parser and forwarding cost but does not invoke DNN inference.

The parser applies three rules. First, it extracts the five-byte TLS record header only when `tcp.dport == 443`; other ports take the shorter bypass path. Second, successful extraction sets one bit in the header-validity bitmap (`hvalid |= (1 << TLS_HDR_ID)`), and stages 0–1 invoke the classifier only when `valid(tls)` is true. Third, the parser reads no encrypted record bytes or decryption keys. These rules bound classifier work and data exposure, but they do not by themselves resolve the privacy and policy obligations of a deployment.

The parser's encapsulation boundary determines where the framework can be deployed. The parser operates on the outermost Ethernet/IP headers that it observes; on the N3 and N9 5G interfaces, packets arrive as Ethernet/IPv4/UDP(2152)/GTP-U/inner-IP/TCP/TLS [45], so the parser would stop at the outer UDP header and never reach the inner TLS record. The evaluated prototype therefore assumes placement at the N6 reference point, where the UPF has already removed the GTP-U tunnel and forwards plain IPv4 packets toward the data network. Supporting N3/N9 placement requires a GTP-U decapsulation stage (outer IPv4/UDP parse, TEID extraction, inner-header re-parse) ahead of the existing FSM; we list this as future work (Section 6.4) and evaluate no GTP-U traffic in this paper.

### 4.3 Quantized Neural Network Inference

The inference engine turns the parsed feature vector into a class that later tables can consume. Sections 4.3.1–4.3.3 specify the MLP, its Q16.16 implementation, and the control-plane training procedure. The paper evaluates this engine only for the two applications defined in Section 1.3.

#### 4.3.1 Network Architecture

The service classifier φ is a single-hidden-layer MLP with four inputs, 24 hidden ReLU units, and four output logits. This topology is small enough for fixed-point execution and contains the matrix operations needed to represent the overlapping ranges in Table 2. It has 220 parameters (880 bytes in Q16.16), although cache behavior remains processor-dependent.

The classifier function is defined as

φ(f) = argmax_k ( W₂ · ReLU(W₁ · f̂ + b₁) + b₂ )_k

where the normalized feature vector is f̂ = [f₁/s₁, f₂/s₂, f₃/s₃, f₄/s₄] with normalization scales s = [1500, 65536, 16384, 256]; W₁ ∈ ℝ^{24×4}, b₁ ∈ ℝ²⁴ are the hidden-layer weights and biases; W₂ ∈ ℝ^{4×24}, b₂ ∈ ℝ⁴ are the output-layer weights and biases; ReLU(z) = max(0, z); and argmax selects the highest-scoring class (no softmax at inference time). The topology comprises 4 inputs, 24 hidden ReLU units, and 4 output logits followed by argmax.

#### 4.3.2 Q16.16 Fixed-Point Quantization

All data-plane inference uses Q16.16 fixed-point arithmetic; floating point is confined to control-plane training. Definition 4 gives the representation, and the resulting operations are:

- **Addition**: q_a + q_b (direct 32-bit integer addition, no rescaling);
- **Multiplication**: (q_a × q_b) >> 16 (multiply in 64 bits to avoid overflow, then right-shift to restore scale); the implementation keeps hidden-layer accumulators and activations in 64-bit storage;
- **ReLU activation**: max(0, q) (single signed comparison);
- **Feature normalization**: (f_i << 16) / s_i (left-shift the raw integer value into Q16.16 space, then integer-divide by the scale factor).

Algorithm 1 presents the forward pass implemented in `src/dnn.c`.

**Algorithm 1: Q16.16 Fixed-Point MLP Forward Pass (implementation form).** The hidden layer accumulates in a 64-bit Q32.32 register initialized from the bias shifted left by 16; hidden activations are stored in 64-bit registers, which after the >> 16 shift hold values inside the Q16.16 range (no 32-bit truncation occurs, and none is required for the evaluated dimensions). The argmax comparator is initialized to INT64_MIN so that all-negative logits are handled correctly.

```
Input: Raw features f[0..3], parameters (W1, b1, W2, b2, scales)
Output: Class k ∈ {0, 1, 2, 3}

1:  for i = 0 to 3 do
2:      xq[i] ← (f[i] << 16) / scales[i]
3:  end for
4:  for j = 0 to 23 do
5:      acc ← (int64_t) b1[j] << 16
6:      for i = 0 to 3 do
7:          acc ← acc + (int64_t) W1[j][i] * xq[i]
8:      end for
9:      h[j] ← max(0, acc >> 16)
10: end for
11: best_v ← INT64_MIN; best ← 0
12: for k = 0 to 3 do
13:     acc ← (int64_t) b2[k] << 16
14:     for j = 0 to 23 do
15:         acc ← acc + (int64_t) W2[k][j] * h[j]
16:     end for
17:     if acc > best_v then best_v ← acc; best ← k
18: end for
19: return best
```

The forward pass performs 192 multiply-accumulate operations, 28 bias additions, 24 ReLU comparisons, and three argmax comparisons. Section 5.4 measures elapsed service time; these counts alone do not predict latency on another processor.

#### 4.3.3 Training Methodology

The control plane trains the model in full-precision floating-point using standard SGD, then quantizes the trained parameters for data-plane deployment:

- **Dataset construction**: synthetic traffic generation with class-conditional feature distributions (6,000 training samples, 4,000 test samples, labels drawn uniformly);
- **Loss function**: softmax cross-entropy L = −Σ_k y_k log(softmax(o)_k);
- **Optimizer**: SGD with fixed learning rate η = 0.05 over 40 epochs;
- **Weight initialization**: independent uniform samples centered at zero and scaled by √(2/n_in) per layer;
- **Post-training quantization**: each floating-point parameter is scaled by 2¹⁶, rounded half away from zero, and saturated at ±2,147,483,000 (Definition 4). The fixed-point and floating-point models agree on all 2,000 samples in the quantization test reported in Section 5.3; this is an empirical result for that sample, not a general zero-loss guarantee.

### 4.4 VLIW Pipeline Execution

The match-action stages execute using a 16-wide Very Long Instruction Word (VLIW) architecture that maximizes per-stage parallelism while maintaining deterministic timing. Three architectural features are central.

**Two-phase commit execution** eliminates read-after-write hazards between ALUs within a single stage. In phase 1 (read), all 16 ALUs sample their source operands from the Packet Header Vector (PHV); in phase 2 (write), all ALUs commit their results to destination fields through a write-back buffer. No ALU observes a partially updated PHV, thereby enabling parallel execution of all instructions within a VLIW word.

**Computed-goto instruction dispatch** replaces a switch-case ALU opcode decoder with GCC computed labels (`&&label` + indirect goto). This eliminates the branch-prediction overhead of a 27-way switch and reduces dispatch to a single indirect jump through a pre-computed label array. The per-stage effect of this substitution is not separately instrumented in this paper; end-to-end service times are reported in Section 5.4.

**Per-stage table indexing** pre-computes which tables belong to each pipeline stage during compilation (`stage_tbl[stage][idx]`), so that the per-packet fast path iterates only over the relevant tables rather than scanning the full table array. This reduces the per-stage lookup overhead from O(total_tables) to O(tables_in_stage), typically 1–2 tables per stage.

Together, these features reduce dispatch and lookup overhead. Section 5 measures their combined effect on the evaluated x86 host; the results do not establish line-rate operation on other processors or with NIC I/O.

### 4.5 Multi-Queue Parallel Scaling

The multi-queue engine scales a compiled pipeline across cores while keeping synchronization off the packet fast path. It uses flow hashing, SPSC rings, core affinity, burst processing, and read-only shared model weights. The implementation is general across the repository's compiled programs; Section 5 measures its throughput and ordering behavior on the stated host.

**RSS flow hashing** distributes packets across worker queues while preserving per-flow ordering. The hash applies FNV-1a 32-bit hashing to the packet's 5-tuple (source IP, destination IP, protocol, source port, destination port), followed by a murmur3 fmix32 finalizer that eliminates structural bias arising from sequential port numbers or subnet-aligned IP addresses.

**SPSC lock-free rings** connect the dispatcher to each worker thread. Each ring is a single-producer single-consumer circular buffer with cache-line-separated head and tail pointers, using C11 acquire/release memory ordering. The SPSC constraint eliminates CAS loops, achieving wait-free enqueue and dequeue.

**Core pinning** binds each worker thread to a distinct CPU via `pthread_setaffinity_np`, eliminating context-switch jitter and improving L1/L2 locality for the worker's pipeline state (PHV, trie nodes, DNN weights).

**Burst processing** amortizes per-batch overhead across up to 64 packets. A single `clock_gettime` call provides the timestamp for all packets in a burst, and PHV initialization is batched via `memcpy` from a pre-computed template.

**DNN concurrent safety** follows from the read-only nature of model weights during inference. Multiple workers perform concurrent MLP forward passes on the same shared weight arrays without locks; only the per-pipeline inference statistics (total count, per-class counts) require mutex protection, and these are updated outside the per-packet critical path.

The first two mechanisms determine ordering; the remaining three reduce per-packet overhead and make shared execution practical. Under the single-dispatcher assumption, they give the ordering guarantee in Theorem 1.

**Theorem 1 (Per-Flow Ordering Preservation).** *Assumption: a single dispatcher thread inserts all incoming packets in wire-arrival order before RSS queue assignment.* Under RSS 5-tuple hashing with SPSC FIFO queues, all packets of a given TCP connection are delivered to the same worker in arrival order, without explicit synchronization.

*Proof.* Consider a TCP connection with 5-tuple t = (src_ip, dst_ip, 6, src_port, dst_port). All packets in this connection share identical 5-tuple values, so the hash function produces a constant h(t) = fmix32(FNV1a(t)). The queue assignment q = h(t) mod N is therefore constant. Within queue q, the SPSC ring preserves FIFO insertion order by construction. Since the single dispatcher inserts packets in wire-arrival order (assumption), the worker observes packets in the same order. ▢

The theorem covers the prototype's single-dispatcher topology. It does not cover multiple independent NIC dispatchers, where cross-queue arrival order is not controlled; a production multi-RX design would need an additional ordering mechanism.

### 4.6 Application A: Service-Aware Forwarding

Application A is the pipeline-resident application. It translates the inferred class into a local QoS mark and then performs destination-based lookup; the prototype does not make the lookup key class-aware.

#### 4.6.1 Local QoS Mark and DSCP Policy

Table 3 defines the prototype's local mapping from inferred classes to DiffServ code points, following the DiffServ architecture [14]. The prototype writes the codepoint number directly into its 8-bit `ipv4.tos` field as a test mark; it does not apply the production wire-format shift into the upper six DS-field bits or preserve ECN bits. The actual per-hop treatment therefore depends on downstream configuration and standards-compliant encoding.

**Table 3: Service Class to Local QoS Mark and DSCP Policy.** The prototype-mark column records the value written to `ipv4.tos`; the DSCP column records the intended codepoint. The final column states the operator policy, and the prototype does not configure downstream queues.

| Class c_k | Service Type | Prototype mark (`ipv4.tos`) | DSCP codepoint | Per-Hop Behavior | Routing Semantics |
|---|---|---|---|---|---|
| c₀ | Web browsing | 0 | 0 (BE) | Best Effort | Default ECMP path, no priority |
| c₁ | Video streaming | 32 | 32 (CS4) | Class Selector 4 | Operator-configured video treatment |
| c₂ | VoIP | 46 | 46 (EF) | Expedited Forwarding | Lowest-latency, strict priority path |
| c₃ | Bulk transfer | 10 | 10 (AF11) | Assured Forwarding class 1 | Operator-configured low-priority treatment |

The policy assigns EF (46) to the latency-sensitive label and AF11 (10) to Bulk. These labels express a policy choice, not an entitlement or a guarantee of a particular queue or path; a production deparser must encode the codepoint in the IPv4 DS field rather than copy the local mark unchanged.

#### 4.6.2 Implemented Routing and Multi-Uplink Extension

Algorithm 2 summarizes the implemented per-packet path through classification, marking, destination lookup, and header rewrite.

**Algorithm 2: Service-Aware Forwarding (Application A).** Lines 1–4 classify supported TLS records and write the local QoS mark; lines 5–8 perform destination-based forwarding. The algorithm does not implement the class-aware argmin in Definition 5.

```
Input: Packet x with (ipv4, tcp, tls), destination prefix d
Output: Egress port p and modified headers

1:  if valid(tls) and tls.type == 23 then
2:      c_k ← DNN_Infer(f(x))
3:      ipv4.tos ← QoS_Mark_Table[c_k]
4:  end if
5:  p ← LPM_Lookup(route_table, d)
6:  (dmac, smac) ← MAC_Table[p]
7:  ipv4.ttl ← ipv4.ttl - 1
8:  return (p, dmac, smac)
```

The design therefore has two routing modes. **Mode A (implemented)** writes the Table 3 local QoS mark and applies IPv4 longest-prefix matching. Downstream queue treatment and physical-path selection remain operator policy. **Mode B (proposed)** would extend the key to `(dst_prefix, meta.class)`, map entries to GEO, LEO, ISL, or terrestrial ports, and update them from link monitoring. The composite key and link-monitoring loop are not implemented or evaluated.

#### 4.6.3 Non-Encrypted Traffic Bypass

A header-validity gate prevents unsupported traffic from invoking DNN inference; such packets still incur the normal parsing and forwarding work, which is not separately benchmarked. Three cases arise:

- Packets without TLS headers (plain HTTP, DNS, ICMP) skip the parse_tls state entirely, resulting in `valid(tls) = false`. Stages 0 and 1 are bypassed, and the packet proceeds directly to standard L3 routing at Stage 2.
- TLS handshake packets (`tls.type ≠ 23`) satisfy `valid(tls) = true` but fail the `tls.type == 23` match in the classify_t table. They receive the default no-op action, preserving their original ToS value.
- Only TLS application_data records (`tls.type == 23`) trigger the full classification-and-marking path.

Thus, only TLS application-data records beginning at the expected location on destination port 443 invoke the classifier.

### 4.7 Application B: Service-Aware Network Coding

Application B evaluates a policy connection rather than a new pipeline stage. The standalone module implements RLNC/XOR encoding and decoding, and the joint harness uses the inferred class to select a profile and update generation counters. Repair-packet emission is not integrated into the RMT data path.

#### 4.7.1 Motivation, TCP Interaction, and Link-Layer Context

Satellite-link loss is time-varying, and recovery cost depends on both path RTT and application requirements. We therefore evaluate coding as a possible UPF policy, while treating transport and link-layer interactions as deployment constraints rather than measured results.

**RTT penalty for retransmission.** End-to-end recovery adds at least one round-trip interval after a loss is detected. Proactive coding can instead trade bandwidth and encode/decode work for recovery from a sufficient set of packets. The prototype validates the coding operations but does not measure end-to-end recovery delay on a satellite path.

**Interaction with TCP congestion control.** Systematic UPF-level coding can hide losses from the TCP sender: if a repair packet restores a segment, the sender's congestion window does not shrink. On a shared capacity-constrained link, an uncoordinated coded flow can therefore retain window share during congestion and disadvantage uncoded flows. Coded TCP [26] and performance-enhancing proxy designs (RFC 3135 [49]) address related loss-hiding effects. EdgeNet-CoM uses transparent RLNC without endpoint participation or feedback to the sender, so deployment would require rate provisioning or bounded redundancy. Congestion interaction is not evaluated here.

**Relation to satellite link-layer FEC.** DVB-S2X adaptive coding and modulation [48] reduces clear-sky residual errors, so UPF-level RLNC would target the remaining burst pattern during rain fade, beam transitions, or handovers rather than a steady i.i.d. loss process. The Table 4 ratios are illustrative; deployment requires measured residual-loss bursts, decoder delay, and path capacity.

**Service-differentiated policy.** The classifier supplies the label used to select one of four illustrative profiles:

- **VoIP (c₂)**: latency-sensitive; assigned the smallest generation with the highest configured redundancy;
- **Video (c₁)**: assigned moderate RLNC protection with a larger generation;
- **Web (c₀)**: assigned a low-complexity XOR parity profile;
- **Bulk (c₃)**: assigned no coding so that the illustrative policy adds no redundancy overhead.

#### 4.7.2 Coding Parameter Selection

The coding policy maps each service class to a tuple ⟨n, k, F⟩, implementing the policy abstraction in Definition 6. Table 4 gives the profiles used by the harness.

**Table 4: Service-Aware Network Coding Parameters.** Each row specifies the coding strategy applied after classification. The redundancy ratio determines bandwidth overhead; generation size bounds buffering delay. VoIP receives the highest configured redundancy and the smallest generation; Bulk receives no coding.

| Class c_k | Service | Coding Mode | k (data) | n (total) | Redundancy | Field | Generation-assembly delay at 20 ms frames | Rationale |
|---|---|---|---|---|---|---|---|---|
| c₂ | VoIP | Systematic RLNC | 4 | 5 | 25% | GF(2⁸) | up to 80 ms | Max protection, low generation |
| c₁ | Video | Systematic RLNC | 8 | 9 | 12.5% | GF(2⁸) | bounded by encoder GOP for typical configurations | Moderate FEC for smooth playback |
| c₀ | Web | XOR parity | 8 | 9 | 12.5% | GF(2) | per-block parity, class-local generation state | Light protection, minimal CPU |
| c₃ | Bulk | None (bypass) | — | — | 0% | — | 0 | TCP handles loss efficiently |

Buffering dominates the policy's latency trade-off. With 20 ms VoIP frames, k = 4 can delay a repair packet by up to 80 ms before propagation, queueing, or processing. This consumes about 53% of the 150 ms conversational-delay guideline in ITU-T G.114 [50]. A deployment targeting that budget should reduce k or add timeout-based early emission; the prototype measures generation accounting, not speech quality.

#### 4.7.3 Coding Algorithm

Algorithm 3 specifies the intended per-packet coding decision and encoding process.

**Algorithm 3: Service-Aware Network Coding (Application B).** The class selects a coding profile and generation buffer. The standalone coding module implements the RLNC/XOR operations and decoder tests. The joint benchmark implements the lookup and generation accounting, but it does not inject the optional repair packet into the RMT output path.

```
Input: Packet x with class c_k, generation buffer G[c_k]
Output: Original x and optional coded packet x_c

1:  params ← Coding_Table[c_k]
2:  if params.mode == NONE then
3:      return x
4:  end if
5:  G[c_k].append(x)
6:  if |G[c_k]| == params.k then
7:      for i = 1 to (params.n - params.k) do
8:          coefficients ← Random(params.field, params.k)
9:          x_c ← LinearCombine(coefficients, G[c_k])
10:         Emit(x_c, with coding header)
11:     end for
12:     G[c_k].clear()
13: end if
14: return x
```

#### 4.7.4 Prototype Integration Boundary

The evaluated prototype connects classification and coding through the C test harness:

```
Pkt -> RMT pipeline -> marked output
                            |
                            v
                  Coding policy harness
                     |             |
             Generation counts  RLNC/XOR tests
```

**Figure 3: Evaluated Classification-Coding Integration.** The RMT pipeline produces the service class through its QoS marking. `nc_demo.c` derives the class from the marked packet, selects the coding profile, and updates per-class generation state. Encoder and decoder correctness are tested separately on payload buffers.

This split validates the policy coupling and the coding algorithms, but it is not equivalent to a stateful NC extern in the packet pipeline. The contribution claim of Section 1.4 is therefore scoped accordingly: one in-pipeline actuation (local QoS marking plus destination routing) and one harness-level actuation (coding-profile selection and generation accounting). An end-to-end implementation still requires buffer ownership across workers, a coding header, repair-packet construction, deparser injection, receiver integration, and congestion-control behavior (Section 4.7.1).

Table 4 makes the policy trade-off explicit: smaller generations reduce buffering, while redundancy increases protection cost. The values are illustrative. Deployment requires measured loss bursts, packet rates, decoder delay, path capacity, and transport interactions.

---

## 5. Prototype and Evaluation

The evaluation connects artifact behavior to measurements. Sections 5.1–5.2 define the implementation and measurement protocols; Section 5.3 evaluates classification; Sections 5.4–5.6 evaluate service time, scaling, and load balance; Sections 5.7–5.9 delimit comparison and functional coverage; Sections 5.10–5.11 evaluate coding; and Section 5.12 summarizes the evidence.

### 5.1 Implementation

The EdgeNet-CoM/UPF prototype is implemented in ISO C11. The core framework comprises 3,510 lines distributed across 11 modules (data-plane modules, P4-inspired front end, runtime, and driver); the classification and coding application modules contribute 1,493 lines; and the tests contribute 1,886 lines. All counts are `wc -l` physical line counts of the files listed in Table 5; the counting method includes blank lines and comments. The implementation uses the C/POSIX runtime and `libm` during model training but no third-party machine-learning framework.

**Table 5: EdgeNet-CoM/UPF Module Decomposition.** Line counts are `wc -l` values for the evaluated artifact.

| Module | LOC | Functionality |
|---|---|---|
| `include/rmt.h` | 494 | Core data structures, PHV inline operations, public API |
| `src/parser.c` | 187 | Programmable parser FSM, deparser, header extraction |
| `src/match.c` | 158 | Cascaded bit-trie lookup engine (exact + LPM) |
| `src/alu.c` | 398 | 16-ALU VLIW executor with computed-goto dispatch |
| `src/dnn.c` | 168 | Q16.16 MLP forward pass, model loading, quantization |
| `src/mq.c` | 389 | Multi-queue engine (RSS, SPSC rings, core-pinned workers) |
| `src/p4c.c` | 933 | Compiler for the repository's P4-like subset |
| `src/pipeline.c` | 368 | Pipeline orchestration, batch/burst API |
| `src/runtime.c` | 212 | Control-plane entry installation API |
| `src/phv.c` | 21 | PHV utilities |
| `src/main.c` | 182 | CLI driver |
| **Core framework total** | **3,510** | |
| `src/enc_classify_demo.c` | 644 | Training, quantization, protocol A/B evaluation, rule baseline, latency benchmark |
| `src/nc_demo.c` | 849 | RLNC/XOR module + joint classification-coding harness |
| **Application modules total** | **1,493** | |
| `src/tests.c` | 899 | Functional tests, bulk/parallel/multi-queue benchmarks |
| `src/test_match.c` | 532 | Match-engine unit tests |
| `src/pcap_test.c` | 455 | Pcap-driven pipeline test (calibrated workload) |
| **Test code total** | **1,886** | |

The subset compiler (`p4c.c`) is the largest core module; the runtime data-plane modules (`parser.c`, `match.c`, `alu.c`, `dnn.c`, `pipeline.c`) total 1,279 lines. The default build uses GCC with `-O3 -flto -march=native -funroll-loops`; a two-pass PGO build (`-fprofile-generate` followed by `-fprofile-use`, with the profile collected from the bulk/parallel/multi-queue workloads) is provided in the Makefile. Portability beyond the tested Linux/x86 environment has not been evaluated.

### 5.2 Experimental Setup

The setup fixes seven elements: hardware, software, timing, traffic generation, dataset protocols, baseline, and metrics.

- **Hardware**: 8-vCPU mobile platform (13th Gen Intel Core i5-13420H: 4 performance cores + 4 efficiency cores, no hyper-threading exposed), 10 GB RAM. All reported measurements use this host; results from other processors are outside the measurement scope.
- **Software**: Linux 7.0.0-28, GCC 13.3.
- **Timing methodology**: every latency figure is the median of at least three runs (five for the single-packet service time); the ranges in parentheses report the minimum and maximum observed values. The turbo/thermal behavior of the mobile platform produces run-to-run variance that a single point estimate would conceal; measurements were spaced by ≥ 20 s idle intervals.
- **Traffic generation**: synthetic TLS feature vectors sampled independently from the class-specific uniform ranges in Table 6; the calibrated workload additionally uses per-class Gaussian distributions (§5.3.5).
- **Dataset split and protocols**: *Protocol A* draws training and test samples from one `rand()` stream seeded with 42 (6,000 training, 4,000 test). *Protocol B* retrains for each of five seeds {42, …, 46} on 6,000 samples from `srand(seed)` and evaluates on 4,000 samples from an independent stream `srand(seed + 7919)`, so that the test samples are not a continuation of the training stream. Both protocols draw class labels uniformly, so the realized per-class counts are close to, but not exactly, 1,000.
- **Baseline**: a threshold-rule classifier derived from the configured ranges of Table 6 (no training) is evaluated on the identical Protocol A feature vectors (§5.3.3).
- **Evaluation metrics**: overall accuracy, per-class recall and precision, confusion matrix, per-packet processing latency, throughput, multi-core scaling efficiency, load distribution, and coding correctness and overhead.

#### 5.2.1 Synthetic Traffic Generator Parameters

To enable reproducibility, Table 6 specifies the exact inclusive integer ranges used by `gen_sample()` in `src/enc_classify_demo.c`. Features are sampled independently from a discrete uniform distribution within each range; `tls.type` is constant.

**Table 6: Per-Class Feature Ranges in the Synthetic Traffic Generator.** U[a,b] denotes a discrete uniform sample over the inclusive integer range.

| Feature | Web (c₀) | Video (c₁) | VoIP (c₂) | Bulk (c₃) |
|---|---|---|---|---|
| `ipv4.len` | U[200,1460] | U[1200,1500] | U[60,200] | U[1400,1500] |
| `tcp.window` | U[16384,45000] | U[40000,65535] | U[4096,16384] | U[60000,65535] |
| `tls.length` | U[100,1400] | U[1200,16000] | U[20,160] | U[14000,16384] |
| `tls.type` | 23 (app_data, 100%) | 23 (app_data, 100%) | 23 (app_data, 100%) | 23 (app_data, 100%) |

The generator uses `rand()` with seed 42 and draws a class label uniformly for each sample. A TLS record may span multiple TCP segments, so `tls.length` can exceed the payload length of the current packet. The deliberately separated ranges make this a controlled feasibility dataset rather than a proxy for the full distribution of operational encrypted traffic.

The accompanying artifact supports reproduction. It contains the C source, P4-like programs, seeded generator, model-training code, and test scripts; the model is retrained deterministically at demo startup rather than distributed as a fixed weight file. `scripts/bench_all.sh` runs the functional and performance tests; timing results remain sensitive to CPU frequency state and host contention, hence the median-of-runs protocol above.

### 5.3 Classification Accuracy

The classification results answer five questions: how the MLP performs on the retained split, where its errors occur, whether a rule cascade reaches the same result, how results vary across seeds, and how performance changes on a calibrated synthetic workload. Section 5.3.6 then checks fixed-point agreement.

#### 5.3.1 Protocol A: Headline Split

The retained Protocol A split yields 99.1% overall accuracy. Table 7 reports the class counts and recalls so that the aggregate result can be interpreted rather than read in isolation.

**Table 7: Per-Class Classification Results, Protocol A (4,000 generated test packets).** "Class recall" is the number of correct predictions divided by the number of generated samples carrying that class label.

| Service Class | Correct / Generated | Class Recall |
|---|---|---|
| Web (c₀) | 981 / 982 | 99.9% |
| Video (c₁) | 951 / 986 | 96.5% |
| VoIP (c₂) | 1013 / 1013 | 100.0% |
| Bulk (c₃) | 1019 / 1019 | 100.0% |
| **Overall** | **3964 / 4000** | **99.1% accuracy** |

#### 5.3.2 Confusion Matrix and Error Direction

The errors concentrate in the web/video/bulk region, and no VoIP packet is misclassified in either direction. The demo records the full 4×4 confusion matrix (rows = true class, columns = predicted class):

| true \ predicted | web | video | voip | bulk | recall |
|---|---|---|---|---|---|
| web | 981 | 1 | 0 | 0 | 99.9% |
| video | 1 | 951 | 0 | 34 | 96.5% |
| voip | 0 | 0 | 1013 | 0 | 100.0% |
| bulk | 0 | 0 | 0 | 1019 | 100.0% |
| **precision** | 99.9% | 99.9% | 100.0% | 96.8% | |

All 36 errors lie in the Web/Video/Bulk region. The dominant direction is Video → Bulk (34 packets), which changes CS4 to AF11 and removes the configured coding profile. No VoIP packet is mapped to Bulk in this run, so the most harmful policy inversion is not observed. The finite sample does not bound its probability on operational traffic; that question requires captured traces.

#### 5.3.3 Rule Baseline on the Identical Workload

A threshold cascade provides a non-neural reference on the same packets. It is derived directly from Table 6, uses no training or tuning, and is implemented in `rule_classify()`:

```
R1: voip   if len ≤ 200  ∧ win ≤ 16384 ∧ tls ≤ 160
R2: bulk   if len ≥ 1400 ∧ win ≥ 60000 ∧ tls ≥ 14000
R3: video  if ≥ 2 of { len ≥ 1200, win ≥ 40000, tls ≥ 1200 }
R4: web    otherwise
```

The cascade reaches 3,907/4,000 (97.68%), with recalls of 91.0% Web, 99.5% Video, and 100% for VoIP and Bulk. Its decision cost is 1.7 ns per feature vector. The MLP gains 1.42 percentage points overall, mainly at the Web/Video boundary, but costs more than two orders of magnitude more per packet (Section 5.4). The result is therefore an accuracy-versus-cost trade-off, not evidence that the MLP is universally preferable.

#### 5.3.4 Protocol B: Five Seeds with Independent Test Streams

The Protocol B runs test sensitivity to model initialization and independent test streams. Table 8 reports the five seed-level outcomes.

**Table 8: Multi-Seed Classification Results, Protocol B (4,000 packets per seed, independent test stream).**

| Seed | Overall | Web recall | Video recall | VoIP recall | Bulk recall |
|---|---|---|---|---|---|
| 42 | 98.62% | 99.5% | 95.0% | 100.0% | 100.0% |
| 43 | 97.92% | 96.7% | 95.0% | 100.0% | 100.0% |
| 44 | 98.80% | 99.7% | 95.6% | 100.0% | 100.0% |
| 45 | 98.28% | 99.7% | 93.4% | 100.0% | 100.0% |
| 46 | 99.03% | 99.6% | 96.7% | 100.0% | 99.9% |
| **Mean** | **98.53% ± 0.39%** | 99.0% | 95.1% | 100.0% | 100.0% |

The Protocol A value lies within one standard deviation of the Protocol B mean, and every seed exceeds 97.9%. These runs support seed stability for the stated generator. They do not test distribution shift, so the acceptance criterion in Table 14 is limited to the declared split and seed protocol.

#### 5.3.5 Calibrated-Distribution Workload

The calibrated workload tests whether the headline result survives a different synthetic distribution. `scripts/gen_realistic_traffic.py` samples 4,000 TLS application-data packets from per-class Gaussian parameterizations informed by published summaries [6], [7], writes randomized 5-tuples to a pcap, and processes the pcap through `pcap_test`. That path trains a sibling 4-24-4 model with 6,000 samples, 50 epochs, learning rate 0.01, and best-training-accuracy selection; it therefore broadens the evidence but remains a calibrated simulation rather than a packet capture and is not identical to Protocol A.

**Table 9: Calibrated-Distribution Results (4,000 packets, 1,000 per class).**

| Service Class | Correct / Generated | Class Recall |
|---|---|---|
| Web (c₀) | 963 / 1000 | 96.3% |
| Video (c₁) | 916 / 1000 | 91.6% |
| VoIP (c₂) | 1000 / 1000 | 100.0% |
| Bulk (c₃) | 1000 / 1000 | 100.0% |
| **Overall** | **3879 / 4000** | **97.0%** |

The calibrated run reaches 97.0% accuracy, with 560 ns mean service time and 1.79 Mpps. The 2.1-point drop from Protocol A is concentrated in Video (91.6% versus 96.5%), consistent with greater overlap between calibrated Video and near-MTU Bulk features. The workload is still synthetic; it narrows the distributional gap but does not validate operational traffic.

#### 5.3.6 Quantization Agreement

The fixed-point implementation introduces no measurable loss relative to floating point. The Q16.16 and Float32 models produce identical predictions on a separate 2,000-sample agreement test (2,000/2,000). This observation is specific to the trained model and the sampled inputs; it does not prove agreement for all possible feature vectors.

Section 5.3 supports three bounded conclusions. The generator makes VoIP and Bulk easier than Web and Video; the rule cascade shows a 1.42-point MLP gain at a much higher compute cost; and the confusion matrix identifies Video → Bulk as the dominant error in the retained run. None of these findings supports extrapolation to operational traffic.

### 5.4 Processing Cost and Throughput

The pipeline's median single-packet service time is 592 ns in the PGO build. Table 10 measures 126-byte TLS application-data packets that pass the parser gate and invoke inference; one `clock_gettime` call brackets each 200,000-packet loop, and the packet pool contains 1,024 distinct packets. Five runs separated by at least 20 s provide the reported medians and ranges.

**Table 10: Classification Pipeline Service Time (126 B packets, DNN enabled).** Both builds include `-O3 -flto -march=native -funroll-loops`; the PGO row adds the two-pass profile build of the Makefile. The in-memory rate is a packet-size multiplication and excludes Ethernet framing and interpacket overhead.

| Configuration | Build | Median service time (range, 5 runs) | Throughput | In-memory rate |
|---|---|---|---|---|
| Single packet | `-O3 -flto` | 600 ns/pkt (597–636) | 1.67 Mpps | 1.68 Gbps |
| Single packet | `-O3 -flto` + PGO | 592 ns/pkt (475–614) | 1.69 Mpps | 1.70 Gbps |
| Batch (64 pkt) | `-O3 -flto` | 611 ns/pkt (594–765) | 1.64 Mpps | 1.65 Gbps |
| Batch (64 pkt) | `-O3 -flto` + PGO | 571 ns/pkt (478–594) | 1.75 Mpps | 1.77 Gbps |

PGO changes service time by 1% for single packets and 7% for batches, within the observed platform variance. The Makefile's separate 10–20% note refers to the SRv6 profile-collection workload, not Table 10; the table reports only reproducible configurations.

### 5.5 Multi-Core Scaling Analysis

The multi-queue engine scales sub-linearly on the PGO SRv6 workload. Table 11 reports three-run medians and ranges for 1,000,000 packets per worker count; speedup is relative to the one-worker median. This benchmark exercises the scaling harness in `src/tests.c`, not the DNN timing path in Table 10.

**Table 11: Multi-Core Scaling Efficiency (1M packets per run, PGO build).** The 16-worker configuration oversubscribes the 8-vCPU host and is labeled accordingly.

| Workers | Throughput, median (range) | Speedup | Efficiency |
|---|---|---|---|
| 1 | 0.949 Mpps (0.940–0.950) | 1.00× | 100% |
| 2 | 1.683 Mpps (1.582–1.932) | 1.77× | 89% |
| 4 | 2.668 Mpps (2.627–3.884) | 2.81× | 70% |
| 8 | 3.407 Mpps (3.130–4.327) | 3.59× | 45% |
| 16† | 4.872 Mpps (2.543–4.883) | 5.13× | 32% |

† Oversubscribed: 16 workers on 8 physical cores.

The results show declining efficiency as workers increase. Sixteen workers still raise median throughput by 43% over eight workers, but the host is oversubscribed and the cause may include scheduler migration across performance and efficiency cores. Wide ranges at 4–16 workers also reflect turbo and thermal variation. The table supports a >2.5× speedup at four or more workers on this host; it does not support a claim of linear scaling or NIC-rate operation.

### 5.6 Load Balancing Quality

The RSS hash achieves near-ideal load balance. Table 12 presents the load distribution metrics across 8 queues processing 200,000 packets.

**Table 12: Load Distribution Across 8 Worker Queues (200,000 packets).** The ideal uniform distribution assigns exactly 25,000 packets (12.5%) to each queue. Per-queue loads: q0=22,660; q1=27,152; q2=25,392; q3=23,235; q4=27,540; q5=25,783; q6=23,439; q7=24,799.

| Metric | Value |
|---|---|
| Minimum queue load | 22,660 packets (11.33%) |
| Maximum queue load | 27,540 packets (13.77%) |
| Ideal uniform distribution | 25,000 packets (12.5%) |
| Maximum deviation from ideal | +1.27 percentage points (−1.17 at the minimum) |
| Jain's Fairness Index | 0.9954 (1.0 is perfect) |

The queue counts support two limited conclusions. The FNV-1a plus fmix32 hash distributes this workload evenly, and the fixed assignment preserves the deterministic queue choice required by Theorem 1. Byte-level output comparison also found no difference between parallel and single-threaded processing in the tested cases. These observations do not establish balance for other traffic distributions.

### 5.7 Scope of Comparison

The comparisons are intentionally same-workload comparisons. Table 1 is architectural, while Section 5 compares the MLP with its floating-point counterpart, a threshold cascade, and a calibrated synthetic workload. Cross-paper rankings and a production-tool baseline are excluded because the traffic, labels, hardware, and measurement boundaries differ.

The design supports two architectural observations. It can act on an individual supported TLS application-data packet, rather than waiting for a flow aggregate, and it passes the class directly to later match-action stages. An end-to-end advantage over production tools remains untested and requires a common real-traffic benchmark.

### 5.8 End-to-End Functional Verification

Functional tests exercise compilation, parsing, classification, marking, routing, and output rewriting. Table 13 lists the eight scenarios and their expected results.

**Table 13: End-to-End Functional Verification Results.** Each row represents one test scenario exercising a specific code path. All 8 scenarios pass in the evaluated build.

| Test Scenario | Input | Expected Output | Result |
|---|---|---|---|
| TLS Web packet | len=600, win=32000, tls_len=500 | class=0, local mark=0 (BE), port=2 | Pass |
| TLS Video packet | len=1350, win=50000, tls_len=8000 | class=1, local mark=32 (CS4), port=2 | Pass |
| TLS VoIP packet | len=100, win=8192, tls_len=60 | class=2, local mark=46 (EF), port=2 | Pass |
| TLS Bulk packet | len=1500, win=65535, tls_len=16000 | class=3, local mark=10 (AF11), port=2 | Pass |
| Non-TLS packet | Destination port 80 | No classification, ToS unchanged | Pass |
| TLS handshake | tls.type=22 (not app_data) | Not classified, no QoS marking | Pass |
| Unroutable dest | Destination not in route table | Packet dropped (default action) | Pass |
| TTL and MAC rewrite | Any routable TLS packet | TTL decremented, MACs rewritten | Pass |

All eight scenarios pass, including bypass cases and an unroutable destination. The tests run in the build script and provide regression checks for the exercised paths; they do not cover unsupported protocol formats or live packet I/O.

### 5.9 Test Scenario Design

Table 14 organizes the test suite into seven categories (T1–T7). The sequence moves from component correctness to performance, then to the joint coding-policy path; the criteria are acceptance checks for this artifact, not deployment guarantees.

**Table 14: Test Scenario Design.** Each category addresses one functional or performance aspect of the framework.

| Category | Target | Method | Key Metrics | Acceptance Criteria |
|---|---|---|---|---|
| T1: Functional Correctness | All dataplanes (MPLS/VXLAN/SRv6) | Inject crafted packets, verify output bytes field-by-field | Byte-level match, port assignment, header manipulation | 100% field-level match across all scenarios |
| T2: Classification Accuracy | DNN inference pipeline | Protocol A (4,000 samples), Protocol B (5 seeds), rule baseline, calibrated workload | Overall accuracy, per-class recall, confusion matrix, quantization agreement | Protocol A ≥ 99%; Protocol B mean ≥ 95%, std ≤ 1 pp; rule baseline and calibrated workload reported; agreement ≥ 95% |
| T3: Processing Benchmark | Single and batch processing | 200K-packet runs, wall-clock instrumentation, median of ≥ 5 runs | ns/pkt, Mpps | < 1000 ns/pkt median (PGO build) |
| T4: Multi-Core Scaling | Horizontal throughput scaling | 1M-packet parallel runs with 1/2/4/8/16 workers | Speedup factor, efficiency percentage | > 2.5× speedup at 4 workers |
| T5: Load Balancing | RSS distribution quality | 200K packets across 8 queues, per-queue packet counts | Max deviation, Jain's Fairness Index | JFI > 0.99, max deviation < 2 pp |
| T6: Network Coding | RLNC/XOR correctness | Encode + decode, loss recovery, per-class parameter verification | Recovery success rate, overhead ratio | 100% single-loss recovery, correct per-class overhead |
| T7: Joint Decision Path | Classify + route + coding-policy harness | 10K generated packets with generation accounting | Service time, projected repair ratio, inferred-class distribution | < 1200 ns/pkt median, Bulk profile = 0% repair |

The suite uses staged evidence: T1–T3 isolate component behavior before T7 tests the connected decision path. Protocol A uses 4,000 packets, Protocol B uses five independent 4,000-packet streams, load balance uses 200,000 packets, and coding stress uses 40,000 packets. Accuracy confidence intervals and significance tests against the rule baseline are not computed. `scripts/bench_all.sh` automates the available builds and checks, but the repository has no hosted CI service.

### 5.10 Network Coding Evaluation

Section 5.10 evaluates coding in the order required for interpretation: field arithmetic, recovery on a controlled loss, encoder cost, and the connected classification-to-policy path. The first three tests concern the standalone module; only the last reuses pipeline metadata.

#### 5.10.1 GF(2^8) Arithmetic Verification

The standalone RLNC module uses GF(2^8), the AES polynomial x⁸ + x⁴ + x³ + x + 1 (0x11B), and primitive element α = 3. Table 15 records exhaustive identity/inverse checks and randomized distributivity checks.

**Table 15: GF(2^8) Arithmetic Verification Results.**

| Property | Test Coverage | Result |
|---|---|---|
| Multiplicative identity: a × 1 = a | All 256 elements | Pass |
| Multiplicative inverse: a × a⁻¹ = 1 | All 255 non-zero elements | Pass |
| Distributive law: a(b+c) = ab + ac | 1000 random triples | Pass |

#### 5.10.2 Encoding/Decoding Correctness

Table 16 tests the narrow recovery claim: every active profile must recover one missing source packet within its tested generation.

**Table 16: RLNC/XOR Encode-Decode Verification.**

| Service | Mode | Parameters | Loss Positions Tested | Recovery Success |
|---|---|---|---|---|
| VoIP (c₂) | Systematic RLNC | k=4, n=5, GF(2^8) | All 4 positions | 4/4 (100%) |
| Video (c₁) | Systematic RLNC | k=8, n=9, GF(2^8) | All 8 positions | 8/8 (100%) |
| Web (c₀) | XOR parity | k=8, n=9, GF(2) | All 8 positions | 8/8 (100%) |
| Bulk (c₃) | None (bypass) | — | N/A | N/A (no coding) |

The results in Table 16 confirm that the standalone RLNC/XOR implementation constructs a repair packet and recovers any one missing source packet within the tested generation. Receiver integration and forwarding compatibility are not exercised.

#### 5.10.3 Coding Computational Cost

Table 17 isolates encoder cost on 64-byte payloads over 10,000 generation cycles under the PGO build. Four runs provide the reported medians and ranges.

**Table 17: Network Coding Computational Cost (64-byte payloads, 10,000 generations).** "Generation time" is the wall-clock time required to encode one complete generation (k data packets → n−k repair packets). "Amortized cost" divides the generation time by k. The ranges reflect the thermal variance of the platform.

| Service | Coding Mode | Generation time, median (range) | Amortized Compute Cost |
|---|---|---|---|
| VoIP (c₂) | RLNC k=4 | 221 ns (178–860) | 55 ns/source packet |
| Video (c₁) | RLNC k=8 | 603 ns (337–1065) | 75 ns/source packet |
| Web (c₀) | XOR k=8 | 4 ns (3–14) | 0.5 ns/source packet |
| Bulk (c₃) | None | 0 | 0 |

The values cover only coefficient generation and payload combination. They exclude buffer copies, repair-header construction, queueing, packet I/O, and receiver work, so they are not end-to-end deployment costs. XOR is roughly two orders of magnitude faster than RLNC on this workload.

#### 5.10.4 Joint Classification and Coding-Decision Path

The joint harness tests whether each inferred class selects its configured profile while route selection remains active. Table 18 reports 10,000 packets whose source labels cycle evenly; inferred counts differ when classification errors occur.

**Table 18: Joint Classification, Coding-Decision, and Routing Results.** "Repair decisions" are counters derived from completed generations; this benchmark does not construct or transmit the corresponding repair packets.

| Metric | Value |
|---|---|
| Total packets processed | 10,000 |
| Joint decision-path latency | 639 ns/pkt median (588–843; 4 runs), ≈ 1.57 Mpps |
| Classification distribution | Web: 2,499 / Video: 2,369 / VoIP: 2,505 / Bulk: 2,627 |
| Repair decisions (Web, XOR) | 312 (12.5% of classified Web packets) |
| Repair decisions (Video, RLNC) | 296 (12.5% of classified Video packets) |
| Repair decisions (VoIP, RLNC) | 626 (25.0% of classified VoIP packets) |
| Repair decisions (Bulk) | 0 (0% overhead) |
| **Projected repair overhead** | **1,234 / 10,000 = 12.3%** |
| Comparison: uniform RLNC (all classes) | 25% overhead |
| Comparison: no coding | 0% overhead (no loss protection) |

Table 18 supports three bounded observations:

**(a) Profile selection is differentiated.** Each inferred class receives its configured profile, and the repair-decision counts equal ⌊classified_count/k⌋. The resulting 12.3% ratio is the expected weighted value for this inferred mix; comparison with a uniform 25% profile measures policy overhead, not delivered recovery.

**(b) The connected decision path is measurable.** Classification, routing, profile lookup, and generation accounting take 639 ns median per packet on this host. Actual encoding and packet emission are excluded.

**(c) Errors propagate to policy.** The harness applies profiles to inferred rather than true classes. The dominant Video → Bulk error therefore removes the configured 12.5% Video profile for those packets; the experiment cannot determine whether that loss is acceptable in operational traffic.

### 5.11 Multi-Generation Stress Test

The stress test asks whether generation counters remain consistent over many complete blocks. The harness processes 40,000 packets (10,000 per source label) and compares observed counts with ⌊data_packets/k⌋.

**Table 19: Multi-Generation Coding Stress Test (40,000 packets).**

| Service Class | Packets | Expected Generations | Actual Generations | Repair Packets | Processing Time |
|---|---|---|---|---|---|
| Web (c₀, XOR k=8) | 10,000 | 1,250 | 1,250 | 1,250 | — |
| Video (c₁, RLNC k=8) | 10,000 | 1,250 | 1,250 | 1,250 | — |
| VoIP (c₂, RLNC k=4) | 10,000 | 2,500 | 2,500 | 2,500 | — |
| Bulk (c₃, none) | 10,000 | 0 | 0 | 0 | — |
| **Total** | **40,000** | **5,000** | **5,000** | **5,000** | **31.6 ms median (22.9–34.2)** |

All observed counts match ⌊data_packets/k⌋ in this run. The 31.6 ms median includes the harness's RLNC work but excludes packet I/O and receiver processing; it is a consistency result, not a link-capacity estimate.

### 5.12 Evaluation Summary

Table 20 consolidates the measurements and labels the evidence type for each claim.

**Table 20: Consolidated Evaluation Results.** Accuracy and overhead results use generated traffic; coding recovery is verified separately from the joint decision-path benchmark.

| Dimension | Design Target | Measured Result | Status |
|---|---|---|---|
| Classification accuracy, Protocol A (overall) | ≥ 95% | 99.1%, one generated split | Met |
| Classification accuracy, Protocol B (5 seeds) | mean ≥ 95%, std ≤ 1 pp | 98.53% ± 0.39% | Met |
| Rule baseline on identical workload | reported | 97.68% (threshold cascade) | Reported |
| Calibrated-distribution workload | reported | 97.0% (Gaussian-calibrated, 4,000 packets) | Reported |
| Per-class recall, Protocol A (minimum) | > 90% | 96.5% (Video) | Met |
| Confusion matrix | recorded | recorded; dominant error video→bulk; no VoIP errors | Met |
| Float/fixed agreement | > 99% | 100%, 2,000 generated samples | Met |
| Single-thread service time | < 1,000 ns median | 592 ns (PGO), 600 ns (−O3) | Met |
| Scaling (≥ 4 workers) | > 2.5× | 2.81× at 4, 3.59× at 8, 5.13× at 16† workers | Met |
| Queue-load Jain index | > 0.99 | 0.9954 | Met |
| RLNC single-loss recovery | 100% | 100% of tested positions | Met |
| XOR single-loss recovery | 100% | 100% of tested positions | Met |
| VoIP-profile repair ratio | 25% | 25.0% | Exact |
| Video-profile repair ratio | 12.5% | 12.5% | Exact |
| Projected mixed repair ratio | report policy overhead | 12.3% (expected weighted average) | Reported |
| Joint decision-path service time | < 1,200 ns median | 639 ns | Met |
| Generation accounting | 100% | 40,000 source packets | Met |
| Functional regression suite | 0 failures | 0 failures | Met |

† Oversubscribed (16 workers on 8 physical cores).

Taken together, the tables establish internal correctness and software-pipeline feasibility under the stated workloads. The 12.3% mixed ratio is an accounting consequence of the configured profiles and inferred class mix; it says nothing about application quality or live-path recovery.

---

## 6. Limitations and Deployment Considerations

The discussion separates validity limits from deployment requirements. Section 6.1 bounds the evidence, Section 6.2 states what a real UPF would still need, Section 6.3 addresses governance, and Section 6.4 prioritizes follow-up experiments.

### 6.1 Limitations

The evidence supports mechanism feasibility, not general traffic or deployment claims. The principal limits are:

**Feature space and application context.** The four features encode coarse Web/Video/VoIP/Bulk labels, not sub-application identity. HTTP/2 can multiplex different streams over one TCP connection, and the packet-level classifier cannot recover that context. Finer labels would require flow state such as inter-arrival time and packet direction.

**Synthetic evidence and uncertainty.** Protocol A/B and the calibrated workload are generated distributions; they omit protocol diversity, middleboxes, temporal correlation, and real multiplexing. The five seeds measure training sensitivity but not distribution shift. Accuracy confidence intervals and significance tests against the rule baseline are not reported, so captured traffic and repeated real-data splits are required.

**Protocol coverage.** The parser supports client-to-server TCP/TLS on destination port 443. It does not classify QUIC or other UDP traffic, and QUIC support would require a different parser and feature model.

**Adversarial robustness.** A sender can manipulate packet-visible features to seek preferential treatment. The prototype provides no confidence threshold, out-of-distribution detector, or adversarial evaluation. Production policy must combine classification with admission control and per-subscriber rate limits rather than treating the inferred class as trusted input.

**Observation boundary.** TLS handshakes bypass classification. The system begins acting only on application-data records that satisfy the parser's segment-boundary assumption, and destination-port gating restricts the current implementation to client-to-server traffic.

**Runtime update concurrency.** `rmt_load_entries()` and `rmt_dnn_load()` modify trie nodes and weight arrays without synchronization; they are not safe to call while workers process packets. Runtime reconfiguration in the current prototype therefore means stop-the-world reload (quiesce workers, swap state, resume). Atomic update mechanisms (RCU for the trie, shadow-table swap, or double-buffered weights) are not implemented (Section 6.2).

**Energy and power characterization.** The model is small (220 parameters, 880 bytes), but the evaluation does not measure energy per packet or execute on a satellite-grade processor. No energy-efficiency or radiation-tolerance conclusion can be drawn from the use of integer arithmetic alone.

**Measurement platform.** Timing runs use a mobile 8-vCPU host with variable turbo and thermal state. The reported ranges expose that variance; the artifact reproduces the protocol, not identical absolute numbers on another processor.

**No deployment validation.** The framework processes packets in memory and has no SMF/N4, PFCP, GTP-U, real-NIC, or satellite-protocol integration. End-to-end 5G and representative-link experiments are required before deployment claims.

### 6.2 Practical Deployment Considerations

Deployment requires the following controls in addition to the prototype's packet path.

**Model lifecycle management.** The prototype can load quantized parameters, but it does not implement drift detection, automatic labeling, retraining, or atomic model rollout. A deployment requires versioned model updates, rollback, confidence monitoring, and an independently evaluated labeling pipeline.

**Graceful degradation under failure.** Unsupported TLS types take the `nop()` path of the table, but the prototype does not detect corrupted weights or inference failures. A production design requires model integrity checks and an explicit fail-open path that preserves destination forwarding without applying inferred QoS.

**Multi-tenancy and slice isolation.** Separate tables and model instances could represent slice-specific policy, but the prototype does not implement slice admission, memory protection, or performance isolation. These mechanisms are prerequisites for a shared production UPF.

**Integration with a 5G UPF.** Three boundaries must be resolved. First, N3/N9 traffic needs GTP-U decapsulation before this parser, or the classifier must remain at N6 [45]. Second, the inferred class must map to an SMF-authorized QoS flow through PFCP/N4 rules [46], [47]; DSCP and QFI are not interchangeable, and inference cannot override subscriber policy. Third, model and table updates must use an atomic reload mechanism. None of these controls is implemented in the artifact.

**TCP and link-layer interaction.** Deployments that enable UPF-level coding must bound redundancy so that loss hiding does not distort congestion control on shared satellite capacity (Section 4.7.1), and must characterize the residual loss left by DVB-S2X ACM [48] before choosing generation sizes and redundancy ratios.

### 6.3 Ethical and Regulatory Considerations

Service-aware routing is a policy intervention, not a neutral measurement. The four classes are local labels rather than standardized QoS entitlements. Operators must document treatment mappings, audit disparate effects and misclassification, provide user and regulator transparency, and recognize that header-only inspection still has privacy implications.

### 6.4 Future Work

The limitations imply six concrete follow-up directions:

1. **Multi-layer DNN with flow state**: extend the in-pipeline inference engine to deeper architectures (2–3 hidden layers) with per-flow stateful features (IAT, packet count) for finer-grained sub-application classification (8–16 classes);
2. **GTP-U-aware parsing and satellite-parameterized evaluation**: add a GTP-U decapsulation stage (outer IPv4/UDP parse, TEID extraction, inner re-parse) so that the pipeline can operate at N3/N9, and evaluate the pipeline and coding policy under satellite delay/loss traces (e.g., netem-driven GEO/LEO profiles or TR 38.821 channel models);
3. **Closed-loop satellite-aware path and coding selection**: integrate real-time satellite link quality feedback (RTT via LEO/GEO probing, per-beam loss counters, ISL availability) to adjust class-to-path mappings and per-class coding redundancy based on current channel conditions;
4. **Inter-flow network coding at the UPF**: extend Application B from intra-flow systematic coding to inter-flow opportunistic coding (COPE-style XOR across flows sharing the same satellite next-hop), including in-pipeline repair-packet construction and emission;
5. **QUIC and UDP traffic support**: adapt the parser and feature extraction for QUIC Initial packets (UDP-based), which are increasingly common in satellite-accessed web traffic;
6. **Integrated coding and acceleration**: complete the receiver path for the coding module; then evaluate DNN and coding acceleration on SmartNIC or FPGA targets with explicit throughput and energy measurements, including representative embedded platforms for the constrained-environment claim.

---

## 7. Conclusion

EdgeNet-CoM demonstrates one bounded mechanism: a packet-visible class can be produced by a Q16.16 MLP and consumed immediately by QoS and destination-routing stages in a software RMT pipeline. The artifact also shows how that class can be passed to a separate coding-policy harness. Across the declared workloads, the MLP reaches 99.1% on the retained split, 98.53% ± 0.39% across five seeds, and 97.0% on the calibrated synthetic workload; the threshold cascade reaches 97.68% on the retained split. The pipeline records 592 ns median service time, while the harness reports the expected 12.3% mixed repair overhead.

The evidence supports internal correctness and software-pipeline feasibility, not operational classification or satellite deployment readiness. The next decisive tests are captured traffic, GTP-U-aware N3/N9 processing, satellite delay/loss traces, real packet I/O, and 5G policy integration. Until those tests are performed, the satellite UPF remains the motivating deployment scenario rather than a validated target.

---

## References

[1] W. Shi, J. Cao, Q. Zhang, Y. Li, and L. Xu, "Edge computing: Vision and challenges," *IEEE Internet of Things Journal*, vol. 3, no. 5, pp. 637–646, 2016.

[2] Y. Mao, C. You, J. Zhang, K. Huang, and K. B. Letaief, "A survey on mobile edge computing: The communication perspective," *IEEE Communications Surveys & Tutorials*, vol. 19, no. 4, pp. 2322–2358, 2017.

[3] Google Transparency Report, "HTTPS encryption on the web," 2024. [Online]. Available: https://transparencyreport.google.com/https/overview

[4] P. Bosshart, G. Gibb, H.-S. Kim, G. Varghese, N. McKeown, M. Izzard, F. Mujica, and M. Horowitz, "Forwarding metamorphosis: Fast programmable match-action processing in hardware for SDN," in *Proc. ACM SIGCOMM*, 2013, pp. 99–110.

[5] A. Dainotti, A. Pescape, and K. C. Claffy, "Issues and future directions in traffic classification," *IEEE Network*, vol. 26, no. 1, pp. 35–40, 2012.

[6] G. Aceto, D. Ciuonzo, A. Montieri, and A. Pescapé, "Mobile encrypted traffic classification using deep learning: Experimental evaluation, lessons learned, and challenges," *IEEE Trans. Network and Service Management*, vol. 16, no. 2, pp. 445–458, 2019.

[7] T. Shapira and Y. Shavitt, "FlowPic: A generic representation for encrypted traffic classification and applications identification," *IEEE Trans. Network and Service Management*, vol. 18, no. 2, pp. 1218–1232, 2021.

[8] M. Lotfollahi, M. J. Siavoshani, R. S. H. Zade, and M. Saberian, "Deep packet: A novel approach for encrypted traffic classification using deep learning," *Soft Computing*, vol. 24, no. 3, pp. 1999–2012, 2020.

[9] W. Wang, M. Zhu, J. Wang, X. Zeng, and Z. Yang, "End-to-end encrypted traffic classification with one-dimensional convolution neural networks," in *Proc. IEEE ISI*, 2017, pp. 43–48.

[10] P. Bosshart, D. Daly, G. Gibb, M. Izzard, N. McKeown, J. Rexford, C. Schlesinger, D. Talayco, A. Vahdat, G. Varghese, and D. Walker, "P4: Programming protocol-independent packet processors," *ACM SIGCOMM Computer Communication Review*, vol. 44, no. 3, pp. 87–95, 2014.

[11] Z. Xiong and N. Zilberman, "Do switches dream of machine learning? Toward in-network classification," in *Proc. ACM HotNets*, 2019, pp. 25–33.

[12] G. Siracusano, R. Bifulco, S. Galea, N. Zilberman, and G. Antichi, "Re-architecting traffic analysis with neural network interface cards," in *Proc. ACM SIGCOMM Workshop NetAI*, 2020.

[13] Z. Xiong and N. Zilberman, "IIsy: Practical in-network classification," *IEEE/ACM Trans. on Networking*, 2024.

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

[39] A. Panda, C. Ng, D. Rozner, J. Ling, P. Telford, A. Iyer, and S. Ratnasamy, "NetBricks: Taking the V out of NFV," in *Proc. USENIX OSDI*, 2016, pp. 203–219.

[40] J. Martins, M. Ahmed, C. Raiciu, V. Olteanu, M. Honda, R. Bifulco, and F. Huici, "ClickOS and the art of network function virtualization," in *Proc. USENIX NSDI*, 2014, pp. 459–473.

[41] D. Borkmann, "The eBPF and XDP revolution," in *Proc. Linux Plumbers Conference*, 2017.

[42] P. G. Kannan, L. E. Ramos, K. Lee, R. Mahajan, A. Akella, and S. Banerjee, "Sonata: Query-driven streaming network telemetry," in *Proc. ACM SIGCOMM*, 2018, pp. 571–585.

[43] Y. Li, R. Miao, C. Kim, and M. Yu, "FlowRadar: Better understanding of network state through data plane telemetry," in *Proc. USENIX NSDI*, 2016, pp. 311–324.

[44] Y. Zhu, N. Kang, J. Rexford, and D. Walker, "Marple: A declarative approach to network monitoring," in *Proc. ACM SIGCOMM*, 2017, pp. 479–492.

[45] 3GPP TS 29.281, "General Packet Radio System (GPRS) Tunnelling Protocol for User Plane (GTP-U)," 2024.

[46] 3GPP TS 29.244, "Interface between the Control Plane and the User Plane Nodes (PFCP)," 2024.

[47] 3GPP TS 23.501, "System architecture for the 5G System (5GS)," 2024.

[48] ETSI EN 302 307-2, "Digital Video Broadcasting (DVB); Second generation framing structure, channel coding and modulation systems for Broadcasting, Interactive Services, News Gathering and other broadband satellite applications; Part 2: DVB-S2X," 2014.

[49] J. Border, M. Kojo, J. Griner, G. Montenegro, and Z. Shelby, "Performance Enhancing Proxies Intended to Mitigate Link-Related Degradations," RFC 3135, 2001.

[50] ITU-T Recommendation G.114, "One-way transmission time," 2003.
