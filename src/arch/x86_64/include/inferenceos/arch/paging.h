#ifndef INFERENCEOS_ARCH_PAGING_H
#define INFERENCEOS_ARCH_PAGING_H

#include <inferenceos/base.h>

bool x86_64_paging_enable_execute_disable(void);
ios_uptr x86_64_paging_root(void);
void x86_64_paging_activate(ios_uptr root_address);
void x86_64_paging_invalidate(ios_uptr virtual_address);

#endif
