#!/usr/bin/env bash

# echo >>/tmp/cppcheck.log "cppcheck ${*}"

cppcheck=$(which cppcheck 2>/dev/null || command -v cppcheck 2>/dev/null)
[ -z "${cppcheck}" ] && echo >&2 "install cppcheck." && exit 1

[ -x "/home/costa/src/cppcheck.git/cppcheck" ] && \
	cppcheck="/home/costa/src/cppcheck.git/cppcheck"

processors=$(grep -c ^processor /proc/cpuinfo)
[ $(( processors )) -lt 1 ] && processors=1

base="$(dirname "${0}")"
[ "${base}" = "." ] && base="${PWD}"

cd "${base}/src" || exit 1

[ ! -d "cppcheck-build" ] && mkdir "cppcheck-build"

file="${1}"
shift
[ "${file}" = "${base}" -o -z "${file}" ] && file="${base}/src"

"${cppcheck}" \
	-j ${processors} \
	--cppcheck-build-dir="cppcheck-build" \
	-I .. \
	--force \
	--enable=warning,performance,portability,information \
	--suppress="unusedFunction:*" \
	--suppress="nullPointerRedundantCheck:*" \
	"${file}" "${@}"
