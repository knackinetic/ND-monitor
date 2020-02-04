#!/usr/bin/env bash
#shellcheck disable=SC2164
#
# Netdata updater utility
#
# Variables needed by script:
#  - PATH
#  - CFLAGS
#  - LDFLAGS
#  - IS_NETDATA_STATIC_BINARY
#  - NETDATA_CONFIGURE_OPTIONS
#  - REINSTALL_OPTIONS
#  - NETDATA_TARBALL_URL
#  - NETDATA_TARBALL_CHECKSUM_URL
#  - NETDATA_TARBALL_CHECKSUM
#  - NETDATA_PREFIX / NETDATA_LIB_DIR (After 1.16.1 we will only depend on lib dir)
#
# Copyright: SPDX-License-Identifier: GPL-3.0-or-later
#
# Author: Paweł Krupa <paulfantom@gmail.com>
# Author: Pavlos Emm. Katsoulakis <paul@netdata.cloud>

info() {
  echo >&3 "$(date) : INFO: " "${@}"
}

error() {
  echo >&3 "$(date) : ERROR: " "${@}"
}

safe_sha256sum() {
  # Within the contexct of the installer, we only use -c option that is common between the two commands
  # We will have to reconsider if we start non-common options
  if command -v sha256sum > /dev/null 2>&1; then
    sha256sum $@
  elif command -v shasum > /dev/null 2>&1; then
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
  if command -v curl > /dev/null 2>&1; then
    curl -sSL --connect-timeout 10 --retry 3 "${url}" > "${dest}" || fatal "Cannot download ${url}"
  elif command -v wget > /dev/null 2>&1; then
    wget -T 15 -O - "${url}" > "${dest}" || fatal "Cannot download ${url}"
  else
    fatal "I need curl or wget to proceed, but neither is available on this system."
  fi
}

set_tarball_urls() {
  local extension="tar.gz"

  if [ ! -z "${NETDATA_LOCAL_TARBAL_OVERRIDE}" ]; then
    info "Not fetching remote tarballs, local override was given"
    return
  fi

  if [ "$2" == "yes" ]; then
    extension="gz.run"
  fi

  if [ "$1" = "stable" ]; then
    local latest
    # Simple version
    # latest="$(curl -sSL https://api.github.com/repos/netdata/netdata/releases/latest | grep tag_name | cut -d'"' -f4)"
    latest="$(download "https://api.github.com/repos/netdata/netdata/releases/latest" /dev/stdout | grep tag_name | cut -d'"' -f4)"
    export NETDATA_TARBALL_URL="https://github.com/netdata/netdata/releases/download/$latest/netdata-$latest.${extension}"
    export NETDATA_TARBALL_CHECKSUM_URL="https://github.com/netdata/netdata/releases/download/$latest/sha256sums.txt"
  else
    export NETDATA_TARBALL_URL="https://storage.googleapis.com/netdata-nightlies/netdata-latest.${extension}"
    export NETDATA_TARBALL_CHECKSUM_URL="https://storage.googleapis.com/netdata-nightlies/sha256sums.txt"
  fi
}

update() {
  [ -z "${logfile}" ] && info "Running on a terminal - (this script also supports running headless from crontab)"

  RUN_INSTALLER=0
  tmpdir=$(create_tmp_directory)
  cd "$tmpdir"

  if [ -z "${NETDATA_LOCAL_TARBAL_OVERRIDE}" ]; then
    download "${NETDATA_TARBALL_CHECKSUM_URL}" "${tmpdir}/sha256sum.txt" >&3 2>&3
    if [[ -n "${NETDATA_TARBALL_CHECKSUM}" ]] && grep "${NETDATA_TARBALL_CHECKSUM}" sha256sum.txt >&3 2>&3; then
      info "Newest version is already installed"
    else
      download "${NETDATA_TARBALL_URL}" "${tmpdir}/netdata-latest.tar.gz"
      if ! grep netdata-latest.tar.gz sha256sum.txt | safe_sha256sum -c - >&3 2>&3; then
        fatal "Tarball checksum validation failed. Stopping netdata upgrade and leaving tarball in ${tmpdir}"
      fi
      NEW_CHECKSUM="$(safe_sha256sum netdata-latest.tar.gz 2> /dev/null | cut -d' ' -f1)"
      tar -xf netdata-latest.tar.gz >&3 2>&3
      rm netdata-latest.tar.gz >&3 2>&3
      cd netdata-*
      RUN_INSTALLER=1
    fi
  else
    info "!!Local tarball override detected!! - Entering directory ${NETDATA_LOCAL_TARBAL_OVERRIDE} for installation, not downloading anything"
    RUN_INSTALLER=1
    cd ${NETDATA_LOCAL_TARBAL_OVERRIDE}
  fi

  # We got the sources, run the update now
  if [ ${RUN_INSTALLER} -eq 1 ]; then
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
    eval "./netdata-installer.sh ${REINSTALL_OPTIONS} --dont-wait ${do_not_start}" >&3 2>&3 || fatal "FAILED TO COMPILE/INSTALL NETDATA"

    # We no longer store checksum info here. but leave this so that we clean up all environment files upon next update.
    sed -i '/NETDATA_TARBALL/d' "${ENVIRONMENT_FILE}"

    info "Updating tarball checksum info"
    echo "${NEW_CHECKSUM}" > "${NETDATA_LIB_DIR}/netdata.tarball.checksum"
  fi

  rm -rf "${tmpdir}" >&3 2>&3
  [ -n "${logfile}" ] && rm "${logfile}" && logfile=

  return 0
}

# Usually stored in /etc/netdata/.environment
: "${ENVIRONMENT_FILE:=THIS_SHOULD_BE_REPLACED_BY_INSTALLER_SCRIPT}"

# shellcheck source=/dev/null
source "${ENVIRONMENT_FILE}" || exit 1

# We dont expect to find lib dir variable on older installations, so load this path if none found
export NETDATA_LIB_DIR="${NETDATA_LIB_DIR:-${NETDATA_PREFIX}/var/lib/netdata}"

# Source the tarbal checksum, if not already available from environment (for existing installations with the old logic)
[[ -z "${NETDATA_TARBALL_CHECKSUM}" ]] && [[ -f ${NETDATA_LIB_DIR}/netdata.tarball.checksum ]] && NETDATA_TARBALL_CHECKSUM="$(cat "${NETDATA_LIB_DIR}/netdata.tarball.checksum")"

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
  exec 3> "${logfile}"
fi

set_tarball_urls "${RELEASE_CHANNEL}" "${IS_NETDATA_STATIC_BINARY}"

if [ "${IS_NETDATA_STATIC_BINARY}" == "yes" ]; then
  TMPDIR="$(create_tmp_directory)"
  PREVDIR="$(pwd)"

  echo >&2 "Entering ${TMPDIR}"
  cd "${TMPDIR}"

  download "${NETDATA_TARBALL_CHECKSUM_URL}" "${TMPDIR}/sha256sum.txt"
  download "${NETDATA_TARBALL_URL}" "${TMPDIR}/netdata-latest.gz.run"
  if ! grep netdata-latest.gz.run "${TMPDIR}/sha256sum.txt" | safe_sha256sum -c - > /dev/null 2>&1; then
    fatal "Static binary checksum validation failed. Stopping netdata installation and leaving binary in ${TMPDIR}"
  fi

  # Do not pass any options other than the accept, for now
  sh "${TMPDIR}/netdata-latest.gz.run" --accept ${REINSTALL_OPTIONS}

  #shellcheck disable=SC2181
  if [ $? -eq 0 ]; then
    rm -r "${TMPDIR}"
  else
    echo >&2 "NOTE: did not remove: ${TMPDIR}"
  fi
  echo >&2 "Switching back to ${PREVDIR}"
  cd "${PREVDIR}"
else
  # the installer updates this script - so we run and exit in a single line
  update && exit 0
fi
