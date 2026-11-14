#!/usr/bin/env bash
# Signale un résultat des workflows dans une issue de T1000A : commente l'issue ouverte de même titre, sinon la crée.
#   tools/ci-issue.sh "<titre>" <fichier du message>     (GH_TOKEN et GITHUB_REPOSITORY fournis par Actions)
set -euo pipefail
title=$1 body=$2
num=$(gh issue list --repo "$GITHUB_REPOSITORY" --state open --limit 100 --json number,title \
      --jq "map(select(.title == \"$title\")) | first | .number // empty")
if [ -n "$num" ]; then
  gh issue comment "$num" --repo "$GITHUB_REPOSITORY" --body-file "$body"
else
  gh issue create --repo "$GITHUB_REPOSITORY" --title "$title" --body-file "$body"
fi
