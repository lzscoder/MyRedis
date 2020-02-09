

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include "zmalloc.h"
#include "util.h"
#include "ziplist.h"
#include "endianconv.h"
#include "assert.h"

/*
 * ziplist 末端标识符，以及 5 字节长长度标识符
 */
#define ZIP_END 255
#define ZIP_BIGLEN 254

/* Different encoding/length possibilities */
/*
 * 字符串编码和整数编码的掩码
 */
#define ZIP_STR_MASK 0xc0
#define ZIP_INT_MASK 0x30

/*
 * 字符串编码类型
 */
#define ZIP_STR_06B (0 << 6)
#define ZIP_STR_14B (1 << 6)
#define ZIP_STR_32B (2 << 6)

/*
 * 整数编码类型
 */
#define ZIP_INT_16B (0xc0 | 0<<4)
#define ZIP_INT_32B (0xc0 | 1<<4)
#define ZIP_INT_64B (0xc0 | 2<<4)
#define ZIP_INT_24B (0xc0 | 3<<4)
#define ZIP_INT_8B 0xfe

/* 4 bit integer immediate encoding
 *
 * 4 位整数编码的掩码和类型
 */
#define ZIP_INT_IMM_MASK 0x0f
#define ZIP_INT_IMM_MIN 0xf1    /* 11110001 */
#define ZIP_INT_IMM_MAX 0xfd    /* 11111101 */
#define ZIP_INT_IMM_VAL(v) (v & ZIP_INT_IMM_MASK)

/*
 * 24 位整数的最大值和最小值
 */
#define INT24_MAX 0x7fffff
#define INT24_MIN (-INT24_MAX - 1)

/* Macro to determine type
 *
 * 查看给定编码 enc 是否字符串编码
 */
#define ZIP_IS_STR(enc) (((enc) & ZIP_STR_MASK) < ZIP_STR_MASK)

/* Utility macros */
/*
 * ziplist 属性宏
 */
// 定位到 ziplist 的 bytes 属性，该属性记录了整个 ziplist 所占用的内存字节数
// 用于取出 bytes 属性的现有值，或者为 bytes 属性赋予新值
#define ZIPLIST_BYTES(zl)       (*((uint32_t*)(zl)))
// 定位到 ziplist 的 offset 属性，该属性记录了到达表尾节点的偏移量
// 用于取出 offset 属性的现有值，或者为 offset 属性赋予新值
#define ZIPLIST_TAIL_OFFSET(zl) (*((uint32_t*)((zl)+sizeof(uint32_t))))
// 定位到 ziplist 的 length 属性，该属性记录了 ziplist 包含的节点数量
// 用于取出 length 属性的现有值，或者为 length 属性赋予新值
#define ZIPLIST_LENGTH(zl)      (*((uint16_t*)((zl)+sizeof(uint32_t)*2)))
// 返回 ziplist 表头的大小
#define ZIPLIST_HEADER_SIZE     (sizeof(uint32_t)*2+sizeof(uint16_t))
// 返回指向 ziplist 第一个节点（的起始位置）的指针
#define ZIPLIST_ENTRY_HEAD(zl)  ((zl)+ZIPLIST_HEADER_SIZE)
// 返回指向 ziplist 最后一个节点（的起始位置）的指针
#define ZIPLIST_ENTRY_TAIL(zl)  ((zl)+intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl)))
// 返回指向 ziplist 末端 ZIP_END （的起始位置）的指针
#define ZIPLIST_ENTRY_END(zl)   ((zl)+intrev32ifbe(ZIPLIST_BYTES(zl))-1)

/*
空白 ziplist 示例图

area        |<---- ziplist header ---->|<-- end -->|

size          4 bytes   4 bytes 2 bytes  1 byte
            +---------+--------+-------+-----------+
component   | zlbytes | zltail | zllen | zlend     |
            |         |        |       |           |
value       |  1011   |  1010  |   0   | 1111 1111 |
            +---------+--------+-------+-----------+
                                       ^
                                       |
                               ZIPLIST_ENTRY_HEAD
                                       &
address                        ZIPLIST_ENTRY_TAIL
                                       &
                               ZIPLIST_ENTRY_END

非空 ziplist 示例图

area        |<---- ziplist header ---->|<----------- entries ------------->|<-end->|

size          4 bytes  4 bytes  2 bytes    ?        ?        ?        ?     1 byte
            +---------+--------+-------+--------+--------+--------+--------+-------+
component   | zlbytes | zltail | zllen | entry1 | entry2 |  ...   | entryN | zlend |
            +---------+--------+-------+--------+--------+--------+--------+-------+
                                       ^                          ^        ^
address                                |                          |        |
                                ZIPLIST_ENTRY_HEAD                |   ZIPLIST_ENTRY_END
                                                                  |
                                                        ZIPLIST_ENTRY_TAIL
*/

/* We know a positive increment can only be 1 because entries can only be
 * pushed one at a time. */
/*
 * 增加 ziplist 的节点数
 *
 * T = O(1)
 */
#define ZIPLIST_INCR_LENGTH(zl,incr) { \
    if (ZIPLIST_LENGTH(zl) < UINT16_MAX) \
        ZIPLIST_LENGTH(zl) = intrev16ifbe(intrev16ifbe(ZIPLIST_LENGTH(zl))+incr); \
}

/*
 * 保存 ziplist 节点信息的结构
 */
typedef struct zlentry {

    // prevrawlen ：前置节点的长度
    // prevrawlensize ：编码 prevrawlen 所需的字节大小
    unsigned int prevrawlensize, prevrawlen;

    // len ：当前节点值的长度
    // lensize ：编码 len 所需的字节大小
    unsigned int lensize, len;

    // 当前节点 header 的大小
    // 等于 prevrawlensize + lensize
    unsigned int headersize;

    // 当前节点值所使用的编码类型
    unsigned char encoding;

    // 指向当前节点的指针
    unsigned char *p;

} zlentry;

/* Extract the encoding from the byte pointed by 'ptr' and set it into
 * 'encoding'.
 *
 * 从 ptr 中取出节点值的编码类型，并将它保存到 encoding 变量中。
 *
 * T = O(1)
 */
#define ZIP_ENTRY_ENCODING(ptr, encoding) do {  \
    (encoding) = (ptr[0]); \
    if ((encoding) < ZIP_STR_MASK) (encoding) &= ZIP_STR_MASK; \
} while(0)

/* Return bytes needed to store integer encoded by 'encoding'
 *
 * 返回保存 encoding 编码的值所需的字节数量
 *
 * T = O(1)
 */
static unsigned int zipIntSize(unsigned char encoding) {

    switch(encoding) {
    case ZIP_INT_8B:  return 1;
    case ZIP_INT_16B: return 2;
    case ZIP_INT_24B: return 3;
    case ZIP_INT_32B: return 4;
    case ZIP_INT_64B: return 8;
    default: return 0; /* 4 bit immediate */
    }

    assert(NULL);
    return 0;
}

/* Encode the length 'l' writing it in 'p'. If p is NULL it just returns
 * the amount of bytes required to encode such a length.
 *
 * 编码节点长度值 l ，并将它写入到 p 中，然后返回编码 l 所需的字节数量。
 *
 * 如果 p 为 NULL ，那么仅返回编码 l 所需的字节数量，不进行写入。
 *
 * T = O(1)
 */
static unsigned int zipEncodeLength(unsigned char *p, unsigned char encoding, unsigned int rawlen) {
    unsigned char len = 1, buf[5];

    // 编码字符串
    if (ZIP_IS_STR(encoding)) {
        /* Although encoding is given it may not be set for strings,
         * so we determine it here using the raw length. */
        if (rawlen <= 0x3f) {
            if (!p) return len;
            buf[0] = ZIP_STR_06B | rawlen;
        } else if (rawlen <= 0x3fff) {
            len += 1;
            if (!p) return len;
            buf[0] = ZIP_STR_14B | ((rawlen >> 8) & 0x3f);
            buf[1] = rawlen & 0xff;
        } else {
            len += 4;
            if (!p) return len;
            buf[0] = ZIP_STR_32B;
            buf[1] = (rawlen >> 24) & 0xff;
            buf[2] = (rawlen >> 16) & 0xff;
            buf[3] = (rawlen >> 8) & 0xff;
            buf[4] = rawlen & 0xff;
        }

    // 编码整数
    } else {
        /* Implies integer encoding, so length is always 1. */
        if (!p) return len;
        buf[0] = encoding;
    }

    /* Store this length at p */
    // 将编码后的长度写入 p
    memcpy(p,buf,len);

    // 返回编码所需的字节数
    return len;
}

/* Decode the length encoded in 'ptr'. The 'encoding' variable will hold the
 * entries encoding, the 'lensize' variable will hold the number of bytes
 * required to encode the entries length, and the 'len' variable will hold the
 * entries length.
 *
 * 解码 ptr 指针，取出列表节点的相关信息，并将它们保存在以下变量中：
 *
 * - encoding 保存节点值的编码类型。
 *
 * - lensize 保存编码节点长度所需的字节数。
 *
 * - len 保存节点的长度。
 *
 * T = O(1)
 */
#define ZIP_DECODE_LENGTH(ptr, encoding, lensize, len) do {                    \
                                                                               \
    /* 取出值的编码类型 */                                                     \
    ZIP_ENTRY_ENCODING((ptr), (encoding));                                     \
                                                                               \
    /* 字符串编码 */                                                           \
    if ((encoding) < ZIP_STR_MASK) {                                           \
        if ((encoding) == ZIP_STR_06B) {                                       \
            (lensize) = 1;                                                     \
            (len) = (ptr)[0] & 0x3f;                                           \
        } else if ((encoding) == ZIP_STR_14B) {                                \
            (lensize) = 2;                                                     \
            (len) = (((ptr)[0] & 0x3f) << 8) | (ptr)[1];                       \
        } else if (encoding == ZIP_STR_32B) {                                  \
            (lensize) = 5;                                                     \
            (len) = ((ptr)[1] << 24) |                                         \
                    ((ptr)[2] << 16) |                                         \
                    ((ptr)[3] <<  8) |                                         \
                    ((ptr)[4]);                                                \
        } else {                                                               \
            assert(NULL);                                                      \
        }                                                                      \
                                                                               \
    /* 整数编码 */                                                             \
    } else {                                                                   \
        (lensize) = 1;                                                         \
        (len) = zipIntSize(encoding);                                          \
    }                                                                          \
} while(0);

/* Encode the length of the previous entry and write it to "p". Return the
 * number of bytes needed to encode this length if "p" is NULL.
 *
 * 对前置节点的长度 len 进行编码，并将它写入到 p 中，
 * 然后返回编码 len 所需的字节数量。
 *
 * 如果 p 为 NULL ，那么不进行写入，仅返回编码 len 所需的字节数量。
 *
 * T = O(1)
 */
static unsigned int zipPrevEncodeLength(unsigned char *p, unsigned int len) {

    // 仅返回编码 len 所需的字节数量
    if (p == NULL) {
        return (len < ZIP_BIGLEN) ? 1 : sizeof(len)+1;

    // 写入并返回编码 len 所需的字节数量
    } else {

        // 1 字节
        if (len < ZIP_BIGLEN) {
            p[0] = len;
            return 1;

        // 5 字节
        } else {
            // 添加 5 字节长度标识
            p[0] = ZIP_BIGLEN;
            // 写入编码
            memcpy(p+1,&len,sizeof(len));
            // 如果有必要的话，进行大小端转换
            memrev32ifbe(p+1);
            // 返回编码长度
            return 1+sizeof(len);
        }
    }
}

/* Encode the length of the previous entry and write it to "p". This only
 * uses the larger encoding (required in __ziplistCascadeUpdate).
 *
 * 将原本只需要 1 个字节来保存的前置节点长度 len 编码至一个 5 字节长的 header 中。
 *
 * T = O(1)
 */
static void zipPrevEncodeLengthForceLarge(unsigned char *p, unsigned int len) {

    if (p == NULL) return;

    // 设置 5 字节长度标识
    p[0] = ZIP_BIGLEN;

    // 写入 len
    memcpy(p+1,&len,sizeof(len));
    memrev32ifbe(p+1);
}

/* Decode the number of bytes required to store the length of the previous
 * element, from the perspective of the entry pointed to by 'ptr'.
 *
 * 解码 ptr 指针，
 * 取出编码前置节点长度所需的字节数，并将它保存到 prevlensize 变量中。
 *
 * T = O(1)
 */
#define ZIP_DECODE_PREVLENSIZE(ptr, prevlensize) do {                          \
    if ((ptr)[0] < ZIP_BIGLEN) {                                               \
        (prevlensize) = 1;                                                     \
    } else {                                                                   \
        (prevlensize) = 5;                                                     \
    }                                                                          \
} while(0);

/* Decode the length of the previous element, from the perspective of the entry
 * pointed to by 'ptr'.
 *
 * 解码 ptr 指针，
 * 取出编码前置节点长度所需的字节数，
 * 并将这个字节数保存到 prevlensize 中。
 *
 * 然后根据 prevlensize ，从 ptr 中取出前置节点的长度值，
 * 并将这个长度值保存到 prevlen 变量中。
 *
 * T = O(1)
 */
#define ZIP_DECODE_PREVLEN(ptr, prevlensize, prevlen) do {                     \
                                                                               \
    /* 先计算被编码长度值的字节数 */                                           \
    ZIP_DECODE_PREVLENSIZE(ptr, prevlensize);                                  \
                                                                               \
    /* 再根据编码字节数来取出长度值 */                                         \
    if ((prevlensize) == 1) {                                                  \
        (prevlen) = (ptr)[0];                                                  \
    } else if ((prevlensize) == 5) {                                           \
        assert(sizeof((prevlensize)) == 4);                                    \
        memcpy(&(prevlen), ((char*)(ptr)) + 1, 4);                             \
        memrev32ifbe(&prevlen);                                                \
    }                                                                          \
} while(0);

/* Return the difference in number of bytes needed to store the length of the
 * previous element 'len', in the entry pointed to by 'p'.
 *
 * 计算编码新的前置节点长度 len 所需的字节数，
 * 减去编码 p 原来的前置节点长度所需的字节数之差。
 *
 * T = O(1)
 */
static int zipPrevLenByteDiff(unsigned char *p, unsigned int len) {
    unsigned int prevlensize;

    // 取出编码原来的前置节点长度所需的字节数
    // T = O(1)
    ZIP_DECODE_PREVLENSIZE(p, prevlensize);

    // 计算编码 len 所需的字节数，然后进行减法运算
    // T = O(1)
    return zipPrevEncodeLength(NULL, len) - prevlensize;
}

/* Return the total number of bytes used by the entry pointed to by 'p'.
 *
 * 返回指针 p 所指向的节点占用的字节数总和。
 *
 * T = O(1)
 */
static unsigned int zipRawEntryLength(unsigned char *p) {
    unsigned int prevlensize, encoding, lensize, len;

    // 取出编码前置节点的长度所需的字节数
    // T = O(1)
    ZIP_DECODE_PREVLENSIZE(p, prevlensize);

    // 取出当前节点值的编码类型，编码节点值长度所需的字节数，以及节点值的长度
    // T = O(1)
    ZIP_DECODE_LENGTH(p + prevlensize, encoding, lensize, len);

    // 计算节点占用的字节数总和
    return prevlensize + lensize + len;
}

/* Check if string pointed to by 'entry' can be encoded as an integer.
 * Stores the integer value in 'v' and its encoding in 'encoding'.
 *
 * 检查 entry 中指向的字符串能否被编码为整数。
 *
 * 如果可以的话，
 * 将编码后的整数保存在指针 v 的值中，并将编码的方式保存在指针 encoding 的值中。
 *
 * 注意，这里的 entry 和前面代表节点的 entry 不是一个意思。
 *
 * T = O(N)
 */
static int zipTryEncoding(unsigned char *entry, unsigned int entrylen, long long *v, unsigned char *encoding) {
    long long value;

    // 忽略太长或太短的字符串
    if (entrylen >= 32 || entrylen == 0) return 0;

    // 尝试转换
    // T = O(N)
    if (string2ll((char*)entry,entrylen,&value)) {

        /* Great, the string can be encoded. Check what's the smallest
         * of our encoding types that can hold this value. */
        // 转换成功，以从小到大的顺序检查适合值 value 的编码方式
        if (value >= 0 && value <= 12) {
            *encoding = ZIP_INT_IMM_MIN+value;
        } else if (value >= INT8_MIN && value <= INT8_MAX) {
            *encoding = ZIP_INT_8B;
        } else if (value >= INT16_MIN && value <= INT16_MAX) {
            *encoding = ZIP_INT_16B;
        } else if (value >= INT24_MIN && value <= INT24_MAX) {
            *encoding = ZIP_INT_24B;
        } else if (value >= INT32_MIN && value <= INT32_MAX) {
            *encoding = ZIP_INT_32B;
        } else {
            *encoding = ZIP_INT_64B;
        }

        // 记录值到指针
        *v = value;

        // 返回转换成功标识
        return 1;
    }

    // 转换失败
    return 0;
}

/* Store integer 'value' at 'p', encoded as 'encoding'
 *
 * 以 encoding 指定的编码方式，将整数值 value 写入到 p 。
 *
 * T = O(1)
 */
static void zipSaveInteger(unsigned char *p, int64_t value, unsigned char encoding) {
    int16_t i16;
    int32_t i32;
    int64_t i64;

    if (encoding == ZIP_INT_8B) {
        ((int8_t*)p)[0] = (int8_t)value;
    } else if (encoding == ZIP_INT_16B) {
        i16 = value;
        memcpy(p,&i16,sizeof(i16));
        memrev16ifbe(p);
    } else if (encoding == ZIP_INT_24B) {
        i32 = value<<8;
        memrev32ifbe(&i32);
        memcpy(p,((uint8_t*)&i32)+1,sizeof(i32)-sizeof(uint8_t));
    } else if (encoding == ZIP_INT_32B) {
        i32 = value;
        memcpy(p,&i32,sizeof(i32));
        memrev32ifbe(p);
    } else if (encoding == ZIP_INT_64B) {
        i64 = value;
        memcpy(p,&i64,sizeof(i64));
        memrev64ifbe(p);
    } else if (encoding >= ZIP_INT_IMM_MIN && encoding <= ZIP_INT_IMM_MAX) {
        /* Nothing to do, the value is stored in the encoding itself. */
    } else {
        assert(NULL);
    }
}

/* Read integer encoded as 'encoding' from 'p'
 *
 * 以 encoding 指定的编码方式，读取并返回指针 p 中的整数值。
 *
 * T = O(1)
 */
static int64_t zipLoadInteger(unsigned char *p, unsigned char encoding) {
    int16_t i16;
    int32_t i32;
    int64_t i64, ret = 0;

    if (encoding == ZIP_INT_8B) {
        ret = ((int8_t*)p)[0];
    } else if (encoding == ZIP_INT_16B) {
        memcpy(&i16,p,sizeof(i16));
        memrev16ifbe(&i16);
        ret = i16;
    } else if (encoding == ZIP_INT_32B) {
        memcpy(&i32,p,sizeof(i32));
        memrev32ifbe(&i32);
        ret = i32;
    } else if (encoding == ZIP_INT_24B) {
        i32 = 0;
        memcpy(((uint8_t*)&i32)+1,p,sizeof(i32)-sizeof(uint8_t));
        memrev32ifbe(&i32);
        ret = i32>>8;
    } else if (encoding == ZIP_INT_64B) {
        memcpy(&i64,p,sizeof(i64));
        memrev64ifbe(&i64);
        ret = i64;
    } else if (encoding >= ZIP_INT_IMM_MIN && encoding <= ZIP_INT_IMM_MAX) {
        ret = (encoding & ZIP_INT_IMM_MASK)-1;
    } else {
        assert(NULL);
    }

    return ret;
}

/* Return a struct with all information about an entry.
 *
 * 将 p 所指向的列表节点的信息全部保存到 zlentry 中，并返回该 zlentry 。
 *
 * T = O(1)
 */
static zlentry zipEntry(unsigned char *p) {
    zlentry e;

    // e.prevrawlensize 保存着编码前一个节点的长度所需的字节数
    // e.prevrawlen 保存着前一个节点的长度
    // T = O(1)
    ZIP_DECODE_PREVLEN(p, e.prevrawlensize, e.prevrawlen);

    // p + e.prevrawlensize 将指针移动到列表节点本身
    // e.encoding 保存着节点值的编码类型
    // e.lensize 保存着编码节点值长度所需的字节数
    // e.len 保存着节点值的长度
    // T = O(1)
    ZIP_DECODE_LENGTH(p + e.prevrawlensize, e.encoding, e.lensize, e.len);

    // 计算头结点的字节数
    e.headersize = e.prevrawlensize + e.lensize;

    // 记录指针
    e.p = p;

    return e;
}

/* Create a new empty ziplist.
 *
 * 创建并返回一个新的 ziplist
 *
 * T = O(1)
 */
unsigned char *ziplistNew(void) {

    // ZIPLIST_HEADER_SIZE 是 ziplist 表头的大小
    // 1 字节是表末端 ZIP_END 的大小
    unsigned int bytes = ZIPLIST_HEADER_SIZE+1;

    // 为表头和表末端分配空间
    unsigned char *zl = zmalloc(bytes);

    // 初始化表属性
    ZIPLIST_BYTES(zl) = intrev32ifbe(bytes);
    ZIPLIST_TAIL_OFFSET(zl) = intrev32ifbe(ZIPLIST_HEADER_SIZE);
    ZIPLIST_LENGTH(zl) = 0;

    // 设置表末端
    zl[bytes-1] = ZIP_END;

    return zl;
}

/* Resize the ziplist.
 *
 * 调整 ziplist 的大小为 len 字节。
 *
 * 当 ziplist 原有的大小小于 len 时，扩展 ziplist 不会改变 ziplist 原有的元素。
 *
 * T = O(N)
 */
static unsigned char *ziplistResize(unsigned char *zl, unsigned int len) {

    // 用 zrealloc ，扩展时不改变现有元素
    zl = zrealloc(zl,len);

    // 更新 bytes 属性
    ZIPLIST_BYTES(zl) = intrev32ifbe(len);

    // 重新设置表末端
    zl[len-1] = ZIP_END;

    return zl;
}

/* When an entry is inserted, we need to set the prevlen field of the next
 * entry to equal the length of the inserted entry. It can occur that this
 * length cannot be encoded in 1 byte and the next entry needs to be grow
 * a bit larger to hold the 5-byte encoded prevlen. This can be done for free,
 * because this only happens when an entry is already being inserted (which
 * causes a realloc and memmove). However, encoding the prevlen may require
 * that this entry is grown as well. This effect may cascade throughout
 * the ziplist when there are consecutive entries with a size close to
 * ZIP_BIGLEN, so we need to check that the prevlen can be encoded in every
 * consecutive entry.
 *
 * 当将一个新节点添加到某个节点之前的时候，
 * 如果原节点的 header 空间不足以保存新节点的长度，
 * 那么就需要对原节点的 header 空间进行扩展（从 1 字节扩展到 5 字节）。
 *
 * 但是，当对原节点进行扩展之后，原节点的下一个节点的 prevlen 可能出现空间不足，
 * 这种情况在多个连续节点的长度都接近 ZIP_BIGLEN 时可能发生。
 *
 * 这个函数就用于检查并修复后续节点的空间问题。
 *
 * Note that this effect can also happen in reverse, where the bytes required
 * to encode the prevlen field can shrink. This effect is deliberately ignored,
 * because it can cause a "flapping" effect where a chain prevlen fields is
 * first grown and then shrunk again after consecutive inserts. Rather, the
 * field is allowed to stay larger than necessary, because a large prevlen
 * field implies the ziplist is holding large entries anyway.
 *
 * 反过来说，
 * 因为节点的长度变小而引起的连续缩小也是可能出现的，
 * 不过，为了避免扩展-缩小-扩展-缩小这样的情况反复出现（flapping，抖动），
 * 我们不处理这种情况，而是任由 prevlen 比所需的长度更长。

 * The pointer "p" points to the first entry that does NOT need to be
 * updated, i.e. consecutive fields MAY need an update.
 *
 * 注意，程序的检查是针对 p 的后续节点，而不是 p 所指向的节点。
 * 因为节点 p 在传入之前已经完成了所需的空间扩展工作。
 *
 * T = O(N^2)
 */
static unsigned char *__ziplistCascadeUpdate(unsigned char *zl, unsigned char *p) {
    size_t curlen = intrev32ifbe(ZIPLIST_BYTES(zl)), rawlen, rawlensize;
    size_t offset, noffset, extra;
    unsigned char *np;
    zlentry cur, next;

    // T = O(N^2)
    while (p[0] != ZIP_END) {

        // 将 p 所指向的节点的信息保存到 cur 结构中
        cur = zipEntry(p);
        // 当前节点的长度
        rawlen = cur.headersize + cur.len;
        // 计算编码当前节点的长度所需的字节数
        // T = O(1)
        rawlensize = zipPrevEncodeLength(NULL,rawlen);

        /* Abort if there is no next entry. */
        // 如果已经没有后续空间需要更新了，跳出
        if (p[rawlen] == ZIP_END) break;

        // 取出后续节点的信息，保存到 next 结构中
        // T = O(1)
        next = zipEntry(p+rawlen);

        /* Abort when "prevlen" has not changed. */
        // 后续节点编码当前节点的空间已经足够，无须再进行任何处理，跳出
        // 可以证明，只要遇到一个空间足够的节点，
        // 那么这个节点之后的所有节点的空间都是足够的
        if (next.prevrawlen == rawlen) break;

        if (next.prevrawlensize < rawlensize) {

            /* The "prevlen" field of "next" needs more bytes to hold
             * the raw length of "cur". */
            // 执行到这里，表示 next 空间的大小不足以编码 cur 的长度
            // 所以程序需要对 next 节点的（header 部分）空间进行扩展

            // 记录 p 的偏移量
            offset = p-zl;
            // 计算需要增加的节点数量
            extra = rawlensize-next.prevrawlensize;
            // 扩展 zl 的大小
            // T = O(N)
            zl = ziplistResize(zl,curlen+extra);
            // 还原指针 p
            p = zl+offset;

            /* Current pointer and offset for next element. */
            // 记录下一节点的偏移量
            np = p+rawlen;
            noffset = np-zl;

            /* Update tail offset when next element is not the tail element. */
            // 当 next 节点不是表尾节点时，更新列表到表尾节点的偏移量
            //
            // 不用更新的情况（next 为表尾节点）：
            //
            // |     | next |      ==>    |     | new next          |
            //       ^                          ^
            //       |                          |
            //     tail                        tail
            //
            // 需要更新的情况（next 不是表尾节点）：
            //
            // | next |     |   ==>     | new next          |     |
            //        ^                        ^
            //        |                        |
            //    old tail                 old tail
            //
            // 更新之后：
            //
            // | new next          |     |
            //                     ^
            //                     |
            //                  new tail
            // T = O(1)
            if ((zl+intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl))) != np) {
                ZIPLIST_TAIL_OFFSET(zl) =
                    intrev32ifbe(intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl))+extra);
            }

            /* Move the tail to the back. */
            // 向后移动 cur 节点之后的数据，为 cur 的新 header 腾出空间
            //
            // 示例：
            //
            // | header | value |  ==>  | header |    | value |  ==>  | header      | value |
            //                                   |<-->|
            //                            为新 header 腾出的空间
            // T = O(N)
            memmove(np+rawlensize,
                np+next.prevrawlensize,
                curlen-noffset-next.prevrawlensize-1);
            // 将新的前一节点长度值编码进新的 next 节点的 header
            // T = O(1)
            zipPrevEncodeLength(np,rawlen);

            /* Advance the cursor */
            // 移动指针，继续处理下个节点
            p += rawlen;
            curlen += extra;
        } else {
            if (next.prevrawlensize > rawlensize) {
                /* This would result in shrinking, which we want to avoid.
                 * So, set "rawlen" in the available bytes. */
                // 执行到这里，说明 next 节点编码前置节点的 header 空间有 5 字节
                // 而编码 rawlen 只需要 1 字节
                // 但是程序不会对 next 进行缩小，
                // 所以这里只将 rawlen 写入 5 字节的 header 中就算了。
                // T = O(1)
                zipPrevEncodeLengthForceLarge(p+rawlen,rawlen);
            } else {
                // 运行到这里，
                // 说明 cur 节点的长度正好可以编码到 next 节点的 header 中
                // T = O(1)
                zipPrevEncodeLength(p+rawlen,rawlen);
            }

            /* Stop here, as the raw length of "next" has not changed. */
            break;
        }
    }

    return zl;
}

/* Delete "num" entries, starting at "p". Returns pointer to the ziplist.
 *
 * 从位置 p 开始，连续删除 num 个节点。
 *
 * 函数的返回值为处理删除操作之后的 ziplist 。
 *
 * T = O(N^2)
 */
static unsigned char *__ziplistDelete(unsigned char *zl, unsigned char *p, unsigned int num) {
    unsigned int i, totlen, deleted = 0;
    size_t offset;
    int nextdiff = 0;
    zlentry first, tail;

    // 计算被删除节点总共占用的内存字节数
    // 以及被删除节点的总个数
    // T = O(N)
    first = zipEntry(p);
    for (i = 0; p[0] != ZIP_END && i < num; i++) {
        p += zipRawEntryLength(p);
        deleted++;
    }

    // totlen 是所有被删除节点总共占用的内存字节数
    totlen = p-first.p;
    if (totlen > 0) {
        if (p[0] != ZIP_END) {

            // 执行这里，表示被删除节点之后仍然有节点存在

            /* Storing `prevrawlen` in this entry may increase or decrease the
             * number of bytes required compare to the current `prevrawlen`.
             * There always is room to store this, because it was previously
             * stored by an entry that is now being deleted. */
            // 因为位于被删除范围之后的第一个节点的 header 部分的大小
            // 可能容纳不了新的前置节点，所以需要计算新旧前置节点之间的字节数差
            // T = O(1)
            nextdiff = zipPrevLenByteDiff(p,first.prevrawlen);
            // 如果有需要的话，将指针 p 后退 nextdiff 字节，为新 header 空出空间
            p -= nextdiff;
            // 将 first 的前置节点的长度编码至 p 中
            // T = O(1)
            zipPrevEncodeLength(p,first.prevrawlen);

            /* Update offset for tail */
            // 更新到达表尾的偏移量
            // T = O(1)
            ZIPLIST_TAIL_OFFSET(zl) =
                intrev32ifbe(intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl))-totlen);

            /* When the tail contains more than one entry, we need to take
             * "nextdiff" in account as well. Otherwise, a change in the
             * size of prevlen doesn't have an effect on the *tail* offset. */
            // 如果被删除节点之后，有多于一个节点
            // 那么程序需要将 nextdiff 记录的字节数也计算到表尾偏移量中
            // 这样才能让表尾偏移量正确对齐表尾节点
            // T = O(1)
            tail = zipEntry(p);
            if (p[tail.headersize+tail.len] != ZIP_END) {
                ZIPLIST_TAIL_OFFSET(zl) =
                   intrev32ifbe(intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl))+nextdiff);
            }

            /* Move tail to the front of the ziplist */
            // 从表尾向表头移动数据，覆盖被删除节点的数据
            // T = O(N)
            memmove(first.p,p,
                intrev32ifbe(ZIPLIST_BYTES(zl))-(p-zl)-1);
        } else {

            // 执行这里，表示被删除节点之后已经没有其他节点了

            /* The entire tail was deleted. No need to move memory. */
            // T = O(1)
            ZIPLIST_TAIL_OFFSET(zl) =
                intrev32ifbe((first.p-zl)-first.prevrawlen);
        }

        /* Resize and update length */
        // 缩小并更新 ziplist 的长度
        offset = first.p-zl;
        zl = ziplistResize(zl, intrev32ifbe(ZIPLIST_BYTES(zl))-totlen+nextdiff);
        ZIPLIST_INCR_LENGTH(zl,-deleted);
        p = zl+offset;

        /* When nextdiff != 0, the raw length of the next entry has changed, so
         * we need to cascade the update throughout the ziplist */
        // 如果 p 所指向的节点的大小已经变更，那么进行级联更新
        // 检查 p 之后的所有节点是否符合 ziplist 的编码要求
        // T = O(N^2)
        if (nextdiff != 0)
            zl = __ziplistCascadeUpdate(zl,p);
    }

    return zl;
}

/* Insert item at "p". */
/*
 * 根据指针 p 所指定的位置，将长度为 slen 的字符串 s 插入到 zl 中。
 *
 * 函数的返回值为完成插入操作之后的 ziplist
 *
 * T = O(N^2)
 */
static unsigned char *__ziplistInsert(unsigned char *zl, unsigned char *p, unsigned char *s, unsigned int slen) {
    // 记录当前 ziplist 的长度
    size_t curlen = intrev32ifbe(ZIPLIST_BYTES(zl)), reqlen, prevlen = 0;
    size_t offset;
    int nextdiff = 0;
    unsigned char encoding = 0;
    long long value = 123456789; /* initialized to avoid warning. Using a value
                                    that is easy to see if for some reason
                                    we use it uninitialized. */
    zlentry entry, tail;

    /* Find out prevlen for the entry that is inserted. */
    if (p[0] != ZIP_END) {
        // 如果 p[0] 不指向列表末端，说明列表非空，并且 p 正指向列表的其中一个节点
        // 那么取出 p 所指向节点的信息，并将它保存到 entry 结构中
        // 然后用 prevlen 变量记录前置节点的长度
        // （当插入新节点之后 p 所指向的节点就成了新节点的前置节点）
        // T = O(1)
        entry = zipEntry(p);
        prevlen = entry.prevrawlen;
    } else {
        // 如果 p 指向表尾末端，那么程序需要检查列表是否为：
        // 1)如果 ptail 也指向 ZIP_END ，那么列表为空；
        // 2)如果列表不为空，那么 ptail 将指向列表的最后一个节点。
        unsigned char *ptail = ZIPLIST_ENTRY_TAIL(zl);
        if (ptail[0] != ZIP_END) {
            // 表尾节点为新节点的前置节点

            // 取出表尾节点的长度
            // T = O(1)
            prevlen = zipRawEntryLength(ptail);
        }
    }

    /* See if the entry can be encoded */
    // 尝试看能否将输入字符串转换为整数，如果成功的话：
    // 1)value 将保存转换后的整数值
    // 2)encoding 则保存适用于 value 的编码方式
    // 无论使用什么编码， reqlen 都保存节点值的长度
    // T = O(N)
    if (zipTryEncoding(s,slen,&value,&encoding)) {
        /* 'encoding' is set to the appropriate integer encoding */
        reqlen = zipIntSize(encoding);
    } else {
        /* 'encoding' is untouched, however zipEncodeLength will use the
         * string length to figure out how to encode it. */
        reqlen = slen;
    }
    /* We need space for both the length of the previous entry and
     * the length of the payload. */
    // 计算编码前置节点的长度所需的大小
    // T = O(1)
    reqlen += zipPrevEncodeLength(NULL,prevlen);
    // 计算编码当前节点值所需的大小
    // T = O(1)
    reqlen += zipEncodeLength(NULL,encoding,slen);

    /* When the insert position is not equal to the tail, we need to
     * make sure that the next entry can hold this entry's length in
     * its prevlen field. */
    // 只要新节点不是被添加到列表末端，
    // 那么程序就需要检查看 p 所指向的节点（的 header）能否编码新节点的长度。
    // nextdiff 保存了新旧编码之间的字节大小差，如果这个值大于 0
    // 那么说明需要对 p 所指向的节点（的 header ）进行扩展
    // T = O(1)
    nextdiff = (p[0] != ZIP_END) ? zipPrevLenByteDiff(p,reqlen) : 0;

    /* Store offset because a realloc may change the address of zl. */
    // 因为重分配空间可能会改变 zl 的地址
    // 所以在分配之前，需要记录 zl 到 p 的偏移量，然后在分配之后依靠偏移量还原 p
    offset = p-zl;
    // curlen 是 ziplist 原来的长度
    // reqlen 是整个新节点的长度
    // nextdiff 是新节点的后继节点扩展 header 的长度（要么 0 字节，要么 4 个字节）
    // T = O(N)
    zl = ziplistResize(zl,curlen+reqlen+nextdiff);
    p = zl+offset;

    /* Apply memory move when necessary and update tail offset. */
    if (p[0] != ZIP_END) {
        // 新元素之后还有节点，因为新元素的加入，需要对这些原有节点进行调整

        /* Subtract one because of the ZIP_END bytes */
        // 移动现有元素，为新元素的插入空间腾出位置
        // T = O(N)
        memmove(p+reqlen,p-nextdiff,curlen-offset-1+nextdiff);

        /* Encode this entry's raw length in the next entry. */
        // 将新节点的长度编码至后置节点
        // p+reqlen 定位到后置节点
        // reqlen 是新节点的长度
        // T = O(1)
        zipPrevEncodeLength(p+reqlen,reqlen);

        /* Update offset for tail */
        // 更新到达表尾的偏移量，将新节点的长度也算上
        ZIPLIST_TAIL_OFFSET(zl) =
            intrev32ifbe(intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl))+reqlen);

        /* When the tail contains more than one entry, we need to take
         * "nextdiff" in account as well. Otherwise, a change in the
         * size of prevlen doesn't have an effect on the *tail* offset. */
        // 如果新节点的后面有多于一个节点
        // 那么程序需要将 nextdiff 记录的字节数也计算到表尾偏移量中
        // 这样才能让表尾偏移量正确对齐表尾节点
        // T = O(1)
        tail = zipEntry(p+reqlen);
        if (p[reqlen+tail.headersize+tail.len] != ZIP_END) {
            ZIPLIST_TAIL_OFFSET(zl) =
                intrev32ifbe(intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl))+nextdiff);
        }
    } else {
        /* This element will be the new tail. */
        // 新元素是新的表尾节点
        ZIPLIST_TAIL_OFFSET(zl) = intrev32ifbe(p-zl);
    }

    /* When nextdiff != 0, the raw length of the next entry has changed, so
     * we need to cascade the update throughout the ziplist */
    // 当 nextdiff != 0 时，新节点的后继节点的（header 部分）长度已经被改变，
    // 所以需要级联地更新后续的节点
    if (nextdiff != 0) {
        offset = p-zl;
        // T  = O(N^2)
        zl = __ziplistCascadeUpdate(zl,p+reqlen);
        p = zl+offset;
    }

    /* Write the entry */
    // 一切搞定，将前置节点的长度写入新节点的 header
    p += zipPrevEncodeLength(p,prevlen);
    // 将节点值的长度写入新节点的 header
    p += zipEncodeLength(p,encoding,slen);
    // 写入节点值
    if (ZIP_IS_STR(encoding)) {
        // T = O(N)
        memcpy(p,s,slen);
    } else {
        // T = O(1)
        zipSaveInteger(p,value,encoding);
    }

    // 更新列表的节点数量计数器
    // T = O(1)
    ZIPLIST_INCR_LENGTH(zl,1);

    return zl;
}

/*
 * 将长度为 slen 的字符串 s 推入到 zl 中。
 *
 * where 参数的值决定了推入的方向：
 * - 值为 ZIPLIST_HEAD 时，将新值推入到表头。
 * - 否则，将新值推入到表末端。
 *
 * 函数的返回值为添加新值后的 ziplist 。
 *
 * T = O(N^2)
 */
unsigned char *ziplistPush(unsigned char *zl, unsigned char *s, unsigned int slen, int where) {

    // 根据 where 参数的值，决定将值推入到表头还是表尾
    unsigned char *p;
    p = (where == ZIPLIST_HEAD) ? ZIPLIST_ENTRY_HEAD(zl) : ZIPLIST_ENTRY_END(zl);

    // 返回添加新值后的 ziplist
    // T = O(N^2)
    return __ziplistInsert(zl,p,s,slen);
}

/* Returns an offset to use for iterating with ziplistNext. When the given
 * index is negative, the list is traversed back to front. When the list
 * doesn't contain an element at the provided index, NULL is returned. */
/*
 * 根据给定索引，遍历列表，并返回索引指定节点的指针。
 *
 * 如果索引为正，那么从表头向表尾遍历。
 * 如果索引为负，那么从表尾向表头遍历。
 * 正数索引从 0 开始，负数索引从 -1 开始。
 *
 * 如果索引超过列表的节点数量，或者列表为空，那么返回 NULL 。
 *
 * T = O(N)
 */
unsigned char *ziplistIndex(unsigned char *zl, int index) {

    unsigned char *p;

    zlentry entry;

    // 处理负数索引
    if (index < 0) {

        // 将索引转换为正数
        index = (-index)-1;

        // 定位到表尾节点
        p = ZIPLIST_ENTRY_TAIL(zl);

        // 如果列表不为空，那么。。。
        if (p[0] != ZIP_END) {

            // 从表尾向表头遍历
            entry = zipEntry(p);
            // T = O(N)
            while (entry.prevrawlen > 0 && index--) {
                // 前移指针
                p -= entry.prevrawlen;
                // T = O(1)
                entry = zipEntry(p);
            }
        }

    // 处理正数索引
    } else {

        // 定位到表头节点
        p = ZIPLIST_ENTRY_HEAD(zl);

        // T = O(N)
        while (p[0] != ZIP_END && index--) {
            // 后移指针
            // T = O(1)
            p += zipRawEntryLength(p);
        }
    }

    // 返回结果
    return (p[0] == ZIP_END || index > 0) ? NULL : p;
}

/* Return pointer to next entry in ziplist.
 *
 * zl is the pointer to the ziplist
 * p is the pointer to the current element
 *
 * The element after 'p' is returned, otherwise NULL if we are at the end. */
/*
 * 返回 p 所指向节点的后置节点。
 *
 * 如果 p 为表末端，或者 p 已经是表尾节点，那么返回 NULL 。
 *
 * T = O(1)
 */
unsigned char *ziplistNext(unsigned char *zl, unsigned char *p) {
    ((void) zl);

    /* "p" could be equal to ZIP_END, caused by ziplistDelete,
     * and we should return NULL. Otherwise, we should return NULL
     * when the *next* element is ZIP_END (there is no next entry). */
    // p 已经指向列表末端
    if (p[0] == ZIP_END) {
        return NULL;
    }

    // 指向后一节点
    p += zipRawEntryLength(p);
    if (p[0] == ZIP_END) {
        // p 已经是表尾节点，没有后置节点
        return NULL;
    }

    return p;
}

/* Return pointer to previous entry in ziplist. */
/*
 * 返回 p 所指向节点的前置节点。
 *
 * 如果 p 所指向为空列表，或者 p 已经指向表头节点，那么返回 NULL 。
 *
 * T = O(1)
 */
unsigned char *ziplistPrev(unsigned char *zl, unsigned char *p) {
    zlentry entry;

    /* Iterating backwards from ZIP_END should return the tail. When "p" is
     * equal to the first element of the list, we're already at the head,
     * and should return NULL. */

    // 如果 p 指向列表末端（列表为空，或者刚开始从表尾向表头迭代）
    // 那么尝试取出列表尾端节点
    if (p[0] == ZIP_END) {
        p = ZIPLIST_ENTRY_TAIL(zl);
        // 尾端节点也指向列表末端，那么列表为空
        return (p[0] == ZIP_END) ? NULL : p;

    // 如果 p 指向列表头，那么说明迭代已经完成
    } else if (p == ZIPLIST_ENTRY_HEAD(zl)) {
        return NULL;

    // 既不是表头也不是表尾，从表尾向表头移动指针
    } else {
        // 计算前一个节点的节点数
        entry = zipEntry(p);
        assert(entry.prevrawlen > 0);
        // 移动指针，指向前一个节点
        return p-entry.prevrawlen;
    }
}

/* Get entry pointed to by 'p' and store in either 'e' or 'v' depending
 * on the encoding of the entry. 'e' is always set to NULL to be able
 * to find out whether the string pointer or the integer value was set.
 * Return 0 if 'p' points to the end of the ziplist, 1 otherwise. */
/*
 * 取出 p 所指向节点的值：
 *
 * - 如果节点保存的是字符串，那么将字符串值指针保存到 *sstr 中，字符串长度保存到 *slen
 *
 * - 如果节点保存的是整数，那么将整数保存到 *sval
 *
 * 程序可以通过检查 *sstr 是否为 NULL 来检查值是字符串还是整数。
 *
 * 提取值成功返回 1 ，
 * 如果 p 为空，或者 p 指向的是列表末端，那么返回 0 ，提取值失败。
 *
 * T = O(1)
 */
unsigned int ziplistGet(unsigned char *p, unsigned char **sstr, unsigned int *slen, long long *sval) {

    zlentry entry;
    if (p == NULL || p[0] == ZIP_END) return 0;
    if (sstr) *sstr = NULL;

    // 取出 p 所指向的节点的各项信息，并保存到结构 entry 中
    // T = O(1)
    entry = zipEntry(p);

    // 节点的值为字符串，将字符串长度保存到 *slen ，字符串保存到 *sstr
    // T = O(1)
    if (ZIP_IS_STR(entry.encoding)) {
        if (sstr) {
            *slen = entry.len;
            *sstr = p+entry.headersize;
        }

    // 节点的值为整数，解码值，并将值保存到 *sval
    // T = O(1)
    } else {
        if (sval) {
            *sval = zipLoadInteger(p+entry.headersize,entry.encoding);
        }
    }

    return 1;
}

/* Insert an entry at "p".
 *
 * 将包含给定值 s 的新节点插入到给定的位置 p 中。
 *
 * 如果 p 指向一个节点，那么新节点将放在原有节点的前面。
 *
 * T = O(N^2)
 */
unsigned char *ziplistInsert(unsigned char *zl, unsigned char *p, unsigned char *s, unsigned int slen) {
    return __ziplistInsert(zl,p,s,slen);
}

/* Delete a single entry from the ziplist, pointed to by *p.
 * Also update *p in place, to be able to iterate over the
 * ziplist, while deleting entries.
 *
 * 从 zl 中删除 *p 所指向的节点，
 * 并且原地更新 *p 所指向的位置，使得可以在迭代列表的过程中对节点进行删除。
 *
 * T = O(N^2)
 */
unsigned char *ziplistDelete(unsigned char *zl, unsigned char **p) {

    // 因为 __ziplistDelete 时会对 zl 进行内存重分配
    // 而内存充分配可能会改变 zl 的内存地址
    // 所以这里需要记录到达 *p 的偏移量
    // 这样在删除节点之后就可以通过偏移量来将 *p 还原到正确的位置
    size_t offset = *p-zl;
    zl = __ziplistDelete(zl,*p,1);

    /* Store pointer to current element in p, because ziplistDelete will
     * do a realloc which might result in a different "zl"-pointer.
     * When the delete direction is back to front, we might delete the last
     * entry and end up with "p" pointing to ZIP_END, so check this. */
    *p = zl+offset;

    return zl;
}

/* Delete a range of entries from the ziplist.
 *
 * 从 index 索引指定的节点开始，连续地从 zl 中删除 num 个节点。
 *
 * T = O(N^2)
 */
unsigned char *ziplistDeleteRange(unsigned char *zl, unsigned int index, unsigned int num) {

    // 根据索引定位到节点
    // T = O(N)
    unsigned char *p = ziplistIndex(zl,index);

    // 连续删除 num 个节点
    // T = O(N^2)
    return (p == NULL) ? zl : __ziplistDelete(zl,p,num);
}

/* Compare entry pointer to by 'p' with 'entry'. Return 1 if equal.
 *
 * 将 p 所指向的节点的值和 sstr 进行对比。
 *
 * 如果节点值和 sstr 的值相等，返回 1 ，不相等则返回 0 。
 *
 * T = O(N)
 */
unsigned int ziplistCompare(unsigned char *p, unsigned char *sstr, unsigned int slen) {
    zlentry entry;
    unsigned char sencoding;
    long long zval, sval;
    if (p[0] == ZIP_END) return 0;

    // 取出节点
    entry = zipEntry(p);
    if (ZIP_IS_STR(entry.encoding)) {

        // 节点值为字符串，进行字符串对比

        /* Raw compare */
        if (entry.len == slen) {
            // T = O(N)
            return memcmp(p+entry.headersize,sstr,slen) == 0;
        } else {
            return 0;
        }
    } else {

        // 节点值为整数，进行整数对比

        /* Try to compare encoded values. Don't compare encoding because
         * different implementations may encoded integers differently. */
        if (zipTryEncoding(sstr,slen,&sval,&sencoding)) {
          // T = O(1)
          zval = zipLoadInteger(p+entry.headersize,entry.encoding);
          return zval == sval;
        }
    }

    return 0;
}

/* Find pointer to the entry equal to the specified entry.
 *
 * 寻找节点值和 vstr 相等的列表节点，并返回该节点的指针。
 *
 * Skip 'skip' entries between every comparison.
 *
 * 每次比对之前都跳过 skip 个节点。
 *
 * Returns NULL when the field could not be found.
 *
 * 如果找不到相应的节点，则返回 NULL 。
 *
 * T = O(N^2)
 */
unsigned char *ziplistFind(unsigned char *p, unsigned char *vstr, unsigned int vlen, unsigned int skip) {
    int skipcnt = 0;
    unsigned char vencoding = 0;
    long long vll = 0;

    // 只要未到达列表末端，就一直迭代
    // T = O(N^2)
    while (p[0] != ZIP_END) {
        unsigned int prevlensize, encoding, lensize, len;
        unsigned char *q;

        ZIP_DECODE_PREVLENSIZE(p, prevlensize);
        ZIP_DECODE_LENGTH(p + prevlensize, encoding, lensize, len);
        q = p + prevlensize + lensize;

        if (skipcnt == 0) {

            /* Compare current entry with specified entry */
            // 对比字符串值
            // T = O(N)
            if (ZIP_IS_STR(encoding)) {
                if (len == vlen && memcmp(q, vstr, vlen) == 0) {
                    return p;
                }
            } else {
                /* Find out if the searched field can be encoded. Note that
                 * we do it only the first time, once done vencoding is set
                 * to non-zero and vll is set to the integer value. */
                // 因为传入值有可能被编码了，
                // 所以当第一次进行值对比时，程序会对传入值进行解码
                // 这个解码操作只会进行一次
                if (vencoding == 0) {
                    if (!zipTryEncoding(vstr, vlen, &vll, &vencoding)) {
                        /* If the entry can't be encoded we set it to
                         * UCHAR_MAX so that we don't retry again the next
                         * time. */
                        vencoding = UCHAR_MAX;
                    }
                    /* Must be non-zero by now */
                    assert(vencoding);
                }

                /* Compare current entry with specified entry, do it only
                 * if vencoding != UCHAR_MAX because if there is no encoding
                 * possible for the field it can't be a valid integer. */
                // 对比整数值
                if (vencoding != UCHAR_MAX) {
                    // T = O(1)
                    long long ll = zipLoadInteger(q, encoding);
                    if (ll == vll) {
                        return p;
                    }
                }
            }

            /* Reset skip count */
            skipcnt = skip;
        } else {
            /* Skip entry */
            skipcnt--;
        }

        /* Move to next entry */
        // 后移指针，指向后置节点
        p = q + len;
    }

    // 没有找到指定的节点
    return NULL;
}

/* Return length of ziplist.
 *
 * 返回 ziplist 中的节点个数
 *
 * T = O(N)
 */
unsigned int ziplistLen(unsigned char *zl) {

    unsigned int len = 0;

    // 节点数小于 UINT16_MAX
    // T = O(1)
    if (intrev16ifbe(ZIPLIST_LENGTH(zl)) < UINT16_MAX) {
        len = intrev16ifbe(ZIPLIST_LENGTH(zl));

    // 节点数大于 UINT16_MAX 时，需要遍历整个列表才能计算出节点数
    // T = O(N)
    } else {
        unsigned char *p = zl+ZIPLIST_HEADER_SIZE;
        while (*p != ZIP_END) {
            p += zipRawEntryLength(p);
            len++;
        }

        /* Re-store length if small enough */
        if (len < UINT16_MAX) ZIPLIST_LENGTH(zl) = intrev16ifbe(len);
    }

    return len;
}

/* Return ziplist blob size in bytes.
 *
 * 返回整个 ziplist 占用的内存字节数
 *
 * T = O(1)
 */
size_t ziplistBlobLen(unsigned char *zl) {
    return intrev32ifbe(ZIPLIST_BYTES(zl));
}

void ziplistPrint(unsigned char *zl){
    unsigned char *p;
    zlentry entry;
    p = ZIPLIST_ENTRY_HEAD(zl);
    while(*p != ZIP_END) {
        entry = zipEntry(p);
        p += entry.headersize;
        if (ZIP_IS_STR(entry.encoding)) {
            if (entry.len > 40) {
                if (fwrite(p,40,1,stdout) == 0) perror("fwrite");
                printf("...");
            } else {
                if (entry.len &&
                    fwrite(p,entry.len,1,stdout) == 0) perror("fwrite");
            }
        } else {
            printf("%lld", (long long) zipLoadInteger(p,entry.encoding));
        }
        printf("\n");
        p += entry.len;
    }
}
void ziplistRepr(unsigned char *zl) {
    unsigned char *p;
    int index = 0;
    zlentry entry;

    printf(
        "{total bytes %d} "
        "{length %u}\n"
        "{tail offset %u}\n",
        intrev32ifbe(ZIPLIST_BYTES(zl)),
        intrev16ifbe(ZIPLIST_LENGTH(zl)),
        intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl)));
    p = ZIPLIST_ENTRY_HEAD(zl);
    while(*p != ZIP_END) {
        entry = zipEntry(p);
        printf(
            "{"
                "addr 0x%08lx, "
                "index %2d, "
                "offset %5ld, "
                "rl: %5u, "
                "hs %2u, "
                "pl: %5u, "
                "pls: %2u, "
                "payload %5u"
            "} ",
            (long unsigned)p,
            index,
            (unsigned long) (p-zl),
            entry.headersize+entry.len,
            entry.headersize,
            entry.prevrawlen,
            entry.prevrawlensize,
            entry.len);
        p += entry.headersize;
        if (ZIP_IS_STR(entry.encoding)) {
            if (entry.len > 40) {
                if (fwrite(p,40,1,stdout) == 0) perror("fwrite");
                printf("...");
            } else {
                if (entry.len &&
                    fwrite(p,entry.len,1,stdout) == 0) perror("fwrite");
            }
        } else {
            printf("%lld", (long long) zipLoadInteger(p,entry.encoding));
        }
        printf("\n");
        p += entry.len;
        index++;
    }
    printf("{end}\n\n");
}

#define TEST_MAIN
#ifdef TEST_MAIN
unsigned char *createList() {
    unsigned char *zl = ziplistNew();
    zl = ziplistPush(zl, (unsigned char*)"foo", 3, ZIPLIST_TAIL);
    zl = ziplistPush(zl, (unsigned char*)"quux", 4, ZIPLIST_TAIL);
    zl = ziplistPush(zl, (unsigned char*)"hello", 5, ZIPLIST_HEAD);
    zl = ziplistPush(zl, (unsigned char*)"1024", 4, ZIPLIST_TAIL);
    return zl;
}
void test(){
    unsigned char* zl = createList();
    ziplistPrint(zl);
}
#endif // TEST_MAIN