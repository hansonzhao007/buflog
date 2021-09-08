
for t in 8
do
numactl -N 0 sudo ../release/cceh_bench --thread=$t --num=50000000 | tee cceh_buflog_$t.log
numactl -N 0 sudo ../CCEH/ycsb_bench --thread=$t --num=50000000 | tee cceh_$t.log
done