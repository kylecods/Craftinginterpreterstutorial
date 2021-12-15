#ifndef file_vm_h
#define file_vm_h

#include "chunk.h"
#include "table.h"
#include "value.h"
#include "object.h"
#include "include/roto.h"


#define FRAMES_MAX 64
#define STACK_MAX (FRAMES_MAX * UINT8_COUNT)

//single ongoing function call
typedef struct{
    ObjClosure* closure;
    uint8_t* ip;
    Value* slots;//points to VMs value stack at the first slot the function can use
} CallFrame;

 struct _rotoVM{
  CallFrame frames[FRAMES_MAX];
  int frameCount;//height of the callframe stack
  Value stack[STACK_MAX];//stack
  Value *stack_top; //the top or beginning of the stack
  Table strings; //for string interning
  ObjString* init_string;
  Table globals; //for globals
  Table listMethods;
  ObjUpvalue* open_upvalues;

  uint8_t next_op_wide;
  size_t bytes_alocated;//running total of no of bytes of managed memory
  size_t next_gc;//threshold that triggers next collection

  Obj* objects;
  int gray_count;
  int gray_capacity;
  Obj** gray_stack;
};

void push(RotoVM* vm, Value value);
Value pop(RotoVM* vm);


void append_to_list(RotoVM* vm,ObjList* list, Value value);
bool is_valid_list_index(ObjList* list, int index);
void delete_from_list(ObjList* list, int index);
void runtime_error(RotoVM* vm,const char *format, ...);
bool is_falsey(Value value);



#endif
