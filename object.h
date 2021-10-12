#ifndef file_object_h
#define file_object_h

#include "common.h"
#include "value.h"
#include "chunk.h"
#include "table.h"
#include "include/roto.h"

#define OBJ_TYPE(value) (AS_OBJ(value)->type)

#define IS_LIST(value)  is_obj_type(value,OBJ_LIST)
#define IS_BOUND_METHOD(value) is_obj_type(value, OBJ_BOUND_METHOD)
#define IS_CLASS(value) is_obj_type(value,OBJ_CLASS)
#define IS_CLOSURE(value)  is_obj_type(value, OBJ_CLOSURE)
#define IS_FUNCTION(value) is_obj_type(value, OBJ_FUNTION)
#define IS_INSTANCE(value) is_obj_type(value, OBJ_INSTANCE)
#define IS_NATIVE(value) is_obj_type(value, OBJ_NATIVE)
#define IS_STRING(value) is_obj_type(value, OBJ_STRING)


#define AS_LIST(value)  ((ObjList*)AS_OBJ(value))
#define AS_BOUND_METHOD(value) ((ObjBoundMethod*)AS_OBJ(value))
#define AS_CLASS(value)  ((ObjClass*)AS_OBJ(value))
#define AS_CLOSURE(value) ((ObjClosure*)AS_OBJ(value))
#define AS_FUNCTION(value) ((ObjFunction*)AS_OBJ(value))
#define AS_INSTANCE(value) ((ObjInstance*)AS_OBJ(value))
#define AS_NATIVE(value) \
(((ObjNative*)AS_OBJ(value))->function)\


#define AS_STRING(value) ((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value) (((ObjString*)AS_OBJ(value))->chars)

typedef enum {
    OBJ_LIST,
    OBJ_BOUND_METHOD,
    OBJ_CLASS,
    OBJ_CLOSURE,
    OBJ_FUNCTION,
    OBJ_INSTANCE,
    OBJ_NATIVE,
    OBJ_STRING,
    OBJ_UPVALUE,
}ObjType;


struct sObj{
  ObjType type;
  bool is_marked;
  struct sObj* next;
};

typedef struct {
    Obj obj;
    ValueArray values;
}ObjList;
typedef struct{
    Obj obj;
    int arity; //number of parameters the function expects
    int upvalue_count;
    Chunk chunk;
    ObjString* name;
} ObjFunction;

typedef Value (*NativeFn)(RotoVM* vm,int arg_count, Value* args);

typedef struct{
    Obj obj;
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

typedef struct{
    Obj obj;
    ObjString* name;
    Table methods;
} ObjClass;

typedef struct{
    Obj obj;
    ObjClass* klass;
    Table fields;
}ObjInstance;

typedef struct{
    Obj obj;
    Value receiver;
    ObjClosure* method;
}ObjBoundMethod;

ObjList* newList(RotoVM* vm);
ObjBoundMethod* newBoundMethod(RotoVM* vm,Value receiver, ObjClosure* method);
ObjClass* newClass(RotoVM* vm,ObjString* name);
ObjClosure* newClosure(RotoVM* vm,ObjFunction* function);
ObjFunction* newFunction(RotoVM* vm);
ObjInstance* newInstance(RotoVM* vm,ObjClass* klass);
ObjNative* newNative(RotoVM* vm,NativeFn function);
ObjString* take_string(RotoVM* vm,char* chars, int length);
ObjString* copy_string(RotoVM* vm,const char* chars, int length);
ObjUpvalue* newUpvalue(RotoVM* vm,Value* slot);
void print_object(Value value);


static inline bool is_obj_type(Value value, ObjType type){
  return IS_OBJ(value) && AS_OBJ(value)->type == type;
}
#endif
