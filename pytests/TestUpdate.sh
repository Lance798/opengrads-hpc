#!/usr/bin/env bash
# Added in 2026 for release updates. GPLv2; see COPYING.
#
# A release archive says at start-up when a newer release is out, and
# "opengrads --update" replaces it with that release. Here a local web
# server stands in for GitHub's releases page (latest redirects to the
# newest tag; the archives and their .sha256 lie under download/vX.Y.Z/),
# and small stand-in archives for the releases: the notice, its daily check
# in the background and what it costs at start-up, turning it off, no
# network; an update, an older version, a bad checksum, a version that does
# not start, one that is not there, a session still running, an install
# reached through a symbolic link, one that cannot be written, and a source
# checkout. Both launchers (Linux and macOS) pass --update on and show the
# notice in a terminal only.
#
# With OPENGRADS_UPDATE_ARCHIVE naming a real release archive, it is also
# updated to a copy of itself numbered one higher, which must then start.

set -euo pipefail

repo_root="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
helper="$repo_root/libexec/opengrads-update"
tmp="${TMPDIR:-/tmp}"
work="$(mktemp -d "${tmp%/}/opengrads-update-test.XXXXXX")"
work="$(CDPATH= cd -P -- "$work" && pwd)"   # as the updater sees it (macOS: /private/var)
server_pid=""
sleeper_pid=""

cleanup()
{
  [[ -n "$sleeper_pid" ]] && kill "$sleeper_pid" 2>/dev/null || true
  [[ -n "$server_pid" ]] && kill "$server_pid" 2>/dev/null || true
  chmod -R u+w "$work" 2>/dev/null || true
  rm -rf -- "$work"
}
trap cleanup EXIT

fail()
{
  printf 'FAIL: %s\n' "$1" >&2
  [[ -n "${2:-}" ]] && printf '%s\n' "$2" >&2
  exit 1
}

case "$(uname -s)" in
  Linux) os=linux ;;
  Darwin) os=macos ;;
  *) printf 'Update checks skipped: no archives for %s\n' "$(uname -s)"; exit 0 ;;
esac
machine="$(uname -m)"

sha256()
{
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$1" | awk '{print $1}'
  else
    shasum -a 256 "$1" | awk '{print $1}'
  fi
}

# The web server: GET .../releases/latest redirects to the tag named in
# the file "latest"; .../slow/latest answers after half a minute; files are
# served from the directory; every request is logged.
mkdir -p "$work/site/releases/download"
cat > "$work/server.py" <<'PYTHON'
import http.server, os, sys, time
site = sys.argv[1]
class Handler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *a, **k):
        super().__init__(*a, directory=site, **k)
    def log_message(self, fmt, *args):
        with open(os.path.join(site, 'requests.log'), 'a') as f:
            f.write(self.path + '\n')
    def do_GET(self):
        if self.path.startswith('/slow/'):
            time.sleep(30)
        if self.path.rstrip('/').endswith('/latest'):
            with open(os.path.join(site, 'latest')) as f:
                v = f.read().strip()
            self.send_response(302)
            self.send_header('Location', 'http://127.0.0.1:%d/releases/tag/v%s'
                             % (self.server.server_port, v))
            self.send_header('Content-Length', '0')
            self.end_headers()
            return
        super().do_GET()
    do_HEAD = do_GET
server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
with open(os.path.join(site, 'port'), 'w') as f:
    f.write(str(server.server_port))
server.serve_forever()
PYTHON
python3 "$work/server.py" "$work/site" >/dev/null 2>&1 &
server_pid=$!
for _ in $(seq 100); do
  [[ -s "$work/site/port" ]] && break
  sleep 0.1
done
[[ -s "$work/site/port" ]] || fail 'the stand-in release server did not start'
port="$(<"$work/site/port")"
url="http://127.0.0.1:$port/releases"
export OPENGRADS_UPDATE_URL="$url"
export XDG_CACHE_HOME="$work/cache"
unset OPENGRADS_UPDATE_CHECK
cache="$XDG_CACHE_HOME/opengrads-hpc/latest"

latest_asked()
{
  grep -c '^/releases/latest' "$work/site/requests.log" 2>/dev/null || true
}

# A stand-in release: the VERSION file, this helper, and a launcher that
# passes --update on and otherwise "starts" (or, KIND=broken, fails to).
# KIND=badsum publishes a checksum of something else.
release()
{
  local v="$1" kind="${2:-good}" base dir out
  base="opengrads-hpc-$v-$os-$machine"
  dir="$work/stage/$base"
  out="$work/site/releases/download/v$v"
  rm -rf "$dir"
  mkdir -p "$dir/libexec" "$out"
  printf 'opengrads-hpc %s\nGrADS base 2.2.1.oga.1\n' "$v" > "$dir/VERSION"
  cp "$helper" "$dir/libexec/opengrads-update"
  {
    printf '#!/usr/bin/env bash\n'
    printf 'root="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"\n'
    printf 'if [[ "${1:-}" == --update ]]; then shift; exec bash "$root/libexec/opengrads-update" "$root" "$@"; fi\n'
    if [[ "$kind" == broken ]]; then
      printf 'echo "error while loading shared libraries: libadios2_c.so.2"; exit 127\n'
    else
      printf 'echo "GX Package Initialization: Size = 11 8.5"; echo "GX Package Terminated"\n'
    fi
  } > "$dir/opengrads"
  chmod +x "$dir/opengrads"
  tar -czf "$out/$base.tar.gz" -C "$work/stage" "$base"
  if [[ "$kind" == badsum ]]; then
    printf 'something else\n' > "$work/stage/else"
    printf '%s  %s\n' "$(sha256 "$work/stage/else")" "$base.tar.gz" > "$out/$base.tar.gz.sha256"
  else
    printf '%s  %s\n' "$(sha256 "$out/$base.tar.gz")" "$base.tar.gz" > "$out/$base.tar.gz.sha256"
  fi
}

release 1.0.11
release 1.0.12
release 1.0.13 badsum
release 1.0.14 broken
printf '1.0.12\n' > "$work/site/latest"

mkdir -p "$work/inst"
tar -xzf "$work/site/releases/download/v1.0.11/opengrads-hpc-1.0.11-$os-$machine.tar.gz" \
  -C "$work/inst"
inst="$work/inst/opengrads-hpc-1.0.11-$os-$machine"
version_of() { awk 'NR == 1 {print $2}' "$1/VERSION"; }

# 1. At start-up with nothing known yet: no line, and the latest release is
#    asked for in the background; the next start-up says it is out.
said="$(bash "$helper" "$inst" --notice 2>&1)"
[[ -z "$said" ]] || fail 'a notice came before any release was known' "$said"
for _ in $(seq 100); do
  [[ -s "$cache" ]] && break
  sleep 0.1
done
[[ "$(cat "$cache" 2>/dev/null)" == 1.0.12 ]] ||
  fail 'the background check did not note the latest release' "$(cat "$cache" 2>/dev/null)"
(( $(latest_asked) == 1 )) || fail "the latest release was asked for $(latest_asked) times, not once"
said="$(bash "$helper" "$inst" --notice 2>&1)"
[[ "$said" == "opengrads-hpc 1.0.12 is out (this is 1.0.11); to update: $inst/opengrads --update" ]] ||
  fail 'the notice did not say that 1.0.12 is out' "$said"
sleep 0.5
(( $(latest_asked) == 1 )) || fail 'the latest release was asked for again within the day'

# 2. A day later it is asked again; OPENGRADS_UPDATE_CHECK=0 asks nothing
#    and says nothing.
touch -t 202001010000 "$cache"
said="$(OPENGRADS_UPDATE_CHECK=0 bash "$helper" "$inst" --notice 2>&1)"
sleep 0.5
[[ -z "$said" && $(latest_asked) == 1 ]] ||
  fail 'OPENGRADS_UPDATE_CHECK=0 did not turn the notice off' "$said"
bash "$helper" "$inst" --notice >/dev/null 2>&1
for _ in $(seq 100); do
  (( $(latest_asked) == 2 )) && break
  sleep 0.1
done
(( $(latest_asked) == 2 )) || fail 'the latest release was not asked for again a day later'

# 3. Versions compare as numbers: 1.0.9 is older than 1.0.11, 1.0.100 newer.
printf '1.0.9\n' > "$cache"
said="$(bash "$helper" "$inst" --notice 2>&1)"
[[ -z "$said" ]] || fail '1.0.9 was taken for newer than 1.0.11' "$said"
printf '1.0.100\n' > "$cache"
said="$(bash "$helper" "$inst" --notice 2>&1)"
[[ "$said" == *'1.0.100 is out'* ]] || fail '1.0.100 was not taken for newer than 1.0.11' "$said"

# 4. No network, or a slow one, costs nothing at start-up: the asking goes
#    on in the background, and is noted so as not to be repeated all day.
start=$(date +%s)
said="$(XDG_CACHE_HOME="$work/cache-slow" OPENGRADS_UPDATE_URL="http://127.0.0.1:$port/slow" \
        bash "$helper" "$inst" --notice 2>&1)"
(( $(date +%s) - start <= 2 )) || fail 'a slow release server held up the start'
[[ -z "$said" ]] || fail 'a slow release server brought a notice' "$said"
said="$(XDG_CACHE_HOME="$work/cache-off" OPENGRADS_UPDATE_URL="http://127.0.0.1:1/releases" \
        bash "$helper" "$inst" --notice 2>&1)"
[[ -z "$said" ]] || fail 'no network brought a notice' "$said"
for _ in $(seq 100); do
  [[ -e "$work/cache-off/opengrads-hpc/latest" ]] && break
  sleep 0.1
done
[[ -e "$work/cache-off/opengrads-hpc/latest" &&
   -n "$(find "$work/cache-off/opengrads-hpc/latest" -mmin -5)" ]] ||
  fail 'a check without network was not noted, so it would be repeated at every start'

# 5. Both launchers pass --update on, and show the notice in a terminal
#    only, before GrADS starts.
printf '1.0.12\n' > "$cache"
for launcher in "$repo_root/opengrads" "$repo_root/release/opengrads-macos"; do
  fake="$work/launch/$(basename "$launcher")"
  mkdir -p "$fake/libexec"
  cp "$launcher" "$fake/opengrads"
  cp "$helper" "$fake/libexec/opengrads-update"
  printf 'opengrads-hpc 1.0.11\n' > "$fake/VERSION"
  said="$("$fake/opengrads" --update --check 2>&1)" || true
  [[ "$said" == "opengrads-hpc 1.0.12 is out (this is 1.0.11); to update: $fake/opengrads --update" ]] ||
    fail "$(basename "$launcher"): --update --check did not reach the helper" "$said"
  said="$(OPENGRADS_BUILD_ROOT=/nonexistent python3 - "$fake/opengrads" <<'PYTHON'
import os, pty, sys, select, time
pid, fd = pty.fork()
if pid == 0:
    os.execv(sys.argv[1], [sys.argv[1], '-bl'])
out, end = b'', time.time() + 10
while time.time() < end:
    r, _, _ = select.select([fd], [], [], 0.2)
    if r:
        try:
            chunk = os.read(fd, 4096)
        except OSError:
            break
        if not chunk:
            break
        out += chunk
os.waitpid(pid, 0)
sys.stdout.write(out.decode('utf-8', 'replace'))
PYTHON
)"
  [[ "$said" == *'opengrads-hpc 1.0.12 is out (this is 1.0.11)'* ]] ||
    fail "$(basename "$launcher"): no notice at start-up in a terminal" "$said"
  said="$(OPENGRADS_BUILD_ROOT=/nonexistent "$fake/opengrads" -bl < /dev/null 2>&1 | cat)" || true
  [[ "$said" != *'is out'* ]] ||
    fail "$(basename "$launcher"): a notice at start-up without a terminal" "$said"
done

# 6. --update installs the latest release in place, keeping the old one;
#    run from inside the install, it says to cd there again.
said="$(cd "$inst" && ./opengrads --update 2>&1)" || fail '--update failed' "$said"
[[ "$(version_of "$inst")" == 1.0.12 && "$(version_of "$inst.previous")" == 1.0.11 ]] ||
  fail '--update did not put 1.0.12 in place and keep 1.0.11 beside it' "$said"
[[ "$said" == *"opengrads-hpc 1.0.12 is installed in $inst."* ]] ||
  fail '--update did not say what it installed' "$said"
ls -a "$work/inst" | grep -q '^\.opengrads-update' && fail '--update left its download behind'
[[ "$said" == *"cd $inst first, or ./opengrads starts 1.0.11."* ]] ||
  fail '--update from inside the install did not say to cd there again' "$said"
said="$("$inst/opengrads" --update 2>&1)" || fail '--update of the latest failed' "$said"
[[ "$said" == *'1.0.12 is the latest release; nothing to do.'* ]] ||
  fail '--update of the latest release did something' "$said"

# 7. A version by number, older too; the earlier .previous makes way.
said="$("$inst/opengrads" --update 1.0.11 2>&1)" || fail '--update 1.0.11 failed' "$said"
[[ "$(version_of "$inst")" == 1.0.11 && "$(version_of "$inst.previous")" == 1.0.12 ]] ||
  fail '--update 1.0.11 did not go back to 1.0.11' "$said"

# 8. What must not be installed is not, and leaves nothing behind: a bad
#    checksum, a version that does not start here, one that does not exist.
for case in '1.0.13:does not match its checksum' '1.0.14:does not start on this machine' \
            '1.0.15:Could not download'; do
  if said="$("$inst/opengrads" --update "${case%%:*}" 2>&1)"; then
    fail "--update ${case%%:*} succeeded" "$said"
  fi
  [[ "$said" == *"${case#*:}"* ]] || fail "--update ${case%%:*} did not say why it failed" "$said"
  [[ "$(version_of "$inst")" == 1.0.11 && "$(version_of "$inst.previous")" == 1.0.12 ]] ||
    fail "--update ${case%%:*} changed the install"
  ls -a "$work/inst" | grep -q '^\.opengrads-update' &&
    fail "--update ${case%%:*} left its download behind"
done

# 9. Not while GrADS runs from the install, unless --force.
# The session: a program in the install that keeps running, a copy of bash
# (not of sleep, which is a script on some systems, coreutils-single's,
# and so runs as /usr/bin/coreutils).
cp "$(command -v bash)" "$inst/grads-stand-in"
"$inst/grads-stand-in" -c 'sleep 30; :' &
sleeper_pid=$!
sleep 0.3
if said="$("$inst/opengrads" --update 2>&1)"; then
  fail '--update went ahead while GrADS ran from the install' "$said"
fi
[[ "$said" == *"GrADS is running from this install (process $sleeper_pid)"* ]] ||
  fail '--update did not say that GrADS runs from the install' "$said"
said="$("$inst/opengrads" --update --force 2>&1)" || fail '--update --force failed' "$said"
[[ "$(version_of "$inst")" == 1.0.12 ]] || fail '--update --force did not update'
kill "$sleeper_pid" 2>/dev/null || true
wait "$sleeper_pid" 2>/dev/null || true
sleeper_pid=""

# 10. Through a symbolic link, the directory it points to is updated, and
#     the link still leads to it.
ln -s "$inst" "$work/inst/opengrads"
said="$("$work/inst/opengrads/opengrads" --update 1.0.11 2>&1)" ||
  fail '--update through a symbolic link failed' "$said"
[[ -L "$work/inst/opengrads" && "$(version_of "$work/inst/opengrads")" == 1.0.11 &&
   "$(version_of "$inst.previous")" == 1.0.12 ]] ||
  fail '--update through a symbolic link did not update the directory behind it' "$said"

# 11. An install that cannot be written (a shared one) says how to do it
#     by hand. (root can write anything, so not as root.)
if (( EUID != 0 )); then
  chmod a-w "$work/inst"
  if said="$("$inst/opengrads" --update 2>&1)"; then
    chmod u+w "$work/inst"
    fail '--update went ahead in an install that cannot be written' "$said"
  fi
  chmod u+w "$work/inst"
  [[ "$said" == *'you cannot change this install'* &&
     "$said" == *"curl -fLO $url/download/v1.0.12/opengrads-hpc-1.0.12-$os-$machine.tar.gz"* ]] ||
    fail '--update did not say how to install by hand' "$said"
  [[ "$(version_of "$inst")" == 1.0.11 ]] || fail 'an install that cannot be written changed'
  readonly_note=', an install that cannot be written'
else
  readonly_note=' (not the read-only case: running as root)'
fi

# 12. A source checkout is updated with git.
if said="$(bash "$helper" "$repo_root" 2>&1)"; then
  fail '--update in a source checkout succeeded' "$said"
fi
[[ "$said" == *'source checkout'*'git pull'* ]] ||
  fail '--update in a source checkout did not point to git' "$said"

# 13. A real archive updates to a copy of itself numbered one higher, which
#     then starts.
real_note=""
if [[ -n "${OPENGRADS_UPDATE_ARCHIVE:-}" ]]; then
  unset OPENGRADS_BUILD_ROOT OPENGRADS_LAUNCHER
  mkdir -p "$work/real" "$work/realstage"
  tar -xzf "$OPENGRADS_UPDATE_ARCHIVE" -C "$work/real"
  real="$(find "$work/real" -mindepth 1 -maxdepth 1 -type d | head -n 1)"
  from="$(version_of "$real")"
  to="$from.1"
  base="opengrads-hpc-$to-$os-$machine"
  cp -R "$real" "$work/realstage/$base"
  sed "1s/.*/opengrads-hpc $to/" "$real/VERSION" > "$work/realstage/$base/VERSION"
  mkdir -p "$work/site/releases/download/v$to"
  tar -czf "$work/site/releases/download/v$to/$base.tar.gz" -C "$work/realstage" "$base"
  printf '%s  %s\n' "$(sha256 "$work/site/releases/download/v$to/$base.tar.gz")" "$base.tar.gz" \
    > "$work/site/releases/download/v$to/$base.tar.gz.sha256"
  rm -rf "$work/realstage"
  printf '%s\n' "$to" > "$work/site/latest"
  said="$("$real/opengrads" --update 2>&1)" || fail "the real archive did not update to $to" "$said"
  [[ "$(version_of "$real")" == "$to" ]] || fail "the real archive is not $to after --update" "$said"
  said="$(OPENGRADS_COLOR=0 "$real/opengrads" -bl -d gxdummy -h gxdummy -c quit < /dev/null 2>&1)" ||
    fail "the updated real archive does not start" "$said"
  [[ "$said" == *'GX Package Terminated'* ]] || fail "the updated real archive does not start" "$said"
  real_note="; $(basename "$OPENGRADS_UPDATE_ARCHIVE") updated to $to and started"
fi

printf 'Update checks passed: notice and daily check, off, offline, update, older version, '
printf 'bad checksum, broken and missing archives, running session, symbolic link%s, ' "$readonly_note"
printf 'source checkout, both launchers%s\n' "$real_note"
