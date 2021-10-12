#include <stdio.h>
#include <string.h>


#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"
#include "vm.h"

#define ALLOCATE_OBJ(vm,type, objType)\
        (type*)allocate_object(vm,sizeof(type), objType)

static Obj* allocate_object(RotoVM* vm, size_t size, ObjType type){
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
ObjList* newList(RotoVM* vm){
    ObjList* list = ALLOCATE_OBJ(vm,ObjList,OBJ_LIST);
    init_val_array(&list->values);
    return list;
}

ObjBoundMethod* newBoundMethod(RotoVM* vm,Value receiver, ObjClosure* method){
    ObjBoundMethod* bound = ALLOCATE_OBJ(vm,ObjBoundMethod,OBJ_BOUND_METHOD);
    bound->receiver = receiver;
    bound->method = method;
    return bound;
}

ObjClass* newClass(RotoVM* vm,ObjString* name){
    ObjClass* klass = ALLOCATE_OBJ(vm,ObjClass,OBJ_CLASS);
    klass->name = name;
    init_table(&klass->methods);
    return klass;
}

ObjClosure* newClosure(RotoVM* vm,ObjFunction* function){
    ObjUpvalue** upvalues = ALLOCATE(vm,ObjUpvalue*, function->upvalue_count);
    for(int i = 0; i < function->upvalue_count; i++){
        upvalues[i] = NULL;
    }
    ObjClosure* closure = ALLOCATE_OBJ(vm,ObjClosure, OBJ_CLOSURE);
    closure->function = function;
    closure->upvalues = upvalues;
    closure->upvalue_count = function->upvalue_count;
    return closure;
}

ObjFunction* newFunction(RotoVM* vm){
    ObjFunction* function = ALLOCATE_OBJ(vm,ObjFunction, OBJ_FUNCTION);

    function->arity = 0;
    function->upvalue_count = 0;
    function->name = NULL;
    init_chunk(&function->chunk);
    return function;
}

ObjInstance* newInstance(RotoVM* vm,ObjClass* klass){
    ObjInstance* instance = ALLOCATE_OBJ(vm,ObjInstance,OBJ_INSTANCE);
    instance->klass = klass;
    init_table(&instance->fields);
    return instance;
}

ObjNative* newNative(RotoVM* vm,NativeFn function){
    ObjNative* native = ALLOCATE_OBJ(vm,ObjNative,OBJ_NATIVE);
    native->function = function;
    return native;
}

// allocate space for string object
static ObjString* allocate_string(RotoVM* vm,char* chars, int length,uint32_t hash){
  ObjString* string = ALLOCATE_OBJ(vm,ObjString, OBJ_STRING);
  string->length = length;
  string->chars = chars;
  string->hash = hash;

  //make new strings reachable for the GC
  push(vm,OBJ_VAL(string));
  //GC trigger
  table_set(vm,&vm->strings, string, NIL_VAL);
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

ObjString* take_string(RotoVM* vm,char* chars, int length){
  uint32_t hash = hash_string(chars, length);
  ObjString* interned = table_find_string(&vm->strings, chars, length, hash);
  if(interned != NULL){
    FREE_ARRAY(vm,char, chars, length + 1);
    return interned;
  }
  return allocate_string(vm,chars, length, hash);
}

//copy the string to memory(heap)
ObjString* copy_string(RotoVM* vm,const char* chars, int length){
  uint32_t hash = hash_string(chars, length);
  ObjString* interned = table_find_string(&vm->strings, chars, length, hash);

  if(interned != NULL) return interned;

  char* heap_chars = ALLOCATE(vm,char, length + 1);
  memcpy(heap_chars, chars, length);
  heap_chars[length] = '\0';

  return allocate_string(vm,heap_chars, length, hash);
}

ObjUpvalue* newUpvalue(RotoVM* vm,Value* slot){
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
      case OBJ_LIST: {
          ObjList *list = AS_LIST(value);
          printf("[");
          for (int i = 0; i < list->values.count; i++) {
              print_value(list->values.values[i]);
              if (i != list->values.count - 1) {
                  printf(", ");
              }
          }
          printf("]");
          break;
      }

      case OBJ_BOUND_METHOD:
          printFunction(AS_BOUND_METHOD(value)->method->function);
          break;
      case OBJ_CLASS:
          //console specific
          __print_with_color(_CYAN, "%s", AS_CLASS(value)->name->chars);
          printf(_RESET);
          break;
      case OBJ_CLOSURE:
          printFunction(AS_CLOSURE(value)->function);
          break;
      case OBJ_STRING:
          printf("%s", AS_CSTRING(value));
          break;
      case OBJ_UPVALUE:
          printf("upvalue");
          break;
      case OBJ_INSTANCE:
          //support toString()
          printf("%s instance",
                 AS_INSTANCE(value)->klass->name->chars);
          break;
      case OBJ_NATIVE:
          printf("<native fn>");
          break;
      case OBJ_FUNCTION:
          printFunction(AS_FUNCTION(value));
          break;
  }
}
