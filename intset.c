#include"intset.h"
#include<stdio.h>
#include<string.h>
#include"zmalloc.h"
#include"endianconv.h"
#include<stdint.h>
#include"redis.h"


/*
    intset的编码方式
*/
#define INTSET_ENC_INT16 (sizeof(int16_t))
#define INTSET_ENC_INT32 (sizeof(int32_t))
#define INTSET_ENC_INT64 (sizeof(int64_t))


/*
    返回适用于传入值v的编码方式
*/
static uint8_t _intsetValueEncoding(int64_t v){
    if(v<INT32_MIN ||v>INT32_MAX)
        return INTSET_ENC_INT64;
    else if(v<INT16_MIN ||v>INT16_MAX)
        return INTSET_ENC_INT32;
    else
        return INTSET_ENC_INT16;
    return 0;
}


/*
    根据给定的编码方式enc,返回集合的底层数组在pos索引上的元素
*/
static int64_t _intsetGetEncoded(intset *is,int pos,uint8_t enc){
    int64_t v64;
    int32_t v32;
    int16_t v16;

    if(enc == INTSET_ENC_INT64){
        memcpy(&v64,((int64_t*)is->contents)+pos,sizeof(v64));
        memrev64ifbe(&v64);
        return v64;
    }else if(enc == INTSET_ENC_INT32){
        memcpy(&v32,((int32_t*)is->contents)+pos,sizeof(v32));
        memrev32ifbe(&v32);
        return v32;
    }else{
        memcpy(&v16,((int16_t*)is->contents)+pos,sizeof(v16));
        memrev16ifbe(&v16);
        return v16;
    }
}


/*
    根据集合的编码方式，返回底层数组在pos索引上的值
*/

static int64_t _intsetGet(intset *is,int pos){
    return _intsetGetEncoded(is,pos,intrev32ifbe(is->encoding));
}


/*
    根据集合的编码方式,将底层数组在pos位置上的值设为value
*/

static void _intsetSet(intset *is,int pos,int64_t value){

    //取出集合的编码方式
    uint32_t encoding = intrev32ifbe(is->encoding);
    if(encoding == INTSET_ENC_INT64){
        ((int64_t*)is->contents)[pos] = value;
        memrev64ifbe(((int64_t*)is->contents)+pos);
    }else if(encoding == INTSET_ENC_INT32){
        ((int32_t*)is->contents)[pos] = value;
        memrev32ifbe(((int32_t*)is->contents)+pos);
    }else{
        ((int16_t*)is->contents)[pos] = value;
        memrev16ifbe(((int16_t*)is->contents)+pos);
    }
}

/*
    取出集合底层数组值`

*/

uint8_t intsetGet(intset *is,uint32_t pos,int64_t *value){

    if(pos < intrev32ifbe(is->length)){
        *value = _intsetGet(is,pos);
        return 1;
    }
    return 0;


}


/*
    创建并返回一个空整数集合

*/
intset *intsetNew(void){
    //为整数集合结构分配空间
    intset *is = zmalloc(sizeof(intset));

    //设置初始编码
    is->encoding = intrev32ifbe(INTSET_ENC_INT16);

    //初始化元素数量
    is->length = 0;
    return is;
}

/*
    调整集合的内存空间大小

    如果调整后的大小要比集合原来的大小要大
    那麽集合中原有元素的值不会被改变
*/

static intset *intsetResize(intset *is,uint32_t len){

    uint32_t size = len * intrev32ifbe(is->encoding);

    is = zrealloc(is,sizeof(intset)+size);

    return is;
}

/*
    根据值value所使用的编码方式，对整数集合的编码进行升级
    并将值value添加到升级之后的整数集合中
    返回添加新元素之后的整数集合
*/

static intset *intsetUpgradeAndAdd(intset *is,int64_t value){
    //当前的编码方式
    uint8_t curenc = intrev32ifbe(is->encoding);

    //新值所需的编码的编码方式
    uint8_t newenc = _intsetValueEncoding(value);


    //当前集合的元素数量
    int length = intrev32ifbe(is->length);

    //value只会添加到最左或者最右

    int prepend = value<0?1:0;
    //prepend为1表示左


    is->encoding = intrev32ifbe(newenc);

    //根据新编码对集合进行空间调整

    is = intsetResize(is,intrev32ifbe(is->length)+1);

    while(length--)
        _intsetSet(is,length+prepend,_intsetGetEncoded(is,length,curenc));

    if(prepend)
        _intsetSet(is,0,value);
    else
        _intsetSet(is,intrev32ifbe(is->length),value);

    //update the element number of intset
    is->length = intrev32ifbe(intrev32ifbe(is->length)+1);

    return is;
}

/*
    查找value 返回bool与position
*/

static uint8_t intsetSearch(intset *is,int64_t value,uint32_t *pos){
    int min = 0,max = intrev32ifbe(is->length)-1,mid = -1;
    int64_t cur = -1;

    if(intrev32ifbe(is->length)==0){
    //intset为空
        if(pos)
            *pos = 0;
        return 0;
    }else{
        //arr是一个有序数组如果大于arr[lenghth-1]小于arr[0]那麽一定不在集合中
        if(value > _intsetGet(is,intrev32ifbe(is->length)-1)){
            if(pos)*pos = intrev32ifbe(is->length);
            return 0;
        }else if(value < _intsetGet(is,0)){
            if(pos)
                *pos = 0;
            return 0;
        }
    }

    //二分查找
    while(max>=min){
        mid = (min+max)/2;
        cur = _intsetGet(is,mid);
        if(value > cur){
            min = mid+1;
        }else if(value < cur){
            max = mid-1;
        }else{
            break;
        }
    }

    //检查是否找到了value
    if(value == cur){
        if(pos)*pos = mid;
        return 1;
    }else{
        if(pos) *pos = min;
        return 0;
    }
}

/*
    bit级别的移动元素
*/

static void intsetMoveTail(intset *is,uint32_t from,uint32_t to){

    void *src,*dst;

    //要移动的元素个数
    uint32_t bytes = intrev32ifbe(is->length) - from;

    //集合的编码方式
    uint32_t encoding = intrev32ifbe(is->encoding);

    //根据编码的不同
    if(encoding == INTSET_ENC_INT64){
        src = (int64_t*)is->contents+from;
        dst = (int64_t*)is->contents+to;
        bytes *= sizeof(int64_t);
    }else if(encoding == INTSET_ENC_INT32){
        src = (int32_t*)is->contents+from;
        dst = (int32_t*)is->contents+to;
        bytes *= sizeof(int32_t);
    }else{
        src = (int16_t*)is->contents+from;
        dst = (int16_t*)is->contents+to;
        bytes *= sizeof(int16_t);
    }

    memmove(dst,src,bytes);
}

/*
    尝试将元素value添加到整数集合中

    success返回是否成功
*/

intset *intsetAdd(intset *is,int64_t value,uint8_t *success){

    //计算编码value所需的长度
    uint8_t valuenc = _intsetValueEncoding(value);
    uint32_t pos;

    if(success)
        *success = 1;

    //need to change code of arr
    if(valuenc > intrev32ifbe(is->encoding)){
        return intsetUpgradeAndAdd(is,value);
    }else{
        //don't need to change code of arr

        if(intsetSearch(is,value,&pos)){
            if(success) *success = 0;
            return is;
        }

        is = intsetResize(is,intrev32ifbe(is->length)+1);

        if(pos < intrev32ifbe(is->length))
            intsetMoveTail(is,pos,pos+1);
    }

    _intsetSet(is,pos,value);
    is->length = intrev32ifbe(intrev32ifbe(is->length)+1);


    return is;
}

/*
    从整数集合中删除元素
*/
intset *intsetRemove(intset *is,int64_t value,int *success){

    //根据大小确定编码方式
    uint8_t valenc = _intsetValueEncoding(value);
    uint32_t pos;

    if(success) *success = 0;

    if(valenc <= intrev32ifbe(is->encoding)&&intsetSearch(is,value,&pos)){
        //编码方式小且找到元素
        uint32_t len = intrev32ifbe(is->length);

        if(success) *success = 1;

        if(pos<(len-1))
            intsetMoveTail(is,pos+1,pos);

        is = intsetResize(is,len-1);

        is->length = intrev32ifbe(len-1);

    }
    return is;
}

/*
    返回value是否存在集合中s
*/

uint8_t intsetFind(intset* is,int64_t value){
    uint8_t valenc = _intsetValueEncoding(value);

    return valenc <= intrev32ifbe(is->encoding)&&intsetSearch(is,value,NULL);
}

/*
    从集合中随机返回一个元素
*/

int64_t intsetRandom(intset *is){
    if(is->length==0)
        redisAssert(0);
    return _intsetGet(is,rand()%intrev32ifbe(is->length));
}

/*
    返回整数集合现有的元素个数
*/

uint32_t intsetLen(intset *is){
    return intrev32ifbe(is->length);
}

/*
    返回整数集合现在占有的字节数量
*/

size_t intsetBlobLen(intset *is){
    return sizeof(intset)+intrev32ifbe(is->length)*intrev32ifbe(is->encoding);
}


/*
    打印intset信息
    不包括元素
*/

void intsetInformation(intset* is){
    printf("---intset information---\n");
    printf("encoding:%d\n",(int)is->encoding);
    printf("length:%d\n",(int)is->length);
}

/*
    打印intset信息
    包括元素
*/

void intsetPrint(intset* is){
    printf("---intset print---\n");
    printf("encoding:%d\n",(int)is->encoding);
    printf("length:%d\n",(int)is->length);
    printf("element:\n");
    for(uint32_t i = 0;i<is->length;i++){
        int64_t value;
        value = _intsetGet(is,i);
        printf("%lld ",(long long)value);
        if((i+1)%10==0)
            printf("\n");
    }
}


#ifdef INTSET_TEST_MAIN

#include<stdio.h>
#include"zmalloc.h"
#include"intset.h"
#include<stdlib.h>
void test1(){
    printf("*********testing intsetAdd function*********\n");
    intset* is = intsetNew();

    //zfree(is);
    //intsetPrint(is);
    uint8_t success;
    for(int i = 1;i<=100;i++){
        //printf("%d\n",i);
        int a = rand()%10;
        int num = rand()%1000;
        if(a>5)
            is = intsetAdd(is,num*-1,&success);
        else
            is = intsetAdd(is,num,&success);
    }
    for(int i = 1;i<=100;i++){
        //printf("%d\n",i);
        long long a = rand()%10;
        long long num = rand()%INT16_MAX + INT16_MAX;
        if(a>5)
            is = intsetAdd(is,num*-1,&success);
        else
            is = intsetAdd(is,num,&success);
    }
    for(int i = 1;i<=100;i++){
        //printf("%d\n",i);
        long long a = rand()%10;
        long long num = rand()%INT32_MAX + INT32_MAX;
        if(a>5)
            is = intsetAdd(is,num*-1,&success);
        else
            is = intsetAdd(is,num,&success);
    }
}
void test2(){
    printf("*********testing intsetReMove function*********\n");
    intset* is = intsetNew();
    uint8_t success;
    for(int i = -100;i<100;i++){
        is = intsetAdd(is,i,&success);
    }
    //intsetPrint(is);
    int success1;
    for(int i = 0;i<100;i++){
        printf("%d\n",i);
        if(i==27)
            is = intsetRemove(is,i,&success1);
        is = intsetRemove(is,i,&success1);
        intsetInformation(is);
    }

    intsetPrint(is);
}

void test3(){
#define TEST_NUM 10000000
    printf("*********testing intsetFind function*********\n");
    intset* is = intsetNew();
    uint8_t success;
    for(int i = -TEST_NUM;i<TEST_NUM;i++){
        is = intsetAdd(is,i,&success);
    }
    int sum = 0;
    for(int i = -TEST_NUM;i<TEST_NUM;i++){
         sum += intsetFind(is,i);
    }

    printf("%d/%d\n",sum,2*TEST_NUM);
}

void test4(){

    printf("*********testing intsetRandom function*********\n");
    intset* is = intsetNew();
    intsetRandom(is);
    uint8_t success;
    for(int i = -10;i<10;i++){
        is = intsetAdd(is,i,&success);
    }
    for(int i = 0;i<10;i++){
        int64_t a = intsetRandom(is);
        printf("%lld\n",(long long)a);
    }
}

void test5(){

    printf("*********testing intsetLen function*********\n");
    intset* is = intsetNew();
    //intsetRandom(is);
    uint8_t success;
    for(int i = -TEST_NUM;i<TEST_NUM;i++){
        is = intsetAdd(is,i,&success);
    }
    printf("elem num:%u\nintset size:%zu",intsetLen(is),intsetBlobLen(is));
}
int main(){

    //test1();
    //test2();
    //test3();
    test5();
    return 0;
}

#endif // INTSET_TEST_MAIN
