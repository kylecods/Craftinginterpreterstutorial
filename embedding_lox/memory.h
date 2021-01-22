#ifndef croto_memory_h
#define croto_memory_h

#include "lox.h"
#include "object.h"
#define ALLOCATE(vm,type, count)\
        (type*)reallocate(vm, NULL, 0, sizeof(type)* (count))

#define FREE(vm, type, pointer)\
        reallocate(vm, pointer, sizeof(type), 0)

#define GROW_CAPACITY(cap)\
        ((cap) < 8 ? 8 : (cap) * 2)

#define GROW_ARRAY(vm, prev, type, old_count, count)\
        (type*)reallocate(vm,prev, sizeof(type) * (old_count),\
         sizeof(type) * (count))

#define FREE_ARRAY(vm, type, pointer, old_count)\
        reallocate(vm, pointer, sizeof(type) * (old_count), 0)

void *reallocate(LoxVM* vm, void* prev, size_t old_size, size_t new_size);
void mark_object(LoxVM* vm,Obj* object);
void mark_value(LoxVM* vm, Value value);
void collect_garbage(LoxVM* vm);
void free_objects(LoxVM* vm);
#endif
