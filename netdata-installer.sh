#!/usr/bin/env bash

export PATH="${PATH}:/sbin:/usr/sbin:/usr/local/bin:/usr/local/sbin"

netdata_source_dir="$(pwd)"
installer_dir="$(dirname "${0}")"

if [ "${netdata_source_dir}" != "${installer_dir}" -a "${installer_dir}" != "." ]
    then
    echo >&2 "Warninng: you are currently in '${netdata_source_dir}' but the installer is in '${installer_dir}'."
fi


# -----------------------------------------------------------------------------
# reload the user profile

[ -f /etc/profile ] && . /etc/profile

# make sure /etc/profile does not change our current directory
cd "${netdata_source_dir}" || exit 1


# -----------------------------------------------------------------------------
# load the required functions

if [ -f "${installer_dir}/installer/functions.sh" ]
    then
    source "${installer_dir}/installer/functions.sh" || exit 1
else
    source "${netdata_source_dir}/installer/functions.sh" || exit 1
fi

# make sure we save all commands we run
run_logfile="netdata-installer.log"


# -----------------------------------------------------------------------------
# fix PKG_CHECK_MODULES error

if [ -d /usr/share/aclocal ]
then
        ACLOCAL_PATH=${ACLOCAL_PATH-/usr/share/aclocal}
        export ACLOCAL_PATH
fi

LC_ALL=C
umask 002

# Be nice on production environments
renice 19 $$ >/dev/null 2>/dev/null

processors=$(grep ^processor </proc/cpuinfo 2>/dev/null | wc -l)
[ $(( processors )) -lt 1 ] && processors=1

# you can set CFLAGS before running installer
CFLAGS="${CFLAGS--O2}"
[ "z${CFLAGS}" = "z-O3" ] && CFLAGS="-O2"

# keep a log of this command
printf "\n# " >>netdata-installer.log
date >>netdata-installer.log
printf "CFLAGS=\"%s\" " "${CFLAGS}" >>netdata-installer.log
printf "%q " "$0" "${@}" >>netdata-installer.log
printf "\n" >>netdata-installer.log

REINSTALL_PWD="${PWD}"
REINSTALL_COMMAND="$(printf "%q " "$0" "${@}"; printf "\n")"

setcap="$(which setcap 2>/dev/null || command -v setcap 2>/dev/null)"

ME="$0"
DONOTSTART=0
DONOTWAIT=0
NETDATA_PREFIX=
LIBS_ARE_HERE=0

usage() {
    netdata_banner "installer command line options"
    cat <<USAGE

${ME} <installer options>

Valid <installer options> are:

   --install /PATH/TO/INSTALL

        If your give: --install /opt
        netdata will be installed in /opt/netdata

   --dont-start-it

        Do not (re)start netdata.
        Just install it.

   --dont-wait

        Do not wait for the user to press ENTER.
        Start immediately building it.

   --zlib-is-really-here
   --libs-are-really-here

        If you get errors about missing zlib,
        or libuuid but you know it is available,
        you have a broken pkg-config.
        Use this option to allow it continue
        without checking pkg-config.

Netdata will by default be compiled with gcc optimization -O2
If you need to pass different CFLAGS, use something like this:

  CFLAGS="<gcc options>" ${ME} <installer options>

For the installer to complete successfully, you will need
these packages installed:

   gcc make autoconf automake pkg-config zlib1g-dev (or zlib-devel)
   uuid-dev (or libuuid-devel)

For the plugins, you will at least need:

   curl nodejs

USAGE
}

md5sum="$(which md5sum 2>/dev/null || command -v md5sum 2>/dev/null)"
get_git_config_signatures() {
    local x s file md5

    [ ! -d "conf.d" ] && echo >&2 "Wrong directory." && return 1
    [ -z "${md5sum}" -o ! -x "${md5sum}" ] && echo >&2 "No md5sum command." && return 1

    echo >configs.signatures.tmp

    for x in $(find conf.d -name \*.conf)
    do
            x="${x/conf.d\//}"
            echo "${x}"
            for c in $(git log --follow "conf.d/${x}" | grep ^commit | cut -d ' ' -f 2)
            do
                    git checkout ${c} "conf.d/${x}" || continue
                    s="$(cat "conf.d/${x}" | md5sum | cut -d ' ' -f 1)"
                    echo >>configs.signatures.tmp "${s}:${x}"
                    echo "    ${s}"
            done
            git checkout HEAD "conf.d/${x}" || break
    done

    cat configs.signatures.tmp |\
        grep -v "^$" |\
        sort -u |\
        {
            echo "declare -A configs_signatures=("
            IFS=":"
            while read md5 file
            do
                echo "  ['${md5}']='${file}'"
            done
            echo ")"
        } >configs.signatures

    rm configs.signatures.tmp

    return 0
}


while [ ! -z "${1}" ]
do
    if [ "$1" = "--install" ]
        then
        NETDATA_PREFIX="${2}/netdata"
        shift 2
    elif [ "$1" = "--zlib-is-really-here" -o "$1" = "--libs-are-really-here" ]
        then
        LIBS_ARE_HERE=1
        shift 1
    elif [ "$1" = "--dont-start-it" ]
        then
        DONOTSTART=1
        shift 1
    elif [ "$1" = "--dont-wait" ]
        then
        DONOTWAIT=1
        shift 1
    elif [ "$1" = "--help" -o "$1" = "-h" ]
        then
        usage
        exit 1
    elif [ "$1" = "get_git_config_signatures" ]
        then
        get_git_config_signatures && exit 0
        exit 1
    else
        echo >&2
        echo >&2 "ERROR:"
        echo >&2 "I cannot understand option '$1'."
        usage
        exit 1
    fi
done

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

if [ "${UID}" -ne 0 ]
    then
    if [ -z "${NETDATA_PREFIX}" ]
        then
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
if [ "$(type autoreconf 2> /dev/null)" ]
then
    autoconf_maj_min() {
        local maj min IFS=.-

        maj=$1
        min=$2

        set -- $(autoreconf -V | sed -ne '1s/.* \([^ ]*\)$/\1/p')
        eval $maj=\$1 $min=\$2
    }
    autoconf_maj_min AMAJ AMIN

    if [ "$AMAJ" -gt 2 ]
    then
        have_autotools=Y
    elif [ "$AMAJ" -eq 2 -a "$AMIN" -ge 60 ]
    then
        have_autotools=Y
    else
        echo "Found autotools $AMAJ.$AMIN"
    fi
else
    echo "No autotools found"
fi

if [ ! "$have_autotools" ]
then
    if [ -f configure ]
    then
        echo "Will skip autoreconf step"
    else
        netdata_banner "autotools v2.60 required"
        cat <<"EOF"

-------------------------------------------------------------------------------
autotools 2.60 or later is required

Sorry, you do not seem to have autotools 2.60 or later, which is
required to build from the git sources of netdata.

You can either install a suitable version of autotools and automake
or download a netdata package which does not have these dependencies.

Source packages where autotools have already been run are available
here:
       https://firehol.org/download/netdata/

The unsigned/master folder tracks the head of the git tree and released
packages are also available.
EOF
        exit 1
    fi
fi

if [ ${DONOTWAIT} -eq 0 ]
    then
    if [ ! -z "${NETDATA_PREFIX}" ]
        then
        eval "read >&2 -ep \$'\001${TPUT_BOLD}${TPUT_GREEN}\002Press ENTER to build and install netdata to \'\001${TPUT_CYAN}\002${NETDATA_PREFIX}\001${TPUT_YELLOW}\002\'\001${TPUT_RESET}\002 > ' -e -r REPLY"
    else
        eval "read >&2 -ep \$'\001${TPUT_BOLD}${TPUT_GREEN}\002Press ENTER to build and install netdata to your system\001${TPUT_RESET}\002 > ' -e -r REPLY"
    fi
fi

build_error() {
    netdata_banner "sorry, it failed to build..."
    cat <<EOF

^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Sorry! netdata failed to build...

You many need to check these:

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

   https://github.com/firehol/netdata/issues


EOF
    trap - EXIT
    exit 1
}

if [ ${LIBS_ARE_HERE} -eq 1 ]
    then
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

if [ "$have_autotools" ]
then
    run ./autogen.sh || exit 1
fi

run ./configure \
    --prefix="${NETDATA_PREFIX}/usr" \
    --sysconfdir="${NETDATA_PREFIX}/etc" \
    --localstatedir="${NETDATA_PREFIX}/var" \
    --with-zlib --with-math --with-user=netdata \
    CFLAGS="${CFLAGS}" || exit 1

# remove the build_error hook
trap - EXIT

# -----------------------------------------------------------------------------
progress "Cleanup compilation directory"

[ -f src/netdata ] && run make clean

# -----------------------------------------------------------------------------
progress "Compile netdata"

run make -j${processors} || exit 1


# -----------------------------------------------------------------------------
progress "Migrate configuration files for node.d.plugin and charts.d.plugin"

# migrate existing configuration files
# for node.d and charts.d
if [ -d "${NETDATA_PREFIX}/etc/netdata" ]
    then
    # the configuration directory exists

    if [ ! -d "${NETDATA_PREFIX}/etc/netdata/charts.d" ]
        then
        run mkdir "${NETDATA_PREFIX}/etc/netdata/charts.d"
    fi

    # move the charts.d config files
    for x in apache ap cpu_apps cpufreq example exim hddtemp load_average mem_apps mysql nginx nut opensips phpfpm postfix sensors squid tomcat
    do
        for y in "" ".old" ".orig"
        do
            if [ -f "${NETDATA_PREFIX}/etc/netdata/${x}.conf${y}" -a ! -f "${NETDATA_PREFIX}/etc/netdata/charts.d/${x}.conf${y}" ]
                then
                run mv -f "${NETDATA_PREFIX}/etc/netdata/${x}.conf${y}" "${NETDATA_PREFIX}/etc/netdata/charts.d/${x}.conf${y}"
            fi
        done
    done

    if [ ! -d "${NETDATA_PREFIX}/etc/netdata/node.d" ]
        then
        run mkdir "${NETDATA_PREFIX}/etc/netdata/node.d"
    fi

    # move the node.d config files
    for x in named sma_webbox snmp
    do
        for y in "" ".old" ".orig"
        do
            if [ -f "${NETDATA_PREFIX}/etc/netdata/${x}.conf${y}" -a ! -f "${NETDATA_PREFIX}/etc/netdata/node.d/${x}.conf${y}" ]
                then
                run mv -f "${NETDATA_PREFIX}/etc/netdata/${x}.conf${y}" "${NETDATA_PREFIX}/etc/netdata/node.d/${x}.conf${y}"
            fi
        done
    done
fi

# -----------------------------------------------------------------------------
progress "Backup existing netdata configuration before installing it"

if [ "${BASH_VERSINFO[0]}" -ge "4" ]
then
    declare -A configs_signatures=()
    if [ -f "configs.signatures" ]
        then
        source "configs.signatures" || echo >&2 "ERROR: Failed to load configs.signatures !"
    fi
fi

# backup user configurations
installer_backup_suffix="${PID}.${RANDOM}"
for x in $(find "${NETDATA_PREFIX}/etc/netdata/" -name '*.conf' -type f)
do
    if [ -f "${x}" ]
        then
        # make a backup of the configuration file
        cp -p "${x}" "${x}.old"

        if [ -z "${md5sum}" -o ! -x "${md5sum}" ]
            then
            # we don't have md5sum - keep it
            echo >&2 "File '${TPUT_CYAN}${x}${TPUT_RESET}' ${TPUT_RET}is not known to distribution${TPUT_RESET}. Keeping it."
            run cp -p "${x}" "${x}.installer_backup.${installer_backup_suffix}"
        else
            # find it relative filename
            f="${x/*\/etc\/netdata\//}"

            # find its checksum
            md5="$(cat "${x}" | ${md5sum} | cut -d ' ' -f 1)"

            # copy the original
            if [ -f "conf.d/${f}" ]
                then
                cp "conf.d/${f}" "${x}.orig"
            fi

            if [ "${BASH_VERSINFO[0]}" -ge "4" ]
            then
                if [ "${configs_signatures[${md5}]}" = "${f}" ]
                then
                    # it is a stock version - don't keep it
                    echo >&2 "File '${TPUT_CYAN}${x}${TPUT_RESET}' is stock version."
                else
                    # edited by user - keep it
                    echo >&2 "File '${TPUT_CYAN}${x}${TPUT_RESET}' ${TPUT_RED} has been edited by user${TPUT_RESET}. Keeping it."
                    run cp -p "${x}" "${x}.installer_backup.${installer_backup_suffix}"
                fi
            else
                echo >&2 "File '${TPUT_CYAN}${x}${TPUT_RESET}' ${TPUT_RET}cannot be checked for custom edits${TPUT_RESET}. Keeping it."
                run cp -p "${x}" "${x}.installer_backup.${installer_backup_suffix}"
            fi
        fi

    elif [ -f "${x}.installer_backup.${installer_backup_suffix}" ]
        then
        rm -f "${x}.installer_backup.${installer_backup_suffix}"
    fi
done


# -----------------------------------------------------------------------------
progress "Install netdata"

run make install || exit 1


# -----------------------------------------------------------------------------
progress "Restore user edited netdata configuration files"

for x in $(find "${NETDATA_PREFIX}/etc/netdata/" -name '*.conf' -type f)
do
    if [ -f "${x}.installer_backup.${installer_backup_suffix}" ]
        then
        run cp -p "${x}.installer_backup.${installer_backup_suffix}" "${x}" && \
            run rm -f "${x}.installer_backup.${installer_backup_suffix}"
    fi
done


# -----------------------------------------------------------------------------
progress "Fix generated files permissions"

run find ./system/ -type f -a \! -name \*.in -a \! -name Makefile\* -a \! -name \*.conf -a \! -name \*.service -a \! -name \*.logrotate -exec chmod 755 {} \;


# -----------------------------------------------------------------------------
progress "Add user netdata to required user groups"

NETDATA_ADDED_TO_DOCKER=0
NETDATA_ADDED_TO_NGINX=0
NETDATA_ADDED_TO_VARNISH=0
NETDATA_ADDED_TO_HAPROXY=0
NETDATA_ADDED_TO_ADM=0
if [ ${UID} -eq 0 ]
    then
    portable_add_group netdata
    portable_add_user netdata
    portable_add_user_to_group docker   netdata && NETDATA_ADDED_TO_DOCKER=1
    portable_add_user_to_group nginx    netdata && NETDATA_ADDED_TO_NGINX=1
    portable_add_user_to_group varnish  netdata && NETDATA_ADDED_TO_VARNISH=1
    portable_add_user_to_group haproxy  netdata && NETDATA_ADDED_TO_HAPROXY=1
    portable_add_user_to_group adm      netdata && NETDATA_ADDED_TO_ADM=1
    run_ok
else
    run_failed "The installer does not run as root."
fi

# -----------------------------------------------------------------------------
progress "Install logrotate configuration for netdata"

if [ ${UID} -eq 0 ]
    then
    if [ -d /etc/logrotate.d -a ! -f /etc/logrotate.d/netdata ]
        then
        run cp system/netdata.logrotate /etc/logrotate.d/netdata
    fi
    
    if [ -f /etc/logrotate.d/netdata ]
        then
        run chmod 644 /etc/logrotate.d/netdata
    fi
fi


# -----------------------------------------------------------------------------
progress "Read installation options from netdata.conf"

# create an empty config if it does not exist
[ ! -f "${NETDATA_PREFIX}/etc/netdata/netdata.conf" ] && \
    touch "${NETDATA_PREFIX}/etc/netdata/netdata.conf"

# function to extract values from the config file
config_option() {
    local key="${1}" value="${2}" line=

    if [ -s "${NETDATA_PREFIX}/etc/netdata/netdata.conf" ]
        then
        line="$( grep "^[[:space:]]*${key}[[:space:]]*=[[:space:]]*" "${NETDATA_PREFIX}/etc/netdata/netdata.conf" | head -n 1 )"
        [ ! -z "${line}" ] && value="$( echo "${line}" | cut -d '=' -f 2 | sed -e "s/^[[:space:]]\+//g" -e "s/[[:space:]]\+$//g" )"
    fi

    echo "${value}"
}

# the user netdata will run as
if [ "${UID}" = "0" ]
    then
    NETDATA_USER="$( config_option "run as user" "netdata" )"
else
    NETDATA_USER="${USER}"
fi

# the owners of the web files
NETDATA_WEB_USER="$(  config_option "web files owner" "${NETDATA_USER}" )"
NETDATA_WEB_GROUP="$( config_option "web files group" "${NETDATA_WEB_USER}" )"

# debug flags
NETDATA_DEBUG="$( config_option "debug flags" 0 )"

# port
defport=19999
NETDATA_PORT="$( config_option "default port" ${defport} )"
NETDATA_PORT2="$( config_option "port" ${defport} )"

if [ "${NETDATA_PORT}" != "${NETDATA_PORT2}" ]
then
    if [ "${NETDATA_PORT2}" != "${defport}" ]
    then
        NETDATA_PORT="${NETDATA_PORT2}"
    fi
fi

# directories
NETDATA_LIB_DIR="$( config_option "lib directory" "${NETDATA_PREFIX}/var/lib/netdata" )"
NETDATA_CACHE_DIR="$( config_option "cache directory" "${NETDATA_PREFIX}/var/cache/netdata" )"
NETDATA_WEB_DIR="$( config_option "web files directory" "${NETDATA_PREFIX}/usr/share/netdata/web" )"
NETDATA_LOG_DIR="$( config_option "log directory" "${NETDATA_PREFIX}/var/log/netdata" )"
NETDATA_CONF_DIR="$( config_option "config directory" "${NETDATA_PREFIX}/etc/netdata" )"
NETDATA_RUN_DIR="${NETDATA_PREFIX}/var/run"


# -----------------------------------------------------------------------------
progress "Fix permissions of netdata directories (using user '${NETDATA_USER}')"

if [ ! -d "${NETDATA_RUN_DIR}" ]
    then
    # this is needed if NETDATA_PREFIX is not empty
    run mkdir -p "${NETDATA_RUN_DIR}" || exit 1
fi

# --- conf dir ----

for x in "python.d" "charts.d" "node.d"
do
    if [ ! -d "${NETDATA_CONF_DIR}/${x}" ]
        then
        echo >&2 "Creating directory '${NETDATA_CONF_DIR}/${x}'"
        run mkdir -p "${NETDATA_CONF_DIR}/${x}" || exit 1
    fi
done
run chown -R "${NETDATA_USER}:${NETDATA_USER}" "${NETDATA_CONF_DIR}"
run find "${NETDATA_CONF_DIR}" -type f -exec chmod 0660 {} \;
run find "${NETDATA_CONF_DIR}" -type d -exec chmod 0775 {} \;

# --- web dir ----

if [ ! -d "${NETDATA_WEB_DIR}" ]
    then
    echo >&2 "Creating directory '${NETDATA_WEB_DIR}'"
    run mkdir -p "${NETDATA_WEB_DIR}" || exit 1
fi
run chown -R "${NETDATA_WEB_USER}:${NETDATA_WEB_GROUP}" "${NETDATA_WEB_DIR}"
run find "${NETDATA_WEB_DIR}" -type f -exec chmod 0664 {} \;
run find "${NETDATA_WEB_DIR}" -type d -exec chmod 0775 {} \;

# --- data dirs ----

for x in "${NETDATA_LIB_DIR}" "${NETDATA_CACHE_DIR}" "${NETDATA_LOG_DIR}"
do
    if [ ! -d "${x}" ]
        then
        echo >&2 "Creating directory '${x}'"
        run mkdir -p "${x}" || exit 1
    fi

    run chown -R "${NETDATA_USER}:${NETDATA_USER}" "${x}"
    #run find "${x}" -type f -exec chmod 0660 {} \;
    #run find "${x}" -type d -exec chmod 0770 {} \;
done

run chmod 755 "${NETDATA_LOG_DIR}"

# --- plugins ----

if [ ${UID} -eq 0 ]
    then
    run chown "${NETDATA_USER}:root" "${NETDATA_LOG_DIR}"
    run chown -R root "${NETDATA_PREFIX}/usr/libexec/netdata"
    run find "${NETDATA_PREFIX}/usr/libexec/netdata" -type d -exec chmod 0755 {} \;
    run find "${NETDATA_PREFIX}/usr/libexec/netdata" -type f -exec chmod 0644 {} \;
    run find "${NETDATA_PREFIX}/usr/libexec/netdata" -type f -a -name \*.plugin -exec chmod 0755 {} \;
    run find "${NETDATA_PREFIX}/usr/libexec/netdata" -type f -a -name \*.sh -exec chmod 0755 {} \;

    setcap_ret=1
    if ! iscontainer
        then
        if [ ! -z "${setcap}" ]
            then
            run setcap cap_dac_read_search,cap_sys_ptrace+ep "${NETDATA_PREFIX}/usr/libexec/netdata/plugins.d/apps.plugin"
            setcap_ret=$?
        fi

        if [ ${setcap_ret} -eq 0 ]
            then
            # if we managed to setcap
            # but we fail to execute apps.plugin
            # trigger setuid to root
            "${NETDATA_PREFIX}/usr/libexec/netdata/plugins.d/apps.plugin" -v >/dev/null 2>&1
            setcap_ret=$?
        fi
    fi

    if [ ${setcap_ret} -ne 0 ]
        then
        # fix apps.plugin to be setuid to root
        run chown root "${NETDATA_PREFIX}/usr/libexec/netdata/plugins.d/apps.plugin"
        run chmod 4755 "${NETDATA_PREFIX}/usr/libexec/netdata/plugins.d/apps.plugin"
    fi
else
    run chown "${NETDATA_USER}:${NETDATA_USER}" "${NETDATA_LOG_DIR}"
    run chown -R "${NETDATA_USER}:${NETDATA_USER}" "${NETDATA_PREFIX}/usr/libexec/netdata"
    run find "${NETDATA_PREFIX}/usr/libexec/netdata" -type f -exec chmod 0755 {} \;
    run find "${NETDATA_PREFIX}/usr/libexec/netdata" -type d -exec chmod 0755 {} \;
fi

# --- fix #1292 bug ---

[ -d "${NETDATA_PREFIX}/usr/libexec" ]       && run chmod a+rX "${NETDATA_PREFIX}/usr/libexec"
[ -d "${NETDATA_PREFIX}/usr/share/netdata" ] && run chmod a+rX "${NETDATA_PREFIX}/usr/share/netdata"



# -----------------------------------------------------------------------------
progress "Install netdata at system init"

installed_init_d=0
install_non_systemd_init() {
    [ "${UID}" != 0 ] && return 1

    local key="unknown"
    if [ -f /etc/os-release ]
        then
        source /etc/os-release || return 1
        key="${ID}-${VERSION_ID}"

    elif [ -f /etc/centos-release ]
        then
        key=$(</etc/centos-release)
    fi

    if [ -d /etc/init.d -a ! -f /etc/init.d/netdata ]
        then
        if [ "${key}" = "gentoo" ]
            then
            run cp system/netdata-openrc /etc/init.d/netdata && \
            run chmod 755 /etc/init.d/netdata && \
            run rc-update add netdata default && \
            installed_init_d=1
        
        elif [ "${key}" = "ubuntu-12.04" -o "${key}" = "ubuntu-14.04" -o "${key}" = "debian-7" ]
            then
            run cp system/netdata-lsb /etc/init.d/netdata && \
            run chmod 755 /etc/init.d/netdata && \
            run update-rc.d netdata defaults && \
            run update-rc.d netdata enable && \
            installed_init_d=1

        elif [ "${key}" = "CentOS release 6.8 (Final)" -o "${key}" = "amzn-2016.09" ]
            then
            run cp system/netdata-init-d /etc/init.d/netdata && \
            run chmod 755 /etc/init.d/netdata && \
            run chkconfig netdata on && \
            installed_init_d=1
        fi
    fi

    return 0
}

if [ "${UID}" -eq 0 ]
    then

    if issystemd
    then
        # systemd is running on this system

        if [ ! -f /etc/systemd/system/netdata.service ]
        then
            echo >&2 "Installing systemd service..."
            run cp system/netdata.service /etc/systemd/system/netdata.service && \
                run systemctl daemon-reload && \
                run systemctl enable netdata
        fi
    else
        install_non_systemd_init
    fi
fi


# -----------------------------------------------------------------------------
# check if we can re-start netdata

started=0

isnetdata() {
    if [ -d /proc/self ]
    then
        [ -z "$1" -o ! -f "/proc/$1/stat" ] && return 1
        [ "$(cat "/proc/$1/stat" | cut -d '(' -f 2 | cut -d ')' -f 1)" = "netdata" ] && return 0
        return 1
    fi
    return 0
}

stop_netdata_on_pid() {
    local pid="${1}" ret=0 count=0

    isnetdata ${pid} || return 0

    printf >&2 "Stopping netdata on pid ${pid} ..."
    while [ ! -z "$pid" -a ${ret} -eq 0 ]
    do
        if [ ${count} -gt 45 ]
            then
            echo >&2 "Cannot stop the running netdata on pid ${pid}."
            return 1
        fi

        count=$(( count + 1 ))

        run kill ${pid} 2>/dev/null
        ret=$?

        test ${ret} -eq 0 && printf >&2 "." && sleep 2
    done

    echo >&2
    if [ ${ret} -eq 0 ]
    then
        echo >&2 "SORRY! CANNOT STOP netdata ON PID ${pid} !"
        return 1
    fi

    echo >&2 "netdata on pid ${pid} stopped."
    return 0
}

stop_all_netdata() {
    local p myns ns

    myns="$(readlink /proc/self/ns/pid 2>/dev/null)"

    # echo >&2 "Stopping a (possibly) running netdata (namespace '${myns}')..."

    for p in $(cat "${NETDATA_RUN_DIR}/netdata.pid" 2>/dev/null) \
        $(cat /var/run/netdata.pid 2>/dev/null) \
        $(cat /var/run/netdata/netdata.pid 2>/dev/null) \
        $(pidof netdata 2>/dev/null)
    do
        ns="$(readlink /proc/${p}/ns/pid 2>/dev/null)"

        if [ -z "${myns}" -o -z "${ns}" -o "${myns}" = "${ns}" ]
            then
            stop_netdata_on_pid ${p}
        fi
    done
}

if [ ${DONOTSTART} -eq 1 ]
    then
    if [ ! -s "${NETDATA_PREFIX}/etc/netdata/netdata.conf" ]
        then
        echo "# Get config from http://127.0.0.1:${NETDATA_PORT}/netdata.conf" >"${NETDATA_PREFIX}/etc/netdata/netdata.conf"

        if [ "${UID}" -eq 0 ]
            then
            chown "${NETDATA_USER}" "${NETDATA_PREFIX}/etc/netdata/netdata.conf"
        fi
        chmod 0644 "${NETDATA_PREFIX}/etc/netdata/netdata.conf"
    fi

else

    progress "Start netdata"

    if [ "${UID}" -eq 0 ]
        then
        service netdata stop
        stop_all_netdata
        service netdata restart && started=1
        if [ ${started} -eq 0 ]
        then
            service netdata start && started=1
        fi
    fi

    if [ ${started} -eq 0 ]
    then
        # still not started...

        stop_all_netdata

        echo >&2 "Starting netdata..."
        run ${NETDATA_PREFIX}/usr/sbin/netdata -P ${NETDATA_RUN_DIR}/netdata.pid "${@}"
        if [ $? -ne 0 ]
            then
            echo >&2
            echo >&2 "SORRY! FAILED TO START NETDATA!"
            exit 1
        else
            echo >&2 "OK. NetData Started!"
        fi

        echo >&2
    fi

    # -----------------------------------------------------------------------------
    # save a config file, if it is not already there

    if [ ! -s "${NETDATA_PREFIX}/etc/netdata/netdata.conf" ]
        then
        echo >&2
        echo >&2 "-------------------------------------------------------------------------------"
        echo >&2
        echo >&2 "Downloading default configuration from netdata..."
        sleep 5

        # remove a possibly obsolete download
        [ -f "${NETDATA_PREFIX}/etc/netdata/netdata.conf.new" ] && rm "${NETDATA_PREFIX}/etc/netdata/netdata.conf.new"

        # disable a proxy to get data from the local netdata
        export http_proxy=
        export https_proxy=

        # try wget
        wget 2>/dev/null -O "${NETDATA_PREFIX}/etc/netdata/netdata.conf.new" "http://localhost:${NETDATA_PORT}/netdata.conf"
        ret=$?

        # try curl
        if [ ${ret} -ne 0 -o ! -s "${NETDATA_PREFIX}/etc/netdata/netdata.conf.new" ]
            then
            curl -s -o "${NETDATA_PREFIX}/etc/netdata/netdata.conf.new" "http://localhost:${NETDATA_PORT}/netdata.conf"
            ret=$?
        fi

        if [ ${ret} -eq 0 -a -s "${NETDATA_PREFIX}/etc/netdata/netdata.conf.new" ]
            then
            mv "${NETDATA_PREFIX}/etc/netdata/netdata.conf.new" "${NETDATA_PREFIX}/etc/netdata/netdata.conf"
            echo >&2 "New configuration saved for you to edit at ${NETDATA_PREFIX}/etc/netdata/netdata.conf"

            if [ "${UID}" -eq 0 ]
                then
                chown "${NETDATA_USER}" "${NETDATA_PREFIX}/etc/netdata/netdata.conf"
            fi
            chmod 0664 "${NETDATA_PREFIX}/etc/netdata/netdata.conf"
        else
            echo >&2 "Cannnot download configuration from netdata daemon using url 'http://localhost:${NETDATA_PORT}/netdata.conf'"
            [ -f "${NETDATA_PREFIX}/etc/netdata/netdata.conf.new" ] && rm "${NETDATA_PREFIX}/etc/netdata/netdata.conf.new"
        fi
    fi
fi

# -----------------------------------------------------------------------------
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

if [ -f "/sys/kernel/mm/ksm/run" ]
    then
    if [ $(cat "/sys/kernel/mm/ksm/run") != "1" ]
        then
        ksm_is_available_but_disabled
    fi
else
    ksm_is_not_available
fi

# -----------------------------------------------------------------------------
progress "Check version.txt"

if [ ! -s web/version.txt ]
    then
    cat <<VERMSG

${TPUT_BOLD}Version update check warning${TPUT_RESET}

The way you downloaded netdata, we cannot find its version. This means the
Update check on the dashboard, will not work.

If you want to have version update check, please re-install it
following the procedure in:

https://github.com/firehol/netdata/wiki/Installation

VERMSG
fi

# -----------------------------------------------------------------------------
progress "Check apps.plugin"

if [ "${UID}" -ne 0 ]
    then
    cat <<SETUID_WARNING

${TPUT_BOLD}apps.plugin needs privileges${TPUT_RESET}

Since you have installed netdata as a normal user, to have apps.plugin collect
all the needed data, you have to give it the access rights it needs, by running
either of the following sets of commands:

To run apps.plugin with escalated capabilities:

    ${TPUT_YELLOW}${TPUT_BOLD}sudo chown root:${NETDATA_USER} \"${NETDATA_PREFIX}/usr/libexec/netdata/plugins.d/apps.plugin\"${TPUT_RESET}
    ${TPUT_YELLOW}${TPUT_BOLD}sudo chmod 0750 \"${NETDATA_PREFIX}/usr/libexec/netdata/plugins.d/apps.plugin\"${TPUT_RESET}
    ${TPUT_YELLOW}${TPUT_BOLD}sudo setcap cap_dac_read_search,cap_sys_ptrace+ep \"${NETDATA_PREFIX}/usr/libexec/netdata/plugins.d/apps.plugin\"${TPUT_RESET}

or, to run apps.plugin as root:

    ${TPUT_YELLOW}${TPUT_BOLD}sudo chown root \"${NETDATA_PREFIX}/usr/libexec/netdata/plugins.d/apps.plugin\"${TPUT_RESET}
    ${TPUT_YELLOW}${TPUT_BOLD}sudo chmod 4755 \"${NETDATA_PREFIX}/usr/libexec/netdata/plugins.d/apps.plugin\"${TPUT_RESET}

apps.plugin is performing a hard-coded function of data collection for all
running processes. It cannot be instructed from the netdata daemon to perform
any task, so it is pretty safe to do this.

SETUID_WARNING
fi

# -----------------------------------------------------------------------------
progress "Generate netdata-uninstaller.sh"

cat >netdata-uninstaller.sh <<UNINSTALL
#!/usr/bin/env bash

# this script will uninstall netdata

if [ "\$1" != "--force" ]
    then
    echo >&2 "This script will REMOVE netdata from your system."
    echo >&2 "Run it again with --force to do it."
    exit 1
fi

echo >&2 "Stopping a possibly running netdata..."
for p in \$(pidof netdata); do kill \$p; done
sleep 2

deletedir() {
    if [ ! -z "\$1" -a -d "\$1" ]
        then
        echo
        echo "Deleting directory '\$1' ..."
        rm -I -R "\$1"
    fi
}

if [ ! -z "${NETDATA_PREFIX}" -a -d "${NETDATA_PREFIX}" ]
    then
    # installation prefix was given

    deletedir "${NETDATA_PREFIX}"

else
    # installation prefix was NOT given

    if [ -f "${NETDATA_PREFIX}/usr/sbin/netdata" ]
        then
        echo "Deleting ${NETDATA_PREFIX}/usr/sbin/netdata ..."
        rm -i "${NETDATA_PREFIX}/usr/sbin/netdata"
    fi

    deletedir "${NETDATA_PREFIX}/etc/netdata"
    deletedir "${NETDATA_PREFIX}/usr/share/netdata"
    deletedir "${NETDATA_PREFIX}/usr/libexec/netdata"
    deletedir "${NETDATA_PREFIX}/var/lib/netdata"
    deletedir "${NETDATA_PREFIX}/var/cache/netdata"
    deletedir "${NETDATA_PREFIX}/var/log/netdata"
fi

if [ -f /etc/logrotate.d/netdata ]
    then
    echo "Deleting /etc/logrotate.d/netdata ..."
    rm -i /etc/logrotate.d/netdata
fi

if [ -f /etc/systemd/system/netdata.service ]
    then
    echo "Deleting /etc/systemd/system/netdata.service ..."
    rm -i /etc/systemd/system/netdata.service
fi

if [ -f /etc/init.d/netdata ]
    then
    echo "Deleting /etc/init.d/netdata ..."
    rm -i /etc/init.d/netdata
fi

getent passwd netdata > /dev/null
if [ $? -eq 0 ]
    then
    echo
    echo "You may also want to remove the user netdata"
    echo "by running:"
    echo "   userdel netdata"
fi

getent group netdata > /dev/null
if [ $? -eq 0 ]
    then
    echo
    echo "You may also want to remove the group netdata"
    echo "by running:"
    echo "   groupdel netdata"
fi

getent group docker > /dev/null
if [ $? -eq 0 -a "${NETDATA_ADDED_TO_DOCKER}" = "1" ]
    then
    echo
    echo "You may also want to remove the netdata user from the docker group"
    echo "by running:"
    echo "   gpasswd -d netdata docker"
fi

getent group nginx > /dev/null
if [ $? -eq 0 -a "${NETDATA_ADDED_TO_NGINX}" = "1" ]
    then
    echo
    echo "You may also want to remove the netdata user from the nginx group"
    echo "by running:"
    echo "   gpasswd -d netdata nginx"
fi

getent group varnish > /dev/null
if [ $? -eq 0 -a "${NETDATA_ADDED_TO_VARNISH}" = "1" ]
    then
    echo
    echo "You may also want to remove the netdata user from the varnish group"
    echo "by running:"
    echo "   gpasswd -d netdata varnish"
fi

getent group haproxy > /dev/null
if [ $? -eq 0 -a "${NETDATA_ADDED_TO_HAPROXY}" = "1" ]
    then
    echo
    echo "You may also want to remove the netdata user from the haproxy group"
    echo "by running:"
    echo "   gpasswd -d netdata haproxy"
fi

getent group adm > /dev/null
if [ $? -eq 0 -a "${NETDATA_ADDED_TO_ADM}" = "1" ]
    then
    echo
    echo "You may also want to remove the netdata user from the adm group"
    echo "by running:"
    echo "   gpasswd -d netdata adm"
fi


UNINSTALL
chmod 750 netdata-uninstaller.sh

# -----------------------------------------------------------------------------
progress "Basic netdata instructions"

cat <<END

netdata by default listens on all IPs on port ${NETDATA_PORT},
so you can access it with:

  ${TPUT_CYAN}${TPUT_BOLD}http://this.machine.ip:${NETDATA_PORT}/${TPUT_RESET}

To stop netdata, just kill it, with:

  ${TPUT_YELLOW}${TPUT_BOLD}killall netdata${TPUT_RESET}

To start it, just run it:

  ${TPUT_YELLOW}${TPUT_BOLD}${NETDATA_PREFIX}/usr/sbin/netdata${TPUT_RESET}


END
echo >&2 "Uninstall script generated: ${TPUT_RED}${TPUT_BOLD}./netdata-uninstaller.sh${TPUT_RESET}"

if [ -d .git ]
    then
    cat >netdata-updater.sh.new <<REINSTALL
#!/usr/bin/env bash

force=0
[ "\${1}" = "-f" ] && force=1

export PATH="\${PATH}:${PATH}"
export CFLAGS="${CFLAGS}"

INSTALL_UID="${UID}"
if [ "\${INSTALL_UID}" != "\${UID}" ]
    then
    echo >&2 "This script should be run as user with uid \${INSTALL_UID} but it now runs with uid \${UID}"
    exit 1
fi

# make sure we cd to the working directory
cd "${REINSTALL_PWD}" || exit 1

# make sure there is .git here
[ \${force} -eq 0 -a ! -d .git ] && echo >&2 "No git structures found at: ${REINSTALL_PWD} (use -f for force re-install)" && exit 1

# signal netdata to start saving its database
# this is handy if your database is big
pids=\$(pidof netdata)
[ ! -z "\${pids}" ] && kill -USR1 \${pids}

tmp=
if [ -t 2 ]
    then
    # we are running on a terminal
    # open fd 3 and send it to stderr
    exec 3>&2
else
    # we are headless
    # create a temporary file for the log
    tmp=\$(mktemp /tmp/netdata-updater-log-XXXXXX.log)
    # open fd 3 and send it to tmp
    exec 3>\${tmp}
fi

info() {
    echo >&3 "\$(date) : INFO: " "\${@}"
}

emptyline() {
    echo >&3
}

error() {
    echo >&3 "\$(date) : ERROR: " "\${@}"
}

# this is what we will do if it fails (head-less only)
failed() {
    error "FAILED TO UPDATE NETDATA : \${1}"

    if [ ! -z "\${tmp}" ]
    then
        cat >&2 "\${tmp}"
        rm "\${tmp}"
    fi
    exit 1
}

get_latest_commit_id() {
    git log -1           2>&3 |\\
        grep ^commit     2>&3 |\\
        head -n 1        2>&3 |\\
        cut -d ' ' -f 2  2>&3
}

update() {
    [ -z "\${tmp}" ] && info "Running on a terminal - (this script also supports running headless from crontab)"

    emptyline

    if [ -d .git ]
        then
        info "Updating netdata source from github..."

        last_commit="\$(get_latest_commit_id)"
        [ \${force} -eq 0 -a -z "\${last_commit}" ] && failed "CANNOT GET LAST COMMIT ID (use -f for force re-install)"

        git pull >&3 2>&3 || failed "CANNOT FETCH LATEST SOURCE (use -f for force re-install)"

        new_commit="\$(get_latest_commit_id)"
        if [ \${force} -eq 0 ]
            then
            [ -z "\${new_commit}" ] && failed "CANNOT GET NEW LAST COMMIT ID (use -f for force re-install)"
            [ "\${new_commit}" = "\${last_commit}" ] && info "Nothing to be done! (use -f to force re-install)" && exit 0
        fi
    elif [ \${force} -eq 0 ]
        then
        failed "CANNOT FIND GIT STRUCTURES IN \$(pwd) (use -f for force re-install)"
    fi

    emptyline
    info "Re-installing netdata..."
    ${REINSTALL_COMMAND// --dont-wait/} --dont-wait >&3 2>&3 || failed "FAILED TO COMPILE/INSTALL NETDATA"

    [ ! -z "\${tmp}" ] && rm "\${tmp}" && tmp=
    return 0
}

# the installer updates this script - so we run and exit in a single line
update && exit 0
###############################################################################
###############################################################################
REINSTALL
    chmod 755 netdata-updater.sh.new
    mv -f netdata-updater.sh.new netdata-updater.sh
    echo >&2 "Update script generated   : ${TPUT_GREEN}${TPUT_BOLD}./netdata-updater.sh${TPUT_RESET}"
    echo >&2
    echo >&2 "${TPUT_DIM}${TPUT_BOLD}netdata-updater.sh${TPUT_RESET}${TPUT_DIM} can work from cron. It will trigger an email from cron"
    echo >&2 "only if it fails (it does not print anything if it can update netdata).${TPUT_RESET}"
    if [ "${UID}" -eq 0 -a -d "/etc/cron.daily" -a ! -f "/etc/cron.daily/netdata-updater.sh" ]
        then
        echo >&2 "${TPUT_DIM}Run this to automatically check and install netdata updates once per day:${TPUT_RESET}"
        echo >&2
        echo >&2 "${TPUT_YELLOW}${TPUT_BOLD}ln -s ${PWD}/netdata-updater.sh /etc/cron.daily/netdata-updater.sh${TPUT_RESET}"
    fi
elif [ -f "netdata-updater.sh" ]
    then
    rm "netdata-updater.sh"
fi

# -----------------------------------------------------------------------------
echo >&2
progress "We are done!"

if [ ${started} -eq 1 ]
    then
    netdata_banner "is installed and running now!"
else
    netdata_banner "is installed now!"
fi

echo >&2 "  enjoy real-time performance and health monitoring..."
echo >&2 
exit 0
