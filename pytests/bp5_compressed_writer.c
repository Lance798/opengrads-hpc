/* Added in 2026 for compressed BP5 regression tests. GPLv2; see COPYING. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <adios2_c.h>

static void check(adios2_error error)
{
  if (error != adios2_error_none) {
    fprintf(stderr, "Compressed BP5 fixture: ADIOS2 error %d\n", (int)error);
    exit(EXIT_FAILURE);
  }
}

static void *handle(void *value)
{
  if (!value) {
    fprintf(stderr, "Compressed BP5 fixture: null ADIOS2 handle\n");
    exit(EXIT_FAILURE);
  }
  return value;
}

int main(int argc, char **argv)
{
  const size_t shape[3] = {4, 16, 32}, start[3] = {0, 0, 0};
  float temperature[4 * 16 * 32];
  double pressure[16 * 32];
  adios2_adios *adios;
  adios2_io *io;
  adios2_variable *temp, *ps;
  adios2_engine *engine;
  adios2_step_status status;
  size_t step, z, y, x, operation;

  if (argc != 3) {
    fprintf(stderr, "Usage: %s OUTPUT.bp CODEC|none\n", argv[0]);
    return EXIT_FAILURE;
  }
  adios = handle(adios2_init_serial());
  io = handle(adios2_declare_io(adios, "compressed_fixture"));
  check(adios2_set_engine(io, "BP5"));
  temp = handle(adios2_define_variable(io, "temperature", adios2_type_float,
      3, shape, start, shape, adios2_constant_dims_true));
  ps = handle(adios2_define_variable(io, "pressure", adios2_type_double,
      2, shape + 1, start + 1, shape + 1, adios2_constant_dims_true));
  if (strcmp(argv[2], "none") != 0) {
    adios2_operator *op = handle(adios2_define_operator(adios, "lossless", "blosc"));
    adios2_variable *fields[2] = {temp, ps};
    size_t field;
    for (field = 0; field < 2; ++field) {
      check(adios2_add_operation(&operation, fields[field], op, "compressor", argv[2]));
      check(adios2_set_operation_parameter(fields[field], operation, "clevel", "5"));
      check(adios2_set_operation_parameter(fields[field], operation,
                                           "doshuffle", "BLOSC_SHUFFLE"));
      check(adios2_set_operation_parameter(fields[field], operation, "nthreads", "1"));
    }
  }
  engine = handle(adios2_open(io, argv[1], adios2_mode_write));
  for (step = 0; step < 2; ++step) {
    for (z = 0; z < 4; ++z)
      for (y = 0; y < 16; ++y)
        for (x = 0; x < 32; ++x)
          temperature[(z * 16 + y) * 32 + x] = (float)(1000 * step + 100 * z + 10 * y + x);
    for (y = 0; y < 16; ++y)
      for (x = 0; x < 32; ++x)
        pressure[y * 32 + x] = (double)(900 + 100 * step + 10 * y + x);
    check(adios2_begin_step(engine, adios2_step_mode_append, 0.0, &status));
    if (status != adios2_step_status_ok) return EXIT_FAILURE;
    check(adios2_put(engine, temp, temperature, adios2_mode_sync));
    check(adios2_put(engine, ps, pressure, adios2_mode_sync));
    check(adios2_end_step(engine));
  }
  check(adios2_close(engine));
  check(adios2_finalize(adios));
  return EXIT_SUCCESS;
}
