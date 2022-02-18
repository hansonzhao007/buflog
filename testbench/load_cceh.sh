NUM=80000000

for t in 1 20 40
do
numactl -N 0 sudo ../release/bench_cceh                 --thread=$t --num=$NUM --read=1000000 --benchmarks=load,readall | tee load_cceh_$t.log
numactl -N 0 sudo ../release/bench_cceh_spoton_buf      --thread=$t --num=$NUM --read=1000000 --benchmarks=load,readall | tee load_cceh_spoton_buf_$t.log
numactl -N 0 sudo ../release/bench_cceh_spoton_buf_otp  --thread=$t --num=$NUM --read=1000000 --benchmarks=load,readall | tee load_cceh_spoton_buf_otp_$t.log
done