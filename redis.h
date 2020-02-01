#ifndef _REDIS_H_
#define _REDIS_H_

#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include"sds.h"
#include"adlist.h"
#include"dict.h"
#include"util.h"



/*
    默认服务器配置
*/

#define REDIS_SHARED_SELECT_CMDS 10
#define REDIS_SHARED_INTEGERS 10000
#define REDIS_SHARED_BULKHDR_LEN 32
#define REDIS_MAX_LOGMSG_LEN    1024 /* Default maximum length of syslog messages */

/*
    对象类型
*/
#define REDIS_STRING 0
#define REDIS_LIST 1
#define REDIS_SET 2
#define REDIS_ZSET 3
#define REDIS_HASH 4


/*
    对象编码
*/
#define REDIS_ENCODING_RAW 0
#define REDIS_ENCODING_INT 1     /* Encoded as integer */
#define REDIS_ENCODING_HT 2      /* Encoded as hash table */
#define REDIS_ENCODING_ZIPMAP 3  /* Encoded as zipmap */
#define REDIS_ENCODING_LINKEDLIST 4 /* Encoded as regular linked list */
#define REDIS_ENCODING_ZIPLIST 5 /* Encoded as ziplist */
#define REDIS_ENCODING_INTSET 6  /* Encoded as intset */
#define REDIS_ENCODING_SKIPLIST 7  /* Encoded as skiplist */
#define REDIS_ENCODING_EMBSTR 8  /* Embedded sds string encoding */



/*
    assert相关
*/
#define redisAssertWithInfo(_c,_o,_e) do{\
                                            if(!_e){\
                                            printf("file:%s\tline:%d\n",__FILE__,__LINE__);\
                                            _exit(1);\
                                            }\
                                        }while(0)

#define redisAssert(_e) do{\
                            if(!(_e)){\
                                printf("file:%s\tline:%d\n",__FILE__,__LINE__);\
                                _exit(1);\
                            }\
                        }while(0)


#define redisPanic(_e) do{\
                            printf("%s\tfile:%s\tline:%d\n",_e,__FILE__,__LINE__);\
                            _exit(1);\
                        }while(0)





#define ZSKIPLIST_MAXLEVEL 32
#define ZSKIPLIST_P 0.25//生成随机数用


/*
    redis对象
*/
#define REDIS_LRU_BITS 24
typedef struct redisObject{
    //类型
    unsigned type:4;

    //编码
    unsigned encoding:4;

    //对象最后一次被访问时间
    unsigned lru:REDIS_LRU_BITS;

    //引用计数
    int refcount;

    //指向实际值的指针

    void* ptr;
}robj;

/*
    跳跃表节点
*/
typedef struct zskiplistNode{

    //成员对象
    robj*obj;

    //分值
    double score;

    //后退指针
    struct zskiplistNode *backward;

    //层
    struct zskiplistLevel{
        //前进指针
        struct zskiplistNode *forward;
        //跨度
        unsigned int span;
    } level[];
}zskiplistNode;

/*
    跳跃表
*/
typedef struct zskiplist{
    //表头节点和表尾节点
    struct zskiplistNode *header,*tail;

    //表中节点数量
    unsigned long length;

    //表中层数最大的节点的层数
    int level;
}zskiplist;


/*
    表示开闭区间的结构
*/
typedef struct{
    //最小值和最大值
    double min,max;

    //闭区间？
    int minex,maxex;
}zrangespec;



typedef struct{
    robj *min,*max;
    int minex,maxex;
}zlexrangespec;




/*
    跳跃表相关函数的定义
*/
zskiplistNode *zslCreateNode(int level,double score,robj *obj);
void zslFreeNode(zskiplistNode *node);
zskiplist *zslCreate(void);
void zslFree(zskiplist *zsl);
zskiplistNode *zslInsert(zskiplist *zsl, double score, robj *obj);
//unsigned char *zzlInsert(unsigned char *zl, robj *ele, double score);
int zslDelete(zskiplist *zsl, double score, robj *obj);
zskiplistNode *zslFirstInRange(zskiplist *zsl, zrangespec *range);
zskiplistNode *zslLastInRange(zskiplist *zsl, zrangespec *range);
//double zzlGetScore(unsigned char *sptr);
//void zzlNext(unsigned char *zl, unsigned char **eptr, unsigned char **sptr);
//void zzlPrev(unsigned char *zl, unsigned char **eptr, unsigned char **sptr);
//unsigned int zsetLength(robj *zobj);
//void zsetConvert(robj *zobj, int encoding);
unsigned long zslGetRank(zskiplist *zsl, double score, robj *o);

//测试函数需要这个所以要将定义放在这里
zskiplistNode* zslGetElementByRank(zskiplist *zsl, unsigned long rank);
void zslPrint(zskiplist *zsl);
void zslPrintInfo(zskiplist *zsl);











/*
    redis object 相关方法的定义
*/
void decrRefCount(robj *o);
void freeStringObject(robj *o);
void freeListObject(robj *o);
void freeSetObject(robj *o);
void freeZsetObject(robj *o);
void freeHashObject(robj *o);
#define sdsEncodedObject(objptr) (objptr->encoding == REDIS_ENCODING_RAW || objptr->encoding == REDIS_ENCODING_EMBSTR)


int compareStringObjects(robj *a, robj *b);
int collateStringObjects(robj *a, robj *b);
int equalStringObjects(robj *a, robj *b);
#define sdsEncodedObject(objptr) (objptr->encoding == REDIS_ENCODING_RAW || objptr->encoding == REDIS_ENCODING_EMBSTR)





// 通过复用来减少内存碎片，以及减少操作耗时的共享对象
struct sharedObjectsStruct {
    robj *crlf, *ok, *err, *emptybulk, *czero, *cone, *cnegone, *pong, *space,
    *colon, *nullbulk, *nullmultibulk, *queued,
    *emptymultibulk, *wrongtypeerr, *nokeyerr, *syntaxerr, *sameobjecterr,
    *outofrangeerr, *noscripterr, *loadingerr, *slowscripterr, *bgsaveerr,
    *masterdownerr, *roslaveerr, *execaborterr, *noautherr, *noreplicaserr,
    *busykeyerr, *oomerr, *plus, *messagebulk, *pmessagebulk, *subscribebulk,
    *unsubscribebulk, *psubscribebulk, *punsubscribebulk, *del, *rpop, *lpop,
    *lpush, *emptyscan, *minstring, *maxstring,
    *select[REDIS_SHARED_SELECT_CMDS],
    *integers[REDIS_SHARED_INTEGERS],
    *mbulkhdr[REDIS_SHARED_BULKHDR_LEN], /* "*<value>\r\n" */
    *bulkhdr[REDIS_SHARED_BULKHDR_LEN];  /* "$<value>\r\n" */
};


/*-----------------------------------------------------------------------------
 * Extern declarations
 *----------------------------------------------------------------------------*/

extern struct sharedObjectsStruct shared;

#endif // _REDIS_H_
