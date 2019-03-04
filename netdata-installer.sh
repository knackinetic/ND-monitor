#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# shellcheck disable=SC1090,SC1091,SC1117,SC2002,SC2034,SC2044,SC2046,SC2086,SC2129,SC2162,SC2166,SC2181

export PATH="${PATH}:/sbin:/usr/sbin:/usr/local/bin:/usr/local/sbin"
uniquepath() {
	local path=""
	while read; do
		if [[ ! ${path} =~ (^|:)"${REPLY}"(:|$) ]]; then
			[ ! -z "${path}" ] && path="${path}:"
			path="${path}${REPLY}"
		fi
	done < <(echo "${PATH}" | tr ":" "\n")

	[ ! -z "${path}" ] && [[ ${PATH} =~ /bin ]] && [[ ${PATH} =~ /sbin ]] && export PATH="${path}"
}
uniquepath

netdata_source_dir="$(pwd)"
installer_dir="$(dirname "${0}")"

if [ "${netdata_source_dir}" != "${installer_dir}" -a "${installer_dir}" != "." ]; then
	echo >&2 "Warning: you are currently in '${netdata_source_dir}' but the installer is in '${installer_dir}'."
fi

# -----------------------------------------------------------------------------
# reload the user profile

[ -f /etc/profile ] && . /etc/profile

# make sure /etc/profile does not change our current directory
cd "${netdata_source_dir}" || exit 1

# -----------------------------------------------------------------------------
# load the required functions

if [ -f "${installer_dir}/packaging/installer/functions.sh" ]; then
	source "${installer_dir}/packaging/installer/functions.sh" || exit 1
else
	source "${netdata_source_dir}/packaging/installer/functions.sh" || exit 1
fi

download() {
	url="${1}"
	dest="${2}"
	if command -v curl >/dev/null 2>&1; then
		run curl -sSL --connect-timeout 10 --retry 3 "${url}" >"${dest}" || fatal "Cannot download ${url}"
	elif command -v wget >/dev/null 2>&1; then
		run wget -T 15 -O - "${url}" >"${dest}" || fatal "Cannot download ${url}"
	else
		fatal "I need curl or wget to proceed, but neither is available on this system."
	fi
}

# make sure we save all commands we run
run_logfile="netdata-installer.log"

# -----------------------------------------------------------------------------
# fix PKG_CHECK_MODULES error

if [ -d /usr/share/aclocal ]; then
	ACLOCAL_PATH=${ACLOCAL_PATH-/usr/share/aclocal}
	export ACLOCAL_PATH
fi

export LC_ALL=C
umask 002

# Be nice on production environments
renice 19 $$ >/dev/null 2>/dev/null

# you can set CFLAGS before running installer
CFLAGS="${CFLAGS--O2}"
[ "z${CFLAGS}" = "z-O3" ] && CFLAGS="-O2"

# keep a log of this command
printf "\n# " >>netdata-installer.log
date >>netdata-installer.log
printf 'CFLAGS="%s" ' "${CFLAGS}" >>netdata-installer.log
printf "%q " "$0" "${@}" >>netdata-installer.log
printf "\n" >>netdata-installer.log

REINSTALL_PWD="${PWD}"
REINSTALL_COMMAND="$(
	printf "%q " "$0" "${@}"
	printf "\n"
)"
# remove options that shown not be inherited by netdata-updater.sh
REINSTALL_COMMAND="${REINSTALL_COMMAND// --dont-wait/}"
REINSTALL_COMMAND="${REINSTALL_COMMAND// --dont-start-it/}"
[ "${REINSTALL_COMMAND:0:1}" != "." -a "${REINSTALL_COMMAND:0:1}" != "/" -a -f "./${0}" ] && REINSTALL_COMMAND="./${REINSTALL_COMMAND}"

# shellcheck disable=SC2230
setcap="$(which setcap 2>/dev/null || command -v setcap 2>/dev/null)"

ME="$0"
DONOTSTART=0
DONOTWAIT=0
AUTOUPDATE=0
NETDATA_PREFIX=
LIBS_ARE_HERE=0
NETDATA_CONFIGURE_OPTIONS="${NETDATA_CONFIGURE_OPTIONS-}"
RELEASE_CHANNEL="nightly"

usage() {
	netdata_banner "installer command line options"
	cat <<USAGE

${ME} <installer options>

Valid <installer options> are:

   --install /PATH/TO/INSTALL

        If you give: --install /opt
        netdata will be installed in /opt/netdata

   --dont-start-it

        Do not (re)start netdata.
        Just install it.

   --dont-wait

        Do not wait for the user to press ENTER.
        Start immediately building it.

   --auto-update | -u

        Install netdata-updater to cron,
        to update netdata automatically once per day

   --stable-channel

        Auto-updater will update netdata only when new release is published
        in GitHub release pages. This results in less frequent updates.
        Default: Use packages from GCS (nightly release channel).

   --enable-plugin-freeipmi
   --disable-plugin-freeipmi

        Enable/disable the FreeIPMI plugin.
        Default: enable it when libipmimonitoring is available.

   --enable-plugin-nfacct
   --disable-plugin-nfacct

        Enable/disable the nfacct plugin.
        Default: enable it when libmnl and libnetfilter_acct are available.

   --enable-lto
   --disable-lto

        Enable/disable Link-Time-Optimization
        Default: enabled

   --disable-x86-sse

        Disable SSE instructions
        Default: enabled

   --zlib-is-really-here
   --libs-are-really-here

        If you get errors about missing zlib,
        or libuuid but you know it is available,
        you have a broken pkg-config.
        Use this option to allow it continue
        without checking pkg-config.

   --disable-telemetry

        Use this flag to opt-out from our anonymous telemetry progam.

   --disable-go

        Flag to disable installation of go.d.plugin

Netdata will by default be compiled with gcc optimization -O2
If you need to pass different CFLAGS, use something like this:

  CFLAGS="<gcc options>" ${ME} <installer options>

For the installer to complete successfully, you will need
these packages installed:

   gcc make autoconf automake pkg-config zlib1g-dev (or zlib-devel)
   uuid-dev (or libuuid-devel)

For the plugins, you will at least need:

   curl, bash v4+, python v2 or v3, node.js

USAGE
}

while [ ! -z "${1}" ]; do
	if [ "$1" = "--install" ]; then
		NETDATA_PREFIX="${2}/netdata"
		shift 2
	elif [ "$1" = "--zlib-is-really-here" -o "$1" = "--libs-are-really-here" ]; then
		LIBS_ARE_HERE=1
		shift 1
	elif [ "$1" = "--dont-start-it" ]; then
		DONOTSTART=1
		shift 1
	elif [ "$1" = "--dont-wait" ]; then
		DONOTWAIT=1
		shift 1
	elif [ "$1" = "--auto-update" -o "$1" = "-u" ]; then
		AUTOUPDATE=1
		shift 1
	elif [ "$1" = "--stable-channel" ]; then
		RELEASE_CHANNEL="stable"
		shift 1
	elif [ "$1" = "--enable-plugin-freeipmi" ]; then
		NETDATA_CONFIGURE_OPTIONS="${NETDATA_CONFIGURE_OPTIONS//--enable-plugin-freeipmi/} --enable-plugin-freeipmi"
		shift 1
	elif [ "$1" = "--disable-plugin-freeipmi" ]; then
		NETDATA_CONFIGURE_OPTIONS="${NETDATA_CONFIGURE_OPTIONS//--disable-plugin-freeipmi/} --disable-plugin-freeipmi"
		shift 1
	elif [ "$1" = "--enable-plugin-nfacct" ]; then
		NETDATA_CONFIGURE_OPTIONS="${NETDATA_CONFIGURE_OPTIONS//--enable-plugin-nfacct/} --enable-plugin-nfacct"
		shift 1
	elif [ "$1" = "--disable-plugin-nfacct" ]; then
		NETDATA_CONFIGURE_OPTIONS="${NETDATA_CONFIGURE_OPTIONS//--disable-plugin-nfacct/} --disable-plugin-nfacct"
		shift 1
	elif [ "$1" = "--enable-lto" ]; then
		NETDATA_CONFIGURE_OPTIONS="${NETDATA_CONFIGURE_OPTIONS//--enable-lto/} --enable-lto"
		shift 1
	elif [ "$1" = "--disable-lto" ]; then
		NETDATA_CONFIGURE_OPTIONS="${NETDATA_CONFIGURE_OPTIONS//--disable-lto/} --disable-lto"
		shift 1
	elif [ "$1" = "--disable-x86-sse" ]; then
		NETDATA_CONFIGURE_OPTIONS="${NETDATA_CONFIGURE_OPTIONS//--disable-x86-sse/} --disable-x86-sse"
		shift 1
	elif [ "$1" = "--disable-telemetry" ]; then
		NETDATA_DISABLE_TELEMETRY=1
		shift 1
	elif [ "$1" = "--disable-go" ]; then
		NETDATA_DISABLE_GO=1
		shift 1
	elif [ "$1" = "--help" -o "$1" = "-h" ]; then
		usage
		exit 1
	else
		echo >&2
		echo >&2 "ERROR:"
		echo >&2 "I cannot understand option '$1'."
		usage
		exit 1
	fi
done

# replace multiple spaces with a single space
NETDATA_CONFIGURE_OPTIONS="${NETDATA_CONFIGURE_OPTIONS//  / }"

netdata_banner "real-time performance monitoring, done right!"
cat <<BANNER1

  You are about to build and install netdata to your system.

  It will be installed at these locations:

   - the daemon     at ${TPUT_CYAN}${NETDATA_PREFIX}/usr/sbin/netdata${TPUT_RESET}
   - config files   in ${TPUT_CYAN}${NETDATA_PREFIX}/etc/netdata${TPUT_RESET}
   - web files      in ${TPUT_CYAN}${NETDATA_PREFIX}/usr/share/netdata${TPUT_RESET}
   - plugins        in ${TPUT_CYAN}${NETDATA_PREFIX}/usr/libexec/netdata${TPUT_RESET}
   - cache files    in ${TPUT_CYAN}${NETDATA_PREFIX}/var/cache/netdata${TPUT_RESET}
   - db files       in ${TPUT_CYAN}${NETDATA_PREFIX}/var/lib/netdata${TPUT_RESET}
   - log files      in ${TPUT_CYAN}${NETDATA_PREFIX}/var/log/netdata${TPUT_RESET}
BANNER1

[ "${UID}" -eq 0 ] && cat <<BANNER2
   - pid file       at ${TPUT_CYAN}${NETDATA_PREFIX}/var/run/netdata.pid${TPUT_RESET}
   - logrotate file at ${TPUT_CYAN}/etc/logrotate.d/netdata${TPUT_RESET}
BANNER2

cat <<BANNER3

  This installer allows you to change the installation path.
  Press Control-C and run the same command with --help for help.

BANNER3

if [ "${UID}" -ne 0 ]; then
	if [ -z "${NETDATA_PREFIX}" ]; then
		netdata_banner "wrong command line options!"
		cat <<NONROOTNOPREFIX

  ${TPUT_RED}${TPUT_BOLD}Sorry! This will fail!${TPUT_RESET}

  You are attempting to install netdata as non-root, but you plan
  to install it in system paths.

  Please set an installation prefix, like this:

      $0 ${@} --install /tmp

  or, run the installer as root:

      sudo $0 ${@}

  We suggest to install it as root, or certain data collectors will
  not be able to work. Netdata drops root privileges when running.
  So, if you plan to keep it, install it as root to get the full
  functionality.

NONROOTNOPREFIX
		exit 1

	else
		cat <<NONROOT

  ${TPUT_RED}${TPUT_BOLD}IMPORTANT${TPUT_RESET}:
  You are about to install netdata as a non-root user.
  Netdata will work, but a few data collection modules that
  require root access will fail.

  If you installing netdata permanently on your system, run
  the installer like this:

     ${TPUT_YELLOW}${TPUT_BOLD}sudo $0 ${@}${TPUT_RESET}

NONROOT
	fi
fi

have_autotools=
if [ "$(type autoreconf 2>/dev/null)" ]; then
	autoconf_maj_min() {
		local maj min IFS=.-

		maj=$1
		min=$2

		set -- $(autoreconf -V | sed -ne '1s/.* \([^ ]*\)$/\1/p')
		eval $maj=\$1 $min=\$2
	}
	autoconf_maj_min AMAJ AMIN

	if [ "$AMAJ" -gt 2 ]; then
		have_autotools=Y
	elif [ "$AMAJ" -eq 2 -a "$AMIN" -ge 60 ]; then
		have_autotools=Y
	else
		echo "Found autotools $AMAJ.$AMIN"
	fi
else
	echo "No autotools found"
fi

if [ ! "$have_autotools" ]; then
	if [ -f configure ]; then
		echo "Will skip autoreconf step"
	else
		netdata_banner "autotools v2.60 required"
		cat <<"EOF"

-------------------------------------------------------------------------------
autotools 2.60 or later is required

Sorry, you do not seem to have autotools 2.60 or later, which is
required to build from the git sources of netdata.

EOF
		exit 1
	fi
fi

if [ ${DONOTWAIT} -eq 0 ]; then
	if [ ! -z "${NETDATA_PREFIX}" ]; then
		eval "read >&2 -ep \$'\001${TPUT_BOLD}${TPUT_GREEN}\002Press ENTER to build and install netdata to \'\001${TPUT_CYAN}\002${NETDATA_PREFIX}\001${TPUT_YELLOW}\002\'\001${TPUT_RESET}\002 > ' -e -r REPLY"
		[ $? -ne 0 ] && exit 1
	else
		eval "read >&2 -ep \$'\001${TPUT_BOLD}${TPUT_GREEN}\002Press ENTER to build and install netdata to your system\001${TPUT_RESET}\002 > ' -e -r REPLY"
		[ $? -ne 0 ] && exit 1
	fi
fi

build_error() {
	netdata_banner "sorry, it failed to build..."
	cat <<EOF

^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Sorry! netdata failed to build...

You may need to check these:

1. The package uuid-dev (or libuuid-devel) has to be installed.

   If your system cannot find libuuid, although it is installed
   run me with the option:  --libs-are-really-here

2. The package zlib1g-dev (or zlib-devel) has to be installed.

   If your system cannot find zlib, although it is installed
   run me with the option:  --libs-are-really-here

3. You need basic build tools installed, like:

   gcc make autoconf automake pkg-config

   Autoconf version 2.60 or higher is required.

If you still cannot get it to build, ask for help at github:

   https://github.com/netdata/netdata/issues


EOF
	trap - EXIT
	exit 1
}

if [ ${LIBS_ARE_HERE} -eq 1 ]; then
	shift
	echo >&2 "ok, assuming libs are really installed."
	export ZLIB_CFLAGS=" "
	export ZLIB_LIBS="-lz"
	export UUID_CFLAGS=" "
	export UUID_LIBS="-luuid"
fi

trap build_error EXIT

# -----------------------------------------------------------------------------
echo >&2
progress "Run autotools to configure the build environment"

if [ "$have_autotools" ]; then
	run autoreconf -ivf || exit 1
fi

run ./configure \
	--prefix="${NETDATA_PREFIX}/usr" \
	--sysconfdir="${NETDATA_PREFIX}/etc" \
	--localstatedir="${NETDATA_PREFIX}/var" \
	--with-zlib \
	--with-math \
	--with-user=netdata \
	${NETDATA_CONFIGURE_OPTIONS} \
	CFLAGS="${CFLAGS}" || exit 1

# remove the build_error hook
trap - EXIT

# -----------------------------------------------------------------------------
progress "Cleanup compilation directory"

run make clean

# -----------------------------------------------------------------------------
progress "Compile netdata"

run make -j${SYSTEM_CPUS} || exit 1

# -----------------------------------------------------------------------------
progress "Migrate configuration files for node.d.plugin and charts.d.plugin"

# migrate existing configuration files
# for node.d and charts.d
if [ -d "${NETDATA_PREFIX}/etc/netdata" ]; then
	# the configuration directory exists

	if [ ! -d "${NETDATA_PREFIX}/etc/netdata/charts.d" ]; then
		run mkdir "${NETDATA_PREFIX}/etc/netdata/charts.d"
	fi

	# move the charts.d config files
	for x in apache ap cpu_apps cpufreq example exim hddtemp load_average mem_apps mysql nginx nut opensips phpfpm postfix sensors squid tomcat; do
		for y in "" ".old" ".orig"; do
			if [ -f "${NETDATA_PREFIX}/etc/netdata/${x}.conf${y}" -a ! -f "${NETDATA_PREFIX}/etc/netdata/charts.d/${x}.conf${y}" ]; then
				run mv -f "${NETDATA_PREFIX}/etc/netdata/${x}.conf${y}" "${NETDATA_PREFIX}/etc/netdata/charts.d/${x}.conf${y}"
			fi
		done
	done

	if [ ! -d "${NETDATA_PREFIX}/etc/netdata/node.d" ]; then
		run mkdir "${NETDATA_PREFIX}/etc/netdata/node.d"
	fi

	# move the node.d config files
	for x in named sma_webbox snmp; do
		for y in "" ".old" ".orig"; do
			if [ -f "${NETDATA_PREFIX}/etc/netdata/${x}.conf${y}" -a ! -f "${NETDATA_PREFIX}/etc/netdata/node.d/${x}.conf${y}" ]; then
				run mv -f "${NETDATA_PREFIX}/etc/netdata/${x}.conf${y}" "${NETDATA_PREFIX}/etc/netdata/node.d/${x}.conf${y}"
			fi
		done
	done
fi

# -----------------------------------------------------------------------------

# shellcheck disable=SC2230
md5sum="$(which md5sum 2>/dev/null || command -v md5sum 2>/dev/null || command -v md5 2>/dev/null)"

deleted_stock_configs=0
if [ ! -f "${NETDATA_PREFIX}/etc/netdata/.installer-cleanup-of-stock-configs-done" ]; then

	progress "Backup existing netdata configuration before installing it"

	if [ "${BASH_VERSINFO[0]}" -ge "4" ]; then
		declare -A configs_signatures=()
		if [ -f "configs.signatures" ]; then
			source "configs.signatures" || echo >&2 "ERROR: Failed to load configs.signatures !"
		fi
	fi

	config_signature_matches() {
		local md5="${1}" file="${2}"

		if [ "${BASH_VERSINFO[0]}" -ge "4" ]; then
			[ "${configs_signatures[${md5}]}" = "${file}" ] && return 0
			return 1
		fi

		if [ -f "configs.signatures" ]; then
			grep "\['${md5}'\]='${file}'" "configs.signatures" >/dev/null
			return $?
		fi

		return 1
	}

	# clean up stock config files from the user configuration directory
	for x in $(find -L "${NETDATA_PREFIX}/etc/netdata" -type f); do
		if [ -f "${x}" ]; then
			# find it relative filename
			f="${x/${NETDATA_PREFIX}\/etc\/netdata\//}"

			# find the stock filename
			t="${f/.conf.installer_backup.*/.conf}"
			t="${t/.conf.old/.conf}"
			t="${t/.conf.orig/.conf}"

			if [ -z "${md5sum}" -o ! -x "${md5sum}" ]; then
				# we don't have md5sum - keep it
				echo >&2 "File '${TPUT_CYAN}${x}${TPUT_RESET}' ${TPUT_RET}is not known to distribution${TPUT_RESET}. Keeping it."
			else
				# find its checksum
				md5="$(${md5sum} <"${x}" | cut -d ' ' -f 1)"

				if config_signature_matches "${md5}" "${t}"; then
					# it is a stock version - remove it
					echo >&2 "File '${TPUT_CYAN}${x}${TPUT_RESET}' is stock version of '${t}'."
					run rm -f "${x}"
					deleted_stock_configs=$((deleted_stock_configs + 1))
				else
					# edited by user - keep it
					echo >&2 "File '${TPUT_CYAN}${x}${TPUT_RESET}' ${TPUT_RED} does not match stock of '${t}'${TPUT_RESET}. Keeping it."
				fi
			fi
		fi
	done
fi
touch "${NETDATA_PREFIX}/etc/netdata/.installer-cleanup-of-stock-configs-done"

# -----------------------------------------------------------------------------
progress "Install netdata"

run make install || exit 1

# -----------------------------------------------------------------------------
progress "Fix generated files permissions"

run find ./system/ -type f -a \! -name \*.in -a \! -name Makefile\* -a \! -name \*.conf -a \! -name \*.service -a \! -name \*.logrotate -exec chmod 755 {} \;

# -----------------------------------------------------------------------------
progress "Add user netdata to required user groups"

homedir="${NETDATA_PREFIX}/var/lib/netdata"
[ ! -z "${NETDATA_PREFIX}" ] && homedir="${NETDATA_PREFIX}"
add_netdata_user_and_group "${homedir}" || run_failed "The installer does not run as root."

# -----------------------------------------------------------------------------
progress "Install logrotate configuration for netdata"

install_netdata_logrotate

# -----------------------------------------------------------------------------
progress "Read installation options from netdata.conf"

# create an empty config if it does not exist
[ ! -f "${NETDATA_PREFIX}/etc/netdata/netdata.conf" ] &&
	touch "${NETDATA_PREFIX}/etc/netdata/netdata.conf"

# function to extract values from the config file
config_option() {
	local section="${1}" key="${2}" value="${3}"

	if [ -s "${NETDATA_PREFIX}/etc/netdata/netdata.conf" ]; then
		"${NETDATA_PREFIX}/usr/sbin/netdata" \
			-c "${NETDATA_PREFIX}/etc/netdata/netdata.conf" \
			-W get "${section}" "${key}" "${value}" ||
			echo "${value}"
	else
		echo "${value}"
	fi
}

# the user netdata will run as
if [ "${UID}" = "0" ]; then
	NETDATA_USER="$(config_option "global" "run as user" "netdata")"
	ROOT_USER="root"
else
	NETDATA_USER="${USER}"
	ROOT_USER="${NETDATA_USER}"
fi
NETDATA_GROUP="$(id -g -n ${NETDATA_USER})"
[ -z "${NETDATA_GROUP}" ] && NETDATA_GROUP="${NETDATA_USER}"

# the owners of the web files
NETDATA_WEB_USER="$(config_option "web" "web files owner" "${NETDATA_USER}")"
NETDATA_WEB_GROUP="${NETDATA_GROUP}"
if [ "${UID}" = "0" -a "${NETDATA_USER}" != "${NETDATA_WEB_USER}" ]; then
	NETDATA_WEB_GROUP="$(id -g -n ${NETDATA_WEB_USER})"
	[ -z "${NETDATA_WEB_GROUP}" ] && NETDATA_WEB_GROUP="${NETDATA_WEB_USER}"
fi
NETDATA_WEB_GROUP="$(config_option "web" "web files group" "${NETDATA_WEB_GROUP}")"

# port
defport=19999
NETDATA_PORT="$(config_option "web" "default port" ${defport})"

# directories
NETDATA_LIB_DIR="$(config_option "global" "lib directory" "${NETDATA_PREFIX}/var/lib/netdata")"
NETDATA_CACHE_DIR="$(config_option "global" "cache directory" "${NETDATA_PREFIX}/var/cache/netdata")"
NETDATA_WEB_DIR="$(config_option "global" "web files directory" "${NETDATA_PREFIX}/usr/share/netdata/web")"
NETDATA_LOG_DIR="$(config_option "global" "log directory" "${NETDATA_PREFIX}/var/log/netdata")"
NETDATA_USER_CONFIG_DIR="$(config_option "global" "config directory" "${NETDATA_PREFIX}/etc/netdata")"
NETDATA_STOCK_CONFIG_DIR="$(config_option "global" "stock config directory" "${NETDATA_PREFIX}/usr/lib/netdata/conf.d")"
NETDATA_RUN_DIR="${NETDATA_PREFIX}/var/run"

cat <<OPTIONSEOF

    Permissions
    - netdata user             : ${NETDATA_USER}
    - netdata group            : ${NETDATA_GROUP}
    - web files user           : ${NETDATA_WEB_USER}
    - web files group          : ${NETDATA_WEB_GROUP}
    - root user                : ${ROOT_USER}

    Directories
    - netdata user config dir  : ${NETDATA_USER_CONFIG_DIR}
    - netdata stock config dir : ${NETDATA_STOCK_CONFIG_DIR}
    - netdata log dir          : ${NETDATA_LOG_DIR}
    - netdata run dir          : ${NETDATA_RUN_DIR}
    - netdata lib dir          : ${NETDATA_LIB_DIR}
    - netdata web dir          : ${NETDATA_WEB_DIR}
    - netdata cache dir        : ${NETDATA_CACHE_DIR}

    Other
    - netdata port             : ${NETDATA_PORT}

OPTIONSEOF

# -----------------------------------------------------------------------------
progress "Fix permissions of netdata directories (using user '${NETDATA_USER}')"

if [ ! -d "${NETDATA_RUN_DIR}" ]; then
	# this is needed if NETDATA_PREFIX is not empty
	run mkdir -p "${NETDATA_RUN_DIR}" || exit 1
fi

# --- conf dir ----

for x in "python.d" "charts.d" "node.d" "health.d" "statsd.d" "go.d"; do
	if [ ! -d "${NETDATA_USER_CONFIG_DIR}/${x}" ]; then
		echo >&2 "Creating directory '${NETDATA_USER_CONFIG_DIR}/${x}'"
		run mkdir -p "${NETDATA_USER_CONFIG_DIR}/${x}" || exit 1
	fi
done
run chown -R "${ROOT_USER}:${NETDATA_GROUP}" "${NETDATA_USER_CONFIG_DIR}"
run find "${NETDATA_USER_CONFIG_DIR}" -type f -exec chmod 0640 {} \;
run find "${NETDATA_USER_CONFIG_DIR}" -type d -exec chmod 0755 {} \;
run chmod 755 "${NETDATA_USER_CONFIG_DIR}/edit-config"

# --- stock conf dir ----

[ ! -d "${NETDATA_STOCK_CONFIG_DIR}" ] && mkdir -p "${NETDATA_STOCK_CONFIG_DIR}"

helplink="000.-.USE.THE.orig.LINK.TO.COPY.AND.EDIT.STOCK.CONFIG.FILES"
[ ${deleted_stock_configs} -eq 0 ] && helplink=""
for link in "orig" "${helplink}"; do
	if [ ! -z "${link}" ]; then
		[ -L "${NETDATA_USER_CONFIG_DIR}/${link}" ] && run rm -f "${NETDATA_USER_CONFIG_DIR}/${link}"
		run ln -s "${NETDATA_STOCK_CONFIG_DIR}" "${NETDATA_USER_CONFIG_DIR}/${link}"
	fi
done
run chown -R "${ROOT_USER}:${NETDATA_GROUP}" "${NETDATA_STOCK_CONFIG_DIR}"
run find "${NETDATA_STOCK_CONFIG_DIR}" -type f -exec chmod 0640 {} \;
run find "${NETDATA_STOCK_CONFIG_DIR}" -type d -exec chmod 0755 {} \;

# --- web dir ----

if [ ! -d "${NETDATA_WEB_DIR}" ]; then
	echo >&2 "Creating directory '${NETDATA_WEB_DIR}'"
	run mkdir -p "${NETDATA_WEB_DIR}" || exit 1
fi
run chown -R "${NETDATA_WEB_USER}:${NETDATA_WEB_GROUP}" "${NETDATA_WEB_DIR}"
run find "${NETDATA_WEB_DIR}" -type f -exec chmod 0664 {} \;
run find "${NETDATA_WEB_DIR}" -type d -exec chmod 0775 {} \;

# --- data dirs ----

for x in "${NETDATA_LIB_DIR}" "${NETDATA_CACHE_DIR}" "${NETDATA_LOG_DIR}"; do
	if [ ! -d "${x}" ]; then
		echo >&2 "Creating directory '${x}'"
		run mkdir -p "${x}" || exit 1
	fi

	run chown -R "${NETDATA_USER}:${NETDATA_GROUP}" "${x}"
	#run find "${x}" -type f -exec chmod 0660 {} \;
	#run find "${x}" -type d -exec chmod 0770 {} \;
done

run chmod 755 "${NETDATA_LOG_DIR}"

# --- plugins ----

if [ ${UID} -eq 0 ]; then
	# find the admin group
	admin_group=
	test -z "${admin_group}" && getent group root >/dev/null 2>&1 && admin_group="root"
	test -z "${admin_group}" && getent group daemon >/dev/null 2>&1 && admin_group="daemon"
	test -z "${admin_group}" && admin_group="${NETDATA_GROUP}"

	run chown "${NETDATA_USER}:${admin_group}" "${NETDATA_LOG_DIR}"
	run chown -R root "${NETDATA_PREFIX}/usr/libexec/netdata"
	run find "${NETDATA_PREFIX}/usr/libexec/netdata" -type d -exec chmod 0755 {} \;
	run find "${NETDATA_PREFIX}/usr/libexec/netdata" -type f -exec chmod 0644 {} \;
	run find "${NETDATA_PREFIX}/usr/libexec/netdata" -type f -a -name \*.plugin -exec chmod 0755 {} \;
	run find "${NETDATA_PREFIX}/usr/libexec/netdata" -type f -a -name \*.sh -exec chmod 0755 {} \;

	if [ -f "${NETDATA_PREFIX}/usr/libexec/netdata/plugins.d/apps.plugin" ]; then
		setcap_ret=1
		if ! iscontainer; then
			if [ ! -z "${setcap}" ]; then
				run chown root:${NETDATA_GROUP} "${NETDATA_PREFIX}/usr/libexec/netdata/plugins.d/apps.plugin"
				run chmod 0750 "${NETDATA_PREFIX}/usr/libexec/netdata/plugins.d/apps.plugin"
				run setcap cap_dac_read_search,cap_sys_ptrace+ep "${NETDATA_PREFIX}/usr/libexec/netdata/plugins.d/apps.plugin"
				setcap_ret=$?
			fi

			if [ ${setcap_ret} -eq 0 ]; then
				# if we managed to setcap
				# but we fail to execute apps.plugin
				# trigger setuid to root
				"${NETDATA_PREFIX}/usr/libexec/netdata/plugins.d/apps.plugin" -t >/dev/null 2>&1
				setcap_ret=$?
			fi
		fi

		if [ ${setcap_ret} -ne 0 ]; then
			# fix apps.plugin to be setuid to root
			run chown root:${NETDATA_GROUP} "${NETDATA_PREFIX}/usr/libexec/netdata/plugins.d/apps.plugin"
			run chmod 4750 "${NETDATA_PREFIX}/usr/libexec/netdata/plugins.d/apps.plugin"
		fi
	fi

	if [ -f "${NETDATA_PREFIX}/usr/libexec/netdata/plugins.d/freeipmi.plugin" ]; then
		run chown root:${NETDATA_GROUP} "${NETDATA_PREFIX}/usr/libexec/netdata/plugins.d/freeipmi.plugin"
		run chmod 4750 "${NETDATA_PREFIX}/usr/libexec/netdata/plugins.d/freeipmi.plugin"
	fi

    if [ -f "${NETDATA_PREFIX}/usr/libexec/netdata/plugins.d/nfacct.plugin" ]; then
        run chown root:${NETDATA_GROUP} "${NETDATA_PREFIX}/usr/libexec/netdata/plugins.d/nfacct.plugin"
        run chmod 4750 "${NETDATA_PREFIX}/usr/libexec/netdata/plugins.d/nfacct.plugin"
    fi

	if [ -f "${NETDATA_PREFIX}/usr/libexec/netdata/plugins.d/cgroup-network" ]; then
		run chown root:${NETDATA_GROUP} "${NETDATA_PREFIX}/usr/libexec/netdata/plugins.d/cgroup-network"
		run chmod 4750 "${NETDATA_PREFIX}/usr/libexec/netdata/plugins.d/cgroup-network"
	fi

	if [ -f "${NETDATA_PREFIX}/usr/libexec/netdata/plugins.d/cgroup-network-helper.sh" ]; then
		run chown root "${NETDATA_PREFIX}/usr/libexec/netdata/plugins.d/cgroup-network-helper.sh"
		run chmod 0550 "${NETDATA_PREFIX}/usr/libexec/netdata/plugins.d/cgroup-network-helper.sh"
	fi

else
	# non-privileged user installation
	run chown "${NETDATA_USER}:${NETDATA_GROUP}" "${NETDATA_LOG_DIR}"
	run chown -R "${NETDATA_USER}:${NETDATA_GROUP}" "${NETDATA_PREFIX}/usr/libexec/netdata"
	run find "${NETDATA_PREFIX}/usr/libexec/netdata" -type f -exec chmod 0755 {} \;
	run find "${NETDATA_PREFIX}/usr/libexec/netdata" -type d -exec chmod 0755 {} \;
fi

# -----------------------------------------------------------------------------

install_go() {
	# When updating this value, ensure correct checksums in packaging/go.d.checksums
	GO_PACKAGE_VERSION="v0.1.0"
	ARCH_MAP=(
		'i386::386'
		'i686::386'
		'x86_64::amd64'
		'aarch64::arm64'
		'armv64::arm64'
		'armv6l::arm'
		'armv7l::arm'
		'armv5tel::arm'
	)

	if [ -z "${NETDATA_DISABLE_GO+x}" ]; then
		progress "Install go.d.plugin"
		ARCH=$(uname -m)
		OS=$(uname -s | tr '[:upper:]' '[:lower:]')

		for index in "${ARCH_MAP[@]}" ; do
			KEY="${index%%::*}"
			VALUE="${index##*::}"
			if [ "$KEY" == "$ARCH" ]; then
				ARCH="${VALUE}"
				break
			fi
		done
		tmp=$(mktemp -d /tmp/netdata-go-XXXXXX)
		GO_PACKAGE_BASENAME="go.d.plugin-$GO_PACKAGE_VERSION.$OS-$ARCH"

		download "https://github.com/netdata/go.d.plugin/releases/download/$GO_PACKAGE_VERSION/$GO_PACKAGE_BASENAME" "${tmp}/$GO_PACKAGE_BASENAME"

		download "https://github.com/netdata/go.d.plugin/releases/download/$GO_PACKAGE_VERSION/config.tar.gz" "${tmp}/config.tar.gz"
		grep "${GO_PACKAGE_BASENAME}\$" "${installer_dir}/packaging/go.d.checksums" > "${tmp}/sha256sums.txt" 2>/dev/null
		grep "config.tar.gz" "${installer_dir}/packaging/go.d.checksums" >> "${tmp}/sha256sums.txt" 2>/dev/null

		# Checksum validation
		if ! (cd "${tmp}" && sha256sum -c "sha256sums.txt"); then
			run_failed "go.d.plugin package files checksum validation failed."
			return 1
		fi

		# Install new files
		run rm -rf "${NETDATA_STOCK_CONFIG_DIR}/go.d"
		run rm -rf "${NETDATA_STOCK_CONFIG_DIR}/go.d.conf"
		run tar -xf "${tmp}/config.tar.gz" -C "${NETDATA_STOCK_CONFIG_DIR}/"
		run chown -R "${ROOT_USER}:${NETDATA_GROUP}" "${NETDATA_STOCK_CONFIG_DIR}"

		run mv "${tmp}/$GO_PACKAGE_BASENAME" "${NETDATA_PREFIX}/usr/libexec/netdata/plugins.d/go.d.plugin"
		if [ ${UID} -eq 0 ]; then
			run chown root:${NETDATA_GROUP} "${NETDATA_PREFIX}/usr/libexec/netdata/plugins.d/go.d.plugin"
		fi
		run chmod 0750 "${NETDATA_PREFIX}/usr/libexec/netdata/plugins.d/go.d.plugin"
	fi
	return 0
}
install_go

# --- fix #1292 bug ---

[ -d "${NETDATA_PREFIX}/usr/libexec" ] && run chmod a+rX "${NETDATA_PREFIX}/usr/libexec"
[ -d "${NETDATA_PREFIX}/usr/share/netdata" ] && run chmod a+rX "${NETDATA_PREFIX}/usr/share/netdata"

# -----------------------------------------------------------------------------
progress "Install netdata at system init"

NETDATA_START_CMD="${NETDATA_PREFIX}/usr/sbin/netdata"
install_netdata_service || run_failed "Cannot install netdata init service."

# -----------------------------------------------------------------------------
# check if we can re-start netdata

started=0
if [ ${DONOTSTART} -eq 1 ]; then
	generate_netdata_conf "${NETDATA_USER}" "${NETDATA_PREFIX}/etc/netdata/netdata.conf" "http://localhost:${NETDATA_PORT}/netdata.conf"

else
	restart_netdata ${NETDATA_PREFIX}/usr/sbin/netdata "${@}"
	if [ $? -ne 0 ]; then
		echo >&2
		echo >&2 "SORRY! FAILED TO START NETDATA!"
		echo >&2
		exit 1
	fi

	started=1
	echo >&2 "OK. NetData Started!"
	echo >&2

	# -----------------------------------------------------------------------------
	# save a config file, if it is not already there

	download_netdata_conf "${NETDATA_USER}" "${NETDATA_PREFIX}/etc/netdata/netdata.conf" "http://localhost:${NETDATA_PORT}/netdata.conf"
fi

if [ "$(uname)" = "Linux" ]; then
	# -------------------------------------------------------------------------
	progress "Check KSM (kernel memory deduper)"

	ksm_is_available_but_disabled() {
		cat <<KSM1

${TPUT_BOLD}Memory de-duplication instructions${TPUT_RESET}

You have kernel memory de-duper (called Kernel Same-page Merging,
or KSM) available, but it is not currently enabled.

To enable it run:

    ${TPUT_YELLOW}${TPUT_BOLD}echo 1 >/sys/kernel/mm/ksm/run${TPUT_RESET}
    ${TPUT_YELLOW}${TPUT_BOLD}echo 1000 >/sys/kernel/mm/ksm/sleep_millisecs${TPUT_RESET}

If you enable it, you will save 40-60% of netdata memory.

KSM1
	}

	ksm_is_not_available() {
		cat <<KSM2

${TPUT_BOLD}Memory de-duplication not present in your kernel${TPUT_RESET}

It seems you do not have kernel memory de-duper (called Kernel Same-page
Merging, or KSM) available.

To enable it, you need a kernel built with CONFIG_KSM=y

If you can have it, you will save 40-60% of netdata memory.

KSM2
	}

	if [ -f "/sys/kernel/mm/ksm/run" ]; then
		if [ $(cat "/sys/kernel/mm/ksm/run") != "1" ]; then
			ksm_is_available_but_disabled
		fi
	else
		ksm_is_not_available
	fi
fi

# -----------------------------------------------------------------------------
progress "Check version.txt"

if [ ! -s web/gui/version.txt ]; then
	cat <<VERMSG

${TPUT_BOLD}Version update check warning${TPUT_RESET}

The way you downloaded netdata, we cannot find its version. This means the
Update check on the dashboard, will not work.

If you want to have version update check, please re-install it
following the procedure in:

https://docs.netdata.cloud/packaging/installer/

VERMSG
fi

if [ -f "${NETDATA_PREFIX}/usr/libexec/netdata/plugins.d/apps.plugin" ]; then
	# -----------------------------------------------------------------------------
	progress "Check apps.plugin"

	if [ "${UID}" -ne 0 ]; then
		cat <<SETUID_WARNING

${TPUT_BOLD}apps.plugin needs privileges${TPUT_RESET}

Since you have installed netdata as a normal user, to have apps.plugin collect
all the needed data, you have to give it the access rights it needs, by running
either of the following sets of commands:

To run apps.plugin with escalated capabilities:

    ${TPUT_YELLOW}${TPUT_BOLD}sudo chown root:${NETDATA_GROUP} \"${NETDATA_PREFIX}/usr/libexec/netdata/plugins.d/apps.plugin\"${TPUT_RESET}
    ${TPUT_YELLOW}${TPUT_BOLD}sudo chmod 0750 \"${NETDATA_PREFIX}/usr/libexec/netdata/plugins.d/apps.plugin\"${TPUT_RESET}
    ${TPUT_YELLOW}${TPUT_BOLD}sudo setcap cap_dac_read_search,cap_sys_ptrace+ep \"${NETDATA_PREFIX}/usr/libexec/netdata/plugins.d/apps.plugin\"${TPUT_RESET}

or, to run apps.plugin as root:

    ${TPUT_YELLOW}${TPUT_BOLD}sudo chown root:${NETDATA_GROUP} \"${NETDATA_PREFIX}/usr/libexec/netdata/plugins.d/apps.plugin\"${TPUT_RESET}
    ${TPUT_YELLOW}${TPUT_BOLD}sudo chmod 4750 \"${NETDATA_PREFIX}/usr/libexec/netdata/plugins.d/apps.plugin\"${TPUT_RESET}

apps.plugin is performing a hard-coded function of data collection for all
running processes. It cannot be instructed from the netdata daemon to perform
any task, so it is pretty safe to do this.

SETUID_WARNING
	fi
fi

# -----------------------------------------------------------------------------
progress "Basic netdata instructions"

cat <<END

netdata by default listens on all IPs on port ${NETDATA_PORT},
so you can access it with:

  ${TPUT_CYAN}${TPUT_BOLD}http://this.machine.ip:${NETDATA_PORT}/${TPUT_RESET}

To stop netdata run:

  ${TPUT_YELLOW}${TPUT_BOLD}${NETDATA_STOP_CMD}${TPUT_RESET}

To start netdata run:

  ${TPUT_YELLOW}${TPUT_BOLD}${NETDATA_START_CMD}${TPUT_RESET}


END

if [ "${AUTOUPDATE}" = "1" ]; then
	if [ "${UID}" -ne "0" ]; then
		echo >&2 "You need to run the installer as root for auto-updating via cron."
	else
		crondir=
		[ -d "/etc/periodic/daily" ] && crondir="/etc/periodic/daily"
		[ -d "/etc/cron.daily" ] && crondir="/etc/cron.daily"

		if [ -z "${crondir}" ]; then
			echo >&2 "Cannot figure out the cron directory to install netdata-updater"
		else
			if [ -f "${crondir}/netdata-updater.sh" ]; then
				progress "Removing incorrect netdata-updater filename in cron"
				rm -f "${crondir}/netdata-updater.sh"
			fi
			progress "Installing new netdata-updater in cron"

			rm ${installer_dir}/netdata-updater.sh || : #TODO(paulfantom): this workaround should be removed after v1.13.0-rc1. It just needs to be propagated

			rm -f "${crondir}/netdata-updater"
			if [ -f "${installer_dir}/packaging/installer/netdata-updater.sh" ]; then
				sed "s|THIS_SHOULD_BE_REPLACED_BY_INSTALLER_SCRIPT|${NETDATA_USER_CONFIG_DIR}/.environment|" "${installer_dir}/packaging/installer/netdata-updater.sh" > ${crondir}/netdata-updater || exit 1
				 #TODO(paulfantom): Following line is a workaround and should be removed after v1.13.0-rc1. It just needs time to be propagated.
				sed "s|THIS_SHOULD_BE_REPLACED_BY_INSTALLER_SCRIPT|${NETDATA_USER_CONFIG_DIR}/.environment|" "${installer_dir}/packaging/installer/netdata-updater.sh" > ${installer_dir}/netdata-updater.sh || exit 1
			else
				sed "s|THIS_SHOULD_BE_REPLACED_BY_INSTALLER_SCRIPT|${NETDATA_USER_CONFIG_DIR}/.environment|" "${netdata_source_dir}/packaging/installer/netdata-updater.sh" > ${crondir}/netdata-updater || exit 1
				 #TODO(paulfantom): Following line is a workaround and should be removed after v1.13.0-rc1. It just needs time to be propagated.
				sed "s|THIS_SHOULD_BE_REPLACED_BY_INSTALLER_SCRIPT|${NETDATA_USER_CONFIG_DIR}/.environment|" "${netdata_source_dir}/packaging/installer/netdata-updater.sh" > ${installer_source_dir}/netdata-updater.sh || exit 1
			fi

			chmod 0755 ${crondir}/netdata-updater
			echo >&2 "Update script is located at ${TPUT_GREEN}${TPUT_BOLD}${crondir}/netdata-updater${TPUT_RESET}"
			echo >&2
			echo >&2 "${TPUT_DIM}${TPUT_BOLD}netdata-updater${TPUT_RESET}${TPUT_DIM} works from cron. It will trigger an email from cron"
			echo >&2 "only if it fails (it should not print anything when it can update netdata).${TPUT_RESET}"
		fi
	fi
fi

# Save environment variables
cat <<EOF > ${NETDATA_USER_CONFIG_DIR}/.environment
# Created by installer
PATH="${PATH}"
CFLAGS="${CFLAGS}"
NETDATA_PREFIX="${NETDATA_PREFIX}"
NETDATA_CONFIGURE_OPTIONS="${NETDATA_CONFIGURE_OPTIONS}"
NETDATA_ADDED_TO_GROUPS="${NETDATA_ADDED_TO_GROUPS}"
INSTALL_UID="${UID}"
REINSTALL_COMMAND="${REINSTALL_COMMAND}"
RELEASE_CHANNEL="${RELEASE_CHANNEL}"
# This value is meant to be populated by autoupdater (if enabled)
NETDATA_TARBALL_CHECKSUM="new_installation"
EOF

# Opt-out from telemetry program
if [ -n "${NETDATA_DISABLE_TELEMETRY+x}" ]; then
	touch ${NETDATA_USER_CONFIG_DIR}/.opt-out-from-anonymous-statistics
fi

# -----------------------------------------------------------------------------
echo >&2
progress "We are done!"

if [ ${started} -eq 1 ]; then
	netdata_banner "is installed and running now!"
else
	netdata_banner "is installed now!"
fi

echo >&2 "  enjoy real-time performance and health monitoring..."
echo >&2
exit 0
