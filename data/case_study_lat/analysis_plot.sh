#!/bin/bash

rm *.parse


for index in cceh fastfair skiplist
do
    # parse the throughput
    original=lat_${index}
    spoton_di=lat_${index}_spoton_di

    for datafile in $original $spoton_di
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
    done
         
done

# python3 plot.py