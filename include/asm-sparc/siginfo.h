#ifndef ___ASM_SPARC_SIGINFO_H
#define ___ASM_SPARC_SIGINFO_H
#if defined(__sparc__) && defined(__arch64__)
#include <asm-sparc/siginfo_64.h>
#else
#include <asm-sparc/siginfo_32.h>
#endif
#endif
