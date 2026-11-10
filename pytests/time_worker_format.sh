# Shared fixture compiler flags for the time-worker regression suites.
# Source after setting repo_root. GPL-2.0-only; see COPYING.
case "${GA_TEST_TIME_FORMAT:-h5}" in
  h5)
    writer_define=-DZS_HDF5
    if [[ -n "${GA_TEST_HDF5_LIBS:-}" ]]; then
      read -r -a hflags <<< "${GA_TEST_HDF5_CFLAGS:-}"
      read -r -a hlibs <<< "$GA_TEST_HDF5_LIBS"
    elif command -v pkg-config >/dev/null 2>&1 && pkg-config --exists hdf5; then
      hdf5_cflags="$(pkg-config --cflags hdf5)" || return 1
      hdf5_libs="$(pkg-config --libs hdf5)" || return 1
      read -r -a hflags <<< "$hdf5_cflags"
      read -r -a hlibs <<< "$hdf5_libs"
    else
      # EL8 packages omit hdf5.pc. Reuse the flags that built GrADS,
      # just as TestTimeLoader.sh does for its child regression suites.
      hdf5_makefile="${OPENGRADS_BUILD_ROOT:-$repo_root/build}/src/Makefile"
      if [[ ! -r "$hdf5_makefile" ]]; then
        printf 'HDF5 flags unavailable: no hdf5.pc or configured build at %s.\n' "$hdf5_makefile" >&2
        printf 'Set OPENGRADS_BUILD_ROOT or GA_TEST_HDF5_CFLAGS and GA_TEST_HDF5_LIBS.\n' >&2
        return 1
      fi
      hdf5_cflags="$(sed -n 's/^HDF5_CFLAGS *= *//p' "$hdf5_makefile")"
      hdf5_ldflags="$(sed -n 's/^HDF5_LDFLAGS *= *//p' "$hdf5_makefile")"
      hdf5_libs="$(sed -n 's/^HDF5_LIBS *= *//p' "$hdf5_makefile")"
      if [[ -z "${hdf5_libs//[[:space:]]/}" ]]; then
        printf 'HDF5 libraries were not configured in %s.\n' "$hdf5_makefile" >&2
        return 1
      fi
      read -r -a hflags <<< "$hdf5_cflags"
      read -r -a hlibs <<< "$hdf5_ldflags $hdf5_libs"
    fi
    # Fixtures also need to run when HDF5 lives outside the system paths.
    for link_flag in "${hlibs[@]}"; do
      if [[ "$link_flag" == -L* ]]; then hlibs+=("-Wl,-rpath,${link_flag#-L}"); fi
    done
    ;;
  nc|nc4)
    writer_define=-DZS_NETCDF
    nc_config="${OPENGRADS_TEST_NETCDF_ROOT:+$OPENGRADS_TEST_NETCDF_ROOT/bin/}nc-config"
    read -r -a hflags <<< "$("$nc_config" --cflags)"
    read -r -a hlibs <<< "$("$nc_config" --libs)"
    for link_flag in "${hlibs[@]}"; do
      if [[ "$link_flag" == -L* ]]; then hlibs+=("-Wl,-rpath,${link_flag#-L}"); fi
    done
    ;;
  *) printf 'Unknown time-worker fixture format: %s\n' "$GA_TEST_TIME_FORMAT" >&2; exit 2 ;;
esac
