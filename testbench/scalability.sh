NUM=120000000

# for t in 4 8 16 20 24 28 32 36 40
do
numactl -N 0 sudo ../release/bench_sptree           --thread=$t --num=$NUM --benchmarks=load,readall,readnon --read=10000000 --stats_interval=200000000 | tee scalability_sptree_$t.data
# numactl -N 0 sudo ../release/bench_fastfair         --thread=$t --num=$NUM --benchmarks=load,readall,readnon --read=10000000 --stats_interval=200000000 | tee scalability_fastfair_$t.data
# numactl -N 0 sudo ../pactree/release/bench_pactree  --thread=$t --num=$NUM --benchmarks=load,readall,readnon --read=10000000 --stats_interval=200000000 | tee scalability_pactree_$t.data
done

