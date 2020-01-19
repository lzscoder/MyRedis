
#ifndef __DICT_H
#define __DICT_H



//字典的操作状态

//操作成功
#define DICT_OK 0
//操作失败
#define DICT_ERR 1

//如果使用的私有数据不使用时,用这个宏避免编译器错误
#define DICT_NOTUSED(v) ((void) v)

#include<inttypes.h>


//哈稀表节点
typedef struct dictEntry{

    void *key;

    union{
        void *val;
        uint64_t u64;
        int64_t s64;
    }v;

    struct dictEntry *next;
}dictEntry;

//字典类型特定函数
typedef struct dictType{

    //计算哈希值的函数
    unsigned int (*hashFunction)(const void*key);

    //复制键的函数
    void *(*keyDup)(void *privdata,const void*key);

    //复制值的函数
    void *(*valDup)(void *privdata,const void*obj);

    //对比键的函数
    int (*keyCompare)(void *privdata,const void* key1,const void *key2);

    //销毁键的函数
    void (*keyDestructor)(void *privdata,void *key);

    //销毁值的函数
    void (*valDestructor)(void *privdata,void *obj);

} dictType;

//哈希表的定义
typedef struct dictht{

    //哈希表数组
    dictEntry **table;

    //哈希表大小
    unsigned long size;

    //哈希表大小掩码 sizemask = size - 1
    unsigned long sizemask;

    //该哈希表已有节点的数量
    unsigned long used;
}dictht;

//字典
typedef struct dict{

    //类型特定函数
    dictType *type;

    //私有数据
    void *privdata;

    //哈希表
    dictht ht[2];

    //当没有进行rehash时值为-1;
    int rehashidx;


    //目前正在运行的安全迭代器数量
    int iterators;

}dict;

//字字典的迭代器
typedef struct dictIterator{

    //被迭代的字典
    dict* d;

    //table:正在被迭代的hash表号码,值可以是0或1
    //index:迭代器当前所指向的哈希表索引的位置
    //safe:标识这个迭代器是否安全
    int table,index,safe;

    dictEntry *entry,*nextEntry;

    long long fingerprint;
}dictIterator;



typedef void (dictScanFunction)(void *privdata,const dictEntry *de);


//哈希表的初始大小
#define DICT_HT_INITAL_SIZE 4



//-----------------------------------Macros----------------------
//释放给定字典节点的值

//void dictFreeVal(dict d,dictEntry entry)
#define dictFreeVal(d,entry) \
    if((d)->type->valDestructor) \
        (d)->type->valDestructor((d)->privdata,(entry)->v.val)




//设置给定字典节点的值
//void dictSetVal(dict d,dictEntry entry,void* _val_)
#define dictSetVal(d,entry,_val_) \
        do{\
            if((d)->type->valDup) \
                entry->v.val = (d)->type->valDup((d)->privdata,_val_);\
            else \
                entry->v.val = (_val_);\
        }while(0)
//为啥要while(0)???


//将一个有符号整数设为节点的值
//void dictSetSignedIntegerVal(dictEntry entry,int64_t _val_)
#define dictSetSignedIntegerVal(entry,_val_) \
    do{entry->v.s64 = _val_;}while(0)

//将一个无符号整数设为节点的值
//void dictSetUnsignedIntegerVal(dictEntry entry,uint64_t _val_)
#define dictSetUnsignedIntegerVal(entry,_val_) \
    do{entry->v.u64 = _val_;}while(0)

//释放给定字典节点的键
//void dictFreeKey(dict d,dictEntry entry)
#define dictFreeKey(d,entry) \
    if((d)->type->keyDestructor) \
        (d)->type->keyDestructor((d)->privdata,(entry)->key)

//设置给定字典节点的键
//void dictSetKey(dict d,dictEntry entry,void* key)
#define dictSetKey(d,entry,_key_) \
        do{\
            if((d)->type->keyDup)\
                entry->key = (d)->type->keyDup((d)->privdata,_key_);\
            else \
                entry->key = (_key_);\
            }while(0)


//设置给定字典的键
//void dictCompareKeys(dict d,void* key1,void* keys2)
#define dictCompareKeys(d,key1,key2) \
    (((d)->type->keyCompare) ? \
        (d)->type->keyCompare((d)->privdata,key1,key2) : \
        (key1) == (key2))


//计算给定键的哈希值
//dictHashKey(dict d,void* key)
#define dictHashKey(d,key) (d)->type->hashFunction(key)

//返回获取给定节点的键
//void* dictGetKey(dictEntry he)
#define dictGetKey(he) ((he)->key)

//返回获取给定节点的值
//void* fictGetVal(dictEntry he)
#define dictGetVal(he) ((he)->v.val)

//返回获取给定节点的有符号整数值
//int64_t dictGetSignedIntegerVal(dictEntry he)
#define dictGetSignedIntegerVal(he) ((he)->v.s64)

//返回给定节点的无符号整数值
//uint64_t dictGetUnsignedIntegerVal(dictEntry he)
#define dictGetUnsignedIntegerVal(he) ((he)->v.u64)

//返回给定字典的大小
//unsigned long dicSlots(dict d)
#define dictSlots(d) ((d)->ht[0].size+(d)->ht[1].size)

//返回字典的已有节点的数量
//unsigned long dictSize(dict d)
#define dictSize(d) ((d)->ht[0].used+(d)->ht[1].used)

//查看字典是否正在rehash
//int dictIsRehashing(dictht ht)
#define dictIsRehashing(ht) ((ht)->rehashidx != -1)


/* API */
dict *dictCreate(dictType *type, void *privDataPtr);//1
int dictExpand(dict *d, unsigned long size);//1
int dictAdd(dict *d, void *key, void *val);//1
dictEntry *dictAddRaw(dict *d, void *key);//1
int dictReplace(dict *d, void *key, void *val);//1
dictEntry *dictReplaceRaw(dict *d, void *key);//1
int dictDelete(dict *d, const void *key);//1
int dictDeleteNoFree(dict *d, const void *key);//1
void dictRelease(dict *d);//1
dictEntry * dictFind(dict *d, const void *key);//1
void *dictFetchValue(dict *d, const void *key);//1
int dictResize(dict *d);//1

dictIterator *dictGetIterator(dict *d);//1
dictIterator *dictGetSafeIterator(dict *d);//1
dictEntry *dictNext(dictIterator *iter);//1
void dictReleaseIterator(dictIterator *iter);//1
dictEntry *dictGetRandomKey(dict *d);//1
int dictGetRandomKeys(dict *d, dictEntry **des, int count);//1
unsigned int dictGenHashFunction(const void *key, int len);//1
unsigned int dictGenCaseHashFunction(const unsigned char *buf, int len);//1
void dictEmpty(dict *d, void(callback)(void*));//1
void dictEnableResize(void);//1
void dictDisableResize(void);//1
int dictRehash(dict *d, int n);//1
int dictRehashMilliseconds(dict *d, int ms);//1
void dictSetHashFunctionSeed(unsigned int initval);//1
unsigned int dictGetHashFunctionSeed(void);//1
unsigned long dictScan(dict *d, unsigned long v, dictScanFunction *fn, void *privdata);
void dictPrint(dict *d);//1
void dictTablePrint(dict *d);//1
/* Hash table types */
extern dictType dictTypeHeapStringCopyKey;
extern dictType dictTypeHeapStrings;
extern dictType dictTypeHeapStringCopyKeyValue;




#endif // __DICT_H
