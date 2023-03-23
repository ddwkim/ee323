#!/bin/bash
SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
FSIZE=50M
NUM_CLI=50
HOST=localhost
PORT=12000
SH=5

# clean
for ((i=0;i<$NUM_CLI;i++))
do
    if [[ -f results/${FSIZE}_$i.txt ]]; then
        rm results/${FSIZE}_$i.txt
    fi
done


for ((i=0;i<$NUM_CLI;i++))
do
    echo "Starting client $i"
    $SCRIPT_DIR/client -h $HOST -p $PORT -o 0 -s $SH \
    < $SCRIPT_DIR/sample/test-vector/$FSIZE.txt > results/${FSIZE}_$i.txt &
    pids[${i}]=$!
done

for pid in ${pids[*]}; do
    wait $pid
done

for ((i=0;i<$NUM_CLI;i++))
do
    RES=$(diff $SCRIPT_DIR/sample/test-vector-result/$FSIZE.txt \
            results/${FSIZE}_$i.txt)
    if [ -z "$RES" ]; then
        echo "Client $i: OK"
    else
        echo "Client $i: FAILED"
    fi
done

