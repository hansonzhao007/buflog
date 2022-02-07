cmd='-g performance'
MAX_CPU=$((`nproc --all` - 1))
for i in $(seq 0 $MAX_CPU); do
    echo "Changing CPU " $i " with parameter "$cmd;
    cpufreq-set -c $i $cmd;
done