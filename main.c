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
//    test1();
//    test2();
//    test3();
//    test5();
//    test6();
//    test7();
//    test8();
//    test9();
    return 0;
}

