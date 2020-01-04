#include"sds.h"
#include<string.h>
#include"zmalloc.h"
#include<stdio.h>
#include <ctype.h>
#include<stdlib.h>
#include<assert.h>
//根据给定的初始化字符串init和字符串长度initlen创建一个新的sds

sds sdsnewlen(const void *init,size_t initlen){
    struct sdshdr *sh;

    if(init){
        sh = zmalloc(sizeof(struct sdshdr)+initlen+1);
    }else{
        sh = zcalloc(sizeof(struct sdshdr)+initlen+1);
    }

    //内存分配失败
    if(sh == NULL) return NULL;

    sh->len = initlen;
    sh->free = 0;

    if(initlen && init)
        memcpy(sh->buf,init,initlen);
    sh->buf[initlen] = '\0';

    return (char*)sh->buf;
}

//创建并返回一个保存字符串为""的sds
sds sdsempty(void){
    return sdsnewlen("",0);
}

//创建buf为inti的sdshdr
sds sdsnew(const char* init){
    size_t initlen = (init == NULL) ? 0 : strlen(init);
    return sdsnewlen(init,initlen);
}

//sds的副本
sds sdsdup(const sds s){
    return sdsnewlen(s,sdslen(s));
}

//释放给定的sds
void sdsfree(sds s){
    if(s==NULL) return;
    zfree(s - sizeof(struct sdshdr));
}

//将长度为len的字符串t追加到sds的字符串末端
sds sdscatlen(sds s,const void* t,size_t len){
    struct sdshdr *sh;

    //获取当前的字符串长度
    size_t curlen = sdslen(s);
    //拓展s的空间
    s = sdsMakeRoomFor(s,len);

    //内存不足
    if(s == NULL) return NULL;

    //复制字符串
    sh = (void*)(s - (sizeof(struct sdshdr)));
    //s+curlen是'\0'的位置
    memcpy(s+curlen,t,len);

    //更新属性

    //这个len应该不包括t的末尾'\0'
    sh->len = curlen+len;
    sh->free = sh->free - len;

    s[curlen+len] = '\0';

    return s;
}

//将字符串t追加到s尾部
sds sdscat(sds s, const char *t) {
    return sdscatlen(s, t, strlen(t));
}
//将一个sds的字符串追加到另一个sds的字符串上
sds sdscatsds(sds s, const sds t) {
    return sdscatlen(s, t, sdslen(t));
}

//拷贝字符串t的前len个字符到s上
sds sdscpylen(sds s,const char *t,size_t len){
    struct sdshdr *sh = (void*) (s-(sizeof(struct sdshdr)));
    size_t totlen = sh->free+sh->len;
    if(totlen<len){
        //原s的长度不够
        s = sdsMakeRoomFor(s,len - sh->len);
        if(s==NULL) return NULL;

        sh = (void*)(s-(sizeof(struct sdshdr)));
        totlen = sh->free+sh->len;
    }

    memcpy(s,t,len);

    s[len] = '\0';

    sh->len = len;
    sh->free = totlen - len;

    return s;
}

//将字符串拷贝到sds上
sds sdscpy(sds s,const char *t){
    return sdscpylen(s,t,strlen(t));
}

//废弃函数，用来更新sds的长度
void sdsupdatelen(sds s) {
    struct sdshdr *sh = (void*) (s-(sizeof(struct sdshdr)));
    int reallen = strlen(s);
    sh->free += (sh->len-reallen);
    sh->len = reallen;
}

//sds的长度变为len，如果当前字符串的长度大于len则直接返回

sds sdsgrowzero(sds s,size_t len){

    struct sdshdr *sh = (void*)(s-(sizeof(struct sdshdr)));
    size_t totlen,curlen = sh->len;

    if(len<curlen) return s;

    s = sdsMakeRoomFor(s,len-curlen);

    if(s == NULL) return NULL;

    sh = (void*)(s-(sizeof(struct sdshdr)));


    //[   )//最左边的元素本来就是'\0',从它开始,后面还有len-curlen+1个元素

    memset(s+curlen,0,(len-curlen+1));

    totlen = sh->len + sh->free;

    sh->len = len;
    sh->free = totlen-sh->len;

    return s;
}

//清空sds,惰性删除
void sdsclear(sds s){
    struct sdshdr* p = (void*)(s - (sizeof(struct sdshdr)));

    p->free += p->len;
    p->len = 0;
    p->buf[0] = '\0';
}


//---------------打印函数------------------------

#define SDS_LLSTR_SIZE 21
int sdsll2str(char *s, long long value) {
    char *p, aux;
    unsigned long long v;
    size_t l;

    /* Generate the string representation, this method produces
     * an reversed string. */
    v = (value < 0) ? -value : value;
    p = s;
    do {
        *p++ = '0'+(v%10);
        v /= 10;
    } while(v);
    if (value < 0) *p++ = '-';

    /* Compute length and add null term. */
    l = p-s;
    *p = '\0';

    /* Reverse the string. */
    p--;
    while(s < p) {
        aux = *s;
        *s = *p;
        *p = aux;
        s++;
        p--;
    }
    return l;
}

/* Identical sdsll2str(), but for unsigned long long type. */
int sdsull2str(char *s, unsigned long long v) {
    char *p, aux;
    size_t l;

    /* Generate the string representation, this method produces
     * an reversed string. */
    p = s;
    do {
        *p++ = '0'+(v%10);
        v /= 10;
    } while(v);

    /* Compute length and add null term. */
    l = p-s;
    *p = '\0';

    /* Reverse the string. */
    p--;
    while(s < p) {
        aux = *s;
        *s = *p;
        *p = aux;
        s++;
        p--;
    }
    return l;
}

/* Create an sds string from a long long value. It is much faster than:
 *
 * sdscatprintf(sdsempty(),"%lld\n", value);
 */
// 根据输入的 long long 值 value ，创建一个 SDS
sds sdsfromlonglong(long long value) {
    char buf[SDS_LLSTR_SIZE];
    int len = sdsll2str(buf,value);

    return sdsnewlen(buf,len);
}

/*
 * 打印函数，被 sdscatprintf 所调用
 *
 * T = O(N^2)
 */
/* Like sdscatpritf() but gets va_list instead of being variadic. */
sds sdscatvprintf(sds s, const char *fmt, va_list ap) {
    va_list cpy;
    char staticbuf[1024], *buf = staticbuf, *t;
    size_t buflen = strlen(fmt)*2;

    /* We try to start using a static buffer for speed.
     * If not possible we revert to heap allocation. */
    if (buflen > sizeof(staticbuf)) {
        buf = zmalloc(buflen);
        if (buf == NULL) return NULL;
    } else {
        buflen = sizeof(staticbuf);
    }

    /* Try with buffers two times bigger every time we fail to
     * fit the string in the current buffer size. */
    while(1) {
        buf[buflen-2] = '\0';
        va_copy(cpy,ap);
        // T = O(N)
        vsnprintf(buf, buflen, fmt, cpy);
        if (buf[buflen-2] != '\0') {
            if (buf != staticbuf) zfree(buf);
            buflen *= 2;
            buf = zmalloc(buflen);
            if (buf == NULL) return NULL;
            continue;
        }
        break;
    }

    /* Finally concat the obtained string to the SDS string and return it. */
    t = sdscat(s, buf);
    if (buf != staticbuf) zfree(buf);
    return t;
}

/*
 * 打印任意数量个字符串，并将这些字符串追加到给定 sds 的末尾
 *
 * T = O(N^2)
 */
/* Append to the sds string 's' a string obtained using printf-alike format
 * specifier.
 *
 * After the call, the modified sds string is no longer valid and all the
 * references must be substituted with the new pointer returned by the call.
 *
 * Example:
 *
 * s = sdsempty("Sum is: ");
 * s = sdscatprintf(s,"%d+%d = %d",a,b,a+b).
 *
 * Often you need to create a string from scratch with the printf-alike
 * format. When this is the need, just use sdsempty() as the target string:
 *
 * s = sdscatprintf(sdsempty(), "... your format ...", args);
 */
sds sdscatprintf(sds s, const char *fmt, ...) {
    va_list ap;
    char *t;
    va_start(ap, fmt);
    // T = O(N^2)
    t = sdscatvprintf(s,fmt,ap);
    va_end(ap);
    return t;
}
/* This function is similar to sdscatprintf, but much faster as it does
 * not rely on sprintf() family functions implemented by the libc that
 * are often very slow. Moreover directly handling the sds string as
 * new data is concatenated provides a performance improvement.
 *
 * However this function only handles an incompatible subset of printf-alike
 * format specifiers:
 *
 * %s - C String
 * %S - SDS string
 * %i - signed int
 * %I - 64 bit signed integer (long long, int64_t)
 * %u - unsigned int
 * %U - 64 bit unsigned integer (unsigned long long, uint64_t)
 * %% - Verbatim "%" character.
 */
sds sdscatfmt(sds s, char const *fmt, ...) {
    struct sdshdr *sh = (void*) (s-(sizeof(struct sdshdr)));
    size_t initlen = sdslen(s);
    const char *f = fmt;
    int i;
    va_list ap;

    va_start(ap,fmt);
    f = fmt;    /* Next format specifier byte to process. */
    i = initlen; /* Position of the next byte to write to dest str. */
    while(*f) {
        char next, *str;
        size_t l;
        long long num;
        unsigned long long unum;

        /* Make sure there is always space for at least 1 char. */
        if (sh->free == 0) {
            s = sdsMakeRoomFor(s,1);
            sh = (void*) (s-(sizeof(struct sdshdr)));
        }

        switch(*f) {
        case '%':
            next = *(f+1);
            f++;
            switch(next) {
            case 's':
            case 'S':
                str = va_arg(ap,char*);
                l = (next == 's') ? strlen(str) : sdslen(str);
                if (sh->free < l) {
                    s = sdsMakeRoomFor(s,l);
                    sh = (void*) (s-(sizeof(struct sdshdr)));
                }
                memcpy(s+i,str,l);
                sh->len += l;
                sh->free -= l;
                i += l;
                break;
            case 'i':
            case 'I':
                if (next == 'i')
                    num = va_arg(ap,int);
                else
                    num = va_arg(ap,long long);
                {
                    char buf[SDS_LLSTR_SIZE];
                    l = sdsll2str(buf,num);
                    if (sh->free < l) {
                        s = sdsMakeRoomFor(s,l);
                        sh = (void*) (s-(sizeof(struct sdshdr)));
                    }
                    memcpy(s+i,buf,l);
                    sh->len += l;
                    sh->free -= l;
                    i += l;
                }
                break;
            case 'u':
            case 'U':
                if (next == 'u')
                    unum = va_arg(ap,unsigned int);
                else
                    unum = va_arg(ap,unsigned long long);
                {
                    char buf[SDS_LLSTR_SIZE];
                    l = sdsull2str(buf,unum);
                    if (sh->free < l) {
                        s = sdsMakeRoomFor(s,l);
                        sh = (void*) (s-(sizeof(struct sdshdr)));
                    }
                    memcpy(s+i,buf,l);
                    sh->len += l;
                    sh->free -= l;
                    i += l;
                }
                break;
            default: /* Handle %% and generally %<unknown>. */
                s[i++] = next;
                sh->len += 1;
                sh->free -= 1;
                break;
            }
            break;
        default:
            s[i++] = *f;
            sh->len += 1;
            sh->free -= 1;
            break;
        }
        f++;
    }
    va_end(ap);

    /* Add null-term */
    s[i] = '\0';
    return s;
}


//-------------------------打印函数结束------------------------------

//按索引截取sds的一段

void sdsrange(sds s, int start, int end){
    struct sdshdr *sh = (void *)(s-(sizeof(struct sdshdr)));
    size_t newlen,len = sdslen(s);

    if(len == 0)return;

    if(start < 0){
        start = len+start;
        if(start < 0) start = 0;
    }
    if(end<0){
        end = len + end;
        if(end<0) end = 0;
    }

    newlen = (start>len)?0:(end-start)+1;
    if(newlen!=0){
        if(start >= (signed)len)
            newlen = 0;
        else if(end >= (signed)len){
            end = len-1;
            newlen = (start>end)?0:(end-start)+1;
        }
    }else{
            start = 0;
    }

    //如果有需要对字符串进行移动
    if(start && newlen) memmove(sh->buf,sh->buf+start,newlen);

    sh->buf[newlen]= 0;
    sh->free = sh->free+(sh->len - newlen);
    sh->len = newlen;
}


sds sdstrim(sds s, const char *cset){
    struct sdshdr *sh = (void*) (s-(sizeof(struct sdshdr)));
    char *start, *end, *sp, *ep;
    size_t len;

    // 设置和记录指针
    sp = start = s;
    ep = end = s+sdslen(s)-1;

    // 修剪, T = O(N^2)
    while(sp <= end && strchr(cset, *sp)) sp++;
    while(ep > start && strchr(cset, *ep)) ep--;

    // 计算 trim 完毕之后剩余的字符串长度
    len = (sp > ep) ? 0 : ((ep-sp)+1);

    // 如果有需要，前移字符串内容
    // T = O(N)
    if (sh->buf != sp) memmove(sh->buf, sp, len);

    // 添加终结符
    sh->buf[len] = '\0';

    // 更新属性
    sh->free = sh->free+(sh->len-len);
    sh->len = len;

    // 返回修剪后的 sds
    return s;
}


//转换为小写字符，大写字符，比较函数
void sdstolower(sds s){
    int len = sdslen(s);
    for(int i = 0;i<len;i++){
        s[i] = tolower(s[i]);
    }
}
void sdstoupper(sds s){
    int len = sdslen(s);
    for(int i = 0;i<len;i++){
        s[i] = toupper(s[i]);
    }
}
int sdscmp(const sds s1,const sds s2){
    size_t l1,l2,minlen;
    int cmp;
    l1 = sdslen(s1);
    l2 = sdslen(s2);
    minlen = (l1<l2)?l1:l2;

    cmp = memcmp(s1,s2,minlen);

    if(cmp==0) return l1-l2;

    return cmp;
}



//用字符串分割字符串,有点难，返回的是sds数组
sds *sdssplitlen(const char *s, int len, const char *sep, int seplen, int *count){
    int elements = 0, slots = 5, start = 0, j;
    sds *tokens;

    if (seplen < 1 || len < 0) return NULL;

    tokens = zmalloc(sizeof(sds)*slots);
    if (tokens == NULL) return NULL;

    if (len == 0) {
        *count = 0;
        return tokens;
    }

    // T = O(N^2)
    for (j = 0; j < (len-(seplen-1)); j++) {
        /* make sure there is room for the next element and the final one */
        if (slots < elements+2) {
            sds *newtokens;

            slots *= 2;
            newtokens = zrealloc(tokens,sizeof(sds)*slots);
            if (newtokens == NULL) goto cleanup;
            tokens = newtokens;
        }
        /* search the separator */
        // T = O(N)
        if ((seplen == 1 && *(s+j) == sep[0]) || (memcmp(s+j,sep,seplen) == 0)) {
            tokens[elements] = sdsnewlen(s+start,j-start);
            if (tokens[elements] == NULL) goto cleanup;
            elements++;
            start = j+seplen;
            j = j+seplen-1; /* skip the separator */
        }
    }
    /* Add the final element. We are sure there is room in the tokens array. */
    tokens[elements] = sdsnewlen(s+start,len-start);
    if (tokens[elements] == NULL) goto cleanup;
    elements++;
    *count = elements;
    return tokens;

cleanup:
    {
        int i;
        for (i = 0; i < elements; i++) sdsfree(tokens[i]);
        zfree(tokens);
        *count = 0;
        return NULL;
    }
}

//释放sds元素
void sdsfreesplitres(sds *tokens, int count){
    if(!tokens) return;
    for(int i= 0;i<count;i++){
        sdsfree(tokens[i]);
    }
    zfree(tokens);
}


sds sdscatrepr(sds s, const char *p, size_t len){

    s = sdscatlen(s,"\"",1);

    while(len--) {
        switch(*p) {
        case '\\':
        case '"':
            s = sdscatprintf(s,"\\%c",*p);
            break;
        case '\n': s = sdscatlen(s,"\\n",2); break;
        case '\r': s = sdscatlen(s,"\\r",2); break;
        case '\t': s = sdscatlen(s,"\\t",2); break;
        case '\a': s = sdscatlen(s,"\\a",2); break;
        case '\b': s = sdscatlen(s,"\\b",2); break;
        default:
            if (isprint(*p))
                s = sdscatprintf(s,"%c",*p);
            else
                s = sdscatprintf(s,"\\x%02x",(unsigned char)*p);
            break;
        }
        p++;
    }
    return sdscatlen(s,"\"",1);

}

int is_hex_digit(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

int hex_digit_to_int(char c) {
    switch(c) {
    case '0': return 0;
    case '1': return 1;
    case '2': return 2;
    case '3': return 3;
    case '4': return 4;
    case '5': return 5;
    case '6': return 6;
    case '7': return 7;
    case '8': return 8;
    case '9': return 9;
    case 'a': case 'A': return 10;
    case 'b': case 'B': return 11;
    case 'c': case 'C': return 12;
    case 'd': case 'D': return 13;
    case 'e': case 'E': return 14;
    case 'f': case 'F': return 15;
    default: return 0;
    }
}

//有点难这个函数，对一行字符串进行划分
sds *sdssplitargs(const char *line, int *argc) {
    const char *p = line;
    char *current = NULL;
    char **vector = NULL;

    *argc = 0;
    while(1) {

        /* skip blanks */
        // 跳过空白
        // T = O(N)
        while(*p && isspace(*p)) p++;

        if (*p) {
            /* get a token */
            int inq=0;  /* set to 1 if we are in "quotes" */
            int insq=0; /* set to 1 if we are in 'single quotes' */
            int done=0;

            if (current == NULL) current = sdsempty();

            // T = O(N)
            while(!done) {
                if (inq) {
                    if (*p == '\\' && *(p+1) == 'x' &&
                                             is_hex_digit(*(p+2)) &&
                                             is_hex_digit(*(p+3)))
                    {
                        unsigned char byte;

                        byte = (hex_digit_to_int(*(p+2))*16)+
                                hex_digit_to_int(*(p+3));
                        current = sdscatlen(current,(char*)&byte,1);
                        p += 3;
                    } else if (*p == '\\' && *(p+1)) {
                        char c;

                        p++;
                        switch(*p) {
                        case 'n': c = '\n'; break;
                        case 'r': c = '\r'; break;
                        case 't': c = '\t'; break;
                        case 'b': c = '\b'; break;
                        case 'a': c = '\a'; break;
                        default: c = *p; break;
                        }
                        current = sdscatlen(current,&c,1);
                    } else if (*p == '"') {
                        /* closing quote must be followed by a space or
                         * nothing at all. */
                        if (*(p+1) && !isspace(*(p+1))) goto err;
                        done=1;
                    } else if (!*p) {
                        /* unterminated quotes */
                        goto err;
                    } else {
                        current = sdscatlen(current,p,1);
                    }
                } else if (insq) {
                    if (*p == '\\' && *(p+1) == '\'') {
                        p++;
                        current = sdscatlen(current,"'",1);
                    } else if (*p == '\'') {
                        /* closing quote must be followed by a space or
                         * nothing at all. */
                        if (*(p+1) && !isspace(*(p+1))) goto err;
                        done=1;
                    } else if (!*p) {
                        /* unterminated quotes */
                        goto err;
                    } else {
                        current = sdscatlen(current,p,1);
                    }
                } else {
                    switch(*p) {
                    case ' ':
                    case '\n':
                    case '\r':
                    case '\t':
                    case '\0':
                        done=1;
                        break;
                    case '"':
                        inq=1;
                        break;
                    case '\'':
                        insq=1;
                        break;
                    default:
                        current = sdscatlen(current,p,1);
                        break;
                    }
                }
                if (*p) p++;
            }
            /* add the token to the vector */
            // T = O(N)
            vector = zrealloc(vector,((*argc)+1)*sizeof(char*));
            vector[*argc] = current;
            (*argc)++;
            current = NULL;
        } else {
            /* Even on empty input string return something not NULL. */
            if (vector == NULL) vector = zmalloc(sizeof(void*));
            return vector;
        }
    }

err:
    while((*argc)--)
        sdsfree(vector[*argc]);
    zfree(vector);
    if (current) sdsfree(current);
    *argc = 0;
    return NULL;
}
//所有在sds中存在from里的字符都替换为to中对应的字符
sds sdsmapchars(sds s, const char *from, const char *to, size_t setlen){

 int len = sdslen(s);

 for(int i = 0;i<len;i++){
    for(int j = 0;j<setlen;j++){
        if(s[i]==from[j]){
            s[i] = to[j];
            break;
        }
    }
 }
 return s;
}
//将数个字符串串在一起并存在sds中
sds sdsjoin(char **argv, int argc, char *sep){

    sds join =  sdsempty();
     int j;
     for(j=0;j<argc;j++){
        join = sdscat(join,argv[j]);
        if(j != argc-1)
            join = sdscat(join,sep);
     }
     return join;
}
//----------------------------底层API--------------------------
//扩展内存
sds sdsMakeRoomFor(sds s,size_t addlen){
    struct sdshdr * sh,*newsh;

    //获取s目前的空余空间长度
    size_t free = sdsavail(s);

    size_t len,newlen;

    if(free>=addlen) return s;

    len = sdslen(s);

    sh = (void*)(s - (sizeof(struct sdshdr)));

    //s最少需要的长度
    newlen = (len+addlen);

    if(newlen < SDS_MAX_PREALLOC)//太小
        newlen *= 2;
    else//过大
        newlen += SDS_MAX_PREALLOC;


    newsh = zrealloc(sh,sizeof(struct sdshdr)+newlen+1);
    if(newsh == NULL) return NULL;

    newsh->free = newlen - len;
    newsh->buf[sizeof(struct sdshdr)+len] = '\0';
    return newsh->buf;
}

//扩展sds的长度
void sdsIncrLen(sds s, int incr){

    struct sdshdr* sh = (void*)(s - (sizeof(struct sdshdr)));

    assert(sh->free >= incr);

    sh->len += incr;

    sh->free -= incr;


    sh->buf[sh->len]='\0';
}
//回收sds中的空余空间
sds sdsRemoveFreeSpace(sds s){

    struct sdshdr* sh = (void*)(s-(sizeof(struct sdshdr)));

    sh = zrealloc(sh,sizeof(struct sdshdr)+sh->len+1);
    sh->free = 0;
    return sh->buf;
}
//返回给定的内存空间长度
size_t sdsAllocSize(sds s){
    struct sdshdr *sh = (void*)(s-(sizeof(struct sdshdr)));
    return sizeof(*sh)+sh->len+sh->free+1;
}



#ifdef DS_TEST_MAIN
# include <stdio.h>
#include"sds.h"
#include<stdlib.h>
#include<string.h>

void test1(){
    //测试柔性数组
    struct sdshdr* sdshdr1 = (struct sdshdr*)malloc(sizeof(struct sdshdr)+100);
    sdshdr1->len = 6;
    sdshdr1->free = 94;
    strcpy(sdshdr1->buf,"testv");

//    sdshdr1->buf[0] = '1';
//    sdshdr1->buf[1] = '2';
//    sdshdr1->buf[2] = '3';
//    sdshdr1->buf[3] = '4';
//    sdshdr1->buf[4] = '5';
//    sdshdr1->buf[5] = '6';
    sds p = sdshdr1->buf;
    printf("len:%zu\nfree:%zu\nbuf:%s\n",sdslen(p),sdsavail(p),p);

    //测试sdscatlen函数

    char add[]=" my love!";
    p = sdscatlen(p,add,9);
    printf("len:%zu\nfree:%zu\nbuf:%s\n",sdslen(p),sdsavail(p),p);

    //测试需要分配较大内存的情况
    sds s1 = sdsMakeRoomFor(p,1024*1024);
    printf("len:%zu\nfree:%zu\nbuf:%s\n",sdslen(s1),sdsavail(s1),s1);

    //测试buf为null的情况
    struct sdshdr* sdshdr2 = (struct sdshdr*)malloc(sizeof(struct sdshdr));
    sdshdr1->len = 0;
    sdshdr1->free = 0;

    sds s2 = sdsMakeRoomFor(sdshdr2->buf,100);
    printf("len:%zu\nfree:%zu\nbuf:%s\n",sdslen(s2),sdsavail(s2),s2);
}

void test2(){
    struct sdshdr* sdshdr1 = (struct sdshdr*)malloc(sizeof(struct sdshdr)+100);
    sdshdr1->len = 5;
    sdshdr1->free = 94;
    strcpy(sdshdr1->buf,"testv");
    sds p = sdshdr1->buf;
    printf("len:%zu\nfree:%zu\nbuf:%s\n",sdslen(p),sdsavail(p),p);

    //测试sdscatlen函数

    char add[]=" my love!";
    p = sdscatlen(p,add,9);
    printf("len:%zu\nfree:%zu\nbuf:%s\n",sdslen(p),sdsavail(p),p);

    printf("testing cpy and cat function:\n");
    //
    //sds sdscatlen(sds s, const void *t, size_t len);
    printf("len:%zu\nfree:%zu\nbuf:%s\n",sdslen(p),sdsavail(p),p);
    p = sdscatlen(p,"123456",6);
    printf("len:%zu\nfree:%zu\nbuf:%s\n",sdslen(p),sdsavail(p),p);

    //sds sdscat(sds s, const char *t);
    p = sdscat(p,"789");
    printf("len:%zu\nfree:%zu\nbuf:%s\n",sdslen(p),sdsavail(p),p);
    //sds sdscatsds(sds s, const sds t);
    p = sdscatsds(p,p);
    printf("len:%zu\nfree:%zu\nbuf:%s\n",sdslen(p),sdsavail(p),p);
}

void test3(){
    //sds sdsnewlen(const void *init, size_t initlen);
    sds p1 = sdsnewlen("123",3);
    printf("len:%zu\nfree:%zu\nbuf:%s\n",sdslen(p1),sdsavail(p1),p1);
    sds p2 = sdsnewlen("",0);
    printf("len:%zu\nfree:%zu\nbuf:%s\n",sdslen(p2),sdsavail(p2),p2);

    //sds sdsempty(void);
    sds p3 = sdsempty();
    printf("len:%zu\nfree:%zu\nbuf:%s\n",sdslen(p3),sdsavail(p3),p3);
    //sds sdsnew(const char *init);
    sds p4 = sdsnew("456");
    printf("len:%zu\nfree:%zu\nbuf:%s\n",sdslen(p4),sdsavail(p4),p4);

    //sds sdsdup(const sds s);

    sds p5 = sdsdup(p4);
    printf("len:%zu\nfree:%zu\nbuf:%s\n",sdslen(p5),sdsavail(p5),p5);

    //void sdsfree(sds s);

    sdsfree(p5);
    printf("adress of p5:%p\n",p5);

    printf("testing sdsupdate function:\n");
    //void sdsupdatelen(sds s);
    sds p6 = sdsnewlen("abcdefg",7);
    struct sdshdr* s = (struct sdshdr*)(p6-sizeof(struct sdshdr));
    printf("len:%d\nfree:%d\nbuf:%s\n",s->len,s->free,s->buf);

    s->buf[3] = '\0';
    printf("len:%d\nfree:%d\nbuf:%s\n",s->len,s->free,s->buf);
    sdsupdatelen(p6);
    printf("len:%d\nfree:%d\nbuf:%s\n",s->len,s->free,s->buf);

}

void test4(){

    //测试sdsgrowlen
    struct sdshdr* sdshdr1 = (struct sdshdr*)malloc(sizeof(struct sdshdr)+100);
    sdshdr1->len = 6;
    sdshdr1->free = 94;
    strcpy(sdshdr1->buf,"testv");

    sds s = sdshdr1->buf;
    printf("len:%zu\nfree:%zu\nbuf:%s\n",sdslen(s),sdsavail(s),s);

    s = sdsgrowzero(s,300);
    printf("len:%zu\nfree:%zu\nbuf:%s\n",sdslen(s),sdsavail(s),s);

    sdsfree(s);
    printf("len:%zu\nfree:%zu\nbuf:%s\n",sdslen(s),sdsavail(s),s);
    s = sdsgrowzero(s,300);
    printf("len:%zu\nfree:%zu\nbuf:%s\n",sdslen(s),sdsavail(s),s);

}

void test5(){
    //测试打印函数，并不知道这个函数怎么用
//    sds s = sdsempty();
//    s = sdscatprintf(s,"%d+%d = %d","2","3");
//    printf("%s",s);

    //测试sdstrim函数
    sds s = sdsnew("dexyeeabcxxyeddd");
    printf("len:%zu\nfree:%zu\nbuf:%s\n",sdslen(s),sdsavail(s),s);
    s = sdstrim(s,"dexy");
    printf("len:%zu\nfree:%zu\nbuf:%s\n",sdslen(s),sdsavail(s),s);

    //测试long long

    long long a = 1234567890123456789;
    s = sdsfromlonglong(a);
    printf("len:%zu\nfree:%zu\nbuf:%s\n",sdslen(s),sdsavail(s),s);

    //测试sdsrange函数
    sdsrange(s,3,9);
    printf("len:%zu\nfree:%zu\nbuf:%s\n",sdslen(s),sdsavail(s),s);

}

void test6(){
    //测试字符串转换以及比较函数
    sds s1 = sdsnew("abcdeRFGSKADHAJqwiuyiasAKHKKK");
    sds s2 = sdsnew("sdsdquwoqueHJHKHASkjuwoeiuq0912838");
    sdstolower(s1);
    sdstoupper(s2);
    printf("len:%zu\nfree:%zu\nbuf:%s\n",sdslen(s1),sdsavail(s1),s1);
    printf("len:%zu\nfree:%zu\nbuf:%s\n",sdslen(s2),sdsavail(s2),s2);

    int flag1 = sdscmp(s1,s2);
    int flag2 = sdscmp(s2,s1);
    printf("flag1:%d\nflag2:%d\n",flag1,flag2);
}
void test7(){
    //void sdsclear(sds s);
    long long a = 1234567890123456789;
    sds s  = sdsfromlonglong(a);
    printf("len:%zu\nfree:%zu\nbuf:%s\n",sdslen(s),sdsavail(s),s);
    sdsclear(s);
    printf("len:%zu\nfree:%zu\nbuf:%s\n",sdslen(s),sdsavail(s),s);
    //sds *sdssplitlen(const char *s, int len, const char *sep, int seplen, int *count);

    printf("***********testing sdssplitlen function*************\n");

    char* sep = "bb"; int seplen = 2;int count = 2;
    sds* arr = sdssplitlen("abbaabbaaabbaaaabbaaaaabb",25,sep,seplen,&count);

    for(int i = 0;i<count;i++){
        printf("len:%zu\nfree:%zu\nbuf:%s\n",sdslen(arr[i]),sdsavail(arr[i]),arr[i]);
    }
    //void sdsfreesplitres(sds *tokens, int count);

    printf("***********testing sdsfreesplitres function*************\n");
    sdsfreesplitres(arr,count);
//    for(int i = 0;i<count;i++){
//        printf("len:%zu\nfree:%zu\nbuf:%s\n",sdslen(arr[i]),sdsavail(arr[i]),arr[i]);
//    }

    //sds sdscatrepr(sds s, const char *p, size_t len)

    printf("***********testing sdscatrepr function*************\n");

    sds t = sdsnew("123456");char* ta = "89";
    printf("len:%zu\nfree:%zu\nbuf:%s\n",sdslen(t),sdsavail(t),t);
    sdscatrepr(t,ta,2);
    printf("len:%zu\nfree:%zu\nbuf:%s\n",sdslen(t),sdsavail(t),t);

}

void test8(){
    //sds *sdssplitargs(const char *line, int *argc);

    printf("***********testing sdssplitargs function*************\n");
    int argc;
    sds *arr = sdssplitargs("timeout 10086\r\nport 123321\r\n",&argc);
    for(int i = 0;i<argc;i++){
        printf("len:%zu\nfree:%zu\nbuf:%s\n",sdslen(arr[i]),sdsavail(arr[i]),arr[i]);
    }

    //sds sdsmapchars(sds s, const char *from, const char *to, size_t setlen);

    printf("***********testing sdsmapchars function*************\n");

    sds s = sdsnew("1234as2545abcde");

    s = sdsmapchars(s,"abcdes","ABCDES",6);
    printf("len:%zu\nfree:%zu\nbuf:%s\n",sdslen(s),sdsavail(s),s);

    printf("***********testing sdsmapchars function*************\n");
    //sds sdsjoin(char **argv, int argc, char *sep);

    char *arr1[] = {"1","22","333","4444"}; char* sep = "|";
    sds s1 = sdsjoin(arr1,4,sep);
    printf("len:%zu\nfree:%zu\nbuf:%s\n",sdslen(s1),sdsavail(s1),s1);


}

void test9(){
    //void sdsIncrLen(sds s, int incr);
    printf("***********testing sdsIncrLen function*************\n");
    struct sdshdr* sh = (void*)(malloc(100));
    sh->len=5;
    sh->free = 94;
    char *p = "testv";
    strcpy(sh->buf,p);
    sds s = sh->buf;
    sdsIncrLen(s,10);
    printf("len:%zu\nfree:%zu\nbuf:%s\n",sdslen(s),sdsavail(s),s);


    //sds sdsRemoveFreeSpace(sds s);
    printf("***********testing sdsRemoveFreeSpace function*************\n");
    s = sdsRemoveFreeSpace(s);
    printf("len:%zu\nfree:%zu\nbuf:%s\n",sdslen(s),sdsavail(s),s);

    //size_t sdsAllocSize(sds s);
    printf("***********testing sdsAllocSize function*************\n");
    printf("AllocSize of sh is:%zu\n",sdsAllocSize(s));

}

int main(void)
{
    //test1();
    //test2();
    //test3();
    //test5();
    //test6();
    //test7();
    //test8();
    //test9();
    return 0;
}


#endif
