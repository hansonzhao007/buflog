NUM=120960000
benchmarks=loadlat,readlat
read=2000000
for t in 20
do
numactl -N 0 sudo ../release/bench_cceh                 --thread=$t --num=$NUM --benchmarks=$benchmarks --read=$read | tee lat_cceh_$t.data
numactl -N 0 sudo ../release/bench_cceh_spoton_di       --thread=$t --num=$NUM --benchmarks=$benchmarks --read=$read | tee lat_cceh_spoton_di_$t.data
numactl -N 0 sudo ../release/bench_fastfair             --thread=$t --num=$NUM --benchmarks=$benchmarks --read=$read | tee lat_fastfair_$t.data
numactl -N 0 sudo ../release/bench_fastfair_spoton_di   --thread=$t --num=$NUM --benchmarks=$benchmarks --read=$read | tee lat_fastfair_spoton_di_$t.data
numactl -N 0 sudo ../release/bench_skiplist             --thread=$t --num=$NUM --benchmarks=$benchmarks --read=$read | tee lat_skiplist_$t.data
numactl -N 0 sudo ../release/bench_skiplist_spoton_di   --thread=$t --num=$NUM --benchmarks=$benchmarks --read=$read | tee lat_skiplist_spoton_di_$t.data
done

