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

# python3 plot.py