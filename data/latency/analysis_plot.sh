#!/bin/bash

rm *.parse


for index in fastfair pactree sptree sptree_buf_log
do
    # parse the throughput
    original=latency_${index}

    for datafile in $original
    do
        # latency=`grep -i -A 30 "loadlat ${datafile}_20.data`
        filename=${datafile}_20.data
        echo $filename

        cat ${datafile}_20.data  | grep -i -A 30 "loadlat      :" > "tmp.log"
        outfile=load_$datafile.parse
        while read line; do
            if [[ $line = \(* ]]; then 
                tmp=`echo $line | awk '{print $3 "," $5}'`
                echo $tmp >> $outfile
             fi
        done < "tmp.log"

        cat ${datafile}_20.data  | grep -i -A 30 "readlat      :" > "tmp.log"
        outfile=read_$datafile.parse
        while read line; do
            if [[ $line = \(* ]]; then 
                tmp=`echo $line | awk '{print $3 "," $5}'`
                echo $tmp >> $outfile
             fi
        done < "tmp.log"

        cat ${datafile}_20.data  | grep -i -A 30 "readnonlat   :" > "tmp.log"
        outfile=readnon_$datafile.parse
        while read line; do
            if [[ $line = \(* ]]; then 
                tmp=`echo $line | awk '{print $3 "," $5}'`
                echo $tmp >> $outfile
             fi
        done < "tmp.log"
    done         
done

for index in fastfair pactree sptree sptree_buf_log
do
    filename=latency_${index}_20.data

    # Median
    raw=`grep -rnIiw "Median:" $filename | awk '{print $4}'`
    arr=(${raw//:/ })
    # load,read,readnon
    echo ${arr[0]},${arr[1]},${arr[2]} > lat_median_${index}.parse
done

# python3 plot.py