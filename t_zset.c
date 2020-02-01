#include"redis.h"
#include<stdlib.h>
#include<math.h>
#include"zmalloc.h"

static int zslLexValueGteMin(robj *value, zlexrangespec *spec);
static int zslLexValueLteMax(robj *value, zlexrangespec *spec);


/*
    创建一个层数为level的跳跃表节点
    将节点的成员变量对象设置为obj,分值设置为score
    返回值为新创建的跳跃表节点
*/

zskiplistNode *zslCreateNode(int level,double score,robj *obj){
    zskiplistNode *zn = zmalloc(sizeof(*zn)+(size_t)level*sizeof(struct zskiplistLevel));
    zn->score = score;
    zn->obj = obj;
    return zn;
}


/*
    创建并返回一个新的跳跃表
*/
zskiplist *zslCreate(void){
    int j;
    zskiplist *zsl;

    //分配空间
    zsl = zmalloc(sizeof(*zsl));

    //设置高度和起始层数

    zsl->level = 1;
    zsl->length = 0;

    //初始化表头节点

    zsl->header = zslCreateNode(ZSKIPLIST_MAXLEVEL,0,NULL);

    for(j = 0;j<ZSKIPLIST_MAXLEVEL;j++){
        zsl->header->level[j].forward  = NULL;
        zsl->header->level[j].span = 0;
    }
    zsl->header->backward = NULL;
    //zsl->score=?
    //zsl->obj=?
    zsl->tail = NULL;
    return zsl;
}


/*
    释放给定的跳跃表节点
*/
void zslFreeNode(zskiplistNode *node){
    decrRefCount(node->obj);
    zfree(node);
}


/*
    释放给定的跳跃表，以及表中的所有节点
*/
void zslFree(zskiplist *zsl){
    zskiplistNode *node = zsl->header->level[0].forward,*next;

    //释放表头节点
    zfree(zsl->header);

    //释放表中所有的节点
    while(node){
        next = node->level[0].forward;
        zslFreeNode(node);
        node = next;
    }

    //释放跳跃表结构
    zfree(zsl);
}


/*
    返回一个随机值用作新跳跃表节点的层数
*/
int zslRandomLevel(void){
    int level = 1;
    while((random()&0xFFFF)<(ZSKIPLIST_P *0xFFFF))
        level+=1;
    return (level<ZSKIPLIST_MAXLEVEL)?level:ZSKIPLIST_MAXLEVEL;
}

/*
    创建一个成员为obj，分值为score的新节点
    并将这个新节点插入到跳跃表zsl中
    函数的返回值为新节点
*/

zskiplistNode *zslInsert(zskiplist *zsl,double score,robj *obj){
    zskiplistNode *update[ZSKIPLIST_MAXLEVEL],*x;
    unsigned int rank[ZSKIPLIST_MAXLEVEL];//存储的是跨度累加
    int i,level;

    redisAssert(!isnan(score));

    //在各个层查找节点的插入位置
    x = zsl->header;
    for(i = zsl->level-1;i>=0;i--){
        rank[i] = i == (zsl->level-1)?0:rank[i+1];
        /* store rank that is crossed to reach the insert position */
        // 如果 i 不是 zsl->level-1 层
        // 那么 i 层的起始 rank 值为 i+1 层的 rank 值//启始时一定成立->rank[i] = 0
        // 各个层的 rank 值一层层累积
        // 最终 rank[0] 的值加一就是新节点的前置节点的排位
        // rank[0] 会在后面成为计算 span 值和 rank 值的基础
        while(x->level[i].forward &&
              //分数小
              (x->level[i].forward->score<score ||
                (x->level[i].forward->score == score &&
                 //分数相同但是对象在前
                 compareStringObjects(x->level[i].forward->obj,obj) < 0))){
                    //记录沿途跨越了多少个节点
                    rank[i] += x->level[i].span;

                    //移动至下一指针
                    x = x->level[i].forward;
        }
        //记录将要和新节点相连接的节点
        update[i] = x;
    }


    //获取一个随机值作为新节点的层数
    level = zslRandomLevel();

    //如果新节点的层数比比表中其他节点的层数都要打
    //那麽初始化表头节点中未使用的层，并将他们记录到update数组中
    //将来也指向新节点
    if(level > zsl->level){
        for(i = zsl->level;i<level;i++){
            rank[i] = 0;
            update[i] = zsl->header;
            update[i]->level[i].span = zsl->length;
        }

        //更新表中节点最大层数
        zsl->level = level;
    }
    //创建新节点
    x = zslCreateNode(level,score,obj);

    //将前面记录的指针指向新节点，并做相应的设置

    for(i = 0;i<level;i++){

        //将新节点的每层插入每层的链表
        x->level[i].forward = update[i]->level[i].forward;
        update[i]->level[i].forward = x;

        //重新计算span值

        x->level[i].span = update[i]->level[i].span - (rank[0] - rank[i]);

        update[i]->level[i].span = (rank[0] - rank[i]) + 1;
    }

    for(i = level;i<zsl->level;i++){
        update[i]->level[i].span++;
    }

    //设置新节点的后退指针
    x->backward = (update[0] == zsl->header)?NULL:update[0];
    if(x->level[0].forward)
        x->level[0].forward->backward = x;
    else
        zsl->tail = x;

    zsl->length++;

    return x;
}


/*
    打印跳跃表
*/

void zslPrint(zskiplist *zsl){
    if(zsl==NULL){
        printf("zsl pointer is NULL.\n");
        return;
    }

    int level = zsl->level;
    int length = zsl->length;

    if(length == 0){
        printf("length is 0,level is %d.\n",level);
        return;
    }

    int** arr = malloc(sizeof(int*)*level);
    for(int i = 0;i<length;i++){
        arr[i] = malloc(sizeof(int)*length);
    }

    for(int i = 0;i<level;i++){
        for(int j = 0;j<length;j++){
            arr[i][j] = 0;
        }
    }

    int i;
    int rank = 0;
    for(i = zsl->level-1;i>=0;i--){
        zskiplistNode *x = zsl->header,*xb=NULL;
        while(x){
            if(xb)
                rank+=xb->level[i].span;
            //printf("%.1lf(%d) ",x->score,rank);
            arr[i][rank-1] = x->score;
            xb = x;
            x = x->level[i].forward;



        }
        rank = 0;
    }
    printf("skip list struct:\n");
    for(int i = 0;i<level;i++){
        for(int j = 0;j<length;j++){
            printf("%02d ",arr[level-i-1][j]);
        }
        printf("\n");
    }
}

/*
    打印跳跃表的一些信息
*/
void zslPrintInfo(zskiplist *zsl){
    printf("skip list information:\n");
    if(zsl)
        printf("zsl:%p\n",zsl);
    else{
        printf("zsl:%p\n",zsl);
        return;
    }
    printf("header:%p\n",zsl->header);
    printf("tail:%p\n",zsl->tail);
    printf("length:%ld\n",zsl->length);
    printf("level:%d\n",zsl->level);
}

/*
    删除跳跃表的某个节点（内部函数)
*/

void zslDeleteNode(zskiplist *zsl,zskiplistNode *x,zskiplistNode **update){
    int i;

    //更新所有和被删除节点x有关的节点的指针，解除他们之间的关系
    for(i = 0;i<zsl->level;i++){
        if(update[i]->level[i].forward == x){
            update[i]->level[i].span += x->level[i].span-1;
            update[i]->level[i].forward = x->level[i].forward;
        }else{
            update[i]->level[i].span -= 1;
        }
    }

    //更新被删除节点x的前进和后退指针
    if(x->level[0].forward){
        x->level[0].forward->backward = x->backward;
    }else{
        zsl->tail = x->backward;
    }

    while(zsl->level>1 && zsl->header->level[zsl->level-1].forward == NULL)
        zsl->level--;

    zsl->length--;
}

/*
    跳跃表删除节点
*/

int zslDelete(zskiplist *zsl, double score, robj *obj){
    zskiplistNode *update[ZSKIPLIST_MAXLEVEL],*x;
    int i;

    x = zsl->header;

    for(i = zsl->level-1;i>=0;i--){
        while(x->level[i].forward &&
              (x->level[i].forward->score<score ||
                    (x->level[i].forward->score == score &&
                     compareStringObjects(x->level[i].forward->obj,obj)<0)))
                        x = x->level[i].forward;
        update[i] = x;
    }
    //update[i]的下一个节点可能需要更新
    //上面的操作视为了以lgn的速度找到x

    x = x->level[0].forward;
    if(x && score == x->score && equalStringObjects(x->obj,obj)){
        zslDeleteNode(zsl,x,update);
        zslFreeNode(x);
        return 1;//只有在这里才能合法退出
    }else{
        return 0;
    }
    return 0;
}


/*
    两个两检查端点的函数
*/
static int zslValueGteMin(double value, zrangespec *spec) {
    return spec->minex ? (value > spec->min) : (value >= spec->min);
}
static int zslValueLteMax(double value, zrangespec *spec) {
    return spec->maxex ? (value < spec->max) : (value <= spec->max);
}

/*
    给定的分值是否在跳跃表的范围内
*/

int zslIsInRange(zskiplist *zsl,zrangespec *range){
    zskiplistNode *x;

    /* Test for ranges that will always be empty. */
    // 先排除总为空的范围值
    if (range->min > range->max ||
            (range->min == range->max && (range->minex || range->maxex)))
        return 0;

    // 检查最大分值
    x = zsl->tail;
    if (x == NULL || !zslValueGteMin(x->score,range))
        return 0;

    // 检查最小分值
    x = zsl->header->level[0].forward;
    if (x == NULL || !zslValueLteMax(x->score,range))
        return 0;

    return 1;
}


/*
    获得在范围内的第一个值
*/

zskiplistNode *zslFirstInRange(zskiplist *zsl, zrangespec *range){
    zskiplistNode *x;
    int i;

    if(!zslIsInRange(zsl,range)) return NULL;

    //遍历跳跃表，查找符合范围max项的节点
    x = zsl->header;
    for(i = zsl->level-1;i>=0;i--){
        while(x->level[i].forward &&
              !zslValueGteMin(x->level[i].forward->score,range))
                x = x->level[i].forward;
    }

    x = x->level[0].forward;
    redisAssert(x != NULL);


    if(!zslValueLteMax(x->score,range)) return NULL;

    return x;

}

/*
    获得在范围内的最后一个值
*/

zskiplistNode *zslLastInRange(zskiplist *zsl, zrangespec *range){
    zskiplistNode *x;
    int i;
    if (!zslIsInRange(zsl,range)) return NULL;

    x = zsl->header;
    for (i = zsl->level-1; i >= 0; i--) {
        while (x->level[i].forward &&
            zslValueLteMax(x->level[i].forward->score,range))
                x = x->level[i].forward;
    }
    redisAssert(x != NULL);

    if (!zslValueGteMin(x->score,range)) return NULL;

    return x;
}

/*
    获取某个节点的rank值
*/

unsigned long zslGetRank(zskiplist *zsl, double score, robj *o){
    zskiplistNode *x;
    unsigned long rank = 0;
    int i;

    x = zsl->header;
    for (i = zsl->level-1; i >= 0; i--) {

        while (x->level[i].forward &&
            (x->level[i].forward->score < score ||
                (x->level[i].forward->score == score &&
                compareStringObjects(x->level[i].forward->obj,o) <= 0))) {
            rank += x->level[i].span;
            x = x->level[i].forward;
        }
        if (x->obj && equalStringObjects(x->obj,o)) {
            return rank;
        }
    }
    return 0;
}


/*
    根据rank查找元素
*/

zskiplistNode* zslGetElementByRank(zskiplist *zsl,unsigned long rank){
    zskiplistNode *x;
    unsigned long traversed = 0;

    int i;

    x = zsl->header;

    for(i = zsl->level - 1;i>=0;i--){
        while(x->level[i].forward && (traversed + x->level[i].span)<=rank){
            traversed+=x->level[i].span;
            x = x->level[i].forward;
        }

        if(traversed == rank)
            return x;
    }
    return NULL;
}


/*
    zslDeleteRangeBy...
    以下三个函数没有仔细看
*/


unsigned long zslDeleteRangeByScore(zskiplist *zsl, zrangespec *range, dict *dict) {
    zskiplistNode *update[ZSKIPLIST_MAXLEVEL], *x;
    unsigned long removed = 0;
    int i;

    // 记录所有和被删除节点（们）有关的节点
    // T_wrost = O(N) , T_avg = O(log N)
    x = zsl->header;
    for (i = zsl->level-1; i >= 0; i--) {
        while (x->level[i].forward && (range->minex ?
            x->level[i].forward->score <= range->min :
            x->level[i].forward->score < range->min))
                x = x->level[i].forward;
        update[i] = x;
    }

    /* Current node is the last with score < or <= min. */
    // 定位到给定范围开始的第一个节点
    x = x->level[0].forward;

    /* Delete nodes while in range. */
    // 删除范围中的所有节点
    // T = O(N)
    while (x &&
           (range->maxex ? x->score < range->max : x->score <= range->max))
    {
        // 记录下个节点的指针
        zskiplistNode *next = x->level[0].forward;
        zslDeleteNode(zsl,x,update);
        dictDelete(dict,x->obj);
        zslFreeNode(x);
        removed++;
        x = next;
    }
    return removed;
}

unsigned long zslDeleteRangeByLex(zskiplist *zsl, zlexrangespec *range, dict *dict) {
    zskiplistNode *update[ZSKIPLIST_MAXLEVEL], *x;
    unsigned long removed = 0;
    int i;


    x = zsl->header;
    for (i = zsl->level-1; i >= 0; i--) {
        while (x->level[i].forward &&
            !zslLexValueGteMin(x->level[i].forward->obj,range))
                x = x->level[i].forward;
        update[i] = x;
    }

    /* Current node is the last with score < or <= min. */
    x = x->level[0].forward;

    /* Delete nodes while in range. */
    while (x && zslLexValueLteMax(x->obj,range)) {
        zskiplistNode *next = x->level[0].forward;

        // 从跳跃表中删除当前节点
        zslDeleteNode(zsl,x,update);
        // 从字典中删除当前节点
        dictDelete(dict,x->obj);
        // 释放当前跳跃表节点的结构
        zslFreeNode(x);

        // 增加删除计数器
        removed++;

        // 继续处理下个节点
        x = next;
    }

    // 返回被删除节点的数量
    return removed;
}

unsigned long zslDeleteRangeByRank(zskiplist *zsl, unsigned int start, unsigned int end, dict *dict) {
    zskiplistNode *update[ZSKIPLIST_MAXLEVEL], *x;
    unsigned long traversed = 0, removed = 0;
    int i;

    // 沿着前进指针移动到指定排位的起始位置，并记录所有沿途指针
    // T_wrost = O(N) , T_avg = O(log N)
    x = zsl->header;
    for (i = zsl->level-1; i >= 0; i--) {
        while (x->level[i].forward && (traversed + x->level[i].span) < start) {
            traversed += x->level[i].span;
            x = x->level[i].forward;
        }
        update[i] = x;
    }

    // 移动到排位的起始的第一个节点
    traversed++;
    x = x->level[0].forward;
    // 删除所有在给定排位范围内的节点
    // T = O(N)
    while (x && traversed <= end) {

        // 记录下一节点的指针
        zskiplistNode *next = x->level[0].forward;

        // 从跳跃表中删除节点
        zslDeleteNode(zsl,x,update);
        // 从字典中删除节点
        dictDelete(dict,x->obj);
        // 释放节点结构
        zslFreeNode(x);

        // 为删除计数器增一
        removed++;

        // 为排位计数器增一
        traversed++;

        // 处理下个节点
        x = next;
    }

    // 返回被删除节点的数量
    return removed;
}



int compareStringObjectsForLexRange(robj *a, robj *b) {
    if (a == b) return 0; /* This makes sure that we handle inf,inf and
                             -inf,-inf ASAP. One special case less. */
    if (a == shared.minstring || b == shared.maxstring) return -1;
    if (a == shared.maxstring || b == shared.minstring) return 1;
    return compareStringObjects(a,b);
}
static int zslLexValueGteMin(robj *value, zlexrangespec *spec) {
    return spec->minex ?
        (compareStringObjectsForLexRange(value,spec->min) > 0) :
        (compareStringObjectsForLexRange(value,spec->min) >= 0);
}
static int zslLexValueLteMax(robj *value, zlexrangespec *spec) {
    return spec->maxex ?
        (compareStringObjectsForLexRange(value,spec->max) < 0) :
        (compareStringObjectsForLexRange(value,spec->max) <= 0);
}


