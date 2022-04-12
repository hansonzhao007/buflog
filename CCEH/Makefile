.PHONY: all clean

CXX := g++-10 -O3 -std=c++17
CFLAGS := -I../test -I../src -I./ -lrt

all: benchmark

benchmark: src/ycsb_bench.cpp CCEH
	$(CXX) $(CFLAGS) -o bench_cceh src/ycsb_bench.cpp src/CCEH_CoW.o -lpmemobj -ltbb -lpmem -lpthread -lgflags -DMULTITHREAD
	$(CXX) $(CFLAGS) -o bench_cceh_spoton_di src/ycsb_bench.cpp src/CCEH_CoW_di.o -lpmemobj -ltbb -lpmem -lpthread -lgflags -DMULTITHREAD

CCEH: src/CCEH.h src/CCEH.cpp
	$(CXX) $(CFLAGS) -c -o src/CCEH.o src/CCEH.cpp -DINPLACE -lpmemobj -lpmem
	$(CXX) $(CFLAGS) -c -o src/CCEH_CoW.o src/CCEH.cpp -lpmemobj -lpmem
	$(CXX) $(CFLAGS) -c -o src/CCEH_CoW_di.o src/CCEH.cpp -lpmemobj -lpmem -DCONFIG_DRAM_INNER

clean:
	rm -rf src/*.o bin/* 
