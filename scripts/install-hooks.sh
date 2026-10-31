#!/usr/bin/env bash
# Installs scripts/pre-commit as the repository's pre-commit hook (works in Git Bash on Windows).
set -eu
ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"
chmod +x scripts/pre-commit
git config core.hooksPath .githooks
mkdir -p .githooks
cat > .githooks/pre-commit <<'HOOK'
#!/usr/bin/env bash
exec "$(git rev-parse --show-toplevel)/scripts/pre-commit" "$@"
HOOK
chmod +x .githooks/pre-commit
echo "hook installed (core.hooksPath=.githooks). Add your local user name to .precommit-private-patterns."
echo "CI should run:  scripts/pre-commit --all"
