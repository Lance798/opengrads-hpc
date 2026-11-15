# Query the last analysis command

`q performance` (or `query performance`) reports the most recent completed
built-in DISPLAY or DEFINE, including an implied definition such as `wind=u+v`.
It takes no arguments and does not repeat the calculation.

```text
set threads 4
set x 5
set y 3
set z 1
set t 1 64
d mag(u,v)
q performance
```

Commands update a bounded in-memory snapshot without printing diagnostics.
Only the query prints the report. Ordinary output, progress bars, iTerm2
progress messages and graphics retain their existing behavior. There is no
new progress display or background sampler.

## What is recorded

| Field | Meaning |
| --- | --- |
| Command and status | Command text (up to 1,023 characters), return code and whether interruption was observed. Failed analyses replace the previous record too. |
| Wall time | Monotonic elapsed time from command entry through command cleanup. Includes evaluation, output, worker startup, waits and undo bookkeeping. |
| Evaluation | Time in DEFINE or the DISPLAY expression evaluator. Includes reads, decompression, worker setup and waiting for results; it is not pure CPU time. Nested evaluation scopes are counted once. |
| Display/output calls | Time spent in the plot/output dispatcher. This also includes PRINT, STAT and FWRITE work selected by `set gxout`. Separate later commands such as `printim` are not part of this time. |
| CPU budget | The `set threads` value at the start of the recorded command, and whether OpenMP was compiled in. Changing it afterwards does not rewrite the snapshot. |
| OpenMP kernel eligibility | Whether the parent encountered a numerical-kernel request above the existing parallel cutoff. Actual OpenMP team sizes are not sampled. |
| Time workers | Highest active worker count, dispatched batch count, newly created versus reused pools, completed per-time results and pool setup time. These are observed counts, not a copy of the requested CPU budget. |
| Worker workspace | Peak estimated workspace for the active workers and minimum memory budget encountered during dispatched batches. These retain the scheduler's existing estimates and limits. |
| Parent peak RSS | The main process's lifetime high-water resident memory, where supported. It is not the last command's memory increase and does not include worker RSS. |

Pool setup time is already included in evaluation and wall time. The fields
are overlapping measurements, not pieces to add together. A command that
dispatches more than one batch can complete more time results than the
number of distinct dataset times; repeated expressions and nested input-time
reductions can do that.

## Backends and execution paths

The report lists input backends actually encountered by instrumented parent
readers and dispatched worker inputs: BP5, NetCDF, HDF4/HDF5, indexed GRIB,
station/binary data, or an in-memory DEFINE. NetCDF-4 is reported through its
NetCDF API backend rather than inferred to be a direct HDF5 read.

Observed paths can include:

- bulk grid/volume readers, contiguous binary slabs and row/segment reads;
- BP5 native time-step batching;
- BP5/HDF5 native column integrals and indexed vertical gathers;
- spatial section/volume batching and defined-array lookup;
- isolated time-worker processes.

A command can use several paths. The list is a deduplicated observation,
not an ordered call graph or a claim that every read took the same path.
Parent-side path observations do not trace the internal reader activity of
each isolated worker.

Eligibility notes identify checks the scheduler actually encountered, such
as disabled workers, a one-CPU budget, too few output times, memory-limited
concurrency or failed startup. A rejected output-time path can coexist with
a successful input-time path in the same command. If no dispatch or known
reason was observed, the report explicitly says the detailed reason is
unavailable; it does not guess which function prevented acceleration.

## Snapshot lifetime and limitations

- Queries, SET, CLOSE, RESET and REINIT preserve the last completed record.
  It contains copied text, counters and backend labels, not open-file handles.
- A `.gs` script records each DISPLAY/DEFINE separately. Its last analysis
  remains available after the script; the whole script is not one timed job.
  Inside a script, use `'q performance'` followed by `say result` to print it.
- Querying before any analysis prints a short no-record message.
- Read/decompression time, transferred bytes, per-worker RSS and hardware
  counters are not measured separately. No throughput or bottleneck diagnosis
  is inferred from elapsed time alone.
- Worker workspace estimates exclude the final DEFINE array, inactive cached
  workers, and library/OS allocations outside the scheduler's estimate. A
  process lifetime RSS figure must not be presented as per-command usage.
- The CPU default and 128-worker implementation limit are unchanged.

The [performance measurements](PERFORMANCE.md),
[time-worker contract](TIME_WORKERS.md) and
[spatial reduction coverage](SPATIAL_REDUCTIONS.md) describe the underlying
execution paths. `GA_TIME_TRACE=1` remains available for detailed worker PIDs.

## Regression checks

`pytests/TestPerformance.sh` checks stable snapshots, errors, implied DEFINE,
script results, timings, observed paths, and byte-identical data/PNG output
with and without a query. Its HDF5/NetCDF/NetCDF-4/BP5 modes also check actual
worker counts, pool reuse, completed results and disabled-worker behavior.
All modes use at most four calculation cores. Existing progress, terminal
display and worker cancellation/recovery suites exercise display compatibility.
