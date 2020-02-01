
#include"redis.h"
#include<stdio.h>
#include<stdlib.h>
#include<math.h>
#include"zmalloc.h"
#include<string.h>




/*
    释放字符串对象
*/
void freeStringObject(robj *o){
    if(o->encoding == REDIS_ENCODING_RAW){
        sdsfree(o->ptr);
    }
}

/*
    释放列表对象
*/
void freeListObject(robj *o){
    switch(o->encoding){
    case REDIS_ENCODING_LINKEDLIST:
        listRelease((list*)o->ptr);
        break;
    case REDIS_ENCODING_ZIPLIST:
        zfree(o->ptr);
        break;
    default:
        redisPanic("Unknown list encoding type");
    }
}

/*
    释放集合对象
*/
void freeSetObject(robj *o){
    switch(o->encoding){
    case REDIS_ENCODING_HT:
        dictRelease((dict*)o->ptr);
        break;
    case REDIS_ENCODING_INTSET:
        zfree(o->ptr);
        break;
    default:
        redisPanic("Unknown list encoding type");

    }
}

/*
    释放有序集合对象//暂时未完成
*/
void freeZsetObject(robj *o){
//    zset *zs;
//    switch(o->encoding){
//    case REDIS_ENCODING_SKIPLIST:
//        zs = o->ptr;
//        dictRelease(zs->dict);
//        zslFree(zs->zsl);
//        zfree(zs);
//        break;
//    case REDIS_ENCODING_ZIPLIST:
//        zfree(o->ptr);
//        break;
//    default:
//        redisPanic("Unknown list encoding type");
//    }
}


/*
    释放hash对象
*/
void freeHashObject(robj *o){
    switch(o->encoding){
    case REDIS_ENCODING_HT:
        dictRelease((dict*)o->ptr);
        break;
    case REDIS_ENCODING_ZIPLIST:
        zfree(o->ptr);
        break;
    default:
        redisPanic("Unknown list encoding type");
    }
}





/*
    为对象的引用计数减一
*/
void decrRefCount(robj *o){
    if(o->refcount <= 0)
        redisPanic("decrRefCount against refcount <= 0");

    //释放对象
    if(o->refcount == 1){
        switch(o->type){
        case REDIS_STRING:freeStringObject(o);break;
        case REDIS_LIST:freeListObject(o);break;
        case REDIS_SET:freeSetObject(o);break;
        case REDIS_ZSET:freeZsetObject(o);break;
        case REDIS_HASH:freeHashObject(o);break;
        default:redisPanic("Unknown object type");break;
        }
        zfree(o);
    }else{
        o->refcount--;
    }
}

//对象的比较相关函数

#define REDIS_COMPARE_BINARY (1<<0)
#define REDIS_COMPARE_COLL (1<<1)

int compareStringObjectsWithFlags(robj *a,robj *b,int flags){
    //确认是是两个String类型的对像
    redisAssertWithInfo(NULL,a,a->type == REDIS_STRING && b->type == REDIS_STRING);

    char bufa[128],bufb[128],*astr,*bstr;
    size_t alen,blen,minlen;

    //同一个对象
    if(a == b) return 0;
    if(sdsEncodedObject(a)){
        astr = a->ptr;
        alen = sdslen(astr);
    }else{
        alen  = ll2string(bufa,sizeof(bufa),(long)a->ptr);
        astr = bufa;
    }
    if(sdsEncodedObject(b)){
        bstr = b->ptr;
        blen = sdslen(bstr);
    }else{
        blen  = ll2string(bufb,sizeof(bufb),(long)b->ptr);
        bstr = bufb;
    }

    //全部转换为支付串
    //对比
    if(flags & REDIS_COMPARE_COLL){
        return strcoll(astr,bstr);//和具体的编码格式有关
    }else{
        //普通的字符串比较
        int cmp;
        minlen = (alen<blen)?alen:blen;
        cmp = memcmp(astr,bstr,minlen);
        if(cmp == 0)return alen - blen;
        return cmp;
    }
}
int compareStringObjects(robj *a, robj *b){
    return compareStringObjectsWithFlags(a,b,REDIS_COMPARE_BINARY);
}
int collateStringObjects(robj *a, robj *b){
    return compareStringObjectsWithFlags(a,b,REDIS_COMPARE_COLL);
}
int equalStringObjects(robj *a, robj *b){
    if(a->encoding == REDIS_ENCODING_INT &&
       b->encoding == REDIS_ENCODING_INT){
        return a->ptr == b->ptr;
       }
    else
        return compareStringObjects(a,b) == 0;
}
