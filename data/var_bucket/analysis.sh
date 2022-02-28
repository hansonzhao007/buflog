#!/bin/bash

rm *.csv

file_rnd="rnd.csv"
file_seq="seq.csv"
file_seq_batch="seq_batch.csv"
file_buffer="buffer.csv"
echo "bucket_size,throughput,wa,pm_w,pm_r,pm_bw_w,pm_bw_r" >> $file_rnd
echo "bucket_size,throughput,wa,pm_w,pm_r,pm_bw_w,pm_bw_r" >> $file_seq
echo "bucket_size,throughput,wa,pm_w,pm_r,pm_bw_w,pm_bw_r" >> $file_seq_batch
echo "bucket_size,throughput,wa,pm_w,pm_r,pm_bw_w,pm_bw_r" >> $file_buffer

# Parse the throughput
for i in 1 2 3 4 5 6 7 8
do
    datafile="bucket_cacheline${i}.data"
    oneline_rnd=""
    oneline_seq=""
    index=0
    
    tmp="$(cat $datafile | grep "bucket size" | awk '{print $3}')"
    oneline_rnd="$tmp"
    oneline_seq="$tmp"
    oneline_seq_batch="$tmp"
    oneline_buffer="$tmp"

    iops_rnd="$(cat $datafile | grep "throughput"| awk 'NR==1{print $2}')"
    iops_seq="$(cat $datafile | grep "throughput"| awk 'NR==2{print $2}')"
    iops_seq_batch="$(cat $datafile | grep "throughput"| awk 'NR==3{print $2}')"
    iops_buffer="$(cat $datafile | grep "throughput"| awk 'NR==4{print $2}')"
    oneline_rnd="$oneline_rnd,$iops_rnd"
    oneline_seq="$oneline_seq,$iops_seq"
    oneline_seq_batch="$oneline_seq_batch,$iops_seq_batch"
    oneline_buffer="$oneline_buffer,$iops_buffer"

    rnd="$(cat $datafile | grep "0   " | awk 'NR==1{print $13}')"
    seq="$(cat $datafile | grep "0   " | awk 'NR==2{print $13}')"
    seq_batch="$(cat $datafile | grep "0   " | awk 'NR==3{print $13}')"
    buffer="$(cat $datafile | grep "0   " | awk 'NR==4{print $13}')"
    oneline_rnd="$oneline_rnd,$rnd"
    oneline_seq="$oneline_seq,$seq"
    oneline_seq_batch="$oneline_seq_batch,$seq_batch"
    oneline_buffer="$oneline_buffer,$buffer"

    rnd="$(cat $datafile | grep "DIMM-W" | awk 'NR==1{print $4}')"
    seq="$(cat $datafile | grep "DIMM-W" | awk 'NR==2{print $4}')"
    seq_batch="$(cat $datafile | grep "DIMM-W" | awk 'NR==3{print $4}')"
    buffer="$(cat $datafile | grep "DIMM-W" | awk 'NR==4{print $4}')"
    oneline_rnd="$oneline_rnd,$rnd"
    oneline_seq="$oneline_seq,$seq"
    oneline_seq_batch="$oneline_seq_batch,$seq_batch"
    oneline_buffer="$oneline_buffer,$buffer"

    rnd="$(cat $datafile | grep "DIMM-R" | awk 'NR==1{print $4}')"
    seq="$(cat $datafile | grep "DIMM-R" | awk 'NR==2{print $4}')"
    seq_batch="$(cat $datafile | grep "DIMM-R" | awk 'NR==3{print $4}')"
    buffer="$(cat $datafile | grep "DIMM-R" | awk 'NR==4{print $4}')"
    oneline_rnd="$oneline_rnd,$rnd"
    oneline_seq="$oneline_seq,$seq"
    oneline_seq_batch="$oneline_seq_batch,$seq_batch"
    oneline_buffer="$oneline_buffer,$buffer"

    rnd="$(cat $datafile | grep "DIMM-W" | awk 'NR==1{print $2}')"
    seq="$(cat $datafile | grep "DIMM-W" | awk 'NR==2{print $2}')"
    seq_batch="$(cat $datafile | grep "DIMM-W" | awk 'NR==3{print $2}')"
    buffer="$(cat $datafile | grep "DIMM-W" | awk 'NR==4{print $2}')"
    oneline_rnd="$oneline_rnd,$rnd"
    oneline_seq="$oneline_seq,$seq"
    oneline_seq_batch="$oneline_seq_batch,$seq_batch"
    oneline_buffer="$oneline_buffer,$buffer"

    rnd="$(cat $datafile | grep "DIMM-R" | awk 'NR==1{print $2}')"
    seq="$(cat $datafile | grep "DIMM-R" | awk 'NR==2{print $2}')"
    seq_batch="$(cat $datafile | grep "DIMM-R" | awk 'NR==3{print $2}')"
    buffer="$(cat $datafile | grep "DIMM-R" | awk 'NR==4{print $2}')"
    oneline_rnd="$oneline_rnd,$rnd"
    oneline_seq="$oneline_seq,$seq"
    oneline_seq_batch="$oneline_seq_batch,$seq_batch"
    oneline_buffer="$oneline_buffer,$buffer"
    

    echo $oneline_rnd >> $file_rnd
    echo $oneline_seq >> $file_seq
    echo $oneline_seq_batch >> $file_seq_batch
    echo $oneline_buffer >> $file_buffer
done
