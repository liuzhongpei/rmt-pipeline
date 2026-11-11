CC      := gcc
CFLAGS  := -O3 -flto -march=native -funroll-loops -Wall -Wextra -std=c11 -pthread -Iinclude
CFLAGS_DBG := -O3 -flto -g -Wall -Wextra -std=c11 -pthread -Iinclude
DEMO    := bin/rmt_demo
TESTS   := bin/rmt_tests
INTDEMO := bin/int_demo
NDNDEMO := bin/ndn_demo
IDSDEMO := bin/ids_demo
TELDEMO := bin/telescope_demo
TESTMATCH := bin/test_match
TESTALU   := bin/test_alu
TESTPARSER := bin/test_parser
TESTSKETCH := bin/test_sketch
ENCDEMO  := bin/enc_classify_demo
NCDEMO   := bin/nc_demo
SKETCHDEMO := bin/sketch_demo_driver
CORE    := src/phv.c src/parser.c src/match.c src/alu.c \
           src/pipeline.c src/p4c.c src/runtime.c src/cache.c src/dnn.c \
           src/mq.c src/sketch.c
COBJS   := $(CORE:src/%.c=build/%.o)

all: $(DEMO) $(TESTS) $(INTDEMO) $(NDNDEMO) $(IDSDEMO) $(TELDEMO) $(TESTMATCH) $(TESTALU) $(TESTPARSER) $(TESTSKETCH) $(ENCDEMO) $(NCDEMO) $(SKETCHDEMO)

# debug build (with -g, no -march=native)
debug: CFLAGS := $(CFLAGS_DBG)
debug: clean all

$(DEMO): $(COBJS) build/main.o | bin
	$(CC) $(CFLAGS) -o $@ $(COBJS) build/main.o

$(TESTS): $(COBJS) build/tests.o | bin
	$(CC) $(CFLAGS) -o $@ $(COBJS) build/tests.o

$(INTDEMO): $(COBJS) build/int_demo.o | bin
	$(CC) $(CFLAGS) -o $@ $(COBJS) build/int_demo.o

$(NDNDEMO): $(COBJS) build/ndn_demo.o | bin
	$(CC) $(CFLAGS) -o $@ $(COBJS) build/ndn_demo.o

$(IDSDEMO): $(COBJS) build/ids_demo.o | bin
	$(CC) $(CFLAGS) -o $@ $(COBJS) build/ids_demo.o -lm

$(TELDEMO): $(COBJS) build/telescope_demo.o | bin
	$(CC) $(CFLAGS) -o $@ $(COBJS) build/telescope_demo.o

$(TESTMATCH): $(COBJS) build/test_match.o | bin
	$(CC) $(CFLAGS) -o $@ $(COBJS) build/test_match.o -lm

$(TESTALU): $(COBJS) build/test_alu.o | bin
	$(CC) $(CFLAGS) -o $@ $(COBJS) build/test_alu.o

$(TESTPARSER): $(COBJS) build/test_parser.o | bin
	$(CC) $(CFLAGS) -o $@ $(COBJS) build/test_parser.o

$(TESTSKETCH): $(COBJS) build/test_sketch.o | bin
	$(CC) $(CFLAGS) -o $@ $(COBJS) build/test_sketch.o

$(ENCDEMO): $(COBJS) build/enc_classify_demo.o | bin
	$(CC) $(CFLAGS) -o $@ $(COBJS) build/enc_classify_demo.o -lm

$(NCDEMO): $(COBJS) build/nc_demo.o | bin
	$(CC) $(CFLAGS) -o $@ $(COBJS) build/nc_demo.o -lm

$(SKETCHDEMO): $(COBJS) build/sketch_demo_driver.o | bin
	$(CC) $(CFLAGS) -o $@ $(COBJS) build/sketch_demo_driver.o

build/%.o: src/%.c include/rmt.h | build
	$(CC) $(CFLAGS) -c -o $@ $<

bin build:
	mkdir -p $@

run: $(DEMO)
	./$(DEMO) p4/srv6.p4 entries/srv6_entries.txt

test: $(TESTS)
	./$(TESTS)

int: $(INTDEMO)
	./$(INTDEMO)

ndn: $(NDNDEMO)
	./$(NDNDEMO)

ids: $(IDSDEMO)
	./$(IDSDEMO)

telescope: $(TELDEMO)
	./$(TELDEMO)

clean:
	rm -rf build bin

test-match: $(TESTMATCH)
	./$(TESTMATCH)

test-alu: $(TESTALU)
	./$(TESTALU)

test-parser: $(TESTPARSER)
	./$(TESTPARSER)

test-sketch: $(TESTSKETCH)
	./$(TESTSKETCH)

enc-classify: $(ENCDEMO)
	./$(ENCDEMO)

nc: $(NCDEMO)
	./$(NCDEMO)

bench: $(TESTS)
	./$(TESTS) bulk 50000
	./$(TESTS) par 8
	./$(TESTS) mq 4
	./$(TESTS) tcpmq 8

# ---- Profile-Guided Optimization (PGO): 2-pass build ----
# GCC names LTO profile data after the *output binary*, so pass 1 must
# produce the same binaries that pass 2 will rebuild (the old target
# trained bin/rmt_tests_pgo and therefore never found the .gcda files).
# All binaries are trained so every source's profile is available.
PGO_BINS := $(DEMO) $(TESTS) $(INTDEMO) $(NDNDEMO) $(IDSDEMO) $(TELDEMO) $(ENCDEMO) $(NCDEMO)

pgo:
	@echo "=== PGO pass 1: instrumented build ==="
	rm -rf build bin
	mkdir -p build bin
	$(MAKE) $(PGO_BINS) CFLAGS="$(CFLAGS) -fprofile-generate=build/prof"
	@echo "=== PGO pass 1: training runs (exit status ignored:"
	@echo "    instrumented builds can trip timing assertions under load) ==="
	-./$(TESTS) bulk 20000 >/dev/null 2>&1
	-./$(TESTS) par 4 >/dev/null 2>&1
	-./$(TESTS) mq 4 >/dev/null 2>&1
	-./$(TESTS) tcpmq 8 >/dev/null 2>&1
	-./$(DEMO) p4/srv6.p4 entries/srv6_entries.txt >/dev/null 2>&1
	-./$(INTDEMO) >/dev/null 2>&1
	-./$(NDNDEMO) >/dev/null 2>&1
	-./$(IDSDEMO) >/dev/null 2>&1
	-./$(TELDEMO) >/dev/null 2>&1
	-./$(ENCDEMO) >/dev/null 2>&1
	-./$(NCDEMO) >/dev/null 2>&1
	@echo "=== PGO pass 2: optimized rebuild ==="
	rm -f build/*.o $(PGO_BINS)
	$(MAKE) $(PGO_BINS) CFLAGS="$(CFLAGS) -fprofile-use=build/prof -fprofile-correction"
	@echo "=== PGO build complete ==="

# pcap test target (honours CC/CFLAGS like every other binary)
pcap-test: $(COBJS) build/pcap_test.o | bin
	$(CC) $(CFLAGS) -o bin/pcap_test $^ -lm

# ---- check: run ALL tests at once ----
check: all
	@echo "==== test_match ===="
	./bin/test_match
	@echo ""
	@echo "==== test_alu ===="
	./bin/test_alu
	@echo ""
	@echo "==== test_parser ===="
	./bin/test_parser
	@echo ""
	@echo "==== test_sketch ===="
	./bin/test_sketch
	@echo ""
	@echo "==== rmt_tests ===="
	./bin/rmt_tests
	@echo ""
	@echo "==== int_demo ===="
	./bin/int_demo
	@echo ""
	@echo "==== ndn_demo ===="
	./bin/ndn_demo
	@echo ""
	@echo "==== ids_demo ===="
	./bin/ids_demo
	@echo ""
	@echo "==== telescope_demo ===="
	./bin/telescope_demo
	@echo ""
	@echo "==== enc_classify_demo ===="
	./bin/enc_classify_demo
	@echo ""
	@echo "==== nc_demo ===="
	./bin/nc_demo
	@echo ""
	@echo "==== ALL CHECKS PASSED ===="

# ---- Sanitizer builds (for debugging) ----
# AddressSanitizer: catches buffer overflows, use-after-free, memory leaks
asan: clean
	$(MAKE) all CFLAGS="-O1 -g -fsanitize=address -fno-omit-frame-pointer -Wall -Wextra -std=c11 -pthread -Iinclude"

# ThreadSanitizer: catches data races (disable LTO — incompatible with TSan)
tsan: clean
	$(MAKE) all CFLAGS="-O1 -g -fsanitize=thread -fno-omit-frame-pointer -fno-optimize-sibling-calls -Wall -Wextra -std=c11 -pthread -Iinclude" LDFLAGS="-fsanitize=thread"

# UndefinedBehaviorSanitizer: catches signed overflow, alignment, etc.
ubsan: clean
	$(MAKE) all CFLAGS="-O1 -g -fsanitize=undefined -fno-omit-frame-pointer -Wall -Wextra -std=c11 -pthread -Iinclude"

.PHONY: all run test int ndn ids telescope test-match test-alu test-parser test-sketch enc-classify nc clean debug bench pgo check asan tsan ubsan pcap-test
