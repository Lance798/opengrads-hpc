# Shared fixture compiler flags for the time-worker regression suites.
# Source after setting repo_root. GPL-2.0-only; see COPYING.
case "${GA_TEST_TIME_FORMAT:-h5}" in
  h5)
    writer_define=-DZS_HDF5
    if [[ -n "${GA_TEST_HDF5_LIBS:-}" ]]; then
      read -r -a hflags <<< "${GA_TEST_HDF5_CFLAGS:-}"
      read -r -a hlibs <<< "$GA_TEST_HDF5_LIBS"
    else
      read -r -a hflags <<< "$(pkg-config --cflags hdf5)"
      read -r -a hlibs <<< "$(pkg-config --libs hdf5)"
    fi
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
