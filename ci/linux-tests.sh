#!/bin/bash
# Run Ardour's unit-test suites; each suite reports on its own and the script
# fails if any suite fails. Usage: ci/linux-tests.sh <ardour-src-dir>
set -uo pipefail

SRC=${1:?ardour source dir}

# libpbd's StringConvertTest needs locales with a comma decimal mark.
sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -qq locales
sudo locale-gen fr_FR fr_FR.UTF-8 de_DE de_DE.UTF-8 nl_NL nl_NL.UTF-8 >/dev/null

# libmidi++2 is left out: its run-tests.sh does not find libtemporal (upstream script issue).
failed=""
for suite in libs/pbd libs/evoral libs/audiographer libs/ardour; do
	echo "::group::$suite"
	(cd "$SRC/$suite" && ./run-tests.sh)
	rc=$?
	echo "::endgroup::"
	echo "$suite: exit $rc"
	[ $rc -eq 0 ] || failed="$failed $suite"
done

if [ -n "$failed" ]; then
	echo "failed suites:$failed"
	exit 1
fi
echo "all suites passed"
