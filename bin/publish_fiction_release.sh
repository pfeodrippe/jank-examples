#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

DEFAULT_JANK_SRC="${JANK_SRC:-/Users/pfeodrippe/dev/jank/compiler+runtime}"
BUILD_DIR="$DEFAULT_JANK_SRC/build-wasm"
WORKFLOW_NAME="Fiction Pages From Release"
TARGET_BRANCH="main"
RUN_BUILD=1
WAIT_FOR_DEPLOY=1
TAG=""
TITLE=""
NOTES=""
REPO=""

usage() {
  cat <<'EOF'
Usage:
  bin/publish_fiction_release.sh <tag> [options]

Examples:
  bin/publish_fiction_release.sh v0.1.1
  bin/publish_fiction_release.sh v0.1.1 --skip-build
  bin/publish_fiction_release.sh v0.1.1 --repo owner/repo --build-dir /path/to/build-wasm

Options:
  --repo <owner/repo>         GitHub repository (default: current gh repo)
  --build-dir <path>          Build output directory with fiction*.html/js/wasm files
  --target-branch <branch>    Branch used when creating a new release tag (default: main)
  --title <text>              Release title (default: "Fiction WASM <tag>")
  --notes <text>              Release notes (default: generated short note)
  --workflow <name>           Workflow name to dispatch for existing releases
  --skip-build                Skip 'make fiction-wasm'
  --no-wait                   Do not wait for Pages deploy workflow completion
  -h, --help                  Show this help
EOF
}

require_arg() {
  local flag="$1"
  local value="${2:-}"
  if [[ -z "$value" || "$value" == --* ]]; then
    echo "Missing value for $flag" >&2
    exit 1
  fi
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -h|--help)
      usage
      exit 0
      ;;
    --repo)
      require_arg "$1" "${2:-}"
      REPO="$2"
      shift 2
      ;;
    --build-dir)
      require_arg "$1" "${2:-}"
      BUILD_DIR="$2"
      shift 2
      ;;
    --target-branch)
      require_arg "$1" "${2:-}"
      TARGET_BRANCH="$2"
      shift 2
      ;;
    --title)
      require_arg "$1" "${2:-}"
      TITLE="$2"
      shift 2
      ;;
    --notes)
      require_arg "$1" "${2:-}"
      NOTES="$2"
      shift 2
      ;;
    --workflow)
      require_arg "$1" "${2:-}"
      WORKFLOW_NAME="$2"
      shift 2
      ;;
    --skip-build)
      RUN_BUILD=0
      shift
      ;;
    --no-wait)
      WAIT_FOR_DEPLOY=0
      shift
      ;;
    --*)
      echo "Unknown option: $1" >&2
      usage
      exit 1
      ;;
    *)
      if [[ -n "$TAG" ]]; then
        echo "Unexpected positional argument: $1" >&2
        usage
        exit 1
      fi
      TAG="$1"
      shift
      ;;
  esac
done

if [[ -z "$TAG" ]]; then
  usage
  exit 1
fi

if ! command -v gh >/dev/null 2>&1; then
  echo "gh CLI is required but not found in PATH." >&2
  exit 1
fi

if [[ -z "$REPO" ]]; then
  REPO="$(gh repo view --json nameWithOwner -q .nameWithOwner)"
fi

if [[ -z "$TITLE" ]]; then
  TITLE="Fiction WASM $TAG"
fi

if [[ -z "$NOTES" ]]; then
  NOTES="Fiction WASM build for $TAG."
fi

if [[ $RUN_BUILD -eq 1 ]]; then
  echo "==> Building fiction wasm (make fiction-wasm)"
  make -C "$REPO_ROOT" fiction-wasm
fi

if [[ ! -d "$BUILD_DIR" ]]; then
  echo "Build directory not found: $BUILD_DIR" >&2
  exit 1
fi

shopt -s nullglob
assets=(
  "$BUILD_DIR"/fiction*.html
  "$BUILD_DIR"/fiction*.js
  "$BUILD_DIR"/fiction*.wasm
  "$BUILD_DIR"/fiction*.data
)
shopt -u nullglob

if [[ ${#assets[@]} -eq 0 ]]; then
  echo "No fiction artifacts found in $BUILD_DIR" >&2
  exit 1
fi

echo "==> Using repo: $REPO"
echo "==> Release tag: $TAG"
echo "==> Build dir: $BUILD_DIR"
echo "==> Found artifacts:"
for f in "${assets[@]}"; do
  echo "   - $(basename "$f")"
done

created_new_release=0
if gh release view "$TAG" --repo "$REPO" >/dev/null 2>&1; then
  echo "==> Release $TAG exists. Uploading/clobbering artifacts."
  gh release upload "$TAG" "${assets[@]}" --repo "$REPO" --clobber
  gh release edit "$TAG" --repo "$REPO" --title "$TITLE" --notes "$NOTES"
else
  echo "==> Creating release $TAG on branch $TARGET_BRANCH"
  gh release create "$TAG" "${assets[@]}" \
    --repo "$REPO" \
    --target "$TARGET_BRANCH" \
    --title "$TITLE" \
    --notes "$NOTES"
  created_new_release=1
fi

dispatch_event="release"
if [[ $created_new_release -eq 0 ]]; then
  echo "==> Triggering Pages deploy workflow manually (release already existed)"
  gh workflow run "$WORKFLOW_NAME" --repo "$REPO" -f tag="$TAG"
  dispatch_event="workflow_dispatch"
fi

if [[ $WAIT_FOR_DEPLOY -eq 1 ]]; then
  echo "==> Waiting for Pages deploy workflow"
  sleep 3
  run_id="$(gh run list \
    --repo "$REPO" \
    --workflow "$WORKFLOW_NAME" \
    --event "$dispatch_event" \
    --limit 1 \
    --json databaseId \
    --jq '.[0].databaseId')"

  if [[ -n "${run_id:-}" && "$run_id" != "null" ]]; then
    gh run watch "$run_id" --repo "$REPO" --exit-status
  else
    echo "Could not resolve workflow run id automatically." >&2
    echo "Check runs with: gh run list --repo $REPO --workflow \"$WORKFLOW_NAME\"" >&2
  fi
fi

release_url="$(gh release view "$TAG" --repo "$REPO" --json url --jq .url)"
pages_url="$(gh api "repos/$REPO/pages" --jq .html_url 2>/dev/null || true)"

echo ""
echo "Release published: $release_url"
if [[ -n "$pages_url" ]]; then
  echo "Pages site: $pages_url"
fi
