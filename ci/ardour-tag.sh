#!/bin/bash
# Ardour's build takes its version number from `git describe`, which needs
# Ardour's annotated release tag (ci/ardour-ref). This repository's branch
# carries Ardour's history but not its tags, so fetch the tag from upstream
# when it is missing. Run from the repository root.
set -euo pipefail

REF=$(cat ci/ardour-ref)
if ! git rev-parse -q --verify "refs/tags/$REF" >/dev/null; then
	git fetch --no-tags https://github.com/Ardour/ardour.git "refs/tags/$REF:refs/tags/$REF"
fi
git describe
