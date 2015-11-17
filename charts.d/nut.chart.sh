#!/bin/sh

nut_ups=
nut_update_every=2

nut_check() {
	local x all

	require_cmd upsc || return 1
	require_cmd awk  || return 1

	all="$nut_ups"
	[ -z "$all" ] && all="$(upsc -l)"

	nut_ups=
	for x in $all
	do
		upsc "$x" >/dev/null
		[ $? -eq 0 ] && nut_ups="$nut_ups $x" && continue
		echo >&2 "nut: ERROR: Cannot get information for NUT UPS '$x'."
	done

	if [ -z "$nut_ups" ]
		then
		echo >&2 "nut: Please set nut_ups='ups_name' in $confd/nut.conf"
		return 1
	fi

	return 0
}

nut_create() {
	# create the charts
	local x

	for x in $nut_ups
	do
		cat <<EOF
CHART nut_$x.charge '' "UPS Charge" "percentage" $x nut area 21001 $nut_update_every
DIMENSION battery_charge charge absolute 1 100

CHART nut_$x.battery_voltage '' "UPS Battery Voltage" "Volts" $x nut line 21002 $nut_update_every
DIMENSION battery_voltage voltage absolute 1 100
DIMENSION battery_voltage_high high absolute 1 100
DIMENSION battery_voltage_low low absolute 1 100
DIMENSION battery_voltage_nominal nominal absolute 1 100

CHART nut_$x.input_voltage '' "UPS Input Voltage" "Volts" $x nut line 21003 $nut_update_every
DIMENSION input_voltage voltage absolute 1 100
DIMENSION input_voltage_fault fault absolute 1 100
DIMENSION input_voltage_nominal nominal absolute 1 100

CHART nut_$x.input_current '' "UPS Input Current" "Ampere" $x nut line 21004 $nut_update_every
DIMENSION input_current_nominal nominal absolute 1 100

CHART nut_$x.input_frequency '' "UPS Input Frequency" "Hz" $x nut line 21005 $nut_update_every
DIMENSION input_frequency frequency absolute 1 100
DIMENSION input_frequency_nominal nominal absolute 1 100

CHART nut_$x.output_voltage '' "UPS Output Voltage" "Volts" $x nut line 21006 $nut_update_every
DIMENSION output_voltage voltage absolute 1 100

CHART nut_$x.load '' "UPS Load" "percentage" $x nut area 21000 $nut_update_every
DIMENSION load load absolute 1 100

CHART nut_$x.temp '' "UPS Temperature" "temperature" $x nut line 21007 $nut_update_every
DIMENSION temp temp absolute 1 100
EOF
	done

	return 0
}


nut_update() {
	# the first argument to this function is the microseconds since last update
	# pass this parameter to the BEGIN statement (see bellow).

	# do all the work to collect / calculate the values
	# for each dimension
	# remember: KEEP IT SIMPLE AND SHORT

	local x

	for x in $nut_ups
	do
		upsc "$x" | awk "
BEGIN {
	battery_charge = 0;
	battery_voltage = 0;
	battery_voltage_high = 0;
	battery_voltage_low = 0;
	battery_voltage_nominal = 0;
	input_voltage = 0;
	input_voltage_fault = 0;
	input_voltage_nominal = 0;
	input_current_nominal = 0;
	input_frequency = 0;
	input_frequency_nominal = 0;
	output_voltage = 0;
	load = 0;
	temp = 0;
}
/^battery.charge: .*/			{ battery_charge = \$2 * 100 };
/^battery.voltage: .*/			{ battery_voltage = \$2 * 100 };
/^battery.voltage.high: .*/		{ battery_voltage_high = \$2 * 100 };
/^battery.voltage.low: .*/		{ battery_voltage_low = \$2 * 100 };
/^battery.voltage.nominal: .*/	{ battery_voltage_nominal = \$2 * 100 };
/^input.voltage: .*/			{ input_voltage = \$2 * 100 };
/^input.voltage.fault: .*/		{ input_voltage_fault = \$2 * 100 };
/^input.voltage.nominal: .*/	{ input_voltage_nominal = \$2 * 100 };
/^input.current.nominal: .*/	{ input_current_nominal = \$2 * 100 };
/^input.frequency: .*/			{ input_frequency = \$2 * 100 };
/^input.frequency.nominal: .*/	{ input_frequency_nominal = \$2 * 100 };
/^output.voltage: .*/			{ output_voltage = \$2 * 100 };
/^ups.load: .*/					{ load = \$2 * 100 };
/^ups.temperature: .*/			{ temp = \$2 * 100 };
END {
	print \"BEGIN nut_$x.charge $1\";
	print \"SET battery_charge = \" battery_charge;
	print \"END\"

	print \"BEGIN nut_$x.battery_voltage $1\";
	print \"SET battery_voltage = \" battery_voltage;
	print \"SET battery_voltage_high = \" battery_voltage_high;
	print \"SET battery_voltage_low = \" battery_voltage_low;
	print \"SET battery_voltage_nominal = \" battery_voltage_nominal;
	print \"END\"

	print \"BEGIN nut_$x.input_voltage $1\";
	print \"SET input_voltage = \" input_voltage;
	print \"SET input_voltage_fault = \" input_voltage_fault;
	print \"SET input_voltage_nominal = \" input_voltage_nominal;
	print \"END\"

	print \"BEGIN nut_$x.input_current $1\";
	print \"SET input_current_nominal = \" input_current_nominal;
	print \"END\"

	print \"BEGIN nut_$x.input_frequency $1\";
	print \"SET input_frequency = \" input_frequency;
	print \"SET input_frequency_nominal = \" input_frequency_nominal;
	print \"END\"

	print \"BEGIN nut_$x.output_voltage $1\";
	print \"SET output_voltage = \" output_voltage;
	print \"END\"

	print \"BEGIN nut_$x.load $1\";
	print \"SET load = \" load;
	print \"END\"

	print \"BEGIN nut_$x.temp $1\";
	print \"SET temp = \" temp;
	print \"END\"
}
"
	done
	
	return 0
}

