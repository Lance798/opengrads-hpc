#!/usr/bin/env bash
# Added in 2026 for the scripts from gscript (lib/scripts/gscript). GPLv2;
# see COPYING (the scripts themselves are GPLv3; see their LICENSE).
#
# The launcher puts lib/scripts/gscript on GASCRP after lib/scripts, so:
# every gscript script is found and starts (GrADS reads a script as it
# runs it, so only the lines run are checked); color
# sets shading levels, and with -xcbar runs xcbar.gs from beside it; a
# script elsewhere loads gscript's functions with gsfallow('on'); and the
# bGASL scripts in lib/scripts come first.

set -euo pipefail

repo_root="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build_root="${OPENGRADS_BUILD_ROOT:-/tmp/opengrads-build-cpu}"
launcher="${OPENGRADS_LAUNCHER:-$repo_root/opengrads}"
data="$repo_root/pytests/data"
gscript="$repo_root/lib/scripts/gscript"

tmp="${TMPDIR:-/tmp}"
work="$(mktemp -d "${tmp%/}/opengrads-gscript-test.XXXXXX")"
trap 'rm -rf -- "$work"' EXIT

fail()
{
  printf 'FAIL: %s\n' "$1" >&2
  [[ -n "${2:-}" ]] && printf '%s\n' "$2" >&2
  exit 1
}

grads()
{
  (cd "$work" && HOME="$work" OPENGRADS_BUILD_ROOT="$build_root" OPENGRADS_COLOR=0 \
    GA_PROGRESS=off "$launcher" -bl -d gxdummy -h gxdummy 2>&1)
}

# 1. Every script is found and starts. Without arguments most print their
#    help; some act and complain of no data, which is fine here.
{
  printf 'open %s\n' "$data/model.ctl"
  for f in "$gscript"/*.gs; do
    printf 'run %s\n' "$(basename "$f")"
  done
  printf 'quit\n'
} > "$work/all.cmds"
scripts=$(grep -c '^run ' "$work/all.cmds")
out="$(grads < "$work/all.cmds")"
bad="$(grep -B2 'Syntax Error\|Unmatched\|Error opening script file\|Error executing script file' \
  <<< "$out" || true)"
[[ -z "$bad" ]] || fail 'a gscript script was not found, or did not start' "$bad"
(( scripts >= 25 )) || fail "only $scripts gscript scripts were found" "$(ls "$gscript")"

# 2. color sets the levels, typed as a command; -xcbar draws the colour bar
#    with xcbar.gs, found beside color.gs.
cat > "$work/color.cmds" <<CMDS
open $data/model.ctl
set gxout shaded
color 220 280 -kind bluered -xcbar
d ts
q shades
quit
CMDS
out="$(grads < "$work/color.cmds")"
grep -Fqx 'clevs= 220 226 232 238 244 250 256 262 268 274 280' <<< "$out" &&
  grep -Fq -- '-> xcbar ' <<< "$out" && grep -Fqx 'Number of levels = 12' <<< "$out" ||
  fail 'color did not set the levels, or did not run xcbar' "$out"
grep -q 'Error\|Unknown command' <<< "$out" && fail 'color or xcbar reported an error' "$out"

# 3. A script elsewhere loads gscript's functions.
cat > "$work/funcs.gs" <<'GS'
function main()
rc = gsfallow('on')
say 'strrep: 'strrep('a-b-c', '-', '+')
say 'strtrim: ['strtrim('  x  ')']'
say 'chcase: 'chcase('hello', 'upper')
say 'cmonth: 'cmonth(3)
say 'printf: 'printf('%03d', 7)
GS
out="$(printf 'run funcs.gs\nquit\n' | grads)"
for line in 'strrep: a+b+c' 'strtrim: [x]' 'chcase: HELLO' 'cmonth: March' 'printf: 007'; do
  grep -Fqx "$line" <<< "$out" || fail "a gscript function gave no \"$line\"" "$out"
done

# 4. lib/scripts comes first on GASCRP, so its scripts keep their names.
out="$(printf '!echo "[$GASCRP]"\nquit\n' | grads)"
grep -q "\[.*$repo_root/lib/scripts[ :;,]\+$repo_root/lib/scripts/gscript\]" <<< "$out" ||
  grep -q "\[$repo_root/lib/scripts[ :;,]\+$repo_root/lib/scripts/gscript[ :;,]" <<< "$out" ||
  fail 'GASCRP does not have lib/scripts, then lib/scripts/gscript' "$out"

# 5. Directories on GASCRP may be separated by colons, as the macOS
#    launchers do it, and not only by spaces: GrADS used to search the
#    first only. A script and a function in the second are found.
mkdir -p "$work/one" "$work/two" "$work/elsewhere"
printf "function main()\nrc = gsfallow('on')\nsay 'second: 'twice(21)\n" > "$work/two/second.gs"
printf "function twice(n)\nreturn n*2\n" > "$work/two/twice.gsf"
printf "function main()\nrc = gsfallow('on')\nsay 'from here: 'twice(4)\n" \
  > "$work/elsewhere/caller.gs"
out="$(cd "$work/elsewhere" && printf 'run second.gs\nrun caller.gs\nquit\n' |
  GASCRP="$work/one:$work/two" HOME="$work" OPENGRADS_BUILD_ROOT="$build_root" \
    OPENGRADS_COLOR=0 "$launcher" -bl -d gxdummy -h gxdummy 2>&1)"
grep -Fqx 'second: 42' <<< "$out" && grep -Fqx 'from here: 8' <<< "$out" ||
  fail 'a script or function in the second of two GASCRP directories was not found' "$out"

# 6. A script of the user's own by the same name comes first: from the
#    current directory, then from the user's GASCRP, before lib/scripts and
#    gscript. A function of the user's own by the same name is the one the
#    user's scripts get, while the bundled scripts keep their own.
mkdir -p "$work/mine" "$work/here"
printf "function main(args)\nsay 'my color 'args\n" > "$work/here/color.gs"
printf "function main(args)\nsay 'my xcbar 'args\n" > "$work/mine/xcbar.gs"
printf "function chcase(s, t)\nreturn 'mine'\n" > "$work/mine/chcase.gsf"
printf "function main()\nrc = gsfallow('on')\nsay 'chcase gives 'chcase('a', 'upper')\n" \
  > "$work/here/usechcase.gs"
cat > "$work/mine.cmds" <<CMDS
color 0 10
xcbar 1 2
run usechcase.gs
open $data/model.ctl
run $repo_root/lib/scripts/gscript/color.gs 0 10 -kind bluered
quit
CMDS
out="$(cd "$work/here" && GASCRP="$work/mine" HOME="$work" OPENGRADS_BUILD_ROOT="$build_root" \
  OPENGRADS_COLOR=0 "$launcher" -bl -d gxdummy -h gxdummy < "$work/mine.cmds" 2>&1)"
grep -Fqx 'my color 0 10' <<< "$out" ||
  fail 'a script of the same name in the current directory did not come first' "$out"
grep -Fqx 'my xcbar 1 2' <<< "$out" ||
  fail "a script of the same name on the user's GASCRP did not come first" "$out"
grep -Fqx 'chcase gives mine' <<< "$out" ||
  fail "the user's script did not get the user's function of the same name" "$out"
grep -Fqx 'clevs= 0 1 2 3 4 5 6 7 8 9 10' <<< "$out" ||
  fail "gscript's color did not keep its own functions beside a user's of the same name" "$out"

printf 'gscript checks passed: %d scripts start, color and xcbar, functions, GASCRP order and colons, the user'"'"'s own first\n' \
  "$scripts"
