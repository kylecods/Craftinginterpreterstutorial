#ifndef croto_object_h
#define croto_object_h

#include "lox.h"
#include "common.h"
#include "value.h"
#include "chunk.h"

#define OBJ_TYPE(value) (AS_OBJ(value)->type)

#define IS_CLOSURE(value)  is_obj_type(value, OBJ_CLOSURE)
#define IS_FUNCTION(value) is_obj_type(value, OBJ_FUNTION)
#define IS_NATIVE(value) is_obj_type(value, OBJ_NATIVE)
#define IS_STRING(value) is_obj_type(value, OBJ_STRING)

#define AS_CLOSURE(value) ((ObjClosure*)AS_OBJ(value))
#define AS_FUNCTION(value) ((ObjFunction*)AS_OBJ(value))
#define AS_NATIVE(value) \
(((ObjNative*)AS_OBJ(value))->function)\


#define AS_STRING(value) ((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value) (((ObjString*)AS_OBJ(value))->chars)

typedef enum {
    OBJ_CLOSURE,
    OBJ_FUNCTION,
    OBJ_NATIVE,
    OBJ_STRING,
    OBJ_UPVALUE,
}ObjType;


struct sObj{
  ObjType type;
  bool is_marked;
  struct sObj* next;
};

typedef struct{
    Obj obj;
    int arity; //number of parameters the function expects
    int upvalue_count;
    Chunk chunk;
    ObjString* name;
} ObjFunction;

typedef Value (*NativeFn)(LoxVM*vm, int arg_count, Value* args);

typedef struct{
    Obj obj;
    //int arity;
    NativeFn function;
} ObjNative;

struct sObjString{
  Obj obj;
  int length;
  char* chars;
  uint32_t hash;
};

typedef struct ObjUpvalue{
    Obj obj;
    Value* location;
    Value closed;
    struct ObjUpvalue* next;
}ObjUpvalue;

typedef struct{
    Obj obj;
    ObjFunction* function;
    ObjUpvalue** upvalues;
    int upvalue_count;//for GC
}ObjClosure;

ObjClosure* newClosure(LoxVM* vm,ObjFunction* function);
ObjFunction* newFunction(LoxVM* vm);
ObjNative* newNative(LoxVM* vm,NativeFn function);
ObjString* take_string(LoxVM* vm, char* chars, int length);
ObjString* copy_string(LoxVM*vm, const char* chars, int length);
ObjUpvalue* newUpvalue(LoxVM*vm, Value* slot);
void print_object(Value value);

static inline bool is_obj_type(Value value, ObjType type){
  return IS_OBJ(value) && AS_OBJ(value)->type == type;
}
#endif