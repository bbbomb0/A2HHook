#!/system/bin/sh

# Device-only contract for the per-package session directory used by the
# production watcher/trigger pair. It verifies the narrow DAC boundary needed
# by trigger.c's sibling-temp-file + rename publication.

set -u

BASE=${A2H_TEST_BASE:-/data/local/tmp}/a2h_session_uid_$$
APP_UID=${1:-}
OTHER_UID=${2:-}
PACKAGE_DIR="$BASE/sessions/example.package"
SESSION="$PACKAGE_DIR/app-session"
TEMP="$PACKAGE_DIR/.app-session.tmp"

fail() {
  printf 'FAIL %s\n' "$1"
  rm -rf "$BASE" 2>/dev/null || true
  exit 1
}

cleanup() {
  rm -rf "$BASE" 2>/dev/null || true
}
trap cleanup EXIT INT TERM HUP

case "$APP_UID" in ''|*[!0-9]*) fail invalid-app-uid ;; esac
case "$OTHER_UID" in ''|*[!0-9]*) fail invalid-other-uid ;; esac
[ "$APP_UID" -ge 10000 ] 2>/dev/null || fail app-uid-range
[ "$OTHER_UID" -ge 10000 ] 2>/dev/null || fail other-uid-range
[ "$APP_UID" != "$OTHER_UID" ] || fail uid-not-distinct

mkdir -p "$PACKAGE_DIR" || fail mkdir
chown 0:"$APP_UID" "$PACKAGE_DIR" || fail chown
chmod 1730 "$PACKAGE_DIR" || fail chmod

mode=$(stat -c '%a' "$PACKAGE_DIR" 2>/dev/null || stat -f '%Lp' "$PACKAGE_DIR" 2>/dev/null || true)
owner=$(stat -c '%u:%g' "$PACKAGE_DIR" 2>/dev/null || stat -f '%u:%g' "$PACKAGE_DIR" 2>/dev/null || true)
[ "$mode" = 1730 ] || fail "mode=$mode"
[ "$owner" = "0:$APP_UID" ] || fail "owner=$owner"

# The target app UID must be able to create and atomically replace its own
# sibling file, exactly as trigger.c does after AAudio becomes ready.
su "$APP_UID" -c "printf '465 ready\\n' > '$TEMP' && mv -f '$TEMP' '$SESSION'" || fail app-uid-publish
[ -f "$SESSION" ] || fail session-missing
[ "$(cat "$SESSION" 2>/dev/null)" = '465 ready' ] || fail session-content
[ ! -e "$TEMP" ] || fail temp-left-behind

# Sticky mode plus the UID-specific group must reject another app UID.
if su "$OTHER_UID" -c "printf '999 ready\\n' > '$PACKAGE_DIR/other.tmp' && mv -f '$PACKAGE_DIR/other.tmp' '$PACKAGE_DIR/other-session'"; then
  fail unrelated-uid-publish
fi
[ ! -e "$PACKAGE_DIR/other-session" ] || fail unrelated-session-created

rm -f "$SESSION"
[ ! -e "$SESSION" ] || fail session-remove
printf 'fallback:70\n' > "$BASE/lease"
[ "$(cat "$BASE/lease")" = fallback:70 ] || fail fallback-token
printf 'policy-deny\n' > "$BASE/lease"
[ "$(cat "$BASE/lease")" = policy-deny ] || fail policy-token
rm -f "$BASE/lease"
[ ! -e "$BASE/lease" ] || fail policy-token-cleanup

printf 'PASS session UID directory 1730 atomic publish and isolation\n'
