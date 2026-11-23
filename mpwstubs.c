/*
 * Apple's 68k Open Transport client libraries were written for MPW, whose
 * runtime provided these. Only the DNS-related parts of the libraries are
 * linked (unused code is removed), and they only need the following.
 */
#include <Memory.h>

long t_errno;
int errno __asm__("errno");
void __dl__FPv(void *p) { if (p) DisposePtr((Ptr)p); }     /* MPW C++ operator delete */
