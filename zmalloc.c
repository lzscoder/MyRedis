#include<stdlib.h>
#include<string.h>
#include"zmalloc.h"
#include<stdio.h>

static void zmalloc_default_oom(size_t size) {
    fprintf(stderr, "zmalloc: Out of memory trying to allocate %zu bytes\n",
        size);
    fflush(stderr);
    abort();
}
static void (*zmalloc_oom_handler)(size_t) = zmalloc_default_oom;


void *zmalloc(size_t size){

    void *ptr = malloc(size);

    if(!ptr) zmalloc_oom_handler(size);

    return ptr;

}

void *zrealloc(void *ptr,size_t size){

    void *newptr;

    if (ptr == NULL) return zmalloc(size);

    newptr = realloc(ptr,size);

    if(!newptr) zmalloc_oom_handler(size);

    return newptr;

}
void *zcalloc(size_t size){
    void* ptr = calloc(1,size);

    if(!ptr) zmalloc_oom_handler(size);

    return (char*)ptr;

}

void zfree(void* ptr){
    if(ptr == NULL) return;
    free(ptr);
}

