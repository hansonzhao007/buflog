
for t in 1 4 8 12 16 20 40
do
numactl -N 0 sudo ../release/cceh_bench --thread=$t --num=50000000 | tee cceh_buflog_$t.log
done