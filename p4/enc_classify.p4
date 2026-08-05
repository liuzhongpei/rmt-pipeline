/*
 * enc_classify.p4 - MLP-based encrypted traffic classification
 *                   on the RMT pipeline
 *
 * Classifies encrypted (TLS/QUIC) traffic into application categories
 * WITHOUT decryption, using only observable packet-level features:
 *
 *     ipv4.len      — packet size (video=large, VoIP=small, web=variable)
 *     tcp.window    — receiver window (streaming=large, interactive=small)
 *     tls.length    — TLS record length (correlates with app payload size)
 *     tls.type      — TLS content type (23=app_data, 22=handshake, etc.)
 *
 * 4 output classes:
 *     class 0 = Web browsing (HTTPS)
 *     class 1 = Video streaming (Netflix/YouTube encrypted)
 *     class 2 = VoIP / real-time communication
 *     class 3 = Bulk file transfer (cloud sync, backup)
 *
 * The model (4-input, 24-hidden, 4-output MLP) is trained in float
 * by the control plane (enc_classify_demo.c), quantized to Q16.16,
 * and installed with rmt_dnn_load(). Classification is entirely
 * based on statistical traffic patterns, not payload inspection.
 *
 * 4 stages:
 *   stage 0  classify_t    TLS app_data -> dnn_infer -> meta.class
 *   stage 1  qos_t         class -> set DSCP / priority marking
 *   stage 2  route         IPv4 LPM -> egress port
 *   stage 3  mac_rewrite   egress port -> next-hop MACs
 */

/* ------------------------- headers ------------------------- */
header eth {
    dmac      : 48;
    smac      : 48;
    ethertype : 16;
}

header ipv4 {
    version : 4;
    ihl     : 4;
    tos     : 8;
    len     : 16;
    id      : 16;
    flags   : 3;
    frag    : 13;
    ttl     : 8;
    proto   : 8;
    csum    : 16;
    src     : 32;
    dst     : 32;
}

header tcp {
    sport  : 16;
    dport  : 16;
    seq    : 32;
    ack    : 32;
    off    : 8;
    flags  : 8;
    window : 16;
    csum   : 16;
    urg    : 16;
}

/* TLS record header (5 bytes): type(1) + version(2) + length(2) */
header tls {
    type    : 8;
    version : 16;
    length  : 16;
}

metadata meta {
    ingress_port : 16;
    egress_port  : 16;
    drop         : 8;
    class        : 8;   /* DNN output: 0=web, 1=video, 2=voip, 3=bulk */
    qos          : 8;   /* DSCP marking after classification */
}

/* --------------------- inference extern -------------------- */
/* 4-input MLP bound to observable features */
dnn enc_net(ipv4.len, tcp.window, tls.length, tls.type);

/* ------------------------- parser -------------------------- */
parser start {
    extract(eth);
    select(eth.ethertype) {
        0x0800  : parse_ipv4;
        default : accept;
    }
}

parser parse_ipv4 {
    extract(ipv4);
    select(ipv4.proto) {
        6       : parse_tcp;
        default : accept;
    }
}

parser parse_tcp {
    extract(tcp);
    select(tcp.dport) {
        443     : parse_tls;
        default : accept;
    }
}

parser parse_tls {
    extract(tls);
    goto accept;
}

/* ------------------------- actions ------------------------- */
action classify() {
    dnn_infer(enc_net, meta.class);
}

action mark_web() {
    set(meta.qos, 0);
    set(ipv4.tos, 0);
}

action mark_video() {
    set(meta.qos, 1);
    set(ipv4.tos, 32);
}

action mark_voip() {
    set(meta.qos, 2);
    set(ipv4.tos, 46);
}

action mark_bulk() {
    set(meta.qos, 3);
    set(ipv4.tos, 10);
}

action forward(port) {
    set(meta.egress_port, port);
    sub(ipv4.ttl, 1);
}

action drop_pkt() {
    set(meta.drop, 1);
}

action set_macs(dmac, smac) {
    set(eth.dmac, dmac);
    set(eth.smac, smac);
}

action nop() {
}

/* ------------------------- tables -------------------------- */
/* stage 0: run MLP on TLS application data packets */
table classify_t {
    key     { tls.type : exact; }
    actions { classify; nop; }
    default_action nop();
}

/* stage 1: QoS marking based on class */
table qos_t {
    key     { meta.class : exact; }
    actions { mark_web; mark_video; mark_voip; mark_bulk; nop; }
    default_action nop();
}

/* stage 2: IPv4 LPM routing */
table route {
    key     { ipv4.dst : lpm; }
    actions { forward; drop_pkt; }
    default_action drop_pkt();
}

/* stage 3: MAC rewrite per egress port */
table mac_rewrite {
    key     { meta.egress_port : exact; }
    actions { set_macs; nop; }
    default_action nop();
}

/* --------------------- control flow ------------------------ */
control ingress {
    if (valid(tls)) {
        apply(classify_t);     /* stage 0: DNN inference */
    }
    if (valid(tls)) {
        apply(qos_t);          /* stage 1: DSCP marking */
    }
    if (valid(ipv4)) {
        apply(route);          /* stage 2: L3 forwarding */
    }
    apply(mac_rewrite);        /* stage 3: MAC rewrite */
}
