BATCH=10000

for NUM in 4000000 8000000 16000000 32000000 64000000 128000000 256000000
do
numactl -N 0 sudo ../release/bench_sptree  --thread=40 --num=$NUM --read=2000000 --batch=$BATCH --benchmarks=load,readall,savetrace,status
numactl -N 0 sudo ../release/bench_sptree  --thread=40 --num=$NUM --read=2000000 --batch=$BATCH --benchmarks=readtrace,recover,status,readall,readnon | tee recovery_$NUM.data
done

