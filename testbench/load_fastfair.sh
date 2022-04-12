NUM=120960000

for t in 40
do
numactl -N 0 sudo ../release/bench_fastfair                     --thread=$t --num=$NUM --benchmarks=load,readall,readlat --read=2000000 | tee load_fastfair_$t.data
numactl -N 0 sudo ../release/bench_fastfair_spoton_buf          --thread=$t --num=$NUM --benchmarks=load,readall,readlat --read=2000000 | tee load_fastfair_spoton_buf_$t.data
numactl -N 0 sudo ../release/bench_fastfair_spoton_buf_otp      --thread=$t --num=$NUM --benchmarks=load,readall,readlat --read=2000000 | tee load_fastfair_spoton_buf_otp_$t.data
# numactl -N 0 sudo ../release/bench_fastfair_spoton_buf_otp_di   --thread=$t --num=$NUM --benchmarks=load,readall,readlat --read=2000000 | tee load_fastfair_spoton_buf_otp_di_$t.data

done

