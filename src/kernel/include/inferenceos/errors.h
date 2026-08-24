#ifndef INFERENCEOS_ERRORS_H
#define INFERENCEOS_ERRORS_H

#include <inferenceos/base.h>

typedef ios_i64 ios_status;

/*
 * Error numbers are stable ABI values. New values may be appended, but an
 * assigned value must never be renumbered or reused. APIs return their
 * negative form; zero is success.
 */
enum ios_error {
    IOS_E_INVALID_ARGUMENT = 1,
    IOS_E_BAD_ADDRESS = 2,
    IOS_E_OUT_OF_RANGE = 3,
    IOS_E_OVERFLOW = 4,
    IOS_E_NOT_SUPPORTED = 5,
    IOS_E_UNSUPPORTED_VERSION = 6,
    IOS_E_INVALID_FLAGS = 7,
    IOS_E_NOT_FOUND = 8,
    IOS_E_ALREADY_EXISTS = 9,
    IOS_E_NO_MEMORY = 10,
    IOS_E_NO_SPACE = 11,
    IOS_E_ACCESS_DENIED = 12,
    IOS_E_BAD_HANDLE = 13,
    IOS_E_WRONG_HANDLE_TYPE = 14,
    IOS_E_BUSY = 15,
    IOS_E_WOULD_BLOCK = 16,
    IOS_E_TIMEOUT = 17,
    IOS_E_INTERRUPTED = 18,
    IOS_E_IO = 19,
    IOS_E_CORRUPT = 20,
    IOS_E_READ_ONLY = 21,
    IOS_E_NOT_EMPTY = 22,
    IOS_E_PROTOCOL = 23,
    IOS_E_INVALID_STATE = 24,
    IOS_E_UNKNOWN_SYSCALL = 25
};

#define IOS_OK ((ios_status)0)
#define IOS_ERROR(error_code) (-(ios_status)(error_code))
#define IOS_SUCCEEDED(status) ((ios_status)(status) >= IOS_OK)
#define IOS_FAILED(status) ((ios_status)(status) < IOS_OK)

#endif
