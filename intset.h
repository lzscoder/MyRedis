#ifndef _INTSET_H_
#define _INTSET_H_

#include<stdint.h>
#include<stddef.h>
typedef struct intset{
    uint32_t encoding;

    uint32_t length;

    int8_t contents[];
}intset;


intset *intsetNew(void);//1

intset *intsetAdd(intset *is,int64_t value,uint8_t *success);//1

intset *intsetRemove(intset *is,int64_t value,int *success);

uint8_t intsetFind(intset* is,int64_t value);

int64_t intsetRandom(intset *is);

uint8_t intsetGet(intset *is,uint32_t pos,int64_t *value);

uint32_t intsetLen(intset *is);

size_t intsetBlobLen(intset *is);

void intsetInformation(intset* is);

void intsetPrint(intset* is);

#endif // _INTSET_H_

