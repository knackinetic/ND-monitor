#!/bin/sh

# _update_every is a special variable - it holds the number of seconds
# between the calls of the _update() function
ap_update_every=

declare -A ap_devs=()

export PATH="${PATH}:/sbin:/usr/sbin:/usr/local/sbin"

# _check is called once, to find out if this chart should be enabled or not
ap_check() {
	local ev=$(iw dev | awk '
		BEGIN {
			i = "";
			ssid = "";
			ap = 0;
		}
		/^[ \t]+Interface / {
			if( ap == 1 ) {
				print "ap_devs[" i "]=\"" ssid "\""
			}

			i = $2;
			ssid = "";
			ap = 0;
		}
		/^[ \t]+ssid / { ssid = $2; }
		/^[ \t]+type AP$/ { ap = 1; }
		END {
			if( ap == 1 ) {
				print "ap_devs[" i "]=\"" ssid "\""
			}
		}
	')
	eval "${ev}"

	# this should return:
	#  - 0 to enable the chart
	#  - 1 to disable the chart

	[ ${#ap_devs[@]} -gt 0 ] && return 0
	return 1
}

# _create is called once, to create the charts
ap_create() {
	local ssid dev

	for dev in "${!ap_devs[@]}"
	do
		ssid="${ap_devs[${dev}]}"

		# create the chart with 3 dimensions
		cat <<EOF
CHART ap_clients.${dev} '' "Connected clients to ${ssid} on ${dev}" "clients" ${dev} ${dev} line 15000 $ap_update_every
DIMENSION clients '' absolute 1 1

CHART ap_bandwidth.${dev} '' "Bandwidth for ${ssid} on ${dev}" "kilobits/s" ${dev} ${dev} area 15001 $ap_update_every
DIMENSION received '' incremental 8 1024
DIMENSION sent '' incremental -8 1024

CHART ap_packets.${dev} '' "Packets for ${ssid} on ${dev}" "packets/s" ${dev} ${dev} line 15002 $ap_update_every
DIMENSION received '' incremental 1 1
DIMENSION sent '' incremental -1 1

CHART ap_issues.${dev} '' "Transmit Issues for ${ssid} on ${dev}" "issues/s" ${dev} ${dev} line 15003 $ap_update_every
DIMENSION retries 'tx retries' incremental 1 1
DIMENSION failures 'tx failures' incremental -1 1

CHART ap_signal.${dev} '' "Average Signal for ${ssid} on ${dev}" "dBm" ${dev} ${dev} line 15004 $ap_update_every
DIMENSION signal 'average signal' absolute 1 1

CHART ap_bitrate.${dev} '' "Bitrate for ${ssid} on ${dev}" "Mbps" ${dev} ${dev} line 15005 $ap_update_every
DIMENSION receive '' absolute 1 1024
DIMENSION transmit '' absolute -1 1024
DIMENSION expected 'expected throughput' absolute 1 1024
EOF

	done

	return 0
}

# _update is called continiously, to collect the values
ap_update() {
	# the first argument to this function is the microseconds since last update
	# pass this parameter to the BEGIN statement (see bellow).

	# do all the work to collect / calculate the values
	# for each dimension
	# remember: KEEP IT SIMPLE AND SHORT

	for dev in "${!ap_devs[@]}"
	do
		iw ${dev} station dump |\
			awk "
				BEGIN {
					c = 0;
					rb = 0;
					tb = 0;
					rp = 0;
					tp = 0;
					tr = 0;
					tf = 0;
					tt = 0;
					rt = 0;
					s = 0;
					g = 0;
					e = 0;
				}
				/^Station/           { c++; }
				/^[ \\t]+rx bytes:/   { rb += \$3 }
				/^[ \\t]+tx bytes:/   { tb += \$3 }
				/^[ \\t]+rx packets:/ { rp += \$3 }
				/^[ \\t]+tx packets:/ { tp += \$3 }
				/^[ \\t]+tx retries:/ { tr += \$3 }
				/^[ \\t]+tx failed:/  { tf += \$3 }
				/^[ \\t]+signal:/     { s  += \$2; }
				/^[ \\t]+rx bitrate:/ { x = \$3; rt += x * 1000; }
				/^[ \\t]+tx bitrate:/ { x = \$3; tt += x * 1000; }
				/^[ \\t]+expected throughput:(.*)Mbps/ {
					x=\$3;
					sub(/Mbps/, \"\", x);
					e += x * 1000;
				}
				END {
					print \"BEGIN ap_clients.${dev}\"
					print \"SET clients = \" c;
					print \"END\"
					print \"BEGIN ap_bandwidth.${dev}\"
					print \"SET received = \" rb;
					print \"SET sent = \" tb;
					print \"END\"
					print \"BEGIN ap_packets.${dev}\"
					print \"SET received = \" rp;
					print \"SET sent = \" tp;
					print \"END\"
					print \"BEGIN ap_issues.${dev}\"
					print \"SET retries = \" tr;
					print \"SET failures = \" tf;
					print \"END\"
					print \"BEGIN ap_signal.${dev}\"
					print \"SET signal = \" s / c;
					print \"END\"

					if( c == 0 ) c = 1;
					print \"BEGIN ap_bitrate.${dev}\"
					print \"SET receive = \" rt / c;
					print \"SET transmit = \" tt / c;
					print \"SET expected = \" e / c;
					print \"END\"
				}
				"
	done

	return 0
}

