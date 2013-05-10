#!/bin/bash
#./multi_netperf_insts.sh 300 -t TCP_RR -H 10.1.147.3 -p 12865 -l 60 -- -r 13,56

NUM=$1
i=0

shift

#echo "$@"

rm -f a.txt

while [ $i -lt $NUM ]
do  
    netperf $@ >> a.txt &
    (( i++ ))
done
