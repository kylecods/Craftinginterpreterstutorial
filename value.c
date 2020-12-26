#include <stdio.h>
#include <string.h>

#include "memory.h"
#include "value.h"
#include "object.h"

void init_val_array(ValueArray *arr) {
  /* code */
  arr->values = NULL;
  arr->capacity = 0;
  arr->count = 0;
}
//possibly also use stretchy buffers? but noteworthy to understand this
void write_val_array(ValueArray *arr, Value value){
  if(arr->capacity < arr->count + 1){
    int old_cap = arr->capacity;
    arr->capacity = GROW_CAPACITY(old_cap);
    arr->values = GROW_ARRAY(arr->values, Value, old_cap, arr->capacity);
  }
  arr->values[arr->count] = value;
  arr->count++;
}

void free_val_array(ValueArray *arr){
  FREE_ARRAY(Value,arr->values,arr->capacity);
  init_val_array(arr);
}

void print_value(Value value){
  switch (value.type) {
    case VAL_BOOL: printf(AS_BOOL(value) ? "true" : "false"); break;
    case VAL_NIL: break;
    case VAL_NUMBER: printf("%g", AS_NUMBER(value)); break;
    case VAL_OBJ: print_object(value); break;
  }

}

bool vals_equal(Value a, Value b){
  if(a.type != b.type) return false;
  switch (a.type) {
    case VAL_BOOL: return AS_BOOL(a) == AS_BOOL(b);
    case VAL_NIL: return true;
    case VAL_NUMBER: return AS_NUMBER(a) == AS_NUMBER(b);
    case VAL_OBJ: return AS_OBJ(a) == AS_OBJ(b);
  }
}
