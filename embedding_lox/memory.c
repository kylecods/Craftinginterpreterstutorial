#include <stdlib.h>


#include "lox.h"
#include "common.h"
#include "compiler.h"
#include "memory.h"
#include "vm.h"

#ifdef DEBUG_LOG_GC
#include <stdio.h>
#include "debug.h"
#endif

//tune this later
#define GC_HEAP_GROW_FACTOR 2

/* MARK-SWEEP ALGORITHM
 * Start off with all objects white.
 * Find all the roots and mark them gray.
 * As long as there are still gray objects:
    - Pick a gray object. Turn any white objects that the object mentions to gray.
    - Mark the original gray object black.
 */

void *reallocate(LoxVM* vm, void* prev, size_t old_size, size_t new_size){
    vm->bytes_alocated += new_size - old_size;
    if (new_size > old_size){
#ifdef DEBUG_STRESS_GC
        collect_garbage(vm);
#endif
        if (vm->bytes_alocated > vm->next_gc){
            collect_garbage(vm);
        }
    }
  if(new_size == 0){
    free(prev);
    return NULL;
  }
  return realloc(prev, new_size);
}

void mark_object(LoxVM* vm, Obj* object){
    if (object == NULL) return;
    if(object->is_marked) return;
#ifdef DEBUG_LOG_GC
    //for readability
//    printf("\x1B[31m");
//
//    printf("%p mark ",(void*)object);
    __print_with_color(_RED,"%p mark ",(void*)object);
    print_value(OBJ_VAL(object));
    printf("\n");

    printf(_RESET);
#endif
    object->is_marked = true;
    if(vm->gray_capacity < vm->gray_count + 1){
        vm->gray_capacity = GROW_CAPACITY(vm->gray_capacity);
        vm->gray_stack = realloc(vm->gray_stack,sizeof(Obj*) * vm->gray_capacity);
        if (vm->gray_stack == NULL) exit(1);
    }

    vm->gray_stack[vm->gray_count++] = object;
}

void mark_value(LoxVM* vm, Value value){
    if(!IS_OBJ(value)) return;
    mark_object(vm, AS_OBJ(value));
}
static void mark_array(LoxVM* vm, ValueArray* array){
    for (int i = 0; i < array->count; i++) {
        mark_value(vm, array->values[i]);
    }
}
static void blacken_object(LoxVM* vm, Obj* object){
#ifdef DEBUG_LOG_GC
    __print_with_color(_YELLOW, "%p blacken ", (void*)object);
    print_value(OBJ_VAL(object));
    printf("\n");
    printf(_RESET);
#endif
    switch (object->type) {
        case OBJ_CLOSURE:{
            ObjClosure* closure = (ObjClosure*)object;
            mark_object(vm,(Obj*)closure->function);
            for (int i = 0; i < closure->upvalue_count; i++) {
                mark_object(vm,(Obj*)closure->upvalues[i]);
            }
            break;
        }
        case OBJ_FUNCTION:{
            ObjFunction* function = (ObjFunction*)object;

            mark_object(vm,(Obj*)function->name);
            mark_array(vm, &function->chunk.constants);
            break;
        }
        case OBJ_UPVALUE:
            mark_value(vm, ((ObjUpvalue*)object)->closed);
            break;
        case OBJ_NATIVE:
        case OBJ_STRING:
            break;
    }
}

static void free_object(LoxVM* vm,Obj *object) {
#ifdef DEBUG_LOG_GC
//    printf("\x1B[34m");
//    printf("%p free type %d\n", (void*)object,object->type);

    __print_with_color(_BLUE, "%p free type %d\n", (void*)object,object->type);
    printf(_RESET);
#endif
  switch (object->type) {
      case OBJ_CLOSURE:{
          ObjClosure* closure = (ObjClosure*)object;
          FREE_ARRAY(vm, ObjUpvalue*, closure->upvalues, closure->upvalue_count);
          FREE(vm, ObjClosure, object);
          break;
      }
      case OBJ_FUNCTION:{
          ObjFunction* function = (ObjFunction*)object;
          free_chunk(vm, &function->chunk);
          FREE(vm, ObjFunction, object);
          break;
      }
      case OBJ_NATIVE:{
          FREE(vm, ObjNative,object);
          break;
      }
    case OBJ_STRING:{
      ObjString* string = (ObjString*)object;
      FREE_ARRAY(vm, char, string->chars, string->length + 1);
      FREE(vm, ObjString, object);
      break;
    }
      case OBJ_UPVALUE:
          FREE(vm, ObjUpvalue, object);
          break;
  }
}

static void mark_roots(LoxVM* vm){
    for(Value* slot = vm->stack; slot < vm->stack_top; slot++){
        mark_value(vm, *slot);
    }
    for (int i = 0; i < vm->frameCount; i++) {
        mark_object(vm, (Obj*)vm->frames[i].closure);
    }
    for (ObjUpvalue* upvalue = vm->open_upvalues; upvalue != NULL; upvalue = upvalue->next) {
        mark_object(vm, (Obj*)upvalue);
    }
    mark_table(vm, &vm->globals);
    mark_compiler_roots(vm);
}
static void trace_references(LoxVM* vm){
    while (vm->gray_count > 0){
        Obj* object = vm->gray_stack[--vm->gray_count];
        blacken_object(vm, object);
    }
}
static void sweep(LoxVM* vm){
    Obj* previous = NULL;
    Obj* object = vm->objects;
    while(object != NULL){
        if(object->is_marked){
            object->is_marked = false;
            previous = object;
            object = object->next;
        } else{
            Obj* unreached = object;
            object = object->next;
            if (previous != NULL){
                previous->next = object;
            } else{
                vm->objects = object;
            }
            free_object(vm,unreached);
        }
    }
}
void free_objects(LoxVM* vm) {
  /* code */
  Obj* object = vm->objects;

  while (object != NULL) {
    Obj* next = object->next;
    free_object(vm, object);
    object = next;
  }
  free(vm->gray_stack);
}

void collect_garbage(LoxVM* vm){
#ifdef DEBUG_LOG_GC
//    printf("\x1B[32m");
//    printf("-- gc begin\n");
    __print_with_color(_GREEN,"-- gc begin\n");
    size_t before = vm->bytes_alocated;
    printf(_RESET);

#endif
    //mark
    mark_roots(vm);
    //trace
    trace_references(vm);

    //weak references
    table_remove_white(&vm->strings);
    //sweep
    sweep(vm);
    vm->next_gc = vm->bytes_alocated * GC_HEAP_GROW_FACTOR;
#ifdef DEBUG_LOG_GC
//    printf("\x1B[32m");
//    printf("-- gc end\n");
    __print_with_color(_GREEN,"-- gc end\n");
    printf("    collected %zd bytes (from %zd to %zd) next at %zd\n",
           before - vm->bytes_alocated, before, vm->bytes_alocated,vm->next_gc);
    printf(_RESET);
#endif
}
