
NUM=120960000

for t in 40
do
numactl -N 0 sudo ../release/bench_sptree --is_seq_trace=true --thread=$t --num=$NUM --benchmarks=load,readall,status
numactl -N 0 sudo ../release/bench_sptree --is_seq_trace=true --thread=$t --num=$NUM --benchmarks=recover,status,readall
done

