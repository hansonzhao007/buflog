
NUM=1000000
for t in 4
do
numactl -N 0 sudo ../release/bench_fastfair_pmem --thread=$t --num=$NUM --batch=1 --benchmarks=load,readrandom
numactl -N 0 sudo ../release/bench_fastfair_pmem --thread=$t --num=$NUM --batch=1 --benchmarks=recover,readrandom
done

