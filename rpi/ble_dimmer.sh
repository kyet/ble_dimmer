#!/bin/bash

DEV_ADDR="00:15:83:00:73:9C"
PORT=$1
ACTION=$2

if [ "$PORT" == "0" ]; then # all
	if [ "$ACTION" == "1" ]; then
		# [y = 1/128x^2] for 5 sec
		cmd="07020f3201018001010402018001010100"
	else
		# [y = -x + 127] for 5 sec
		cmd="05020f32010601ff017f020501ff017f00"
	fi
elif [ "$PORT" == "1" ]; then
	if [ "$ACTION" == "1" ]; then
		# [y = x] for 5 sec
		#cmd="0502093201040101010100"
		# [y = 1/128x^2] for 5 sec
		cmd="0702093201018001010100"
	else
		# [y = -x + 127] for 5 sec
		cmd="05020932010501ff017F00"
	fi
elif [ "$PORT" == "2" ]; then
	if [ "$ACTION" == "1" ]; then
		# [y = x] for 5 sec
		#cmd="0502093202040101010100"
		# [y = 1/128x^2] for 5 sec
		cmd="0702093202018001010100"
	else
		# [y = -x + 127] for 5 sec
		cmd="05020932020501ff017F00"
	fi
else
	exit
fi	

expect << EOF
spawn gatttool -b $DEV_ADDR -I 
send "connect\n" 
expect "Connection successful" 
expect ">" 
send "char-write-cmd 0x25 $cmd\n" 
expect ">" 
send "char-write-cmd 0x25 $cmd\n" 
expect ">" 
send "char-write-cmd 0x25 $cmd\n" 
expect ">" 
exit
EOF
