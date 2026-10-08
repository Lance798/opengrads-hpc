#!/usr/bin/env bash
# Added in 2026 for faster spatial averages. GPLv2; see COPYING.
#
# ave and its kin over x, y or z of a plain variable, when the result varies
# in one dimension at most (a horizontal-mean profile, a zonal mean, a
# point), read every position at once, as one section, instead of a read
# for each. A profile averaged over x and y was a read of a column for every
# point of the plane. Each result must match, to the last bit, the same
# average taken a position at a time, which "+0" forces: weights by
# latitude and by -b bounds, global wrap-around in longitude, levels in
# decreasing pressure, positions outside the file, and every reduction.

set -euo pipefail

repo_root="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build_root="${OPENGRADS_BUILD_ROOT:-/tmp/opengrads-build-cpu}"
launcher="${OPENGRADS_LAUNCHER:-$repo_root/opengrads}"
model_ctl="$repo_root/pytests/data/model.ctl"

fail()
{
  printf 'FAIL: %s\n' "$1" >&2
  if [[ -n "${2:-}" ]]; then
    printf '%s\n' "$2" >&2
  fi
  exit 1
}

# SETUP|SETUP|...|EXPRESSION|VARIABLE
cases=(
  "set x 1|set y 20|set lev 1000 100|set t 2|ave(ave(ta,x=1,x=72),y=1,y=46)|ta"
  "set x 1|set y 20|set lev 1000 100|set t 2|mean(mean(ta,x=1,x=72),y=1,y=46)|ta"
  "set x 1|set y 20|set lev 1000 100|set t 2|sum(ta,x=1,x=72)|ta"
  "set x 1|set y 20|set lev 1000 100|set t 2|sumg(ta,y=1,y=46)|ta"
  "set x 1|set y 20|set lev 1000 100|set t 2|min(ta,x=1,x=72)|ta"
  "set x 1|set y 20|set lev 1000 100|set t 2|max(ta,y=1,y=46)|ta"
  "set x 1|set y 20|set lev 1000 100|set t 2|minloc(ta,x=1,x=72)|ta"
  "set x 1|set y 20|set lev 1000 100|set t 2|maxloc(ta,y=1,y=46)|ta"
  "set x 1|set y 20|set lev 1000 100|set t 2|ave(ta,lon=0,lon=360)|ta"
  "set x 1|set y 20|set lev 1000 100|set t 2|ave(ta,lat=-90,lat=90)|ta"
  "set x 1|set y 20|set lev 1000 100|set t 2|ave(ta,lon=10,lon=300,-b)|ta"
  "set x 1|set y 20|set lev 1000 100|set t 2|ave(ta,x=-5,x=80)|ta"
  "set lon 0 360|set lat 10|set lev 500|set t 2|ave(ta,lat=-80,lat=80)|ta"
  "set lon 0 360|set lat 10|set t 2|ave(ta,lev=1000,lev=100)|ta"
  "set lon 100|set lat 10|set lev 500|set t 3|ave(ps,lon=0,lon=355)|ps"
)

commands="open $model_ctl"$'\n'"set gxout print"$'\n'"set prnopts %.17g 8 1"
n=0
for c in "${cases[@]}"; do
  n=$((n + 1))
  var="${c##*|}"
  rest="${c%|*}"
  expr="${rest##*|}"
  setup="${rest%|*}"
  commands+=$'\n'"${setup//|/$'\n'}"
  commands+=$'\n'"!echo MARK $n plain"$'\n'"d $expr"
  commands+=$'\n'"!echo MARK $n plus0"$'\n'"d ${expr/$var,/$var+0,}"
done
commands+=$'\n'"!echo MARK end"$'\n'"quit"
output="$(OPENGRADS_BUILD_ROOT="$build_root" OPENGRADS_COLOR=0 GA_PROGRESS=off \
  "$launcher" -bl -d gxdummy -h gxdummy <<< "$commands" 2>&1)"

# the numbers each display printed, keyed by case and form
values="$(awk '/^MARK / {key = $2 " " $3; next}
               key != "" && /^-?[0-9][0-9.e+-]*( +-?[0-9][0-9.e+-]*)* *$/ {print key ": " $0}' \
  <<< "$output")"
for (( i = 1; i <= ${#cases[@]}; i++ )); do
  plain="$(sed -n "s/^$i plain: //p" <<< "$values")"
  plus0="$(sed -n "s/^$i plus0: //p" <<< "$values")"
  [[ -n "$plain" ]] || fail "case $i printed no values: ${cases[i-1]}" "$output"
  [[ "$plain" == "$plus0" ]] ||
    fail "case $i read as a section differs from a position at a time: ${cases[i-1]}" \
      "$(diff <(printf '%s\n' "$plain") <(printf '%s\n' "$plus0") | head -10)"
done

printf 'Section average checks passed: %d averages over x, y and z match position-at-a-time reads\n' \
  "${#cases[@]}"
