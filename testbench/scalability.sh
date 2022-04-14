#!/bin/bash

NUM=120960000

declare -a allThreads=(40 36 32 28 24 20 16 12 8 4 2 1)
READS=4000000
BENCHMARKS=load,readrandom,readnon,scan,scan,status
INTERVALS=200000000
# declare -a allThreads=(40)

for t in ${allThreads[@]};
do
numactl -N 0 sudo ../release/bench_fastfair         --thread=$t --num=$NUM --benchmarks=$BENCHMARKS --read=$READS --stats_interval=$INTERVALS | tee scalability_fastfair_$t.data
done

for t in ${allThreads[@]};
do
numactl -N 0 sudo ../pactree/release/bench_pactree  --thread=$t --num=$NUM --benchmarks=$BENCHMARKS --read=$READS --stats_interval=$INTERVALS | tee scalability_pactree_$t.data
done


for t in ${allThreads[@]};
do
numactl -N 0 sudo ../release/bench_sptree           --thread=$t --num=$NUM --benchmarks=$BENCHMARKS --read=$READS --stats_interval=$INTERVALS | tee scalability_sptree_$t.data
done


# for t in ${allThreads[@]};
# do
# numactl -N 0 sudo ../release/bench_sptree           --thread=$t --num=$NUM --benchmarks=$BENCHMARKS --read=$READS --stats_interval=$INTERVALS --writebuffer=true --log=true | tee scalability_sptree_bug_log_$t.data
# done


