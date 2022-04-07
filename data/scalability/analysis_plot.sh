#!/bin/bash


rm *.parse

outfile_load="scalability_load.parse"
outfile_read="scalability_read.parse"
outfile_readnon="scalability_readnon.parse"
echo "thread,fastfair,pactree,sptree" >> $outfile_load
echo "thread,fastfair,pactree,sptree" >> $outfile_read
echo "thread,fastfair,pactree,sptree" >> $outfile_readnon

outfile_load_io_r="scalability_load_io_r.parse"
outfile_load_io_w="scalability_load_io_w.parse"
outfile_read_io_r="scalability_read_io_r.parse"
outfile_read_io_w="scalability_read_io_w.parse"
outfile_readnon_io_r="scalability_readnon_io_r.parse"
outfile_readnon_io_w="scalability_readnon_io_w.parse"
echo "thread,fastfair,pactree,sptree" >> $outfile_load_io_r
echo "thread,fastfair,pactree,sptree" >> $outfile_load_io_w
echo "thread,fastfair,pactree,sptree" >> $outfile_read_io_r
echo "thread,fastfair,pactree,sptree" >> $outfile_read_io_w
echo "thread,fastfair,pactree,sptree" >> $outfile_readnon_io_r
echo "thread,fastfair,pactree,sptree" >> $outfile_readnon_io_w

outfile_load_bw_r="scalability_load_bw_r.parse"
outfile_load_bw_w="scalability_load_bw_w.parse"
outfile_read_bw_r="scalability_read_bw_r.parse"
outfile_read_bw_w="scalability_read_bw_w.parse"
outfile_readnon_bw_r="scalability_readnon_bw_r.parse"
outfile_readnon_bw_w="scalability_readnon_bw_w.parse"
echo "thread,fastfair,pactree,sptree" >> $outfile_load_bw_r
echo "thread,fastfair,pactree,sptree" >> $outfile_load_bw_w
echo "thread,fastfair,pactree,sptree" >> $outfile_read_bw_r
echo "thread,fastfair,pactree,sptree" >> $outfile_read_bw_w
echo "thread,fastfair,pactree,sptree" >> $outfile_readnon_bw_r
echo "thread,fastfair,pactree,sptree" >> $outfile_readnon_bw_w

for t in 1 2 4 8 16 20 24 28 32 36 40
do
    # parse the throughput
    oneline_load="$t"
    oneline_read="$t"
    oneline_readnon="$t"
    for i in fastfair pactree sptree
    do
        datafile="scalability_${i}_${t}.data"

        tmp=`cat $datafile | grep "load         :" | awk '{print$5}'`
        oneline_load="$oneline_load,$tmp"   

        tmp=`cat $datafile | grep "readrandom   :" | awk '{print$5}'`
        oneline_read="$oneline_read,$tmp"  

        tmp=`cat $datafile | grep "readnon      :" | awk '{print$5}'`
        oneline_readnon="$oneline_readnon,$tmp"  
        
    done    
    echo $oneline_load >> $outfile_load
    echo $oneline_read >> $outfile_read
    echo $oneline_readnon >> $outfile_readnon

    # parse the io read
    oneline_load="$t"
    oneline_read="$t"
    oneline_readnon="$t"
    for i in fastfair pactree sptree
    do
        datafile="scalability_${i}_${t}.data"
        
        # all data
        raw=`grep "DIMM-R:" $datafile | awk '{print$4}'`        
        arr=(${raw//:/ })

        # load        
        oneline_load="$oneline_load,${arr[0]}"   

        # readrandom
        oneline_read="$oneline_read,${arr[1]}"

        # readnon
        oneline_readnon="$oneline_readnon,${arr[2]}"  

    done    
    echo $oneline_load >> $outfile_load_io_r
    echo $oneline_read >> $outfile_read_io_r
    echo $oneline_readnon >> $outfile_readnon_io_r

    # parse the io write
    oneline_load="$t"
    oneline_read="$t"
    oneline_readnon="$t"
    for i in fastfair pactree sptree
    do
        datafile="scalability_${i}_${t}.data"
        
        # all data
        raw=`grep "DIMM-W:" $datafile | awk '{print$4}'`        
        arr=(${raw//:/ })

        # load        
        oneline_load="$oneline_load,${arr[0]}"   

        # readrandom
        oneline_read="$oneline_read,${arr[1]}"

        # readnon
        oneline_readnon="$oneline_readnon,${arr[2]}"  

    done    
    echo $oneline_load >> $outfile_load_io_w
    echo $oneline_read >> $outfile_read_io_w
    echo $oneline_readnon >> $outfile_readnon_io_w

    # parse the bw read
    oneline_load="$t"
    oneline_read="$t"
    oneline_readnon="$t"
    for i in fastfair pactree sptree
    do
        datafile="scalability_${i}_${t}.data"
        
        # all data
        raw=`grep "DIMM-R:" $datafile | awk '{print$2}'`        
        arr=(${raw//:/ })

        # load        
        oneline_load="$oneline_load,${arr[0]}"   

        # readrandom
        oneline_read="$oneline_read,${arr[1]}"

        # readnon
        oneline_readnon="$oneline_readnon,${arr[2]}"  

    done    
    echo $oneline_load >> $outfile_load_bw_r
    echo $oneline_read >> $outfile_read_bw_r
    echo $oneline_readnon >> $outfile_readnon_bw_r

    # parse the bw write
    oneline_load="$t"
    oneline_read="$t"
    oneline_readnon="$t"
    for i in fastfair pactree sptree
    do
        datafile="scalability_${i}_${t}.data"
        
        # all data
        raw=`grep "DIMM-W:" $datafile | awk '{print$2}'`        
        arr=(${raw//:/ })

        # load        
        oneline_load="$oneline_load,${arr[0]}"   

        # readrandom
        oneline_read="$oneline_read,${arr[1]}"

        # readnon
        oneline_readnon="$oneline_readnon,${arr[2]}"  

    done    
    echo $oneline_load >> $outfile_load_bw_w
    echo $oneline_read >> $outfile_read_bw_w
    echo $oneline_readnon >> $outfile_readnon_bw_w
done

# python3 plot.py