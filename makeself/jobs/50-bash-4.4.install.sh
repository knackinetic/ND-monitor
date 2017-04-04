#!/usr/bin/env bash

. $(dirname "${0}")/../functions.sh "${@}" || exit 1

fetch "bash-4.4" "http://ftp.gnu.org/gnu/bash/bash-4.4.tar.gz"

run ./configure \
	--prefix=${NETDATA_INSTALL_PATH} \
	--enable-static-link \
	--disable-nls \
	--without-bash-malloc \
#	--disable-rpath \
#	--enable-alias \
#	--enable-arith-for-command \
#	--enable-array-variables \
#	--enable-brace-expansion \
#	--enable-casemod-attributes \
#	--enable-casemod-expansions \
#	--enable-command-timing \
#	--enable-cond-command \
#	--enable-cond-regexp \
#	--enable-directory-stack \
#	--enable-dparen-arithmetic \
#	--enable-function-import \
#	--enable-glob-asciiranges-default \
#	--enable-help-builtin \
#	--enable-job-control \
#	--enable-net-redirections \
#	--enable-process-substitution \
#	--enable-progcomp \
#	--enable-prompt-string-decoding \
#	--enable-readline \
#	--enable-select \
		

run make clean
run make -j${PROCESSORS}

cat >examples/loadables/Makefile <<EOF
all:
clean:
install:
EOF

run make install

run strip ${NETDATA_INSTALL_PATH}/bin/bash
