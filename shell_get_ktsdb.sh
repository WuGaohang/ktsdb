#!/bin/bash
SLEEPCOUNT="0.5" 
HOSTNAME="172.16.16.171 5555"

FILENAME="/root/WGH/sop_prefix"

(
while read LINE
do
echo get $LINE;
sleep $SLEEPCOUNT;
done  < $FILENAME

) | telnet $HOSTNAME