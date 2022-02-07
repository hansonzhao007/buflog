

NUM=80000000

for t in 20
do
numactl -N 0 sudo ../release/bench_skiplist_buflog --thread=$t --num=$NUM --benchmarks=load | tee load_skiplist_buflog_$t.log
numactl -N 0 sudo ../release/bench_skiplist_pmem   --thread=$t --num=$NUM --benchmarks=load | tee load_skiplist_$t.log
done

