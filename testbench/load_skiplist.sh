NUM=120960000

for t in 20
do
numactl -N 0 sudo ../release/bench_skiplist                     --thread=$t --num=$NUM --benchmarks=load,readall,readlat --read=2000000 | tee load_skiplist_$t.data
numactl -N 0 sudo ../release/bench_skiplist_spoton_di           --thread=$t --num=$NUM --benchmarks=load,readall,readlat --read=2000000 | tee load_skiplist_spoton_di_$t.data
numactl -N 0 sudo ../release/bench_skiplist_spoton_buf_otp      --thread=$t --num=$NUM --benchmarks=load,readall,readlat --read=2000000 | tee load_skiplist_spoton_buf_otp_$t.data
numactl -N 0 sudo ../release/bench_skiplist_spoton_buf_otp_di   --thread=$t --num=$NUM --benchmarks=load,readall,readlat --read=2000000 | tee load_skiplist_spoton_buf_otp_di_$t.data
done

