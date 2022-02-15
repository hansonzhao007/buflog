NUM=40000000

for t in 1 20 40
do
numactl -N 0 sudo ../release/bench_fastfair                     --thread=$t --num=$NUM --benchmarks=load | tee load_fastfair_$t.log
numactl -N 0 sudo ../release/bench_fastfair_spoton_buf          --thread=$t --num=$NUM --benchmarks=load | tee load_fastfair_spoton_buf_$t.log
numactl -N 0 sudo ../release/bench_fastfair_spoton_buf_otp      --thread=$t --num=$NUM --benchmarks=load | tee load_fastfair_spoton_buf_otp_$t.log
numactl -N 0 sudo ../release/bench_fastfair_spoton_buf_otp_di   --thread=$t --num=$NUM --benchmarks=load | tee load_fastfair_spoton_buf_otp_di_$t.log
done

