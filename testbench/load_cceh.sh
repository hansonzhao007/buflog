NUM=80000000

for t in 20
do
numactl -N 0 sudo ../release/bench_cceh_buflog --thread=$t --num=$NUM | tee load_cceh_buflog_$t.log
numactl -N 0 sudo ../release/bench_cceh_pmem   --thread=$t --num=$NUM | tee load_cceh_$t.log
done