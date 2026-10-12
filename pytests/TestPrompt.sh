#!/usr/bin/env bash
# Added in 2026 for the prompt: ~ in file names, usage hints after errors,
# and Tab completion. GPLv2; see COPYING.
#
# 1. A file name may start with ~ for the home directory, as in a shell:
#    in open, printim, run (the script, not its arguments), a script's
#    read and write, and a descriptor's DSET and INDEX.
# 2. A command typed at the prompt that fails is followed by its usage: a
#    line of syntax and an example, for functions, set and query options,
#    draw and the commands; a name that is not known by the nearest known
#    ones. Commands run by a script print what they always printed, since
#    scripts read it.
# 3. Tab completes, at the prompt, set options and their keywords (on|off,
#    the kinds of gxout), query options, variables, defined variables and
#    functions in an expression, scripts for run, and file names after ~.
#    Only with GNU Readline in the build.

set -euo pipefail

repo_root="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build_root="${OPENGRADS_BUILD_ROOT:-/tmp/opengrads-build-cpu}"
launcher="${OPENGRADS_LAUNCHER:-$repo_root/opengrads}"
data="$repo_root/pytests/data"

tmp="${TMPDIR:-/tmp}"
work="$(mktemp -d "${tmp%/}/opengrads-prompt-test.XXXXXX")"
work="$(CDPATH= cd -P -- "$work" && pwd)"
trap 'rm -rf -- "$work"' EXIT

fail()
{
  printf 'FAIL: %s\n' "$1" >&2
  [[ -n "${2:-}" ]] && printf '%s\n' "$2" >&2
  exit 1
}

# GrADS sees $work/home as the home directory.
home="$work/home"
mkdir -p "$home/data" "$home/gs"
cp "$data/model.ctl" "$data/model.grb" "$data/model.gmp" "$home/data/"
sed -e 's|^dset .*|dset ~/data/model.grb|' -e 's|^index .*|index ~/data/model.gmp|' \
  "$data/model.ctl" > "$home/data/tilde.ctl"
cat > "$home/gs/args.gs" <<'GS'
function main(args)
  say 'args: 'args
  rc = write('~/out.txt', 'written to ~')
  rc = close('~/out.txt')
  rec = read('~/out.txt')
  say 'read back: 'sublin(rec,2)
  'set gxout'
  say 'result of a failed set: 'result
GS

grads()
{
  HOME="$home" OPENGRADS_BUILD_ROOT="$build_root" OPENGRADS_COLOR=0 GA_PROGRESS=off \
    "$launcher" -bl -d gxdummy -h gxdummy 2>&1
}

# 1. ~
out="$(cd "$work" && grads <<'CMDS'
open ~/data/model.ctl
q file
close 1
open ~/data/tilde.ctl
d aave(ts,lon=0,lon=360,lat=-90,lat=90)
printim ~/plot.png
run ~/gs/args.gs ~/not-a-path
quit
CMDS
)"
grep -q "Descriptor: $home/data/model.ctl" <<< "$out" ||
  fail 'open ~/data/model.ctl did not open the file in the home directory' "$out"
grep -Eq '^Result value = 2[0-9][0-9]\.' <<< "$out" ||
  fail 'a descriptor whose DSET and INDEX start with ~ did not read its data' "$out"
grep -q "Created PNG file $home/plot.png" <<< "$out" ||
  fail 'printim ~/plot.png did not write to the home directory' "$out"
grep -qx 'args: ~/not-a-path' <<< "$out" ||
  fail 'run expanded ~ in the arguments of the script, or did not find it' "$out"
grep -qx 'read back: written to ~' <<< "$out" && [[ -s "$home/out.txt" ]] ||
  fail "a script's write and read of ~/out.txt did not use the home directory" "$out"

# 2. Usage hints, at the prompt; none in a script's result
grep -q 'result of a failed set: SET error:  Missing or invalid arguments for GXOUT option$' <<< "$out" ||
  fail "a script's result changed" "$out"
grep -q '^result of a failed set: .*Usage' <<< "$out" &&
  fail 'a usage hint went into a script result' "$out"
out="$(grads <<CMDS
open $data/model.ctl
d mean(ts)
d mean(
d mena(ts,t=1,t=3)
d tss
tm = ave(ts,t=1
set
set gxout
set gxot shaded
set grads maybe
q fiel
draw strng 1 1 hello
displya ts
set gxout shaded
d ts
quit
CMDS
)"
expect()
{
  local after="$1" line="$2" block
  block="$(awk -v a="ga-> $after" 'index($0,a)==1 {on=1; next} on && /^ga-> / {exit} on' <<< "$out")"
  grep -Fqx -- "$line" <<< "$block" || fail "after \"$after\" no line \"$line\"" "$block"
}
expect 'd mean(ts)' '  Usage: mean(expr, dim1, dim2 <,tinc> <,-b>)'
expect 'd mean(ts)' '  e.g.   mean(ts, t=1, t=12)'
expect 'd mean(' '  Usage: mean(expr, dim1, dim2 <,tinc> <,-b>)'
expect 'd mena(ts,t=1,t=3)' '  Did you mean: mean, amean, min?'
expect 'd tss' '  Did you mean: ts?'
expect 'tm = ave(ts,t=1' '  Usage: ave(expr, dim1, dim2 <,tinc> <,-b>)'
expect 'set' '  Usage: set option values    (Tab after "set " lists the options)'
expect 'set gxout' '  Usage: set gxout contour|shaded|grfill|line|bar|vector|stream|barb|grid|print|stat|...'
expect 'set gxot shaded' '  Did you mean: gxout, wxopt?'
expect 'set grads maybe' '  Usage: set grads on|off'
expect 'q fiel' '  Did you mean: file, files?'
expect 'draw strng 1 1 hello' '  Usage: draw string x y text'
expect 'displya ts' '  Did you mean: display?'
block="$(awk 'index($0,"ga-> d ts")==1 {on=1; next} on && /^ga-> / {exit} on' <<< "$out")"
grep -q 'Usage' <<< "$block" && fail 'a command that worked got a usage hint' "$block"
grep -q 'Usage: quit' <<< "$out" && fail 'quit got a usage hint' "$out"
hints=13

# Every name the hints and Tab offer is one GrADS takes: set options (but
# chunksize and shpattr, which need NetCDF-4 and shapefile support in the
# build), query options, draw, and functions.
names()
{
  awk "/^static const struct gahelp $1\\[\\]/,/^};/" "$repo_root/cola/src/gahelp.c" |
    sed -n 's/^  {"\([a-z0-9]*\)".*/\1/p'
}
{
  printf 'open %s\n' "$data/model.ctl"
  for n in $(names sets); do printf 'set %s\n' "$n"; done
  for n in $(names qrys); do [[ $n == pos || $n == dialog ]] || printf 'q %s\n' "$n"; done
  for n in $(names draws); do printf 'draw %s\n' "$n"; done
  for n in $(names funcs); do printf 'd %s()\n' "$n"; done
  printf 'quit\n'
} > "$work/names.cmds"
out="$(grads < "$work/names.cmds")"
rejected="$(grep -B3 'Invalid operand$\|Invalid QUERY argument\|not a variable or function name' <<< "$out" |
  sed -n 's/^ga-> //p' | grep -v '^set chunksize$\|^set shpattr$' || true)"
[[ -z "$rejected" ]] || fail 'the hints name what GrADS does not take' "$rejected"
counted=$(grep -c . "$work/names.cmds")

# 3. Tab completion, typed at the prompt
config="$(HOME="$home" OPENGRADS_BUILD_ROOT="$build_root" "$launcher" -bl -d gxdummy -h gxdummy \
  -c 'q config' 2>&1 < /dev/null || true)"
if ! grep -qi readline <<< "$config"; then
  printf 'Prompt checks passed: ~ in file names, %d usage hints, %d names known (no Tab: no readline in the build)\n' \
    "$hints" "$counted"
  exit 0
fi
cat > "$work/tab.py" <<'PYTHON'
import os, pty, sys, time, select, re
launcher, ctl, home = sys.argv[1:4]
pid, fd = pty.fork()
if pid == 0:
    os.environ.update(HOME=home, OPENGRADS_COLOR='0', TERM='dumb', GA_PROGRESS='off')
    os.chdir(home)
    os.execv(launcher, [launcher, '-bl', '-d', 'gxdummy', '-h', 'gxdummy'])
def pump(t):
    out = b''; end = time.time() + t
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.05)
        if r:
            try: out += os.read(fd, 65536)
            except OSError: break
    return out.decode('utf-8', 'replace')
def until(text, t=10):
    out = ''; end = time.time() + t
    while time.time() < end and text not in out:
        out += pump(0.2)
    return out
until('ga->')
os.write(fd, b'open ' + ctl.encode() + b'\r'); until('ga->')
os.write(fd, b'define tmean = ave(ts,t=1,t=3)\r'); until('ga->')
for keys in sys.argv[4:]:
    os.write(fd, keys.encode().decode('unicode_escape').encode())
    s = pump(1.0)
    os.write(fd, b'\x01\x0b')        # ctrl-a ctrl-k: an empty line again
    pump(0.3)
    s = re.sub(r'\x1b\[[0-9;?]*[A-Za-z]|\x07|\r|\x08', '', s)
    print('%s => %s' % (keys, ' '.join(s.split())))
os.write(fd, b'quit\r'); pump(1)
PYTHON
cp "$repo_root/lib/scripts/cbarn.gs" "$home/gs/" 2>/dev/null || true
tabs="$(OPENGRADS_BUILD_ROOT="$build_root" GASCRP="$home/gs" python3 "$work/tab.py" "$launcher" \
  "$data/model.ctl" "$home" \
  'set gx\t' 'set gxout sh\t\t' 'set grads o\t\t' 'set mpdset h\t' 'q fi\t\t' 'd t\t\t' \
  'd ave(t\t\t' 'run arg\t' 'open ~/data/mod\t' 'draw st\t' 'undefine tm\t' 2>&1)"
tab()
{
  grep -F -- "$1 => " <<< "$tabs" | grep -Fq -- "$2" ||
    fail "Tab after \"$1\" did not give \"$2\"" "$tabs"
}
tab 'set gx\t' 'set gxout'
tab 'set gxout sh\t\t' 'shade1 shade2 shade2b shaded shp'
tab 'set grads o\t\t' 'off on'
tab 'set mpdset h\t' 'set mpdset hires'
tab 'q fi\t\t' 'q file'
grep -F 'd t\t\t => ' <<< "$tabs" | grep -q ' ts ' && grep -F 'd t\t\t => ' <<< "$tabs" | grep -q 'tmean' &&
  grep -F 'd t\t\t => ' <<< "$tabs" | grep -q 'tloop(' ||
  fail 'Tab in an expression did not offer variables, defined variables and functions' "$tabs"
grep -F 'd ave(t\t\t => ' <<< "$tabs" | grep -q 't= ' ||
  fail 'Tab inside a function did not offer the dimensions' "$tabs"
tab 'run arg\t' 'run args.gs'
tab 'open ~/data/mod\t' 'open ~/data/model.'
tab 'draw st\t' 'draw string'
tab 'undefine tm\t' 'undefine tmean'

printf 'Prompt checks passed: ~ in file names, %d usage hints, %d names known, 11 Tab completions\n' \
  "$hints" "$counted"
