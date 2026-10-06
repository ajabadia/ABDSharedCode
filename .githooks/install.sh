#!/usr/bin/env bash
#
# install.sh — install ABDSharedCode git hooks
#
# Configures `core.hooksPath` so that git uses the hooks in `.githooks/`
# instead of the default `.git/hooks/` directory.
#
# Run once after cloning:
#   bash .githooks/install.sh
#
# To uninstall:
#   git config --unset core.hooksPath
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "Installing ABDSharedCode pre-commit hooks..."
echo "  hooks path: $SCRIPT_DIR"

git config core.hooksPath "$SCRIPT_DIR"

echo ""
echo "✓  core.hooksPath configured."
echo ""
echo "The pre-commit hook will auto-format staged .h/.cpp files with clang-format."
echo "Run 'bash .githooks/install.sh' again if you re-clone or switch branches."
echo ""
echo "To uninstall: git config --unset core.hooksPath"
