# Contract: Hyper-V Input, Display, and Power

## Keyboard

Bind class `f912ad6d-2b17-48ea-bd65-f927a61c7684`, negotiate protocol 1.0, validate all in-band
message envelopes, and translate make/break, E0/E1, Unicode, modifier, repeat, and caps-lock state
through the shared scan-code/event implementation. Keyboard failure prevents a claim of usable
Hyper-V CUI support.

## Pointer

Bind class `cfa8b69e-5b4a-4cc0-b98b-8ba1a1f3f95a`, negotiate SynthHID 2.0, validate device/HID/report
descriptors, and parse only supported mouse usages. Malformed or unsupported descriptors disable the
pointer without disabling the CUI.

## Display

The loader accepts only a directly writable retained GOP mode whose size, stride, resolution, and
pixel format pass overflow-safe validation. PixelBltOnly is rejected. The kernel never calls GOP
after ExitBootServices. A failed Hyper-V display qualification disables the GUI and preserves CUI.

## Power

The existing power controller owns synchronization and device flush ordering. Only after success may
the Hyper-V platform call the retained UEFI ResetSystem entry using the Microsoft x64 ABI with cold
reset or shutdown. If ResetSystem returns, the platform reports failure or halts; it never reports a
completed transition.

