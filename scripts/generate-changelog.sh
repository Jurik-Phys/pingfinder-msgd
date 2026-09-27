#!/bin/bash

set -euo pipefail

PROJECT_NAME="pingfinder-msgd"
DEBIAN_REVISION="1"

# Get the repository root
REPO_ROOT=$(git rev-parse --show-toplevel)

CMAKE_FILE="${REPO_ROOT}/CMakeLists.txt"
CHANGELOG_FILE="${REPO_ROOT}/debian/changelog"

# Check current branch
CURRENT_BRANCH=$(git branch --show-current)

if [[ "$CURRENT_BRANCH" != "develop" ]]; then
    echo "Error: script must be run on the develop branch." >&2
    exit 1
fi

# Extract project version from CMakeLists.txt
VERSION=$(sed -nE \
    "s/^project\(${PROJECT_NAME}[[:space:]]+VERSION[[:space:]]+([0-9]+\.[0-9]+\.[0-9]+).*/\1/p" \
    "$CMAKE_FILE")

if [[ -z "$VERSION" ]]; then
    echo "Error: project version not found in $CMAKE_FILE." >&2
    exit 1
fi

echo "Project version: $VERSION"

# Find the latest release tag
LATEST_TAG=$(git tag --list 'v[0-9]*.[0-9]*.[0-9]*' --sort=-version:refname | head -n 1)

if [[ -n "$LATEST_TAG" ]]; then
    LATEST_VERSION="${LATEST_TAG#v}"

    echo "Latest release: $LATEST_TAG"

    if dpkg --compare-versions "$VERSION" lt "$LATEST_VERSION"; then
        echo "Error: project version $VERSION is older than latest release $LATEST_VERSION." >&2
        exit 1
    elif dpkg --compare-versions "$VERSION" eq "$LATEST_VERSION"; then
        echo "Version status: current release version."
    else
        echo "Version status: new release version."
    fi
else
    echo "Latest release: none"
fi

# Get changes accumulated on develop since the latest main release.
LOG_RANGE="main..develop"

# Get commit messages
COMMITS=$(git log "$LOG_RANGE" --pretty=format:'%s' --reverse)

if [[ -z "$COMMITS" ]]; then
    echo "Error: no changes found since the previous release." >&2
    exit 1
fi

# Get maintainer information from Git configuration
MAINTAINER_NAME=$(git config user.name)
MAINTAINER_EMAIL=$(git config user.email)

if [[ -z "$MAINTAINER_NAME" || -z "$MAINTAINER_EMAIL" ]]; then
    echo "Error: Git user.name or user.email is not configured." >&2
    exit 1
fi

# Generate Debian changelog
{
    echo "$PROJECT_NAME (${VERSION}-${DEBIAN_REVISION}) unstable; urgency=medium"
    echo

    while IFS= read -r commit; do
        echo "  * $commit"
    done <<< "$COMMITS"

    echo
    printf ' -- %s <%s>  %s\n' \
        "$MAINTAINER_NAME" \
        "$MAINTAINER_EMAIL" \
        "$(date -R)"
} > "$CHANGELOG_FILE"

echo "Generated $CHANGELOG_FILE:"
echo
cat "$CHANGELOG_FILE"
