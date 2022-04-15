.PHONY: all clean
.DEFAULT_GOAL := all

LIBS=-lrt -lm -pthread -lpmemobj
INCLUDES=-I./include -I../test -I../src
CFLAGS=-O0 -std=c++11 -g

output = benchmark

all: benchmark benchmark2

benchmark: ycsb_bench.cc btree.h
	$(CXX) $(CFLAGS) $(INCLUDES) -o bench_fastfair_original ycsb_bench.cc -lpmemobj -ltbb -lpmem -lpthread -lgflags -DPMEM


benchmark2: ycsb_bench.cc btree_dram.h
	$(CXX) $(CFLAGS) $(INCLUDES) -o bench_fastfair_dram ycsb_bench.cc -lpmemobj -ltbb -lpmem -lpthread -lgflags -DDRAM

clean: 
	rm $(output)