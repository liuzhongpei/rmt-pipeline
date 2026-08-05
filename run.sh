#!/bin/bash
#
# run.sh - build & run helper for the software RMT pipeline
#
# Usage:
#   ./run.sh [target]
#
# Targets:
#   all        build then run all test suites (default)
#   build      compile only (bin/rmt_demo + bin/rmt_tests)
#   test       run the 3 example dataplanes (MPLS / VXLAN / SRv6)
#   test-v     same as test, with verbose pipeline trace
#              (parser states, table hits, per-ALU VLIW execution, hexdump)
#   mpls       run only the nested-MPLS demo (verbose)
#   vxlan      run only the VXLAN gateway demo (verbose)
#   srv6       run only the SRv6 demo (verbose)
#   bulk [N]   SRv6 bulk forwarding test with N packets (default 1000)
#   par [T]    SRv6 concurrent test: T threads share one pipeline (default 16)
#   mq [Q]     SRv6 multi-queue test: RSS dispatch to Q worker queues (default 4)
#   tcpmq [Q]  TCP per-flow dispatch test: 5-tuple hash to Q queues (default 8, max 128)
#   int        INT telemetry demo: 3-switch path, per-hop metadata + collector
#   int-v      same, with verbose pipeline trace of every hop
#   ndn        NDN demo: name-based forwarding + in-network cache (CS/PIT/FIB)
#   ndn-v      same, with verbose pipeline trace
#   ids        DNN anomaly detection demo: train MLP, quantize, detect in-net
#   ids-v      same, with verbose pipeline trace
#   telescope  dynamic network telescope demo (MORP4-style): infer unused
#              space in the dataplane, capture unsolicited traffic
#   telescope-v  same, with verbose pipeline trace
#   enc        encrypted traffic classification demo (MLP: web/video/voip/bulk)
#   enc-v      same, with verbose pipeline trace
#   nc         network coding demo (service-aware FEC: RLNC/XOR per class)
#   bench-all  run comprehensive test suite (all scenarios)
#   demo       run the standalone SRv6 demo binary
#   clean      remove build outputs
#
set -e
cd "$(dirname "$0")"

build() {
    make -s all
    echo "build ok: bin/rmt_demo bin/rmt_tests bin/int_demo bin/ndn_demo bin/ids_demo bin/telescope_demo bin/enc_classify_demo bin/nc_demo"
}

case "${1:-all}" in
    build)
        build
        ;;
    test)
        build
        ./bin/rmt_tests
        ;;
    test-v)
        build
        ./bin/rmt_tests -v
        ;;
    mpls)
        build
        ./bin/rmt_tests -v 2>&1 | sed -n '/p4\/mpls.p4/,/p4\/vxlan.p4/p' | head -n -1
        ;;
    vxlan)
        build
        ./bin/rmt_tests -v 2>&1 | sed -n '/p4\/vxlan.p4/,/p4\/srv6.p4/p' | head -n -1
        ;;
    srv6)
        build
        ./bin/rmt_tests -v 2>&1 | sed -n '/p4\/srv6.p4/,$p'
        ;;
    bulk)
        build
        ./bin/rmt_tests bulk "${2:-1000}"
        ;;
    par)
        build
        ./bin/rmt_tests par "${2:-16}"
        ;;
    mq)
        build
        ./bin/rmt_tests mq "${2:-4}"
        ;;
    tcpmq)
        build
        ./bin/rmt_tests tcpmq "${2:-8}"
        ;;
    int)
        build
        ./bin/int_demo
        ;;
    int-v)
        build
        ./bin/int_demo -v
        ;;
    ndn)
        build
        ./bin/ndn_demo
        ;;
    ndn-v)
        build
        ./bin/ndn_demo -v
        ;;
    ids)
        build
        ./bin/ids_demo
        ;;
    ids-v)
        build
        ./bin/ids_demo -v
        ;;
    telescope)
        build
        ./bin/telescope_demo
        ;;
    telescope-v)
        build
        ./bin/telescope_demo -v
        ;;
    enc)
        build
        ./bin/enc_classify_demo
        ;;
    enc-v)
        build
        ./bin/enc_classify_demo -v
        ;;
    nc)
        build
        ./bin/nc_demo
        ;;
    bench-all)
        build
        bash scripts/bench_all.sh --full
        ;;
    demo)
        build
        ./bin/rmt_demo p4/srv6.p4 entries/srv6_entries.txt
        ;;
    clean)
        make -s clean
        echo "cleaned"
        ;;
    all)
        build
        ./bin/rmt_tests
        ;;
    *)
        echo "usage: $0 [all|build|test|test-v|mpls|vxlan|srv6|bulk [N]|par [T]|mq [Q]|tcpmq [Q]|int|int-v|ndn|ndn-v|ids|ids-v|telescope|telescope-v|enc|enc-v|nc|bench-all|demo|clean]" >&2
        exit 1
        ;;
esac
