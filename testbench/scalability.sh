#!/bin/bash

NUM=120960000

declare -a allThreads=(40 36 32 28 24 20 16 12 8 4 2 1)

# for t in ${allThreads[@]};
# do
# numactl -N 0 sudo ../release/bench_fastfair         --thread=$t --num=$NUM --benchmarks=load,readrandom,readnon        --read=10000000 --stats_interval=200000000 | tee scalability_fastfair_$t.data
# done

# for t in ${allThreads[@]};
# do
# numactl -N 0 sudo ../pactree/release/bench_pactree  --thread=$t --num=$NUM --benchmarks=load,readrandom,readnon,status --read=10000000 --stats_interval=200000000 | tee scalability_pactree_$t.data
# done

for t in ${allThreads[@]};
do
numactl -N 0 sudo ../release/bench_sptree           --thread=$t --num=$NUM --benchmarks=load,readrandom,readnon,status --read=10000000 --stats_interval=200000000 | tee scalability_sptree_$t.data
done


