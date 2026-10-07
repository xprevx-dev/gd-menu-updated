#!/usr/bin/env bash
# Re-point this clone at Prevx's own repo and set commit identity.
# Run this if the remotes/identity ever look wrong:  bash scripts/setup-git.sh
set -euo pipefail
cd "$(dirname "$0")/.."
git remote set-url origin   https://github.com/xprevx-dev/gd-menu-updated.git || git remote add origin   https://github.com/xprevx-dev/gd-menu-updated.git
git remote set-url upstream https://github.com/Cyber39DreamGD/GDMenu.git      || git remote add upstream https://github.com/Cyber39DreamGD/GDMenu.git
git config user.name  "Prevx"
git config user.email "334238404+xprevx-dev@users.noreply.github.com"
echo "origin   -> $(git remote get-url origin)"
echo "upstream -> $(git remote get-url upstream)"
echo "identity -> $(git config user.name) <$(git config user.email)>"
