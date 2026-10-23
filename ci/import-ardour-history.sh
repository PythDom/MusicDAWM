#!/bin/bash
# One-time import: merge Ardour's full history (up to the tag in ci/ardour-ref)
# into this repository, then turn patches/upstream/*.patch into ordinary commits.
# Afterwards the repository root is the Ardour source tree plus our files.
# Runs in GitHub Actions (.github/workflows/import-ardour.yml) because the push
# is several hundred MB. Set DRY_RUN=1 to stop before pushing.
# Usage: ci/import-ardour-history.sh <branch-to-push>
set -euo pipefail

BRANCH=${1:?branch}
REF=$(cat ci/ardour-ref)

if git cat-file -e HEAD:wscript 2>/dev/null; then
	echo "Ardour history is already imported (wscript is in the tree); nothing to do."
	exit 0
fi

git config user.name "Claude"
git config user.email "noreply@anthropic.com"

git remote add ardour https://github.com/Ardour/ardour.git 2>/dev/null || true
git fetch --no-tags ardour "+refs/tags/$REF:refs/tags/ardour-$REF"
UPSTREAM=$(git rev-parse "ardour-$REF^{commit}")
echo "Ardour $REF = $UPSTREAM"

# Both trees have a .gitattributes; everything else is disjoint.
if ! git merge --allow-unrelated-histories --no-ff --no-commit "$UPSTREAM"; then
	conflicts=$(git diff --name-only --diff-filter=U)
	if [ "$conflicts" != ".gitattributes" ]; then
		echo "unexpected merge conflicts: $conflicts"
		exit 1
	fi
	{
		echo "# From Ardour"
		git show "$UPSTREAM:.gitattributes"
		echo
		echo "# MusicDAWM"
		git show "HEAD:.gitattributes" | grep -v '^#'
	} > .gitattributes
	git add .gitattributes
fi
git commit -q -F - <<EOF
Import Ardour $REF history

Merge the full history of Ardour (https://github.com/Ardour/ardour, tag
$REF, $UPSTREAM) so this repository is the MusicDAWM fork itself.
Our planning documents, CI and tools stay at the top level next to
Ardour's tree. Later Ardour releases are merged the same way.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_012woem7gpF63F73AqdVTFfU
EOF

# Our changes to Ardour become ordinary commits on top of the import.
for p in patches/upstream/*.patch; do
	git am -q --committer-date-is-author-date "$p"
done
git rm -q -r patches/upstream
git commit -q -F - <<'EOF'
Drop patches/upstream: the patches are now commits in the tree

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_012woem7gpF63F73AqdVTFfU
EOF

git log --oneline -6
if [ "${DRY_RUN:-0}" = 1 ]; then
	echo "DRY_RUN: not pushing"
	exit 0
fi
git push origin "HEAD:refs/heads/$BRANCH"
