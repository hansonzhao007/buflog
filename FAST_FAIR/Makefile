.PHONY: all clean
.DEFAULT_GOAL := all

LIBS=-lrt -lm -pthread -lpmemobj
INCLUDES=-I./include -I../test -I../src
CFLAGS=-O0 -std=c++11 -g

output = benchmark

all: benchmark

benchmark: ycsb_bench.cc btree.h
	$(CXX) $(CFLAGS) $(INCLUDES) -o bench_fastfair ycsb_bench.cc -lpmemobj -ltbb -lpmem -lpthread -lgflags


clean: 
	rm $(output)