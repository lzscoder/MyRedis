#include"adlist.h"
#include"zmalloc.h"
#include<stdio.h>

//创建一个链表
list* listCreate(){
    list *l;
    l = (list*)zmalloc(sizeof(*l));

    if(!l)
        return NULL;

    l->head = l->tail = NULL;

    l->len = 0;

    l->dup = NULL;
    l->free = NULL;
    l->match = NULL;

    return l;
}


//释放整个链表
void listRelease(list *l){
    unsigned long len;

    listNode *current,*next;

    current = l->head;

    len = l->len;

    for(int i = 0;i<len;i++){

        next = current->next;

        if(l->free) l->free(current->value);

        zfree(current);

        current = next;
    }
    zfree(l);
}

//增加节点到表头
list *listAddNodeHead(list* l,void *value){
    listNode* node;

    if((node = zmalloc(sizeof(*node)))==NULL)
        return NULL;

    node->value = value;

    if(l->len==0){
        l->head = l->tail = node;
        node->prev = node->next = NULL;
    }else{
        node->prev = NULL;
        node->next = l->head;
        l->head->prev = node;
        l->head = node;
    }

    l->len++;
    return l;
}
//
//添加一个节点到list的尾部

list *listAddNodeTail(list *l,void *value){

    listNode* node;

    if((node = zmalloc(sizeof(*node)))==NULL)
        return NULL;

    node->value = value;

    if(l->len==0){
        l->head = l->tail = node;
        node->prev = node->next = NULL;
    }else{
        node->next = NULL;
        node->prev = l->tail;
        l->tail->next = node;
        l->tail = node;
    }

    l->len++;
    return l;

}

//插入到列表中的某节点之前或者之后
list *listInsertNode(list *l,listNode *old_node,void *value,int after){
    listNode *node;

    if((node = zmalloc(sizeof(*node))) == NULL)
       return NULL;

    node->value = value;

    if(after){
        node->prev = old_node;
        node->next = old_node->next;

        //old_node节点为末尾节点
        if(l->tail == old_node){
            l->tail = node;
            old_node->next = node;
        }
        else{
            old_node->next = node;
            node->next->prev = node;
        }
    }else{
        node->next = old_node;
        node->prev = old_node->prev;

        //old_node为首节点
        if(l->head == old_node){
            l->head = node;
            old_node->prev = node;
        }else{
            old_node->prev = node;
            node->prev->next = node;
        }
    }
    l->len++;

    return l;

}

//删除list中给定的节点
void listDelNode(list *l,listNode *node){

    if(node->prev)
        node->prev->next = node->next;
    else
        l->head = node->next;


    if(node->next)
        node->next->prev = node->prev;
    else
        l->tail = node->prev;

    if(l->free) l->free(node->value);

    zfree(node);

    l->len--;
}


//给链表创建一个迭代器
listIter *listGetIterator(list *l,int direction){
    listIter *iter;
    if((iter = zmalloc(sizeof(*iter)))==NULL) return NULL;

    if(direction == AL_START_HEAD)
        iter->next = l->head;
    else
        iter->next = l->tail;

    iter->direction = direction;

    return iter;
}

//释放迭代器
void listReleaseIterator(listIter *iter) {
    zfree(iter);
}
//迭代器的返回并移动
listNode *listNext(listIter *iter){
    listNode *current = iter->next;

    if(current != NULL){
        if(iter->direction == AL_START_HEAD)
            iter->next = current->next;
        else
            iter->next = current->prev;
    }

    return current;
}

//复制整个链表
list *listDup(list *orig){
    list * l;
    listIter *iter;
    listNode *node;

    if((l = listCreate())==NULL)
        return NULL;
    l->dup = orig->dup;
    l->free = orig->free;
    l->match = orig->match;

    iter = listGetIterator(orig,AL_START_HEAD);

    while((node = listNext(iter))!=NULL){

        void* value;

        if(l->dup){
            value = l->dup(node->value);
            if(value == NULL){
                listRelease(l);
                listReleaseIterator(iter);
                return NULL;
            }
        }else{
            value = node->value;
        }

        if(listAddNodeTail(l,value) == NULL){
            listRelease(l);
            listReleaseIterator(iter);
            return NULL;
        }
    }

    listReleaseIterator(iter);

    return l;

}

//查找list中值和key匹配的节点
listNode *listSearchKey(list *list, void *key){
    listIter *iter;
    listNode *node;

    //迭代整个链表
    iter = listGetIterator(list,AL_START_HEAD);

    while((node=listNext(iter))!=NULL){

        if(list->match){
            if(list->match(node->value,key)){
                listReleaseIterator(iter);
                return node;
            }
        }else{
            if(key == node->value){
                listReleaseIterator(iter);
                return node;
            }
        }
    }
    listReleaseIterator(iter);
    return NULL;
}

listNode *listIndex(list *l,long index){
    listNode *n;
    //index都是从0开始计算的
    if(index<0){
        index = (-1*index)-1;
        n = l->tail;
        while(index-- && n) n = n->prev;
    }else{
        n = l->head;
        while(index-- && n) n = n->next;
    }
    return n;
}
//重新设置迭代器
void listRewind(list *l,listIter *li){
    li->next = l->head;
    li->direction = AL_START_HEAD;
}
//反向设置迭代器
void listRewindTail(list *l,listIter *li){
    li->next = l->tail;
    li->direction = AL_START_TAIL;
}

//把表尾写到表头
void listRotate(list *l){

    listNode *tail = l->tail;
    if(listLength(l)<=1) return;

    l->tail = l->tail->prev;
    l->tail->next = NULL;

    l->head->prev = tail;
    tail->prev = NULL;
    tail->next = l->head;
    l->head = tail;
}

//打印链表
void listPrint(list* l){

    listNode* node = l->head;
    printf("list length:%ld\n",l->len);
    int i = 1;
    while(node){
        printf("%dth node:%d\n",i++,*((int*)node->value));
        node = node->next;
    }
}

#ifdef DS_TEST_MAIN
# include <stdio.h>
#include"sds.h"
#include<stdlib.h>
#include<string.h>
#include"adlist.h"

void test1(){

    //list *listCreate(void);
    printf("**********testing listCreate function************\n");
    list *l = listCreate();
    printf("l:%p\nl->head:%p\nl->tail:%p\nl->match():%p\nl->dup():%p\nl->free():%p\nl->len:%ld\n",l,l->head,l->tail,l->match,l->dup,l->free,l->len);

    //list *listAddNodeHead(list *list,void *value);
    printf("**********testing listAddNodeHead and listPrint function************\n");
    int a = 100,b = 200,c = 300;
    l = listAddNodeHead(l,&a);
    l = listAddNodeHead(l,&b);
    l = listAddNodeHead(l,&c);
    listPrint(l);
    //list *listAddNodeTail(list *list,void *value);
    printf("**********testing listAddNodeTail and listPrint function************\n");
    int A = 100,B = 200,C = 300;
    l = listAddNodeTail(l,&A);
    l = listAddNodeTail(l,&B);
    l = listAddNodeTail(l,&C);
    listPrint(l);
    //list *listInsertNode(list *list,listNode *old_node,void *value,int after);
    printf("**********testing listInsertNode function************\n");
    int t1=1;
    l = listInsertNode(l,l->head->next,&t1,1);
    listPrint(l);

    //void listRelease(list *list);
    printf("**********testing listRelease function************\n");
    listRelease(l);
    printf("l:%p\nl->len:%ld\n",l,l->len);
    listPrint(l);

}

void test2(){
    //void listDelNode(list *l,listNode *node);
    printf("**********testing listRelease function************\n");
    list *l = listCreate();
    int a = 100,b = 200,c = 300;
    l = listAddNodeHead(l,&a);
    l = listAddNodeHead(l,&b);
    l = listAddNodeHead(l,&c);
    int A = 400,B = 500,C = 600;
    l = listAddNodeTail(l,&A);
    l = listAddNodeTail(l,&B);
    l = listAddNodeTail(l,&C);
    listPrint(l);
    printf("\n---\n");
    listDelNode(l,l->head);
    listPrint(l);
    printf("\n---\n");
    listDelNode(l,l->tail);
    listPrint(l);
    printf("\n---\n");
    listDelNode(l,l->head->next);
    listPrint(l);

    //listIter *listGetIterator(list *l,int direction);
    //listNode *listNext(listIter *iter);
    printf("**********testing listGetIterator and listNext function************\n");
    l = listAddNodeHead(l,&a);
    l = listAddNodeHead(l,&b);
    l = listAddNodeHead(l,&c);
    listPrint(l);

    listIter *iter = listGetIterator(l,0);

    printf("iterator--->\n");
    int i=1;
    while(iter->next){
        listNode* node = listNext(iter);
        printf("%dth node:%d\n",i++,*((int*)node->value));
    }

    printf("------------listGetIterator testing is over------------\n\n\n\n");
    //list *listDup(list *orig);

    printf("**********testing listDup function************\n");
    list* copy = listDup(l);
    listPrint(copy);



    //listNode *listSearchKey(list *l,void *key);
    printf("**********testing listSearchKey function************\n");

    listNode* find = listSearchKey(l,&A);
    printf("*find=%d\n",*((int*)find->value));
}

void test3(){
    //listNode *listIndex(list *l,long index);
    printf("**********testing listIndex function************\n");
    list *l = listCreate();
    int a = 100,b = 200,c = 300;
    l = listAddNodeHead(l,&a);
    l = listAddNodeHead(l,&b);
    l = listAddNodeHead(l,&c);
    int A = 400,B = 500,C = 600;
    l = listAddNodeTail(l,&A);
    l = listAddNodeTail(l,&B);
    l = listAddNodeTail(l,&C);
    listPrint(l);
    printf("\n---\n");

    for(int i = 0;i<6;i++){
        listNode* node = listIndex(l,-1*i);
        printf("%dth node:%d\n",-1*i,*((int*)node->value));
    }
    //void listRewindTail(list *l,listIter *li);
    //void listRewind(list *l,listIter *li);
    printf("**********testing listRewind and listRewindTail function************\n");
    listIter *li = listGetIterator(l,0);
    printf("node=%d,direction=%d\n",*((int*)li->next->value),li->direction);
    listRewindTail(l,li);
    printf("node=%d,direction=%d\n",*((int*)li->next->value),li->direction);
    listRewind(l,li);
    printf("node=%d,direction=%d\n",*((int*)li->next->value),li->direction);

    //void listRotate(list *l);
    printf("**********testing listRotate function************\n");
    listRotate(l);listPrint(l);
}
int main(void)
{
    //test1();
    //test2();
    //test3();
    return 0;
}

#endif // DS_TEST_MAIN
