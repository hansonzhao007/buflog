NUM=40000000

for t in 1 20 40
do
numactl -N 0 sudo ../release/bench_skiplist                     --thread=$t --num=$NUM --benchmarks=load | tee load_skiplist_$t.log
numactl -N 0 sudo ../release/bench_skiplist_spoton_buf_otp      --thread=$t --num=$NUM --benchmarks=load | tee load_skiplist_spoton_buf_otp_$t.log
numactl -N 0 sudo ../release/bench_skiplist_spoton_buf_otp_di   --thread=$t --num=$NUM --benchmarks=load | tee load_skiplist_spoton_buf_otp_di_$t.log
done

