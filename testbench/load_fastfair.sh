

NUM=20000000

for t in 1
do
numactl -N 0 sudo ../release/bench_fastfair_spoton --thread=$t --num=$NUM --benchmarks=load | tee load_fastfair_spoton_$t.log
numactl -N 0 sudo ../release/bench_fastfair        --thread=$t --num=$NUM --benchmarks=load | tee load_fastfair_$t.log
done

