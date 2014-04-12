#!/bin/sh

echo "MYPID $$"

# check if we have a valid number for interval
t=$1
sleep_time=$(( t + 1 - 1 ))
if [ "$sleep_time" -eq 0 ]
	then
	sleep_time=1
fi

devices=
fix_names=
show_tc() {
	local x="$1"

	echo "BEGIN $x"
	
	/sbin/tc -s class show dev $x
	
	# check FireQOS names for classes
	if [ ! -z "$fix_names" -a -f /var/run/fireqos/ifaces/$x ]
	then
		name="`cat /var/run/fireqos/ifaces/$x`"
		echo "SETDEVICENAME $name"

		interface_classes=
		. /var/run/fireqos/$name.conf
		for x in $interface_classes_monitor
		do
				echo "SETCLASSNAME $x"
		done
	fi
	echo "END $x"
}

all_devices() {
	cat /proc/net/dev | grep ":" | cut -d ':' -f 1 | while read dev
	do
		l=`/sbin/tc class show dev $dev | wc -l`
		[ $l -ne 0 ] && echo $dev
	done
}

names_every=$((120 / sleep_time))
c=0
while [ 1 ]
do
	fix_names=
	c=$((c + 1))

	if [ $c -le 1 -o $c -ge $names_every ]
	then
		c=1
		fix_names="YES"
		devices="`all_devices`"
	fi

	for d in $devices
	do
		show_tc $d
	done

	sleep $sleep_time
done
