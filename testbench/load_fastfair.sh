

NUM=80000000

for t in 20
do
numactl -N 0 sudo ../release/bench_fastfair_buflog --thread=$t --num=$NUM --benchmarks=load | tee load_fastfair_buflog_$t.log
numactl -N 0 sudo ../release/bench_fastfair_pmem   --thread=$t --num=$NUM --benchmarks=load | tee load_fastfair_$t.log
done

