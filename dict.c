#include"fmacros.h"
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<stdarg.h>
#include<limits.h>
#include<sys/time.h>
#include<ctype.h>
#include<unistd.h>
#include<inttypes.h>
#include"dict.h"
#include"zmalloc.h"
#include<assert.h>

//指示字典是否启用rehash的标识
static int dict_can_resize = 1;

//强制rehash的比率
static unsigned int dict_force_resize_ratio = 5;


//private prototypes
static int _dictExpandIfNeeded(dict *ht);
static unsigned long _dictNextPower(unsigned long size);
static int _dictKeyIndex(dict *ht,const void *key);
static int _dictInit(dict *ht,dictType *type,void *privDataPtr);


//4种hash function
/*整型的Hash算法使用的是Thomas Wang's 32 Bit / 64 Bit Mix Function
这是一种基于位移运算的散列方法。基于移位的散列是使用Key值进行移位操作。通
常是结合左移和右移。每个移位过程的结果进行累加，最后移位的结果作为最终结果。
这种方法的好处是避免了乘法运算，从而提高Hash函数本身的性能。
*/
unsigned int dictIntHashFunction(unsigned int key)
{
    key += ~(key << 15);
    key ^=  (key >> 10);
    key +=  (key << 3);
    key ^=  (key >> 6);
    key += ~(key << 11);
    key ^=  (key >> 16);
    return key;
}
//直接将整型数作为hash的键值
unsigned int dictIdentityHashFuction(unsigned int key){
    return key;
}
static uint32_t dict_hash_function_seed = 5381;
void dictSetHashFunctionSeed(uint32_t seed){
    dict_hash_function_seed = seed;
}
uint32_t dictGetHashFunctionSeed(void) {
    return dict_hash_function_seed;
}
/*
字符串使用的MurmurHash算法，MurmurHash算法具有高运算性能，低碰撞率的特点，
由Austin Appleby创建于2008年，现已应用到Hadoop、libstdc++、nginx、
libmemcached等开源系统。2011年Appleby被Google雇佣，随后Google推出其
变种的CityHash算法。murmur是 multiply and rotate的意思，因为算法的核
心就是不断的乘和移位（x *= m; k ^= k >> r;）
*/
unsigned int dictGenHashFunction(const void *key, int len) {
    /* 'm' and 'r' are mixing constants generated offline.
     They're not really 'magic', they just happen to work well.  */
    uint32_t seed = dict_hash_function_seed;
    const uint32_t m = 0x5bd1e995;
    const int r = 24;

    /* Initialize the hash to a 'random' value */
    uint32_t h = seed ^ len;

    /* Mix 4 bytes at a time into the hash */
    const unsigned char *data = (const unsigned char *)key;

    while(len >= 4) {
        uint32_t k = *(uint32_t*)data;
        k *= m;
        k ^= k >> r;
        k *= m;
        h *= m;
        h ^= k;
        data += 4;
        len -= 4;
    }
    /* Handle the last few bytes of the input array  */
    switch(len) {
    case 3: h ^= data[2] << 16;
    case 2: h ^= data[1] << 8;
    case 1: h ^= data[0]; h *= m;
    };
    /* Do a few final mixes of the hash to ensure the last few
     * bytes are well-incorporated. */
    h ^= h >> 13;
    h *= m;
    h ^= h >> 15;
    return (unsigned int)h;
}
/*
redis对DJB算法的重写
DJB:
unsigned int DJBHash(char *str)
{
    unsigned int hash = 5381;

    while (*str){
        hash = ((hash << 5) + hash) + (*str++); //times 33
    }
    hash &= ~(1 << 31); //strip the highest bit
    return hash;
}
*/
unsigned int dictGenCaseHashFunction(const unsigned char *buf, int len) {
    unsigned int hash = (unsigned int)dict_hash_function_seed;
    while (len--)
        hash = ((hash << 5) + hash) + (tolower(*buf++)); /* hash * 33 + c */
    return hash;
}











//创建一个新的字典
dict *dictCreate(dictType *type,void *privDataPtr){

    dict *d = zmalloc(sizeof(*d));
    int tmp = _dictInit(d,type,privDataPtr);
    tmp = tmp;//tmp只是为了没有编译警告

    return d;
}
//重置hashtable
static void _dictReset(dictht *ht){

    ht->table = NULL;
    ht->size = 0;
    ht->sizemask = 0;
    ht->used = 0;
}
//初始化哈希表
int _dictInit(dict *d,dictType *type,void *privDataPtr){

    //初始化两个hash table的属性
    _dictReset(&d->ht[0]);
    _dictReset(&d->ht[1]);

    //设置类型特定函数
    d->type = type;

    //设置私有数据
    d->privdata = privDataPtr;

    //设置hash table 状态
    d->rehashidx = -1;

    //设置字典的安全迭代器数量
    d->iterators = 0;

    return DICT_OK;
}








//计算第一个大于等于size的2的N次方
static unsigned long _dictNextPower(unsigned long size){

    unsigned long i = DICT_HT_INITAL_SIZE;
    if(size >= LONG_MAX)
        return LONG_MAX;//溢出了就这样简单处理？？？？

    while(1){
        if(i>=size)
            return i;
        i *= 2;
    }
    return i;
}
//创建一个新的hash表
int dictExpand(dict *d,unsigned long size){

    //新hash table
    dictht n;

    //根据size参数，计算哈希表的大小
    unsigned long realsize = _dictNextPower(size);

    //不能在字典rehash的同时进行
    //size的值不能小于ht[0].used--->realsize可能比size小

    //size = minimal一般不比size小
    if(dictIsRehashing(d) || d->ht[0].used > size)
        return  DICT_ERR;

    n.size = realsize;
    n.sizemask = realsize - 1;

    n.table = zcalloc(realsize*sizeof(dictEntry*));
    n.used = 0;

    //0号hash表为空，那么就为第一次初始化
    if(d->ht[0].table == NULL){
        d->ht[0] = n;
    }else{
        //0号哈希表不为空，这是一次rehash
        d->ht[1] = n;
        d->rehashidx = 0;
    }
    return DICT_OK;
}
//缩小给定的字典，使已用节点和字典大小的比率接近1:1
int dictResize(dict *d){
    int minimal;

    //不能在关闭rehash 或者正在rehash的时候调用
    if(!dict_can_resize || dictIsRehashing(d)) return DICT_ERR;

    //计算让比率接近1:1多需要最小节点的数量
    minimal = d->ht[0].used;//缩小节点的长度

    if(minimal < DICT_HT_INITAL_SIZE)
        minimal = DICT_HT_INITAL_SIZE;//扩大节点到初始化的长度

    //调整字典的大小
    return dictExpand(d,minimal);
}







/*
    进行 N 步渐进式 rehash，返回 1 表示仍有键
    需要从 0 号哈希表移动到 1 号哈希表，返回 0
    则表示所有键都已经迁移完毕。注意，每步 rehash
    都是以一个哈希表索引（桶）作为单位的，一个桶
    里可能会有多个节点，被rehash的桶里的所有节
    点都会被移动到新哈希表

*/
int dictRehash(dict *d, int n) {

    // 只可以在 rehash 进行中时执行
    if (!dictIsRehashing(d))
        return 0;

    //进行n步迁移
    while(n--){
        dictEntry *de,*nextde;

        //如果0号hash表为空，那麽表示rehash执行完毕
        if(d->ht[0].used == 0){
            zfree(d->ht[0].table);
            d->ht[0] = d->ht[1];
            _dictReset(&d->ht[1]);
            d->rehashidx = -1;
            return 0;
        }

        assert(d->ht[0].size > (unsigned)d->rehashidx);
        //redisassert.h这个文件还没有写好，用的系统assert.h


        //略过数组中为空的索引
        while(d->ht[0].table[d->rehashidx]==NULL)d->rehashidx++;

        //指向索引的头链表表头节点
        de = d->ht[0].table[d->rehashidx];

        while(de){

            unsigned int h;

            nextde = de->next;

            h = dictHashKey(d,de->key) & d->ht[1].sizemask;

            de->next = d->ht[1].table[h];
            d->ht[1].table[h] = de;

            d->ht[0].used--;
            d->ht[1].used++;

            de = nextde;
        }

        d->ht[0].table[d->rehashidx] = NULL;
        d->rehashidx++;
    }
    return 1;
}

/*
    在字典不存在安全迭代器的情况下，对字典进行单步rehash
    字典有安全迭代器的情况下不能进行rehash
    因为两种不同的迭代和修改操作可能弄乱字典


    这个函数被多个通用的查找 更新操作调用
    它可以让字典在被使用的同时进行rehash

*/
static void _dictRehashStep(dict *d) {
    if (d->iterators == 0) dictRehash(d,1);
}


/*
    根据需要，初始化字典（的哈希表或
    者对字典（的现有哈希表）进行扩展
*/
static int _dictExpandIfNeeded(dict *d){

    //rehash时直接返回
    if(dictIsRehashing(d)) return DICT_OK;

    //如果字典0号hash表为空，那麽创建并返回初始化大小的0号hash表

    if(d->ht[0].size == 0)
        return dictExpand(d,DICT_HT_INITAL_SIZE);
    //以下两个条件之一为真时对字典进行扩展
    //1)used和size的比率接近1:1且dict_can_resize为真
    //2)已使用节点数和字典大小之间额比率超过dict_force_resize_ratio

    if(d->ht[0].used >= d->ht[0].size &&
       (dict_can_resize ||
        d->ht[0].used/d->ht[0].size > dict_force_resize_ratio)){
            return dictExpand(d,d->ht[0].used*2);
    }

    return DICT_OK;

}


/*
    返回可以将 key 插入到哈希表的索引位置
    如果 key 已经存在于哈希表，那么返回 -1
    注意，如果字典正在进行 rehash ，那么总
    是返回 1 号哈希表的索引,因为在字典进行
    rehash 时，新节点总是插入到 1 号哈希表
*/
static int _dictKeyIndex(dict *d,const void *key){
    unsigned int h,idx,table;
    dictEntry *he;

    //判断是否需要扩展dict
    if(_dictExpandIfNeeded(d) == DICT_ERR)
        return -1;

    //计算key的hash值
    h = dictHashKey(d,key);//使用的hash函数在dict的type中决定

    for(table = 0;table<=1;table++){

        //计算索引值
        idx = h & d->ht[table].sizemask;

        //查找key是否存在
        he = d->ht[table].table[idx];

        while(he){
            if(dictCompareKeys(d,key,he->key))
                return -1;
            he = he->next;
        }
        //如果运行到这里,说明0号hash表中所有的节点都不包含key
        //如果这时rehash正在进行,那麽继续对1号hash表进行rehash
        //否则直接返回idx
        if(!dictIsRehashing(d)) break;
    }
    return idx;
}

/*
  尝试将键插入到字典中，如果键已经存在字典中，那麽返回NULL
  如果键不存在，那麽程序创建新的hash节点，将节点和键关联并
  插入到字典，然后返回节点本身
*/
dictEntry *dictAddRaw(dict *d,void *key){
    int index;
    dictEntry *entry;
    dictht *ht;

    //可以进行单步rehash
    if(dictIsRehashing(d)) _dictRehashStep(d);

    //查找键在hash表中的索引值
    //-1代表已经存在

    if((index = _dictKeyIndex(d,key)) == -1)
        return NULL;

    //否则将插入到新节点中

    //正在rehash时,那麽将新键添加到1号hash表
    //否则将新键添加到0号hash表
    ht = dictIsRehashing(d) ? &d->ht[1] : &d->ht[0];

    entry = zmalloc(sizeof(*entry));

    entry->next = ht->table[index];

    ht->table[index] = entry;

    ht->used++;

    dictSetKey(d,entry,key);

    return entry;
}

/*
    尝试将给定的键值对添加到字典中
    只有给定键key不存在与字典时操作才会成功
    最坏O(n)，平均O(1)

*/
int dictAdd(dict *d, void *key, void *val){

    //插入键
    dictEntry *entry = dictAddRaw(d,key);

    if(!entry) return DICT_ERR;

    //插入值
    dictSetVal(d,entry,val);

    return DICT_OK;
}


void dictPrint(dict *d){
    printf("dict d:%p\n",d);

    printf("\n\ndict->type:%p\n",d->type);
    if(d->type){
        printf("dict->type->hashFunction:%p\n",d->type->hashFunction);
        printf("dict->type->keyDup:%p\n",d->type->keyDup);
        printf("dict->type->valDup:%p\n",d->type->valDup);
        printf("dict->type->keyCompare:%p\n",d->type->keyCompare);
        printf("dict->type->keyDestructor:%p\n",d->type->keyDestructor);
        printf("dict->type->valDestructor:%p\n\n",d->type->valDestructor);
    }
    printf("dict->private:%p\n",d->privdata);
    printf("\n\nhash table1:\n");
    printf("dict->ht[0]->table:%p\n",d->ht[0].table);
    printf("dict->ht[0]->size:%zu\n",d->ht[0].size);
    printf("dict->ht[0]->sizemask:%zu\n",d->ht[0].sizemask);
    printf("dict->ht[0]->used:%zu\n",d->ht[0].used);
    printf("hash table2:\n");
    printf("dict->ht[1]->table:%p\n",d->ht[1].table);
    printf("dict->ht[1]->size:%zu\n",d->ht[1].size);
    printf("dict->ht[1]->sizemask:%zu\n",d->ht[1].sizemask);
    printf("dict->ht[1]->used:%zu\n",d->ht[1].used);
    printf("\n\ndict->rehashidx:%d\n",d->rehashidx);
    printf("dict->iterators:%d\n",d->iterators);
}


void dictTablePrint(dict *d){
    if(d==NULL)
    {
        printf("dict id null\n");
        return;
    }
    printf("********element********\n");
    printf("table1:\n");
    if(d->ht[0].table){
        int size = d->ht[0].size;
        dictEntry *de,*nextde;
        for(int i = 0;i<size;i++){
            de = d->ht[0].table[i];
            while(de){
                nextde = de->next;
                printf("key:%s\tvalue:%s\n",(char*)de->key,(char*)de->v.val);
                de = nextde;
            }

        }
    }
    printf("table2:\n");
    if(d->ht[1].table){
        int size = d->ht[1].size;
        dictEntry *de,*nextde;
        for(int i = 0;i<size;i++){
            de = d->ht[1].table[i];
            while(de){
                nextde = de->next;
                printf("key:%s\tvalue:%s\n",(char*)de->key,(char*)de->v.val);
                de = nextde;
            }

        }
    }
}





/*
    查找key是否存在dict中
*/
dictEntry * dictFind(dict *d, const void *key){

    dictEntry *he;
    unsigned int h,idx,table;

    //hash表为空
    if(d->ht[0].size == 0) return NULL;

    if(dictIsRehashing(d)) _dictRehashStep(d);

    h = dictHashKey(d,key);

    for(table = 0;table<=1;table++){
        idx = h & d->ht[table].sizemask;

        he = d->ht[table].table[idx];

        while(he){
            if(dictCompareKeys(d,key,he->key))
                return he;

            he = he->next;
        }

        if(!dictIsRehashing(d)) return NULL;
    }
    return NULL;
}
/*
    向dict中插入键值对
    1.不存在则直接插入
    2.存在则修改值

*/
int dictReplace(dict *d, void *key, void *val){
    dictEntry *entry,auxentry;

    if(dictAdd(d,key,val)==DICT_OK)
        return 1;

    entry = dictFind(d,key);

    auxentry = *entry;

    dictSetVal(d,entry,val);

    dictFreeVal(d,&auxentry);

    return 0;
}

/*
    先查询是否存在
    1.存在则返回查找到的entry
    2.否则插入新的节点并返回
*/
dictEntry *dictReplaceRaw(dict *d, void *key){
    dictEntry *entry = dictFind(d,key);
    return entry?entry:dictAddRaw(d,key);
}







/*

    查找并删除给定键的节点

    0代表调用键和值的释放函数
    1代表不调用

    找到并成功删除则返回dick_ok，否则返回dick_err

*/
static int dictGenericDelete(dict *d,const void *key,int nofree){
    unsigned int h,idx;
    dictEntry *he,*preHe;
    int table;

    //字典为空
    if(d->ht[0].size == 0) return DICT_ERR;

    //执行单步hash
    if(dictIsRehashing(d)) _dictRehashStep(d);

    h = dictHashKey(d,key);

    //遍历hash表
    for(table = 0;table<=1;table++){

        idx = h & d->ht[table].sizemask;

        he = d->ht[table].table[idx];

        preHe = NULL;

        while(he){

            if(dictCompareKeys(d,key,he->key)){

                //找到目标节点

                if(preHe)
                    preHe->next = he->next;
                else
                    d->ht[table].table[idx] = he->next;

                if(!nofree){
                    dictFreeKey(d,he);
                    dictFreeVal(d,he);
                }

                zfree(he);

                d->ht[table].used--;

                return DICT_OK;
            }
            preHe = he;
            he = he->next;
        }
        if(!dictIsRehashing(d)) break;
    }
    return DICT_ERR;
}

/*
    从字典中删除包含给定键的节点

    并且调用键的释放函数来删除键值

    找到并成功删除则返回dick_ok，否则返回dick_err

*/
int dictDelete(dict *d, const void *key){
    return dictGenericDelete(d,key,0);
}
/*
    从字典中删除包含给定键的节点

    但不调用键的释放函数来删除键值

    找到并成功删除则返回dick_ok，否则返回dick_err

*/
int dictDeleteNoFree(dict *d, const void *key){
    return dictGenericDelete(d,key,1);
}














/*
    删除hash表上所有的节点
    重置hash表上各项属性
*/
int _dictClear(dict *d,dictht *ht,void(callback)(void*)){
    unsigned long i;
    //遍历整个hash表
    for(i = 0;i<ht->size && ht->used>0;i++){
        dictEntry *he,*nextHe;
        if(callback && (i & 65535) == 0) callback(d->privdata);
        //跳过空索引
        if((he = ht->table[i]) == NULL) continue;

        while(he){
            nextHe = he->next;
            dictFreeKey(d,he);
            dictFreeVal(d,he);
            zfree(he);
            ht->used--;
            he = nextHe;
        }
    }
    //释放table
    zfree(ht->table);
    //重置ht中的数据
    _dictReset(ht);

    return DICT_OK;
}


/*
    删除并释放整个dict
*/
void dictRelease(dict *d){
    _dictClear(d,&d->ht[0],NULL);
    _dictClear(d,&d->ht[1],NULL);
    zfree(d);
}


/*
    清空字典上所有的hash表节点
    重置字典的某些属性

*/
void dictEmpty(dict *d,void (callback)(void*)){

    _dictClear(d,&d->ht[0],callback);
    _dictClear(d,&d->ht[1],callback);
    d->rehashidx = -1;
    d->iterators = 0;
}






/*
    创建并返回给定字典的不安全迭代器
*/
dictIterator *dictGetIterator(dict *d){

    dictIterator *iter = zmalloc(sizeof(*iter));

    iter->d = d;
    iter->table = 0;
    iter->index = -1;
    iter->safe = 0;
    iter->entry = NULL;
    iter->nextEntry = NULL;

    return iter;
}

/*
    创建并返回给定字典的安全迭代器
*/
dictIterator *dictGetSafeIterator(dict *d){

    dictIterator *i = dictGetIterator(d);
    i->safe = 1;

    return i;
}



/* A fingerprint is a 64 bit number that represents the state of the dictionary
 * at a given time, it's just a few dict properties xored together.
 * When an unsafe iterator is initialized, we get the dict fingerprint, and check
 * the fingerprint again when the iterator is released.
 * If the two fingerprints are different it means that the user of the iterator
 * performed forbidden operations against the dictionary while iterating.
 */
long long dictFingerprint(dict *d) {
    long long integers[6], hash = 0;
    int j;

    integers[0] = (long) d->ht[0].table;
    integers[1] = d->ht[0].size;
    integers[2] = d->ht[0].used;
    integers[3] = (long) d->ht[1].table;
    integers[4] = d->ht[1].size;
    integers[5] = d->ht[1].used;

    /* We hash N integers by summing every successive integer with the integer
     * hashing of the previous sum. Basically:
     *
     * Result = hash(hash(hash(int1)+int2)+int3) ...
     *
     * This way the same set of integers in a different order will (likely) hash
     * to a different number. */
    for (j = 0; j < 6; j++) {
        hash += integers[j];
        /* For the hashing step we use Tomas Wang's 64 bit integer hash. */
        hash = (~hash) + (hash << 21); // hash = (hash << 21) - hash - 1;
        hash = hash ^ (hash >> 24);
        hash = (hash + (hash << 3)) + (hash << 8); // hash * 265
        hash = hash ^ (hash >> 14);
        hash = (hash + (hash << 2)) + (hash << 4); // hash * 21
        hash = hash ^ (hash >> 28);
        hash = hash + (hash << 31);
    }
    return hash;
}

/*
    返回迭代器指向的当前节点
    字典迭代完毕时，返回NULL
    T = O(1)
*/

dictEntry *dictNext(dictIterator *iter){
    while(1){
        //进入循环时
        //1.迭代器第一次运行
        //2.当前索引链表中的节点已经迭代完
        if(iter->entry == NULL){

            //指向被迭代的hash表
            dictht *ht = &iter->d->ht[iter->table];

            //初次迭代时执行
            if(iter->index == -1 && iter->table == 0){
                if(iter->safe)
                    iter->d->iterators++;
                else//如果不是安全迭代器则计算指纹
                    iter->fingerprint = dictFingerprint(iter->d);
            }
            //更新索引
            iter->index++;

            //如果迭代器的当前索引大于当前被迭代的hash表的大小
            //那麽说明hash表已经迭代完毕
            if(iter->index >= (signed) ht->size){
                //如果正在rehash那麽跳到1号hash表
                if(dictIsRehashing(iter->d) && iter->table == 0){
                    iter->table++;
                    iter->index = 0;
                    ht = &iter->d->ht[1];
                }else{
                    break;
                }
            }

            //如果到达这里说明hash表并未迭代完
            //更新节点指针，指向下个索引链表的表头节点
            iter->entry = ht->table[iter->index];
        }else{
            //执行到这里说明程序正在迭代某个链表
            //将节点指针指向链表的下个节点
            iter->entry = iter->nextEntry;
        }

        //如果当前节点不为空,那麽记录下该节点的下个节点
        //因为安全迭代器可能会有将迭代器返回的当前节点删除

        if(iter->entry){
            iter->nextEntry = iter->entry->next;
            return iter->entry;
        }
    }
    //迭代完毕
    return NULL;
}













/*
    获取包含给定键的节点值

    如果键为空返回NULL

    否则返回节点的值

*/
void *dictFetchValue(dict *d, const void *key){
    dictEntry *he;
    he = dictFind(d,key);
    return he?dictGetVal(he):NULL;
}


/*
    释放给定字典迭代器
*/
void dictReleaseIterator(dictIterator *iter){

    if(!(iter->index == -1 && iter->table == 0)){
        //释放安全迭代器，安全迭代器计数器减一
        if(iter->safe)
            iter->d->iterators--;
        else
            assert(iter->fingerprint == dictFingerprint(iter->d));
    }
    zfree(iter);

}









/*
    随机返回字典中的任意一个节点
    可用于实现随机化算法
*/
dictEntry *dictGetRandomKey(dict *d){
    dictEntry *he,*orighe;
    unsigned int h;

    int listlen,listele;

    if(dictSize(d)==0)return NULL;

    if(dictIsRehashing(d)) _dictRehashStep(d);

    if(dictIsRehashing(d)){
        do{
            h = random()%(d->ht[0].size + d->ht[1].size);

            he = (h>=d->ht[0].size)?d->ht[1].table[h-d->ht[0].size]:d->ht[0].table[h];
        }while(he == NULL);
    }else{
        do{
            h = random() & d->ht[0].sizemask;
            he = d->ht[0].table[h];
        }while(he == NULL);
    }

    listlen = 0;
    orighe = he;
    while(he){
        he = he->next;
        listlen++;
    }

    listele = random()%listlen;
    he = orighe;

    while(listele--) he = he->next;

    return he;
}



/*********************以下函数都没有测试**********************/
/*
    返回count个entry元素
    存在des中返回
*/

int dictGetRandomKeys(dict *d, dictEntry **des, int count){
    int j; /* internal hash table id, 0 or 1. */
    int stored = 0;

    if (dictSize(d) < count) count = dictSize(d);
    while(stored < count) {
        for (j = 0; j < 2; j++) {
            /* Pick a random point inside the hash table 0 or 1. */
            unsigned int i = random() & d->ht[j].sizemask;
            int size = d->ht[j].size;

            /* Make sure to visit every bucket by iterating 'size' times. */
            while(size--) {
                dictEntry *he = d->ht[j].table[i];
                while (he) {
                    /* Collect all the elements of the buckets found non
                     * empty while iterating. */
                    *des = he;
                    des++;
                    he = he->next;
                    stored++;
                    if (stored == count) return stored;
                }
                i = (i+1) & d->ht[j].sizemask;
            }
            /* If there is only one table and we iterated it all, we should
             * already have 'count' elements. Assert this condition. */
            assert(dictIsRehashing(d) != 0);
        }
    }
    return stored;
}


/*
    开启自动rehash
*/
void dictEnableResize(void){
    dict_can_resize = 1;
}

/*
    关闭自动rehash
*/
void dictDisableResize(void){
    dict_can_resize = 0;
}


/*
    返回以毫秒为单位的unix时间戳
*/
long long timeInMilliseconds(void){
    struct timeval tv;
    gettimeofday(&tv,NULL);
    return (((long long)tv.tv_sec)*1000)+(tv.tv_usec/1000);
}

/*
    在给定毫秒内，以100步为单位对字典进行rehash
*/
int dictRehashMilliseconds(dict *d, int ms){
    long long start = timeInMilliseconds();
    int rehashs = 0;
    while(dictRehash(d,100)){
        rehashs += 100;
        if(timeInMilliseconds()-start > ms) break;
    }
    return rehashs;
}



/*

    以下都是dict的遍历,算法有些巧妙暂时没看


*/
/* Function to reverse bits. Algorithm from:
 * http://graphics.stanford.edu/~seander/bithacks.html#ReverseParallel */
static unsigned long rev(unsigned long v) {
    unsigned long s = 8 * sizeof(v); // bit size; must be power of 2
    unsigned long mask = ~0;
    while ((s >>= 1) > 0) {
        mask ^= (mask << s);
        v = ((v >> s) & mask) | ((v << s) & ~mask);
    }
    return v;
}

/* dictScan() is used to iterate over the elements of a dictionary.
 *
 * dictScan() 函数用于迭代给定字典中的元素。
 *
 * Iterating works in the following way:
 *
 * 迭代按以下方式执行：
 *
 * 1) Initially you call the function using a cursor (v) value of 0.
 *    一开始，你使用 0 作为游标来调用函数。
 * 2) The function performs one step of the iteration, and returns the
 *    new cursor value that you must use in the next call.
 *    函数执行一步迭代操作，
 *    并返回一个下次迭代时使用的新游标。
 * 3) When the returned cursor is 0, the iteration is complete.
 *    当函数返回的游标为 0 时，迭代完成。
 *
 * The function guarantees that all the elements that are present in the
 * dictionary from the start to the end of the iteration are returned.
 * However it is possible that some element is returned multiple time.
 *
 * 函数保证，在迭代从开始到结束期间，一直存在于字典的元素肯定会被迭代到，
 * 但一个元素可能会被返回多次。
 *
 * For every element returned, the callback 'fn' passed as argument is
 * called, with 'privdata' as first argument and the dictionar entry
 * 'de' as second argument.
 *
 * 每当一个元素被返回时，回调函数 fn 就会被执行，
 * fn 函数的第一个参数是 privdata ，而第二个参数则是字典节点 de 。
 *
 * HOW IT WORKS.
 * 工作原理
 *
 * The algorithm used in the iteration was designed by Pieter Noordhuis.
 * The main idea is to increment a cursor starting from the higher order
 * bits, that is, instead of incrementing the cursor normally, the bits
 * of the cursor are reversed, then the cursor is incremented, and finally
 * the bits are reversed again.
 *
 * 迭代所使用的算法是由 Pieter Noordhuis 设计的，
 * 算法的主要思路是在二进制高位上对游标进行加法计算
 * 也即是说，不是按正常的办法来对游标进行加法计算，
 * 而是首先将游标的二进制位翻转（reverse）过来，
 * 然后对翻转后的值进行加法计算，
 * 最后再次对加法计算之后的结果进行翻转。
 *
 * This strategy is needed because the hash table may be resized from one
 * call to the other call of the same iteration.
 *
 * 这一策略是必要的，因为在一次完整的迭代过程中，
 * 哈希表的大小有可能在两次迭代之间发生改变。
 *
 * dict.c hash tables are always power of two in size, and they
 * use chaining, so the position of an element in a given table is given
 * always by computing the bitwise AND between Hash(key) and SIZE-1
 * (where SIZE-1 is always the mask that is equivalent to taking the rest
 *  of the division between the Hash of the key and SIZE).
 *
 * 哈希表的大小总是 2 的某个次方，并且哈希表使用链表来解决冲突，
 * 因此一个给定元素在一个给定表的位置总可以通过 Hash(key) & SIZE-1
 * 公式来计算得出，
 * 其中 SIZE-1 是哈希表的最大索引值，
 * 这个最大索引值就是哈希表的 mask （掩码）。
 *
 * For example if the current hash table size is 16, the mask is
 * (in binary) 1111. The position of a key in the hash table will be always
 * the last four bits of the hash output, and so forth.
 *
 * 举个例子，如果当前哈希表的大小为 16 ，
 * 那么它的掩码就是二进制值 1111 ，
 * 这个哈希表的所有位置都可以使用哈希值的最后四个二进制位来记录。
 *
 * WHAT HAPPENS IF THE TABLE CHANGES IN SIZE?
 * 如果哈希表的大小改变了怎么办？
 *
 * If the hash table grows, elements can go anyway in one multiple of
 * the old bucket: for example let's say that we already iterated with
 * a 4 bit cursor 1100, since the mask is 1111 (hash table size = 16).
 *
 * 当对哈希表进行扩展时，元素可能会从一个槽移动到另一个槽，
 * 举个例子，假设我们刚好迭代至 4 位游标 1100 ，
 * 而哈希表的 mask 为 1111 （哈希表的大小为 16 ）。
 *
 * If the hash table will be resized to 64 elements, and the new mask will
 * be 111111, the new buckets that you obtain substituting in ??1100
 * either 0 or 1, can be targeted only by keys that we already visited
 * when scanning the bucket 1100 in the smaller hash table.
 *
 * 如果这时哈希表将大小改为 64 ，那么哈希表的 mask 将变为 111111 ，
 *
 * By iterating the higher bits first, because of the inverted counter, the
 * cursor does not need to restart if the table size gets bigger, and will
 * just continue iterating with cursors that don't have '1100' at the end,
 * nor any other combination of final 4 bits already explored.
 *
 * Similarly when the table size shrinks over time, for example going from
 * 16 to 8, If a combination of the lower three bits (the mask for size 8
 * is 111) was already completely explored, it will not be visited again
 * as we are sure that, we tried for example, both 0111 and 1111 (all the
 * variations of the higher bit) so we don't need to test it again.
 *
 * WAIT... YOU HAVE *TWO* TABLES DURING REHASHING!
 * 等等。。。在 rehash 的时候可是会出现两个哈希表的阿！
 *
 * Yes, this is true, but we always iterate the smaller one of the tables,
 * testing also all the expansions of the current cursor into the larger
 * table. So for example if the current cursor is 101 and we also have a
 * larger table of size 16, we also test (0)101 and (1)101 inside the larger
 * table. This reduces the problem back to having only one table, where
 * the larger one, if exists, is just an expansion of the smaller one.
 *
 * LIMITATIONS
 * 限制
 *
 * This iterator is completely stateless, and this is a huge advantage,
 * including no additional memory used.
 * 这个迭代器是完全无状态的，这是一个巨大的优势，
 * 因为迭代可以在不使用任何额外内存的情况下进行。
 *
 * The disadvantages resulting from this design are:
 * 这个设计的缺陷在于：
 *
 * 1) It is possible that we return duplicated elements. However this is usually
 *    easy to deal with in the application level.
 *    函数可能会返回重复的元素，不过这个问题可以很容易在应用层解决。
 * 2) The iterator must return multiple elements per call, as it needs to always
 *    return all the keys chained in a given bucket, and all the expansions, so
 *    we are sure we don't miss keys moving.
 *    为了不错过任何元素，
 *    迭代器需要返回给定桶上的所有键，
 *    以及因为扩展哈希表而产生出来的新表，
 *    所以迭代器必须在一次迭代中返回多个元素。
 * 3) The reverse cursor is somewhat hard to understand at first, but this
 *    comment is supposed to help.
 *    对游标进行翻转（reverse）的原因初看上去比较难以理解，
 *    不过阅读这份注释应该会有所帮助。
 */
unsigned long dictScan(dict *d,
                       unsigned long v,
                       dictScanFunction *fn,
                       void *privdata){
    dictht *t0, *t1;
    const dictEntry *de;
    unsigned long m0, m1;

    // 跳过空字典
    if (dictSize(d) == 0) return 0;

    // 迭代只有一个哈希表的字典
    if (!dictIsRehashing(d)) {

        // 指向哈希表
        t0 = &(d->ht[0]);

        // 记录 mask
        m0 = t0->sizemask;

        /* Emit entries at cursor */
        // 指向哈希桶
        de = t0->table[v & m0];
        // 遍历桶中的所有节点
        while (de) {
            fn(privdata, de);
            de = de->next;
        }

    // 迭代有两个哈希表的字典
    } else {

        // 指向两个哈希表
        t0 = &d->ht[0];
        t1 = &d->ht[1];

        /* Make sure t0 is the smaller and t1 is the bigger table */
        // 确保 t0 比 t1 要小
        if (t0->size > t1->size) {
            t0 = &d->ht[1];
            t1 = &d->ht[0];
        }

        // 记录掩码
        m0 = t0->sizemask;
        m1 = t1->sizemask;

        /* Emit entries at cursor */
        // 指向桶，并迭代桶中的所有节点
        de = t0->table[v & m0];
        while (de) {
            fn(privdata, de);
            de = de->next;
        }

        /* Iterate over indices in larger table that are the expansion
         * of the index pointed to by the cursor in the smaller table */
        // Iterate over indices in larger table             // 迭代大表中的桶
        // that are the expansion of the index pointed to   // 这些桶被索引的 expansion 所指向
        // by the cursor in the smaller table               //
        do {
            /* Emit entries at cursor */
            // 指向桶，并迭代桶中的所有节点
            de = t1->table[v & m1];
            while (de) {
                fn(privdata, de);
                de = de->next;
            }

            /* Increment bits not covered by the smaller mask */
            v = (((v | m0) + 1) & ~m0) | (v & m0);

            /* Continue while bits covered by mask difference is non-zero */
        } while (v & (m0 ^ m1));
    }

    /* Set unmasked bits so incrementing the reversed cursor
     * operates on the masked bits of the smaller table */
    v |= ~m0;

    /* Increment the reverse cursor */
    v = rev(v);
    v++;
    v = rev(v);

    return v;
}

#ifdef DICT_TEST_MAIN
#include<stdio.h>
#include"dict.h"
#include<assert.h>
#include<string.h>
#include<stdlib.h>
#define MAX_ELEM_NUM 500000
void MyReverse(char *str1){
	unsigned long len = strlen(str1);
	unsigned long i = 0,j = len -1;
	while(i<j){
        char tmp = str1[i];
        str1[i] = str1[j];
        str1[j] = tmp;
        i++;
        j--;
	}
}
void itoa (unsigned long n,char s[]){
    unsigned long i,sign;
    if((sign=n)<0)//记录符号
        n=-n;//使n成为正数
    i=0;
    do{
       s[i++]=n%10+'0';//取下一个数字
    }
    while ((n/=10)>0);//删除该数字
    if(sign<0)
        s[i++]='-';
    s[i]='\0';
    MyReverse(s);
}
unsigned int MyhashFunction(const void*key){
    int len = strlen((char*)key);
    return dictGenHashFunction(key,len);
}
void test1(){
    printf("*****tesing dictCreate fuction*****\n");
    dict *d = dictCreate(NULL,NULL);
    printf("sizeof dict = %zu\n",sizeof(dict));
    printf("sizeof dictht = %zu\n",sizeof(dictht));
    printf("sizeof dictEntry = %zu\n",sizeof(dictEntry));
    printf("sizeof dictEntry** = %zu\n",sizeof(dictEntry**));

    printf("*****tesing dictIsRehash fuction*****\n");
    printf("dict is rehash:%d\n",dictIsRehashing(d));

    printf("*****tesing dictResize fuction*****\n");
    dictPrint(d);
    dictResize(d);
    dictPrint(d);

}
void test2(){
    printf("*****tesing dictCreate fuction*****\n");
    dict *d = dictCreate(NULL,NULL);
    dictType type;
    type.hashFunction = MyhashFunction;
    type.keyCompare = NULL;
    type.keyDestructor = NULL;
    type.keyDup = NULL;
    type.valDestructor = NULL;
    type.valDup = NULL;
    d->type = &type;
    dictResize(d);
    char* str1 = "1";
    char* str2 = "2";
    char* str3 = "3";
    char* str4 = "4";
    char* str5 = "5";
    char* str11 = "11";
    char* str22 = "22";
    char* str33 = "33";
    char* str44 = "44";
    char* str55 = "55";
    dictPrint(d);
    dictAdd(d,str1,str11);
    dictAdd(d,str2,str22);
    dictAdd(d,str3,str33);
    dictAdd(d,str4,str44);
    dictTablePrint(d);
    dictAdd(d,str1,str11);
    dictTablePrint(d);
    dictAdd(d,str5,str55);
    dictTablePrint(d);
    dictAdd(d,str1,str11);
    dictTablePrint(d);

}
void test3(){
    char* str[MAX_ELEM_NUM][2];
    for(unsigned long i = 0;i<MAX_ELEM_NUM;i++){
        for(unsigned long j = 0;j<2;j++){
            str[i][j] = malloc(sizeof("00000000"));
            strcpy(str[i][j],"00000000");
        }
    }
    for(unsigned long i = 0;i<MAX_ELEM_NUM;i++){
        itoa(i,str[i][0]);
        itoa(i,str[i][1]);
    }

    printf("*****tesing dictAdd fuction*****\n");
    dict *d = dictCreate(NULL,NULL);
    dictType type;
    type.hashFunction = MyhashFunction;
    type.keyCompare = NULL;
    type.keyDestructor = NULL;
    type.keyDup = NULL;
    type.valDestructor = NULL;
    type.valDup = NULL;
    d->type = &type;

    for(unsigned long i=0;i<MAX_ELEM_NUM;i++){
        //printf("before add number:%d\n",i);
        //dictTablePrint(d);
        dictAdd(d,str[i][0],str[i][1]);
    //printf("\n\n");
    }
    for(unsigned long i=0;i<MAX_ELEM_NUM;i++){
        dictAdd(d,str[i][0],str[i][1]);
    }
    printf("ending.....\n");
    //dictTablePrint(d);
    dictPrint(d);
}
void test4(){
    char* str[10][2];
    for(unsigned long i = 0;i<10;i++){
        for(unsigned long j = 0;j<2;j++){
            str[i][j] = malloc(sizeof("00000000"));
            strcpy(str[i][j],"00000000");
        }
    }
    for(unsigned long i = 0;i<10;i++){
        itoa(i,str[i][0]);
        itoa(i,str[i][1]);
    }

    printf("*****tesing dictReplace fuction*****\n");
    dict *d = dictCreate(NULL,NULL);
    dictType type;
    type.hashFunction = MyhashFunction;
    type.keyCompare = NULL;
    type.keyDestructor = NULL;
    type.keyDup = NULL;
    type.valDestructor = NULL;
    type.valDup = NULL;
    d->type = &type;

    for(unsigned long i=0;i<10;i++){
        dictAdd(d,str[i][0],str[i][1]);
    }
    dictTablePrint(d);
    char* a = "100";char* aa ="1000";
    dictReplace(d,a,aa);
    //dictTablePrint(d);
    char* bb ="10000";
    dictReplace(d,a,bb);
    //dictTablePrint(d);
    char* cc ="100000";
    dictReplace(d,a,cc);
    dictTablePrint(d);

    printf("\n\n\n*****tesing dictDelete fuction*****\n");

    printf("Origin table:\n");
    dictTablePrint(d);
    for(int i = 0;i<10;i++){
        dictDelete(d,str[i][0]);
        printf("after delete: %d\n",i);
        dictTablePrint(d);
    }
    dictPrint(d);

    for(unsigned long i=0;i<10;i++){
        dictAdd(d,str[i][0],str[i][1]);
    }
    printf("\n\n\n*****tesing dictRelease fuction*****\n");
    dictRelease(d);
    dictPrint(d);
}
void test5(){
    char* str[10][2];
    for(unsigned long i = 0;i<10;i++){
        for(unsigned long j = 0;j<2;j++){
            str[i][j] = malloc(sizeof("00000000"));
            strcpy(str[i][j],"00000000");
        }
    }
    for(unsigned long i = 0;i<10;i++){
        itoa(i,str[i][0]);
        itoa(i,str[i][1]);
    }

    printf("*****tesing dictNext fuction*****\n");
    dict *d = dictCreate(NULL,NULL);
    dictType type;
    type.hashFunction = MyhashFunction;
    type.keyCompare = NULL;
    type.keyDestructor = NULL;
    type.keyDup = NULL;
    type.valDestructor = NULL;
    type.valDup = NULL;
    d->type = &type;

    for(unsigned long i=0;i<10;i++){
        dictAdd(d,str[i][0],str[i][1]);
    }
    for(unsigned long i=0;i<10;i++){
        dictAdd(d,str[i][0],str[i][1]);
    }

    dictTablePrint(d);
    dictIterator* iter = dictGetSafeIterator(d);

    dictEntry* dd = dictNext(iter);
    while(dd){
        printf("%s\n",(char*)dd->key);
        dd = dictNext(iter);
    }

    printf("*****tesing dictFetchValue fuction*****\n");
    for(int i = 0;i<10;i++){
        void* a = dictFetchValue(d,str[i][0]);
        printf("%s\n",(char*)a);
    }

//    while(iter){
//        iter = dictNext(iter);
//        printf("%s\n",(char*)iter->entry->key);
//    }
}
void test6(){
    char* str[100][2];
    for(unsigned long i = 0;i<100;i++){
        for(unsigned long j = 0;j<2;j++){
            str[i][j] = malloc(sizeof("00000000"));
            strcpy(str[i][j],"00000000");
        }
    }
    for(unsigned long i = 0;i<100;i++){
        itoa(i,str[i][0]);
        itoa(i,str[i][1]);
    }

    printf("*****tesing dictGetRandomKey fuction*****\n");
    dict *d = dictCreate(NULL,NULL);
    dictType type;
    type.hashFunction = MyhashFunction;
    type.keyCompare = NULL;
    type.keyDestructor = NULL;
    type.keyDup = NULL;
    type.valDestructor = NULL;
    type.valDup = NULL;
    d->type = &type;

    for(unsigned long i=0;i<100;i++){
        dictAdd(d,str[i][0],str[i][1]);
    }

    int arr[100] = {0};

    for(int i = 0;i<100000000;i++){
        dictEntry* t =dictGetRandomKey(d);
        int a = atoi((char*)t->key);
        arr[a]++;
    }
    for(int i = 0;i<100;i++){
        printf("%d:%d\n",i,arr[i]);
    }
}
int main(){

    printf("**************MyRedis**************\n");
    //test1();
    //test2();
    //test3();
    //test4();
    //test5();
    //test6();

    return 0;
}
#endif // DICT_TEST_MAIN
