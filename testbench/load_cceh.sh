NUM=120960000

for t in 20
do
numactl -N 0 sudo ../release/bench_cceh                 --thread=$t --num=$NUM --read=1000000 --benchmarks=load,readall,readlat --read=2000000 | tee load_cceh_$t.data
numactl -N 0 sudo ../release/bench_cceh_spoton_di       --thread=$t --num=$NUM --read=1000000 --benchmarks=load,readall,readlat --read=2000000 | tee load_cceh_spoton_di_$t.data
# numactl -N 0 sudo ../release/bench_cceh_spoton_buf      --thread=$t --num=$NUM --read=1000000 --benchmarks=load,readall,readlat --read=2000000 | tee load_cceh_spoton_buf_$t.data
# numactl -N 0 sudo ../release/bench_cceh_spoton_buf_otp  --thread=$t --num=$NUM --read=1000000 --benchmarks=load,readall,readlat --read=2000000 | tee load_cceh_spoton_buf_otp_$t.data
done