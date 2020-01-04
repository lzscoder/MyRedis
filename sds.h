#ifndef __SDS_H
#define __SDS_H

//最大预先分配长度1MB
#define SDS_MAX_PREALLOC (1024*1024)

#include<sys/types.h>
#include<stdarg.h>

//类型别名
typedef char *sds;

struct sdshdr
{
    int len;
    int free;
    //柔性数组，不占用内存
    char buf[];
};
//返回已用的空间长度
static inline size_t sdslen(const sds s){
    struct sdshdr *sh = (void*)(s-(sizeof(struct sdshdr)));
    return sh->len;
}
//返回剩余空间长度
static inline size_t sdsavail(const sds s){
    struct sdshdr *sh = (void*)(s-(sizeof(struct sdshdr)));
    return sh->free;
}
//inline函数的定义
size_t sdslen(const sds s);
size_t sdsavail(const sds s);
//-----------------------------------------------
sds sdsnewlen(const void *init, size_t initlen);
sds sdsempty(void);
sds sdsnew(const char *init);
sds sdsdup(const sds s);
void sdsfree(sds s);
void sdsupdatelen(sds s);
sds sdsgrowzero(sds s, size_t len);
void sdsclear(sds s);
//--------------打印函数------------直接拷贝过来-------------可能有用--------------
sds sdsfromlonglong(long long value);
sds sdscatvprintf(sds s, const char *fmt, va_list ap);
sds sdscatprintf(sds s, const char *fmt, ...);
sds sdscatfmt(sds s, char const *fmt, ...);
//-------------------------------------------------------------------
void sdsrange(sds s, int start, int end);
sds sdstrim(sds s, const char *cset);
sds sdscatlen(sds s, const void *t, size_t len);
sds sdscat(sds s, const char *t);
sds sdscatsds(sds s, const sds t);
sds sdscpylen(sds s, const char *t, size_t len);
sds sdscpy(sds s, const char *t);

void sdstolower(sds s);
void sdstoupper(sds s);
int sdscmp(const sds s1, const sds s2);
sds *sdssplitlen(const char *s, int len, const char *sep, int seplen, int *count);
void sdsfreesplitres(sds *tokens, int count);
sds sdscatrepr(sds s, const char *p, size_t len);
sds *sdssplitargs(const char *line, int *argc);
sds sdsmapchars(sds s, const char *from, const char *to, size_t setlen);
sds sdsjoin(char **argv, int argc, char *sep);
/* Low level functions exposed to the user API */
sds sdsMakeRoomFor(sds s, size_t addlen);
void sdsIncrLen(sds s, int incr);
sds sdsRemoveFreeSpace(sds s);
size_t sdsAllocSize(sds s);


#endif // __SDS_H
