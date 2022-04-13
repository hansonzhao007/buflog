
#!/bin/bash

rm *.parse

# thread iops io_r io_w

for index in cceh fastfair skiplist
do
    for t in 1 20 40
    do
        for type in "" "buf_log" "buf_otp_log" "buf_otp_di_log"
        do
            if [[ "$type" == buf_log ]] && [[ "$index" == skiplist ]]; then
                continue
            fi
            outfile="${index}_$t.parse"
            # echo $outfile    
            
            if [[ "$type" == "" ]]; then
                filename=buf_${index}_${t}.data
            else
                filename=buf_${index}_spoton_${type}_${t}.data
            fi
            
            echo $filename

            # parse iops
            iops=`grep "load         :" $filename | awk '{print $5}'`            
            
            # parse io_r
            raw=`grep "DIMM-R:" $filename | awk '{print$4}'`        
            arr=(${raw//:/ })                
            io_r="$oneline_load,${arr[0]}"   
            # echo $io_r

            # parse io_w
            raw=`grep "DIMM-W:" $filename | awk '{print$4}'`        
            arr=(${raw//:/ })                
            io_w="$oneline_load,${arr[0]}"
            # echo $io_w

            oneline=$iops$io_r$io_w
            echo $oneline

            echo $oneline >> $outfile

        done
    done
done

# python3 plot.py