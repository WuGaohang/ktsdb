#!/bin/bash
SLEEPCOUNT="0.5" 
HOSTNAME="172.16.16.171 5555"

FILENAME="/root/WGH/sop_data"

(
while read LINE
do
echo put $LINE;
sleep $SLEEPCOUNT;
done  < $FILENAME

) | telnet $HOSTNAME