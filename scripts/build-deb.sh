#!/bin/bash

REPO_ROOT=$(git rev-parse --show-toplevel)

"${REPO_ROOT}/scripts/generate-changelog.sh"

cd "$REPO_ROOT"

dpkg-buildpackage -us -uc
