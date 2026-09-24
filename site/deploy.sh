#!/usr/bin/env bash
# Upload site/public/ to smartlada.saschapo.me (vdsina).
#   site/deploy.sh          upload, delete what is gone locally, ping IndexNow
#   site/deploy.sh --dry    show what would change, touch nothing
# site/public/ is exactly what lives on the server in /var/www/smartlada.
set -euo pipefail

root="$(cd "$(dirname "$0")" && pwd)"
host="${SMARTLADA_HOST:-vps}"          # ~/.ssh/config alias for root@178.217.99.181
dry=()
[ "${1:-}" = "--dry" ] && dry=(--dry-run)

# Files saved with mode 600 give nginx a 403; macOS rsync 2.6.9 has no --chmod, so fix them here.
chmod -R a+rX "$root/public"

rsync -avz --delete --exclude '.DS_Store' ${dry[@]+"${dry[@]}"} "$root/public/" "$host:/var/www/smartlada/"

# IndexNow (Yandex, Bing). Key file: public/<key>.txt. A failure here does not fail the deploy.
if [ ${#dry[@]} -eq 0 ]; then
  key="9894ba0138df12e13bbf102c25338e79"
  code="$(curl -s -o /dev/null -w '%{http_code}' -X POST https://yandex.com/indexnow \
    -H 'Content-Type: application/json; charset=utf-8' \
    -d "{\"host\":\"smartlada.saschapo.me\",\"key\":\"$key\",\"keyLocation\":\"https://smartlada.saschapo.me/$key.txt\",\"urlList\":[\"https://smartlada.saschapo.me/\"]}" || true)"
  echo "IndexNow: HTTP $code (200/202 = accepted)"
fi
