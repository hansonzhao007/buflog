NUM=80000000

for t in 40
do
# numactl -N 0 sudo ../release/bench_cceh_spoton --thread=$t --num=$NUM --benchmarks=load| tee load_cceh_spoton_$t.log
numactl -N 0 sudo ../release/bench_cceh        --thread=$t --num=$NUM --benchmarks=load| tee load_cceh_$t.log
done