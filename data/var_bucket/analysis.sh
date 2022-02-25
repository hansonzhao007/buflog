#!/bin/bash

rm *.csv

file_rnd="rnd.csv"
file_seq="seq.csv"
echo "bucket_size,throughput,wa,pm_w,pm_r,pm_bw_w,pm_bw_r" >> $file_rnd
echo "bucket_size,throughput,wa,pm_w,pm_r,pm_bw_w,pm_bw_r" >> $file_seq

# Parse the throughput
for i in 1 2 3 4 5 6 7 8 9 10
do
    datafile="bucket_cacheline${i}.data"
    oneline_rnd=""
    oneline_seq=""
    index=0
    
    tmp="$(cat $datafile | grep "bucket size" | awk '{print $3}')"
    oneline_rnd="$tmp"
    oneline_seq="$tmp"


    iops_rnd="$(cat $datafile | grep "throughput"| awk 'NR==1{print $2}')"
    iops_seq="$(cat $datafile | grep "throughput"| awk 'NR==2{print $2}')"
    oneline_rnd="$oneline_rnd,$iops_rnd"
    oneline_seq="$oneline_seq,$iops_seq"

    rnd="$(cat $datafile | grep "0  " | awk 'NR==1{print $13}')"
    seq="$(cat $datafile | grep "0  " | awk 'NR==2{print $13}')"
    oneline_rnd="$oneline_rnd,$rnd"
    oneline_seq="$oneline_seq,$seq"

    rnd="$(cat $datafile | grep "DIMM-W" | awk 'NR==1{print $4}')"
    seq="$(cat $datafile | grep "DIMM-W" | awk 'NR==2{print $4}')"
    oneline_rnd="$oneline_rnd,$rnd"
    oneline_seq="$oneline_seq,$seq"

    rnd="$(cat $datafile | grep "DIMM-R" | awk 'NR==1{print $4}')"
    seq="$(cat $datafile | grep "DIMM-R" | awk 'NR==2{print $4}')"
    oneline_rnd="$oneline_rnd,$rnd"
    oneline_seq="$oneline_seq,$seq"

    rnd="$(cat $datafile | grep "DIMM-W" | awk 'NR==1{print $2}')"
    seq="$(cat $datafile | grep "DIMM-W" | awk 'NR==2{print $2}')"
    oneline_rnd="$oneline_rnd,$rnd"
    oneline_seq="$oneline_seq,$seq"

    rnd="$(cat $datafile | grep "DIMM-R" | awk 'NR==1{print $2}')"
    seq="$(cat $datafile | grep "DIMM-R" | awk 'NR==2{print $2}')"
    oneline_rnd="$oneline_rnd,$rnd"
    oneline_seq="$oneline_seq,$seq"
    

    echo $oneline_rnd >> $file_rnd
    echo $oneline_seq >> $file_seq
done
