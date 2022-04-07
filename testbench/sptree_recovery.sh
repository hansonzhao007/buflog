
NUM=120960000
BATCH=1000
for t in 40
do
numactl -N 0 sudo ../release/bench_sptree  --thread=$t --num=$NUM --read=2000000 --batch=$BATCH --benchmarks=load,readall,readnon,status,savetrace
numactl -N 0 sudo ../release/bench_sptree  --thread=$t --num=$NUM --read=2000000 --batch=$BATCH --benchmarks=readtrace,recover,status,readall,readnon
done

