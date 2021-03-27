#!/bin/bash
for (( i=1; i<10+1; i++ ))
do
    echo Test $i
    ./client 1 # quickstart for list root folder
done
