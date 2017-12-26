#!/usr/bin/env bash

cpus=$(grep ^processor </proc/cpuinfo| wc -l)
[ -z "${cpus}" ] && cpus=1

token=
[ -f .coverity-token ] && token="$(<.coverity-token)"
[ -z "${token}" ] && \
	echo >&2 "Save the coverity token to .coverity-token" && \
	exit 1

echo >&2 "Coverity token: ${token}"

covbuild="$(which cov-build 2>/dev/null || command -v cov-build 2>/dev/null)"
[ -z "${covbuild}" -a -f .coverity-build ] && covbuild="$(<.coverity-build)"
[ -z "${covbuild}" ] && \
	echo "Save command the full filename of cov-build in .coverity-build" && \
	exit 1

[ ! -x "${covbuild}" ] && \
	echo "The command ${covbuild} is not executable. Save command the full filename of cov-build in .coverity-build" && \
	exit 1

version="$(cat config.h | grep "^#define PACKAGE_VERSION" | cut -d '"' -f 2)"
echo >&2 "Working on netdata version: ${version}"

echo >&2 "Cleaning up old builds..."
make clean || exit 1

[ -d "cov-int" ] && \
	rm -rf "cov-int"

[ -f netdata-coverity-analysis.tgz ] && \
	rm netdata-coverity-analysis.tgz

"${covbuild}" --dir cov-int make -j${cpus} || exit 1

echo >&2 "Compressing data..."
tar czvf netdata-coverity-analysis.tgz cov-int || exit 1

echo >&2 "Sending analysis..."
curl --progress-bar --form token="${token}" \
  --form email=costa@tsaousis.gr \
  --form file=@netdata-coverity-analysis.tgz \
  --form version="${version}" \
  --form description="netdata, real-time performance monitoring, done right." \
  https://scan.coverity.com/builds?project=firehol%2Fnetdata
