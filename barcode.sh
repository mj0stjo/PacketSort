while read line; do
	echo $line
	echo $line > /dev/rtf3
	# do something here, like e.g. echo to RTAI FIFO
done < <(cat /dev/ttyUSB0)

