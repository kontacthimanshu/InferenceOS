#ifndef INFERENCEOS_COMPILER_H
#define INFERENCEOS_COMPILER_H

/*
 * Compiler-specific spelling is owned by this header. Keep these wrappers in
 * lockstep with docs/compiler-extensions.md and do not add a wrapper without
 * first extending that allowlist.
 */
#if !defined(__clang__) && !defined(__GNUC__)
#error "InferenceOS requires an approved GCC- or Clang-compatible compiler"
#endif

#define IOS_ALIGNED(alignment) __attribute__((aligned(alignment)))
#define IOS_PACKED __attribute__((packed))
#define IOS_SECTION(section_name) __attribute__((section(section_name)))
#define IOS_USED __attribute__((used))

/* Valid only for declarations at the UEFI firmware call boundary. */
#define IOS_UEFI_API __attribute__((ms_abi))

/* Used only by the PE32+ UEFI loader when entering the SysV x86-64 kernel ABI. */
#define IOS_SYSV_API __attribute__((sysv_abi))

#endif
