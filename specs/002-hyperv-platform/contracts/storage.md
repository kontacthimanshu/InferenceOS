# Contract: Hyper-V Storage and Disk Safety

## StorVSC

The driver binds only the Generation 2 synthetic-SCSI class. It negotiates modern protocol versions,
queries controller properties, probes LUNs 0–63, and publishes only direct-access block devices with
512-byte logical sectors and validated capacity.

Reads and writes use SCSI READ/WRITE(10) through GPA-direct packets. The request byte count, GPA
range, bounce allocation, CDB block count, and returned transfer length must agree. Flush waits for
all prior writes and a successful SYNCHRONIZE CACHE(10) completion with IMMED clear. One transaction
is outstanding at a time. Timeout recovery freezes submission and reset/drains before resources can
be reused.

## Disk Safety

Before a block device receives a format/root capability, read-only classification must:

1. compare validated GPT partition identity with the loader boot-partition identity;
2. validate primary and, when required, backup GPT including CRCs;
3. protect every disk containing the boot partition or any EFI System Partition;
4. protect partitioned or recognized foreign-filesystem media;
5. admit only unpartitioned blank media or an existing valid InferenceOS-FS whole disk;
6. fail closed on read, arithmetic, checksum, or ambiguity errors.

Protected devices are never added to the format-capable `diskN` catalog. Format and root mount check
capabilities rather than relying on names, attachment order, controller location, LUN, or capacity.

