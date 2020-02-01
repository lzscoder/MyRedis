//https://blog.csdn.net/weixin_33895695/article/details/88882927
#include<stdio.h>
#include"redis.h"


#define TEST_NODE_NUM 1000000

void test1(){
    printf("-------------testing zslInsert function---------------\n");
    zskiplist* zsl = zslCreate();
    robj* r = malloc(sizeof(*r));
    //最多只能插入了千万个数据
    for(int i = 1;i<10000000;i++){
        r->refcount = 1;
        r->encoding = 5;
        r->lru = 1;
        r->ptr = malloc(sizeof(int));
        r->type = 1;
        zslInsert(zsl,i,r);
    }
    printf("ending....:\n");
    zslPrintInfo(zsl);
}
void test2(){
    printf("-------------testing zslDelete function---------------\n");
    zskiplist* zsl = zslCreate();
    robj* r[20];
    for(int i =0;i<20;i++){
        r[i] = malloc(sizeof(robj));
    }
    //最多只能插入了千万个数据
    int arr[20];
    for(int i = 0;i<20;i++){
        r[i]->refcount = 2;
        r[i]->encoding = 5;
        r[i]->lru = 1;
        r[i]->ptr = malloc(sizeof(int));
        r[i]->type = 1;
        arr[i] = rand()%20+1;
        zslInsert(zsl,arr[i],r[i]);
    }
    printf("ending....:\n");
    zslPrintInfo(zsl);
    zslPrint(zsl);

    for(int i = 0;i<20;i++){
        printf("after delete %d:\n",arr[i]);
        zslDelete(zsl,arr[i],r[i]);
        zslPrint(zsl);
    }
    zslPrintInfo(zsl);


    for(int i =0;i<20;i++){
        r[i] = malloc(sizeof(robj));
    }
    //最多只能插入了千万个数据
    for(int i = 0;i<20;i++){
        r[i]->refcount = 2;
        r[i]->encoding = 5;
        r[i]->lru = 1;
        r[i]->ptr = malloc(sizeof(int));
        r[i]->type = 1;
        arr[i] = rand()%20+1;
        zslInsert(zsl,arr[i],r[i]);
    }

    printf("ending....:\n");
    zslPrintInfo(zsl);
    zslPrint(zsl);

    for(int i = 0;i<20;i++){
        printf("after delete %d:\n",arr[i]);
        zslDelete(zsl,arr[i],r[i]);
        zslPrint(zsl);
    }
    zslPrintInfo(zsl);

}
void test3(){
    printf("-------------testing zslFirstInRange function---------------\n");
    zskiplist* zsl = zslCreate();
    robj* r[20];
    for(int i =0;i<20;i++){
        r[i] = malloc(sizeof(robj));
    }
    //最多只能插入了千万个数据
    int arr[20];
    for(int i = 0;i<20;i++){
        r[i]->refcount = 2;
        r[i]->encoding = 5;
        r[i]->lru = 1;
        r[i]->ptr = malloc(sizeof(int));
        r[i]->type = 1;
        arr[i] = rand()%20+1;
        zslInsert(zsl,arr[i],r[i]);
    }

    zslPrint(zsl);
    zrangespec* range = malloc(sizeof(zrangespec));
    range->max = 16;
    range->min =4;
    range->maxex = 1;
    range->minex = 1;
    zskiplistNode* f = zslFirstInRange(zsl,range);
    zskiplistNode* l = zslLastInRange(zsl,range);
    if(f&&l)
        printf("f:%.2lf l:%.2lf\n",f->score,l->score);
}
void test4(){

    printf("-------------testing zslGetRank function---------------\n");
    zskiplist* zsl = zslCreate();
    robj* r[TEST_NODE_NUM];
    for(int i =0;i<TEST_NODE_NUM;i++){
        r[i] = malloc(sizeof(robj));
    }
    //最多只能插入了千万个数据
    int arr[TEST_NODE_NUM];
    for(int i = 0;i<TEST_NODE_NUM;i++){
        r[i]->refcount = 2;
        r[i]->encoding = 5;
        r[i]->lru = 1;
        r[i]->ptr = malloc(sizeof(int));
        r[i]->type = 1;
        arr[i] = rand()%TEST_NODE_NUM+1;
        zslInsert(zsl,arr[i],r[i]);
    }

    //zslPrint(zsl);

    int k=0,j=0;
    zskiplistNode *node = NULL;
    unsigned long result;
    for(int i = 0;i<TEST_NODE_NUM;i++){
        result = zslGetRank(zsl,arr[i],r[i]);
        node = zslGetElementByRank(zsl,result);
        if(node->obj==r[i]&&node->score == arr[i])
            k++;
        j++;
    }


    printf("right rate:%d/%d\n",k,j);

}
void test5(){

}
int main(){

    printf("***************Myredis**************\n");
    //test1();
    //test2();
    //test3();
    //test4();
    //100w个数据就会崩
    zskiplist* zsl = zslCreate();
    robj* r[TEST_NODE_NUM];
    for(int i =0;i<TEST_NODE_NUM;i++){
        r[i] = malloc(sizeof(robj));
    }
    return 0;
}
