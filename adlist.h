#ifndef __ADLIST_H_
#define __ADLIST_H_



//双端链表节点
typedef struct listNode{

    struct listNode* prev;

    struct listNode* next;

    void *value;

}listNode;


//双端列表迭代器
typedef struct listIter{

    //当前迭代到的节点
    listNode *next;

    //迭代的方向
    int direction;

}listIter;




//双端链表结构
typedef struct list{

    //表头节点
    listNode *head;

    //表尾节点
    listNode *tail;

    //节点值复制函数
    void *(*dup)(void *ptr);

    //节点值释放函数
    void *(*free)(void *ptr);

    //节点值对比函数
    void *(*match)(void *ptr,void *key);

    //链表所包含的节点数量
    unsigned long len;
}list;

//返回给定链表所包含的节点数量
#define listLength(l) ((l)->len)

//返回头节点
#define listFirst(l) ((l)->head)

//返回给定链表的尾节点
#define listLast(l) ((l)->tail)

//返回给定链表的前置节点
#define listPreNode(n) ((n)->prev)

//返回给定节点的后置节点
#define listNextNode(n) ((n)->next)

//返回给定节点的值
#define listValueNode(n) ((n)->value)


//将链表l的值复制函数设置为m
#define listSetDupMethod(l,m) ((l)->dup = (m))

//将链表l的值释放函数设置为m
#define listSetFreeMethod(l,m) ((l)->free = (m))

//将链表l的值对比函数设置为m
#define listSetMatchMethod(l,m) ((l)->match = (m))

//返回给定链表的值复制函数
#define listGetDupMethod(l) ((l)->dup)

//返回给定链表的值释放函数
#define listGetFree(l) ((l)->free)

//返回给定链表的值对比函数
#define listGetMatchMethod(l) ((l)->match)


//prototypes

list *listCreate(void);
void listRelease(list *l);
list *listAddNodeHead(list *l,void *value);
list *listAddNodeTail(list *l,void *value);
list *listInsertNode(list *l,listNode *old_node,void *value,int after);
void listDelNode(list *l,listNode *node);
listIter *listGetIterator(list *l,int direction);
listNode *listNext(listIter *iter);
list *listDup(list *orig);
listNode *listSearchKey(list *l,void *key);
listNode *listIndex(list *l,long index);
void listRewind(list *l,listIter *li);
void listRewindTail(list *l,listIter *li);
void listRotate(list *l);
void listPrint(list* l);

/*迭代器迭代的方向*/
//从表头到表尾
#define AL_START_HEAD 0
//从表尾到表头
#define AL_START_TAIL 1


#endif // __ADLIST_H_
