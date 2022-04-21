#!/bin/bash

rm *.parse

outfile=recovery.parse

for num in 4000000 8000000 16000000 32000000 64000000 128000000 256000000
do
    filename=recovery_${num}.data
    echo $filename

    tmp=`cat $filename | grep "recover time:" | awk '{print $3}'`
    echo ${num},${tmp} >> $outfile
    
done

# python3 plot.py