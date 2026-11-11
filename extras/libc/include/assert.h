#ifndef _ASSERT_H
#define _ASSERT_H
void __assert_fail(const char *expr, const char *file, int line);
#define assert(e) ((e) ? (void)0 : __assert_fail(#e, __FILE__, __LINE__))
#endif
