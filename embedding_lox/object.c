#include <stdio.h>
#include <string.h>


#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"
#include "vm.h"

#define ALLOCATE_OBJ(vm,type, objType)\
        (type*)allocate_object(vm, sizeof(type), objType)

static Obj* allocate_object(LoxVM* vm, size_t size, ObjType type){
  Obj* object = (Obj*)reallocate(vm,NULL,0,size);
  object->type = type;
  object->is_marked = false;
  object->next = vm->objects;
  vm->objects = object;
#ifdef DEBUG_LOG_GC
//    printf("\x1B[36m");
//  printf("%p allocate %ld for %d\n", (void*)object,size,type);
    __print_with_color(_CYAN,"%p allocate %zd for %d\n", (void*)object,size,type)
    printf(_RESET);
#endif
  return object;
}

ObjClosure* newClosure(LoxVM* vm,ObjFunction* function){
    ObjUpvalue** upvalues = ALLOCATE(vm,ObjUpvalue*, function->upvalue_count);
    for(int i = 0; i < function->upvalue_count; i++){
        upvalues[i] = NULL;
    }
    ObjClosure* closure = ALLOCATE_OBJ(vm, ObjClosure, OBJ_CLOSURE);
    closure->function = function;
    closure->upvalues = upvalues;
    closure->upvalue_count = function->upvalue_count;
    return closure;
}

ObjFunction* newFunction(LoxVM* vm){
    ObjFunction* function = ALLOCATE_OBJ(vm,ObjFunction, OBJ_FUNCTION);

    function->arity = 0;
    function->upvalue_count = 0;
    function->name = NULL;
    init_chunk(&function->chunk);
    return function;
}

ObjNative* newNative(LoxVM* vm,NativeFn function){
    ObjNative* native = ALLOCATE_OBJ(vm,ObjNative,OBJ_NATIVE);
    native->function = function;
    return native;
}


// allocate space for string object
static ObjString* allocate_string(LoxVM* vm, char* chars, int length,uint32_t hash){
  ObjString* string = ALLOCATE_OBJ(vm,ObjString, OBJ_STRING);
  string->length = length;
  string->chars = chars;
  string->hash = hash;

  //make new strings reachable for the GC
  push(vm,OBJ_VAL(string));
  //GC trigger
  table_set(vm, &vm->strings, string, NIL_VAL);
  pop(vm);
  return string;
}

static uint32_t hash_string(const char* key, int length){
  uint32_t hash = 2166136261u;

  for (int i = 0; i < length; i++) {
    hash ^= key[i];
    hash *= 16777619;
  }
  return hash;
}

ObjString* take_string(LoxVM* vm, char* chars, int length){
  uint32_t hash = hash_string(chars, length);
  ObjString* interned = table_find_string(&vm->strings, chars, length, hash);
  if(interned != NULL){
    FREE_ARRAY(vm,char, chars, length + 1);
    return interned;
  }
  return allocate_string(vm,chars, length, hash);
}

//copy the string to memory(heap)
ObjString* copy_string(LoxVM* vm,const char* chars, int length){
  uint32_t hash = hash_string(chars, length);
  ObjString* interned = table_find_string(&vm->strings, chars, length, hash);

  if(interned != NULL) return interned;

  char* heap_chars = ALLOCATE(vm,char, length + 1);
  memcpy(heap_chars, chars, length);
  heap_chars[length] = '\0';

  return allocate_string(vm, heap_chars, length, hash);
}

ObjUpvalue* newUpvalue(LoxVM* vm, Value* slot){
    ObjUpvalue* upvalue = ALLOCATE_OBJ(vm,ObjUpvalue,OBJ_UPVALUE);
    upvalue->closed = NIL_VAL;
    upvalue->location = slot;
    upvalue->next = NULL;
    return upvalue;
}


static void printFunction(ObjFunction* function){
    if(function->name == NULL){
        printf("<script>");
        return;
    }
    printf("<fn %s>", function->name->chars);
}

void print_object(Value value){
  switch(OBJ_TYPE(value)){
      case OBJ_CLOSURE:
          printFunction(AS_CLOSURE(value)->function);
          break;
      case OBJ_STRING:
          printf("%s", AS_CSTRING(value));
          break;
      case OBJ_UPVALUE:
          printf("upvalue");
          break;
      case OBJ_NATIVE:
          printf("<native fn>");
          break;
      case OBJ_FUNCTION:
          printFunction(AS_FUNCTION(value));
          break;
  }
}
