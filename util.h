#ifndef __UTIL_H_
#define __UTIL_H_

#include"sds.h"

int ll2string(char *s, size_t len, long long value);
int string2ll(const char *s, size_t slen, long long *value);
int string2l(const char *s, size_t slen, long *value);
#endif // __UTIL_H_
