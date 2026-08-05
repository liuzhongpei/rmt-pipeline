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
ENCDEMO  := bin/enc_classify_demo
NCDEMO   := bin/nc_demo
CORE    := src/phv.c src/parser.c src/match.c src/alu.c \
           src/pipeline.c src/p4c.c src/runtime.c src/cache.c src/dnn.c \
           src/mq.c
COBJS   := $(CORE:src/%.c=build/%.o)

all: $(DEMO) $(TESTS) $(INTDEMO) $(NDNDEMO) $(IDSDEMO) $(TELDEMO) $(TESTMATCH) $(ENCDEMO) $(NCDEMO)

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

$(ENCDEMO): $(COBJS) build/enc_classify_demo.o | bin
	$(CC) $(CFLAGS) -o $@ $(COBJS) build/enc_classify_demo.o -lm

$(NCDEMO): $(COBJS) build/nc_demo.o | bin
	$(CC) $(CFLAGS) -o $@ $(COBJS) build/nc_demo.o -lm

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

.PHONY: all run test int ndn ids telescope test-match enc-classify nc clean debug bench pgo

test-match: $(TESTMATCH)
	./$(TESTMATCH)

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
# Pass 1: instrument, run benchmark to collect profile data
# Pass 2: rebuild with profile feedback (10-20% additional speedup)
pgo:
	@echo "=== PGO pass 1: instrumented build ==="
	rm -rf build bin
	mkdir -p build bin
	$(CC) $(CFLAGS) -fprofile-generate=build/prof $(CORE) src/tests.c -o bin/rmt_tests_pgo -lm
	@echo "=== PGO pass 1: training run ==="
	./bin/rmt_tests_pgo bulk 20000 >/dev/null 2>&1
	./bin/rmt_tests_pgo par 4 >/dev/null 2>&1
	./bin/rmt_tests_pgo mq 4 >/dev/null 2>&1
	./bin/rmt_tests_pgo tcpmq 8 >/dev/null 2>&1
	@echo "=== PGO pass 2: optimized rebuild ==="
	$(CC) $(CFLAGS) -fprofile-use=build/prof -fprofile-correction $(CORE) src/tests.c -o bin/rmt_tests -lm
	$(CC) $(CFLAGS) -fprofile-use=build/prof -fprofile-correction $(CORE) src/main.c -o bin/rmt_demo
	$(CC) $(CFLAGS) -fprofile-use=build/prof -fprofile-correction $(CORE) src/int_demo.c -o bin/int_demo
	$(CC) $(CFLAGS) -fprofile-use=build/prof -fprofile-correction $(CORE) src/ndn_demo.c -o bin/ndn_demo
	$(CC) $(CFLAGS) -fprofile-use=build/prof -fprofile-correction $(CORE) src/ids_demo.c -o bin/ids_demo -lm
	$(CC) $(CFLAGS) -fprofile-use=build/prof -fprofile-correction $(CORE) src/telescope_demo.c -o bin/telescope_demo
	$(CC) $(CFLAGS) -fprofile-use=build/prof -fprofile-correction $(CORE) src/enc_classify_demo.c -o bin/enc_classify_demo -lm
	$(CC) $(CFLAGS) -fprofile-use=build/prof -fprofile-correction $(CORE) src/nc_demo.c -o bin/nc_demo -lm
	@echo "=== PGO build complete ==="

# pcap test target
pcap-test: build/phv.o build/parser.o build/match.o build/alu.o build/pipeline.o \
           build/p4c.o build/runtime.o build/cache.o build/dnn.o build/mq.o build/pcap_test.o
	gcc -O3 -flto -march=native -o bin/pcap_test $^ -lm

build/pcap_test.o: src/pcap_test.c include/rmt.h
	gcc -O3 -flto -march=native -Wall -Wextra -std=c11 -pthread -Iinclude -c -o $@ $<
