#!/usr/bin/env bash
#shellcheck disable=SC2164

# this script will uninstall netdata

# Variables needed by script:
#  - PATH
#  - CFLAGS
#  - NETDATA_CONFIGURE_OPTIONS
#  - REINSTALL_COMMAND
#  - NETDATA_TARBALL_URL
#  - NETDATA_TARBALL_CHECKSUM_URL
#  - NETDATA_TARBALL_CHECKSUM

info() {
	echo >&3 "$(date) : INFO: " "${@}"
}

error() {
	echo >&3 "$(date) : ERROR: " "${@}"
}

safe_sha256sum() {
	# Within the contexct of the installer, we only use -c option that is common between the two commands
	# We will have to reconsider if we start non-common options
	if command -v sha256sum >/dev/null 2>&1; then
		sha256sum $@
	elif command -v shasum >/dev/null 2>&1; then
		shasum -a 256 $@
	else
		fatal "I could not find a suitable checksum binary to use"
	fi
}

# this is what we will do if it fails (head-less only)
fatal() {
	error "FAILED TO UPDATE NETDATA : ${1}"

	if [ -n "${logfile}" ]; then
		cat >&2 "${logfile}"
		rm "${logfile}"
	fi
	exit 1
}

create_tmp_directory() {
	# Check if tmp is mounted as noexec
	if grep -Eq '^[^ ]+ /tmp [^ ]+ ([^ ]*,)?noexec[, ]' /proc/mounts; then
		pattern="$(pwd)/netdata-updater-XXXXXX"
	else
		pattern="/tmp/netdata-updater-XXXXXX"
	fi

	mktemp -d "$pattern"
}

download() {
	url="${1}"
	dest="${2}"
	if command -v curl >/dev/null 2>&1; then
		curl -sSL --connect-timeout 10 --retry 3 "${url}" >"${dest}" || fatal "Cannot download ${url}"
	elif command -v wget >/dev/null 2>&1; then
		wget -T 15 -O - "${url}" >"${dest}" || fatal "Cannot download ${url}"
	else
		fatal "I need curl or wget to proceed, but neither is available on this system."
	fi
}

set_tarball_urls() {
	if [ "$1" == "stable" ]; then
		local latest
		# Simple version
		# latest="$(curl -sSL https://api.github.com/repos/netdata/netdata/releases/latest | grep tag_name | cut -d'"' -f4)"
		latest="$(download "https://api.github.com/repos/netdata/netdata/releases/latest" /dev/stdout | grep tag_name | cut -d'"' -f4)"
		export NETDATA_TARBALL_URL="https://github.com/netdata/netdata/releases/download/$latest/netdata-$latest.tar.gz"
		export NETDATA_TARBALL_CHECKSUM_URL="https://github.com/netdata/netdata/releases/download/$latest/sha256sums.txt"
	else
		export NETDATA_TARBALL_URL="https://storage.googleapis.com/netdata-nightlies/netdata-latest.tar.gz"
		export NETDATA_TARBALL_CHECKSUM_URL="https://storage.googleapis.com/netdata-nightlies/sha256sums.txt"
	fi
}

update() {
	[ -z "${logfile}" ] && info "Running on a terminal - (this script also supports running headless from crontab)"

	dir=$(create_tmp_directory)
	cd "$dir"

	download "${NETDATA_TARBALL_CHECKSUM_URL}" "${dir}/sha256sum.txt" >&3 2>&3
	if grep "${NETDATA_TARBALL_CHECKSUM}" sha256sum.txt >&3 2>&3; then
		info "Newest version is already installed"
	else
		download "${NETDATA_TARBALL_URL}" "${dir}/netdata-latest.tar.gz"
		if ! grep netdata-latest.tar.gz sha256sum.txt | safe_sha256sum -c - >&3 2>&3; then
			fatal "Tarball checksum validation failed. Stopping netdata upgrade and leaving tarball in ${dir}"
		fi
		NEW_CHECKSUM="$(safe_sha256sum netdata-latest.tar.gz 2>/dev/null| cut -d' ' -f1)"
		tar -xf netdata-latest.tar.gz >&3 2>&3
		rm netdata-latest.tar.gz >&3 2>&3
		cd netdata-*

		# signal netdata to start saving its database
		# this is handy if your database is big
		pids=$(pidof netdata)
		do_not_start=
		if [ -n "${pids}" ]; then
			#shellcheck disable=SC2086
			kill -USR1 ${pids}
		else
			# netdata is currently not running, so do not start it after updating
			do_not_start="--dont-start-it"
		fi

		info "Re-installing netdata..."
		eval "${REINSTALL_COMMAND} --dont-wait ${do_not_start}" >&3 2>&3 || fatal "FAILED TO COMPILE/INSTALL NETDATA"
		sed -i '/NETDATA_TARBALL/d' "${ENVIRONMENT_FILE}"
		cat <<EOF >>"${ENVIRONMENT_FILE}"
NETDATA_TARBALL_CHECKSUM="$NEW_CHECKSUM"
EOF
	fi

	rm -rf "${dir}" >&3 2>&3
	[ -n "${logfile}" ] && rm "${logfile}" && logfile=
	return 0
}

# Usually stored in /etc/netdata/.environment
: "${ENVIRONMENT_FILE:=THIS_SHOULD_BE_REPLACED_BY_INSTALLER_SCRIPT}"

# shellcheck source=/dev/null
source "${ENVIRONMENT_FILE}" || exit 1

if [ "${INSTALL_UID}" != "$(id -u)" ]; then
	fatal "You are running this script as user with uid $(id -u). We recommend to run this script as root (user with uid 0)"
fi

logfile=
if [ -t 2 ]; then
	# we are running on a terminal
	# open fd 3 and send it to stderr
	exec 3>&2
else
	# we are headless
	# create a temporary file for the log
	logfile=$(mktemp ${logfile}/netdata-updater.log.XXXXXX)
	# open fd 3 and send it to logfile
	exec 3>"${logfile}"
fi

set_tarball_urls "${RELEASE_CHANNEL}"

# the installer updates this script - so we run and exit in a single line
update && exit 0
