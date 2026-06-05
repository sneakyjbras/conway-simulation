#!/usr/bin/env bash
# Run local pre-commit quality checks:
#   1. C++ formatting via clang-format
#   2. C++ linting   via clang-tidy
#
# Formatting runs first and may update files in-place.
# Linting starts only after formatting finishes.
# A final summary reports whether formatting was needed and whether linting passed.

set -uo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${PROJECT_ROOT}"

CPP_FORMAT_STATUS=0
CPP_LINT_STATUS=0
CPP_FORMAT_CHANGED="unknown"

has_git_worktree() {
  git rev-parse --is-inside-work-tree >/dev/null 2>&1
}

snapshot_git_diff() {
  local output_path="$1"
  if has_git_worktree; then
    git diff --binary > "${output_path}"
  else
    : > "${output_path}"
  fi
}

run_step() {
  local label="$1"
  shift
  echo
  echo "==> ${label}"
  "$@"
}

run_formatter() {
  local result_var="$1"
  local changed_var="$2"
  local label="$3"
  shift 3

  local before_diff after_diff
  before_diff="$(mktemp)"
  after_diff="$(mktemp)"

  if has_git_worktree; then
    snapshot_git_diff "${before_diff}"
  fi

  run_step "${label}" "$@"
  local status=$?

  if has_git_worktree; then
    snapshot_git_diff "${after_diff}"
    if cmp -s "${before_diff}" "${after_diff}"; then
      printf -v "${changed_var}" '%s' "no"
    else
      printf -v "${changed_var}" '%s' "yes"
    fi
  else
    printf -v "${changed_var}" '%s' "unknown"
  fi

  rm -f "${before_diff}" "${after_diff}"
  printf -v "${result_var}" '%s' "${status}"
}

run_linter() {
  local result_var="$1"
  local label="$2"
  shift 2
  run_step "${label}" "$@"
  local status=$?
  printf -v "${result_var}" '%s' "${status}"
}

format_summary() {
  local status="$1"
  local changed="$2"

  if [[ "${status}" -ne 0 ]]; then
    echo "failed"
    return
  fi
  case "${changed}" in
    yes) echo "needed formatting; files were updated" ;;
    no)  echo "already formatted" ;;
    *)   echo "completed; formatting changes could not be detected (not a git worktree)" ;;
  esac
}

lint_summary() {
  local status="$1"
  if [[ "${status}" -eq 0 ]]; then echo "passed"; else echo "failed"; fi
}

run_formatter CPP_FORMAT_STATUS CPP_FORMAT_CHANGED "C++ formatting: clang-format" \
  ./scripts/run-clang-format.sh

echo
echo "==> Formatting finished. Starting linting."

run_linter CPP_LINT_STATUS "C++ linting: clang-tidy" \
  ./scripts/run-clang-tidy.sh

echo
echo "Quality summary"
echo "---------------"
echo "C++ formatting: $(format_summary "${CPP_FORMAT_STATUS}" "${CPP_FORMAT_CHANGED}")"
echo "C++ linting:    $(lint_summary "${CPP_LINT_STATUS}")"

if has_git_worktree && git diff --quiet --exit-code; then
  echo "Working tree:   no tracked formatting changes"
elif has_git_worktree; then
  echo "Working tree:   tracked changes present; review with: git diff"
else
  echo "Working tree:   not checked"
fi

if [[ "${CPP_FORMAT_STATUS}" -ne 0 || "${CPP_LINT_STATUS}" -ne 0 ]]; then
  echo
  echo "One or more checks failed. See the output above."
  exit 1
fi

echo
echo "All formatting and linting commands completed successfully."
