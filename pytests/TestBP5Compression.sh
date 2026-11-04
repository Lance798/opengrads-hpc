#!/usr/bin/env bash
# Added in 2026 for lossless compressed BP5 reads and packaging. GPLv2.
set -euo pipefail

repo_root="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
mode="${1:-}"
case "$mode" in
  '')
    test_root="$(mktemp -d /tmp/opengrads-bp5-compression.XXXXXX)"
    trap 'rm -rf -- "$test_root"' EXIT ;;
  --write-fixtures|--read-fixtures)
    if [[ $# -ne 2 ]]; then
      printf 'Usage: %s [--write-fixtures|--read-fixtures DIRECTORY]\n' "$0" >&2
      exit 2
    fi
    test_root="$2" ;;
  *) printf 'Unknown argument: %s\n' "$mode" >&2; exit 2 ;;
esac
codecs=(blosclz lz4 lz4hc zlib zstd)

if [[ "$mode" != --read-fixtures ]]; then
  adios2_root="${OPENGRADS_ADIOS2_ROOT:-}"
  adios2_config="${adios2_root:+$adios2_root/bin/}adios2-config"
  read -r -a cflags <<< "$("$adios2_config" --serial --c-flags)"
  read -r -a libs <<< "$("$adios2_config" --serial --c-libs)"
  mkdir -p "$test_root"
  "${CC:-gcc}" -std=gnu99 -Wall -Wextra -Werror "${cflags[@]}" \
    "$repo_root/pytests/bp5_compressed_writer.c" "${libs[@]}" -o "$test_root/writer"
  # Honor the private Blosc2 prefix when testing a development build.
  writer_path="${OPENGRADS_DEPS_ROOT:-${adios2_root%/*}/deps}/lib"
  case "$(uname -s)" in
    Darwin)
      library_variable=DYLD_LIBRARY_PATH
      writer_path="$adios2_root/lib:$adios2_root/lib64:$writer_path${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}" ;;
    *)
      library_variable=LD_LIBRARY_PATH
      writer_path="$adios2_root/lib:$adios2_root/lib64:$writer_path${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" ;;
  esac
  for codec in none "${codecs[@]}"; do
    env "$library_variable=$writer_path" \
      "$test_root/writer" "$test_root/$codec.bp" "$codec"
    cat > "$test_root/$codec.ctl" <<CTL
dset ^$codec.bp
dtype bp5
undef -9999
xdef 32 linear 1 1
ydef 16 linear 1 1
zdef 4 linear 1 1
tdef 2 linear 00z01jan2000 1mn
vars 2
temperature=>temp 4 z,y,x Temperature
pressure=>ps 0 y,x Pressure
endvars
CTL
  done
  # These arrays exceed Blosc's threshold. Prove they were compressed rather
  # than silently falling back to memcpy, which would miss a disabled codec.
  raw_bytes="$(wc -c < "$test_root/none.bp/data.0")"
  for codec in "${codecs[@]}"; do
    if (( $(wc -c < "$test_root/$codec.bp/data.0") >= raw_bytes )); then
      printf 'Fixture was not compressed: %s\n' "$codec" >&2
      exit 1
    fi
  done
  [[ "$mode" != --write-fixtures ]] || exit 0
fi

launcher="${OPENGRADS_LAUNCHER:-$repo_root/opengrads}"
for codec in "${codecs[@]}"; do
  for open_command in "open $test_root/$codec.ctl" "bpopen $test_root/$codec.bp"; do
    if [[ "$open_command" == open\ * ]]; then temp=temp; ps=ps
    else temp=temperature; ps=pressure; fi
    output="$(OPENGRADS_COLOR=0 "$launcher" -bl -d gxdummy -h gxdummy 2>&1 <<GRADS
$open_command
set gxout print
set prnopts %g 8 1
set x 32
set y 16
set z 4
set t 1
d $temp
set t 2
d $temp
set z 1
d $ps
set z 4
d ave($temp,t=1,t=2)
d ave($temp+0,t=1,t=2)
set x 1 32
set y 1 16
set z 1
set t 1
set gxout stat
d $temp
set t 2
d $temp
d $ps
d ave($temp,t=1,t=2)
d ave($temp+0,t=1,t=2)
quit
GRADS
)"
    for expected in 481 1481 1181 981; do
      if ! grep -Eq "^${expected}[[:space:]]*$" <<< "$output"; then
        printf 'Compressed BP5 regression failed (%s, %s): missing value %s\n%s\n' \
          "$codec" "$open_command" "$expected" "$output" >&2
        exit 1
      fi
    done
    for expected in 'Undef count = 0  Valid count = 512' 'Min, Max = 0 181' \
      'Min, Max = 1000 1181' 'Min, Max = 500 681' \
      'Stats[sum,sumsqr,root(sumsqr),n]:     46336'; do
      if ! grep -Fq "$expected" <<< "$output"; then
        printf 'Compressed BP5 regression failed (%s, %s): missing %s\n%s\n' \
          "$codec" "$open_command" "$expected" "$output" >&2
        exit 1
      fi
    done
    if [[ $(grep -Ec '^981[[:space:]]*$' <<< "$output") -ne 2 ||
          $(grep -Fc 'Min, Max = 500 681' <<< "$output") -ne 2 ]]; then
      printf 'Compressed BP5 bulk/ordinary averages disagree (%s):\n%s\n' \
        "$codec" "$output" >&2
      exit 1
    fi
    if grep -Eq 'Error|ERROR|Exception|exception' <<< "$output"; then
      printf 'Compressed BP5 regression failed (%s):\n%s\n' "$codec" "$output" >&2
      exit 1
    fi
  done
done
printf 'Compressed BP5 regression passed: five lossless codecs, CTL/bpopen, float/double, two steps, planes, and time averages.\n'
