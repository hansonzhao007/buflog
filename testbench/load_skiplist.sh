

NUM=80000000

for t in 40
do
numactl -N 0 sudo ../release/bench_skiplist_spoton --thread=$t --num=$NUM --benchmarks=load | tee load_skiplist_spoton_$t.log
numactl -N 0 sudo ../release/bench_skiplist        --thread=$t --num=$NUM --benchmarks=load | tee load_skiplist_$t.log
done

