NUM=120960000
benchmarks=load,readrandom
read=2000000

for t in 40
do
numactl -N 0 sudo ../release/bench_cceh                         --thread=$t --num=$NUM --benchmarks=$benchmarks --read=$read | tee buf_cceh_$t.data
numactl -N 0 sudo ../release/bench_cceh_spoton_buf_log          --thread=$t --num=$NUM --benchmarks=$benchmarks --read=$read | tee buf_cceh_spoton_buf_log_$t.data
numactl -N 0 sudo ../release/bench_cceh_spoton_buf_otp_log      --thread=$t --num=$NUM --benchmarks=$benchmarks --read=$read | tee buf_cceh_spoton_buf_otp_log_$t.data
numactl -N 0 sudo ../release/bench_cceh_spoton_buf_otp_di_log   --thread=$t --num=$NUM --benchmarks=$benchmarks --read=$read | tee buf_cceh_spoton_buf_otp_di_log_$t.data
rm -rf /mnt/pmem/objpool.data
done

for t in 40
do
numactl -N 0 sudo ../release/bench_fastfair                         --thread=$t --num=$NUM --benchmarks=$benchmarks --read=$read | tee buf_fastfair_$t.data
numactl -N 0 sudo ../release/bench_fastfair_spoton_buf_log          --thread=$t --num=$NUM --benchmarks=$benchmarks --read=$read | tee buf_fastfair_spoton_buf_log_$t.data
numactl -N 0 sudo ../release/bench_fastfair_spoton_buf_otp_log      --thread=$t --num=$NUM --benchmarks=$benchmarks --read=$read | tee buf_fastfair_spoton_buf_otp_log_$t.data
numactl -N 0 sudo ../release/bench_fastfair_spoton_buf_otp_di_log   --thread=$t --num=$NUM --benchmarks=$benchmarks --read=$read | tee buf_fastfair_spoton_buf_otp_di_log_$t.data
rm -rf /mnt/pmem/fastfair*
done

for t in 20
do
numactl -N 0 sudo ../release/bench_skiplist                         --thread=$t --num=$NUM --benchmarks=$benchmarks --read=$read | tee buf_skiplist_$t.data
numactl -N 0 sudo ../release/bench_skiplist_spoton_buf_otp_log      --thread=$t --num=$NUM --benchmarks=$benchmarks --read=$read | tee buf_skiplist_spoton_buf_otp_log_$t.data
numactl -N 0 sudo ../release/bench_skiplist_spoton_buf_otp_di_log   --thread=$t --num=$NUM --benchmarks=$benchmarks --read=$read | tee buf_skiplist_spoton_buf_otp_di_log_$t.data
rm -rf /mnt/pmem/skiplist*
done

