# Generic Block-Device Contract

## Purpose

Provide a synchronous transport-neutral sector interface between cache/filesystem code and ATA PIO.

## Properties and Operations

- Properties: stable identifier, opaque driver context, logical sector size/count, and ready/busy/read-only/failed/absent status.
- `read(start_lba, count, destination)`: copies every requested sector or returns a defined complete failure.
- `write(start_lba, count, source)`: accepts every requested sector or returns a defined complete failure.
- `flush()`: makes prior accepted writes durable to the emulated-device contract.
- `query()`: returns immutable geometry and current status.

Failures distinguish invalid handle/argument, arithmetic overflow, range, absent/not-ready/read-only, timeout, controller/device error, and unsupported flush. Partial completion is never full success; ATA detail remains available to diagnostics.

## Invariants

1. Validate `start_lba + count` without overflow and against capacity before port I/O.
2. Bound every ATA status poll.
3. Filesystem code never accesses ATA ports.
4. Ordered cache flush writes required dirty sectors, then invokes device flush.

## Test Seam

The memory device implements the same interface, records requests, and injects failures by operation number/type and LBA interval so ordering and absence of out-of-range I/O are assertable.
