# Real-Time Operation (PREEMPT_RT)

Linux is the target platform for timing-accurate simulation
([ADR 0003](../adr/0003-linux-rt-target-windows-best-effort.md)).
Windows builds behave identically but without timing guarantees.

## Options

Both `iotwin-bus` and `can_sim` accept:

| Option              | Effect (Linux)                                               |
|---------------------|--------------------------------------------------------------|
| `--rt-priority N`   | `SCHED_FIFO` with priority 1..99 for the main loop           |
| `--rt-cpu N`        | pin the main loop to CPU core N                              |
| `--rt-lock-memory`  | `mlockall(MCL_CURRENT \| MCL_FUTURE)` - no page faults later |

`can_sim` additionally has `--tick-ms N` (loop period, default 10 ms) and
`--stats N` (log wake-up latency every N seconds).

On start both programs log whether the kernel is PREEMPT_RT
(`/sys/kernel/realtime`) and which settings could be applied.

## Permissions

Real-time scheduling and memory locking need privileges. Either run with
`sudo`, or grant them once:

```bash
sudo setcap cap_sys_nice,cap_ipc_lock+ep build/gcc-release/bin/can_sim
```

or via `/etc/security/limits.conf`:

```
@realtime  -  rtprio   90
@realtime  -  memlock  unlimited
```

## Example

```bash
iotwin-bus --rt-priority 70 --rt-lock-memory &
can_sim --tick-ms 1 --rt-priority 80 --rt-cpu 2 --rt-lock-memory --stats 5
```

Typical output line:

```
loop 1 ms: 5000 cycles, wake-up latency min/avg/max = 0.004/0.011/0.038 ms, overruns: 0
```

The maximum latency is the relevant figure. On a standard kernel under load
it can reach milliseconds; on PREEMPT_RT it typically stays in the
tens of microseconds. WSL2 and Windows are not suitable for such measurements.
