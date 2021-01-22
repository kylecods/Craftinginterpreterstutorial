#ifndef croto_value_h
#define croto_value_h

#include "common.h"

typedef struct sObj Obj;
typedef struct sObjString ObjString;

typedef enum{
  VAL_BOOL,
  VAL_NIL,
  VAL_NUMBER,
  VAL_OBJ
}ValueType;

/*
@wren
NAN stuff
typedef uint64_t Value ?make 32bit type?
*/

typedef struct {
  ValueType type;
  union{
    bool boolean;
    double number;
    Obj* obj; //pointer to heap
  }as;
} Value;

#define IS_BOOL(value) ((value).type == VAL_BOOL)
#define IS_NIL(value) ((value).type == VAL_NIL)
#define IS_NUMBER(value) ((value).type == VAL_NUMBER)
#define IS_OBJ(value) ((value).type == VAL_OBJ)

#define AS_BOOL(value) ((value).as.boolean)
#define AS_NUMBER(value) ((value).as.number)
#define AS_OBJ(value) ((value).as.obj)

#define BOOL_VAL(value)   ((Value){ VAL_BOOL, {.boolean = value } })
#define NIL_VAL           ((Value){ VAL_NIL, {.number = 0 } })
#define NUMBER_VAL(value) ((Value){ VAL_NUMBER, {.number = value } })
#define OBJ_VAL(object)   ((Value){ VAL_OBJ, {.obj = (Obj*)object}})

typedef struct{
  /* data */
  int capacity;
  int count;
  Value *values;
}ValueArray;

bool vals_equal(Value a, Value b);
void init_val_array(ValueArray *arr);
void write_val_array(LoxVM* vm,ValueArray *arr, Value value);
void free_val_array(LoxVM* vm, ValueArray *arr);

void print_value(Value value);
#endif
