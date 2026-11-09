# Source this file from Bash: source dev/activate.sh
# All project commands use private dependencies and an explicit host toolchain.
if [[ -z "${BASH_VERSION:-}" ]]; then
  echo 'OpenGrADS development activation requires Bash.' >&2
  return 1
fi

_opengrads_activate() {
  local root variable hdf5_flags python_binary hdf5_version host_tmux
  root="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)" || return
  if declare -p _opengrads_present >/dev/null 2>&1; then
    if [[ "${OPENGRADS_DEV_ROOT:-}" == "$root" ]]; then return 0; fi
    opengrads_deactivate
  fi
  host_tmux="${OPENGRADS_HOST_TMUX:-$(command -v tmux || true)}"
  declare -gA _opengrads_saved=() _opengrads_present=()
  local variables=(PATH CPATH C_INCLUDE_PATH CPLUS_INCLUDE_PATH LIBRARY_PATH
    CMAKE_PREFIX_PATH PKG_CONFIG_PATH PKG_CONFIG_LIBDIR SUPPLIBS
    CC CXX FC CFLAGS CXXFLAGS CPPFLAGS LDFLAGS LD_LIBRARY_PATH TMPDIR
    OPENGRADS_DEV_ROOT OPENGRADS_BUILD_ROOT OPENGRADS_ADIOS2_ROOT
    OPENGRADS_DEPS_ROOT OPENGRADS_EXTRA_LIBRARY_PATH OPENGRADS_BUNDLE_ROOT
    OPENGRADS_LAUNCHER UDUNITS2_XML_PATH OPENGRADS_PYTHON
    OPENGRADS_HOST_TMUX TEST_TMUX)
  for variable in "${variables[@]}"; do
    if [[ -v "$variable" ]]; then
      _opengrads_present[$variable]=1
      _opengrads_saved[$variable]="${!variable}"
    else
      _opengrads_present[$variable]=0
    fi
  done
  unset CPATH C_INCLUDE_PATH CPLUS_INCLUDE_PATH LIBRARY_PATH CMAKE_PREFIX_PATH
  unset PKG_CONFIG_LIBDIR SUPPLIBS CFLAGS CXXFLAGS OPENGRADS_BUNDLE_ROOT
  export OPENGRADS_DEV_ROOT="$root"
  export TMPDIR="$root/tmp"
  mkdir -p "$TMPDIR"
  export OPENGRADS_BUILD_ROOT="$root/build"
  export OPENGRADS_ADIOS2_ROOT="$root/.release-work/adios2"
  export OPENGRADS_DEPS_ROOT="$root/.release-work/deps"
  export OPENGRADS_LAUNCHER="$root/opengrads"
  export OPENGRADS_EXTRA_LIBRARY_PATH=""
  # Private dependencies include tmux for isolated tests. Interactive
  # commands keep the host version so an existing server stays compatible.
  export OPENGRADS_HOST_TMUX="$host_tmux"
  export TEST_TMUX="${TEST_TMUX:-$OPENGRADS_DEPS_ROOT/bin/tmux}"
  export PATH="$root:$OPENGRADS_DEPS_ROOT/bin:$OPENGRADS_ADIOS2_ROOT/bin:${OPENGRADS_TOOLCHAIN_PATH:-/usr/local/bin:/usr/bin:/bin}"
  python_binary="$(command -v python3)"
  if ! "$python_binary" -c 'import sys; sys.exit(sys.version_info < (3, 8))'; then
    python_binary="$(command -v python3.8)" || return 1
  fi
  export OPENGRADS_PYTHON="$python_binary"
  export PATH="$root/dev/bin:$PATH"
  export CC="$(command -v gcc)" CXX="$(command -v g++)" FC="$(command -v gfortran || true)"
  export CPPFLAGS="-I$OPENGRADS_DEPS_ROOT/include"
  export LDFLAGS="-L$OPENGRADS_DEPS_ROOT/lib -Wl,-rpath,$OPENGRADS_DEPS_ROOT/lib"
  export PKG_CONFIG_PATH="$OPENGRADS_DEPS_ROOT/lib/pkgconfig:$OPENGRADS_DEPS_ROOT/share/pkgconfig"
  # RHEL 8 can provide HDF5 headers/libraries without an hdf5.pc file.
  if ! pkg-config --exists hdf5 && [[ -r /usr/include/hdf5.h && -r /usr/lib64/libhdf5.so ]]; then
    mkdir -p "$TMPDIR/pkgconfig"
    hdf5_version="$(awk '/^#define H5_VERS_(MAJOR|MINOR|RELEASE)[[:space:]]/ {v[++n]=$3} END {printf "%s.%s.%s",v[1],v[2],v[3]}' /usr/include/H5public.h)"
    printf 'Name: HDF5\nDescription: Host serial HDF5\nVersion: %s\nLibs: -L/usr/lib64 -lhdf5_hl -lhdf5 -lz\nCflags: -I/usr/include\n' "$hdf5_version" > "$TMPDIR/pkgconfig/hdf5.pc"
    export PKG_CONFIG_PATH="$PKG_CONFIG_PATH:$TMPDIR/pkgconfig"
  fi
  # Debian installs serial HDF5 outside the default include/library search path.
  if command -v pkg-config >/dev/null && pkg-config --exists hdf5; then
    hdf5_flags="$(pkg-config --cflags hdf5)"
    export CPPFLAGS="$CPPFLAGS $hdf5_flags"
    export LDFLAGS="$LDFLAGS $(pkg-config --libs-only-L hdf5)"
  fi
  export LD_LIBRARY_PATH="$OPENGRADS_ADIOS2_ROOT/lib:$OPENGRADS_DEPS_ROOT/lib"
  export UDUNITS2_XML_PATH="$OPENGRADS_DEPS_ROOT/share/udunits/udunits2.xml"
}

opengrads_deactivate() {
  local variable
  for variable in "${!_opengrads_present[@]}"; do
    if [[ "${_opengrads_present[$variable]}" == 1 ]]; then
      export "$variable=${_opengrads_saved[$variable]}"
    else
      unset "$variable"
    fi
  done
  unset _opengrads_saved _opengrads_present
  unset -f opengrads_deactivate
  hash -r
}

_opengrads_activate
unset -f _opengrads_activate
