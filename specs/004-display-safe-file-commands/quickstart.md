# Quickstart: Validate Display-Safe File Commands

Create and initialize a new extensionless text file in one command:

```text
write FILE1 "Hello World"
```

## Hosted Validation

```powershell
cmake --preset gcc-host-debug
cmake --build --preset gcc-host-debug
ctest --preset gcc-integration
ctest --preset gcc-contract

cmake --preset clang-host-debug
cmake --build --preset clang-host-debug
ctest --preset clang-integration
ctest --preset clang-contract
```

Expected: resolver, identity-operation, CUI, metadata-boundary, and regression tests pass with both compilers.

## Guest Workflow

```text
mkdir /DOCS
create /DOCS/NOTE.TXT
write /DOCS/NOTE "hello"
append /DOCS/NOTE " world"
fileinfo /DOCS/NOTE
rename /DOCS/NOTE /DOCS/SUMMARY
dir /DOCS
```

Expected: `SUMMARY` has size 11 and retains the original hidden type.

## Binary Denial

Start with a fixture or preloaded disk containing a non-empty `IMAGE.PNG` with its normal binary
signature. Then use only its displayed name:

```text
write /DOCS/IMAGE "not an image"
append /DOCS/IMAGE "more"
fileinfo /DOCS/IMAGE
```

Expected: both commands return `unexpected_format`; the original image bytes and size are unchanged.

## Visible-Name Uniqueness

```text
create /DOCS/REPORT.TXT
create /DOCS/REPORT.LOG
create /DOCS/REPORT.BIN
dir /DOCS
write /DOCS/REPORT "first"
```

Expected: the first create succeeds; the `.LOG` and `.BIN` creates return `already_exists`; `dir`
shows only `REPORT`; and `write` initializes that one file. Injected legacy-media tests separately
verify that existing collision labels remain exactly selectable for repair.

## Delete and Persistence

```text
delete /DOCS/SUMMARY
dir /DOCS
sync
reboot
dir /DOCS
```

Expected: `SUMMARY` remains absent and unrelated files remain coherent.
