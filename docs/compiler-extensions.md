# Compiler Extension Policy

InferenceOS project-owned C code is freestanding ISO C17. CMake disables language extensions with
`C_EXTENSIONS NO`, and GCC and Clang builds use `-Wpedantic`. An implementation detail is permitted
only when it appears in the allowlist below, stays inside its stated scope, and passes both compiler
profiles. Standard C17 syntax and `_Static_assert` do not require an allowlist entry.

This policy applies to every project-owned `.c`, `.h`, and inline-assembly use beneath `src/`.
Standalone `.S` files are governed by the assembly rules below. Third-party inputs, generated build
files, tests, and build tooling are not project source, but they must not be copied into `src/` to
evade this policy.

## Approved extensions

| ID | Approved construct | Required wrapper | Permitted scope | Purpose and constraints |
|---|---|---|---|---|
| EXT-001 | GCC/Clang `__attribute__` forms: `aligned`, `packed`, `section`, and `used` | A named `IOS_*` macro in `src/kernel/include/inferenceos/compiler.h` | Project-owned C declarations that require the stated representation or linkage | Fixed ABI/layout, linker placement, and retained entry objects. `packed` does not replace explicit serialization or compile-time size/offset assertions. Use standard C17 `_Noreturn` for non-returning functions. |
| EXT-002 | GCC/Clang `__attribute__((ms_abi))` | `IOS_UEFI_API` in `src/kernel/include/inferenceos/compiler.h` | UEFI declarations and call boundaries under `src/boot/uefi/` | Implements the x86-64 UEFI calling convention. It must not be used for kernel, syscall, IPC, filesystem, Shell, GUI, or application APIs. |
| EXT-003 | GCC/Clang `__asm__ volatile` | A typed function or macro whose public declaration contains no assembly syntax | Implementations under `src/arch/x86_64/` only | CPU instructions, control registers, interrupt state, descriptor-table operations, port I/O, and compiler/CPU barriers that cannot be expressed in C17. Operands, clobbers, volatility, and memory effects must be explicit. Prefer a standalone `.S` file when control flow or register ownership is non-trivial. |
| EXT-004 | GCC/Clang `__attribute__((sysv_abi))` | `IOS_SYSV_API` | The UEFI-to-kernel entry function-pointer declaration under `src/boot/uefi/` only | Crosses from the PE32+ Microsoft x64 firmware ABI into the kernel's documented SysV x86-64 handoff ABI after `ExitBootServices`. |

No other compiler extension is currently approved. In particular, GNU statement expressions,
`typeof`/`__typeof__`, nested functions, zero-length arrays, case ranges, computed goto, naked
functions, vector types, and compiler-specific atomics are prohibited until added through the
approval process. Variable-length arrays are standard but optional in C17 and are prohibited in
kernel and system-application code because they create unbounded stack use.

Compiler-identification macros such as `__GNUC__` and `__clang__` may be inspected only in
`compiler.h` to implement an approved wrapper or issue an unsupported-compiler diagnostic. They are
not a mechanism for creating unlisted compiler-specific behavior.

## Ownership and placement

- The portability layer owns extension spelling. Ordinary call sites use `IOS_*` wrappers and must
  not spell `__attribute__`, `__asm__`, `__GNUC__`, or `__clang__` directly.
- `src/kernel/include/inferenceos/compiler.h` owns portable declarations and compiler selection.
  Architecture-specific implementations remain under `src/arch/x86_64/`; UEFI ABI uses remain
  under `src/boot/uefi/`.
- Public wrappers must describe their semantics, arguments, observable side effects, and ordering
  guarantees. A wrapper must fail compilation on an unsupported compiler rather than silently
  weaken behavior.
- Filesystem policy, allocation, hashing, parsing, filename handling, GUI layout/rendering,
  windowing, and command semantics must not contain inline or standalone assembly.
- On-disk and IPC layout correctness must use explicit encoders/decoders plus `_Static_assert` for
  fixed sizes and offsets. `packed` layout alone is never authoritative.
- Build and portability maintainers own this document and `compiler.h`. Architecture maintainers
  own the implementations behind architecture wrappers. Review of either area must enforce the
  same restrictions; ownership does not authorize bypassing the allowlist.

## Standalone assembly

Standalone x86-64 assembly is limited to entry, traps, context transition, and CPU control that
cannot reasonably be expressed through an approved C interface. Each `.S` file must have a narrow
C-facing ABI, document registers and stack state, and avoid policy decisions. Assembly must not be
used as an optimization shortcut. Both compiler profiles must preprocess/assemble it, and a C or
host-testable implementation must remain the default for non-CPU algorithms.

## Adding or changing an extension

An allowlist change must be reviewed before dependent source is merged. The change must:

1. Add a stable ID and exact syntax to the table above.
2. Name its wrapper, owner, and smallest permitted path scope.
3. Explain why ISO C17 or a small standalone assembly boundary is insufficient.
4. Specify ABI, layout, optimization, and undefined-behavior risks.
5. Add positive GCC and Clang coverage and a negative layout/source-policy check where applicable.
6. State whether generated artifacts, on-disk formats, or public contracts change.
7. Include a removal condition when the extension is temporary.

Approval requires the normal code review plus explicit review from build/portability ownership and,
for assembly or ABI changes, architecture ownership. A compiler accepting syntax is not approval.

## Enforcement

The source-layout validation task scans project-owned source for direct extension spellings and
assembly outside approved paths. The GCC and Clang configure/build/test presets compile with
freestanding C17, extensions disabled, pedantic warnings, and warnings-as-errors. Reviews must also
verify semantic scope because textual scanning cannot prove that an approved primitive is used for
an approved purpose.

Any violation fails the build gate. Suppressing a diagnostic, weakening a compiler flag, moving
source outside `src/`, or hiding a construct behind an unreviewed macro is also a policy violation.
