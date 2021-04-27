
# recover can be tested when CONFIG_DRAM_INNER is disabled
for t in 1
do
numactl -N 0 sudo ../release/fastfair_bench --thread=$t --num=100 --batch=1 --benchmarks=load,readrandom
numactl -N 0 sudo ../release/fastfair_bench --thread=$t --num=100 --batch=1 --benchmarks=recover,readrandom
done

