#!/usr/bin/env bash

. $(dirname "${0}")/../functions.sh "${@}" || exit 1


# -----------------------------------------------------------------------------
# create a few quick links

run cd "${NETDATA_INSTALL_PATH}"
run ln -s etc/netdata netdata-configs
run ln -s usr/share/netdata/web netdata-web-files
run ln -s usr/libexec/netdata netdata-plugins
run ln -s var/lib/netdata netdata-dbs
run ln -s var/cache/netdata netdata-metrics
run ln -s var/log/netdata netdata-logs


# -----------------------------------------------------------------------------
# copy the files needed by makeself installation

run mkdir -p "${NETDATA_INSTALL_PATH}/system"
run cd "${NETDATA_SOURCE_PATH}" || exit 1

cp \
    makeself/post-installer.sh \
    makeself/install-or-update.sh \
    installer/functions.sh \
    configs.signatures \
    system/netdata-init-d \
    system/netdata-lsb \
    system/netdata-openrc \
    system/netdata.logrotate \
    system/netdata.service \
    "${NETDATA_INSTALL_PATH}/system/"


# -----------------------------------------------------------------------------
# create a wrapper to start our netdata with a modified path

mv "${NETDATA_INSTALL_PATH}/bin/netdata" \
    "${NETDATA_INSTALL_PATH}/bin/netdata.bin" || exit 1

cat >"${NETDATA_INSTALL_PATH}/bin/netdata" <<EOF
#!${NETDATA_INSTALL_PATH}/bin/bash
export PATH="${NETDATA_INSTALL_PATH}/bin:${PATH}"
exec "${NETDATA_INSTALL_PATH}/bin/netdata.bin" "${@}"
EOF
chmod 755 "${NETDATA_INSTALL_PATH}/bin/netdata"


# -----------------------------------------------------------------------------
# move etc to protect the destination when unpacked

if [ -d "${NETDATA_INSTALL_PATH}/etc/netdata" ]
    then
    if [ -d "${NETDATA_INSTALL_PATH}/etc/netdata.new" ]
        then
        rm -rf "${NETDATA_INSTALL_PATH}/etc/netdata.new" || exit 1
    fi

    mv "${NETDATA_INSTALL_PATH}/etc/netdata" \
        "${NETDATA_INSTALL_PATH}/etc/netdata.new" || exit 1
fi


# -----------------------------------------------------------------------------
# create the makeself archive

"${NETDATA_MAKESELF_PATH}/makeself.sh" \
    --gzip \
    --notemp \
    --header "${NETDATA_MAKESELF_PATH}/makeself-header.sh" \
    --lsm "${NETDATA_MAKESELF_PATH}/makeself.lsm" \
    --license "${NETDATA_MAKESELF_PATH}/makeself-license.txt" \
    --help-header "${NETDATA_MAKESELF_PATH}/makeself-help-header.txt" \
    "${NETDATA_INSTALL_PATH}" \
    "${NETDATA_INSTALL_PATH}.gz.run" \
    "netdata, the real-time performance and health monitoring system" \
    ./system/post-installer.sh \
    ${NULL}

