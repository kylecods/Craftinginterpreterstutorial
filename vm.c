#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <math.h>

#include "common.h"
#include "debug.h"
#include "vm.h"
#include "compiler.h"
#include "object.h"
#include "memory.h"

#define CRED printf("\x1B[31m");







VM vm;//should be a pointer for multiple instances and dont make a it global variable
static void runtime_error(const char *format, ...);
static Value cprint_native(int arg_count,Value* args);

static char* format_type(char* args){
         if(strcmp(args,"%s") == 0){
        return "%s";
    }else if(strcmp(args,"%g") == 0){
        return "%g";
    }else{
        return "format is wrong";
         }

}


/*
 * Things ive seen needed for a language to be complete.
 * IO e.g printf, FILE*, etc
 * */
static Value clock_native(int arg_count, Value* args) {
    return NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
}
static Value sin_native(int arg_count, Value* args){
    double d = AS_NUMBER(*args);
    return NUMBER_VAL((double)sin(d));
}

static Value floor_native(int arg_count, Value* args){
    double d = AS_NUMBER(*args);
    return NUMBER_VAL((double)floor(d));
}

static Value strlen_native(int arg_count, Value* args){
    char *f = AS_CSTRING(*args++);
    return NUMBER_VAL((double)strlen(f));
}
static Value cprintln_native(int arg_count,Value* args){
    //@fix use available helper fns...
    //@add later figure out how to support multiple values eg. printf("%s%d",string,double)..
    // trying to copy c printf by having users declare the format types
    // otherwise could have just used print_value function.


        for (int i = 1; i < arg_count; i++){
            char* format = AS_CSTRING(args[0]);
            CRED;
            switch (IS_STRING(args[i])) {
                case true:
                    printf(format, AS_CSTRING(args[i]));
                    break;
                default:
                    printf(format, AS_NUMBER(args[i]));
                    break;
            }
            printf("\n");
            printf("\x1B[37m");
        }

    return NIL_VAL;
}
static void  reset_stack() {
  /* code */
  vm.stack_top = vm.stack;
  vm.frameCount = 0;
  vm.open_upvalues = NULL;
}
//static inline ObjFunction* get_framefunction(CallFrame* frame) {
//    if (frame->function->type == OBJ_FUNCTION){
//        return (ObjFunction*)frame->function;
//    } else{
//        return ((ObjClosure*)frame->function)->function;
//    }
//
//}

static void runtime_error(const char *format, ...){
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
  fputs("\n", stderr);

    for (int i = vm.frameCount - 1; i >= 0; i--) {
        CallFrame* frame = &vm.frames[i];
        ObjFunction* function = frame->closure->function;
        //-1 because the IP is sitting on the next instruction to be executed
        size_t instruction = frame->ip - function->chunk.code - 1;
        fprintf(stderr, "\x1B[31m[line %d] in ", function->chunk.lines[instruction]);
        if(function->name == NULL) {
            fprintf(stderr, "\x1B[31mscript\n");
        } else{
            fprintf(stderr, "\x1B[31m%s()\n", function->name->chars);
        }
    }

  CallFrame* frame = &vm.frames[vm.frameCount - 1];
  size_t instr = frame->ip - frame->closure->function->chunk.code - 1;
  int line = frame->closure->function->chunk.lines[instr];
  fprintf(stderr, "\x1B[31m[line %d] in script\n", line);

  reset_stack();
}
static void define_native(const char* name, NativeFn function) {
    push(OBJ_VAL(copy_string(name, (int)strlen(name))));
    push(OBJ_VAL(newNative(function)));
    table_set(&vm.globals, AS_STRING(vm.stack[0]),vm.stack[1]);
    pop();
    pop();
}

void init_vm(){
  reset_stack();
  vm.objects = NULL;
  vm.gray_count = 0;
  vm.gray_capacity = 0;
  vm.gray_stack = NULL;

  init_table(&vm.globals);
  init_table(&vm.strings);


  define_native("clock", clock_native);
  define_native("sin", sin_native);
    define_native("floor", floor_native);
    define_native("strlength", strlen_native);
    define_native("printfln", cprintln_native);
}

void free_vm() {
  /* code */
  free_table(&vm.globals);
  free_table(&vm.strings);
  free_objects();
}

void push(Value value) {
  /* code */
  *vm.stack_top = value;
  vm.stack_top++;
}

Value pop(){
  vm.stack_top--;
  return *vm.stack_top;
}

static Value peek(int distance){
  return vm.stack_top[-1 - distance];
}

static bool call(ObjClosure* closure, int arg_count){
    if(arg_count != closure->function->arity) {
        runtime_error("Expected %d arguments but got %d.", closure->function->arity, arg_count);
        return false;
    }
    if (vm.frameCount == FRAMES_MAX){
        runtime_error("Stack overflow.");
        return false;
    }
    CallFrame* frame = &vm.frames[vm.frameCount++];
    frame->closure = closure;
    frame->ip = closure->function->chunk.code;

    frame->slots = vm.stack_top - arg_count - 1;
    return true;

}

static bool call_value(Value callee, int arg_count) {
    if(IS_OBJ(callee)) {
        switch (OBJ_TYPE(callee)) {
            case OBJ_CLOSURE:
                return call(AS_CLOSURE(callee), arg_count);
//            case OBJ_FUNCTION:
//                return call(AS_FUNCTION(callee),arg_count);
            case OBJ_NATIVE:{
                NativeFn native = AS_NATIVE(callee);
                Value result = native(arg_count, vm.stack_top-arg_count);
                vm.stack_top -= arg_count + 1;
                push(result);
                return true;
            }
            default:
                break;
        }
    }

    runtime_error("Can only call functions and classes.");
    return false;
}
static ObjUpvalue* capture_upvalue(Value* local){
    ObjUpvalue* prev_upvalue = NULL;
    ObjUpvalue* upvalue = vm.open_upvalues;
    while (upvalue != NULL && upvalue->location > local){
        prev_upvalue = upvalue;
        upvalue = upvalue->next;
    }
    if (upvalue != NULL && upvalue->location == local){
        return upvalue;
    }
    ObjUpvalue* created_upvalue = newUpvalue(local);
    created_upvalue->next = upvalue;
    if(prev_upvalue == NULL){
        vm.open_upvalues = created_upvalue;
    } else{
        prev_upvalue->next = created_upvalue;
    }
    return created_upvalue;
}

static void close_upvalues(Value* last){
    while (vm.open_upvalues != NULL && vm.open_upvalues->location >= last){
        ObjUpvalue* upvalue = vm.open_upvalues;
        upvalue->closed = *upvalue->location;
        upvalue->location = &upvalue->closed;
        vm.open_upvalues = upvalue->next;
    }
}


//@fix
static bool is_falsey(Value value){
  return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static void concatenate(){
  ObjString* b = AS_STRING(pop());
  ObjString* a = AS_STRING(pop());

  int length = a->length + b->length;
  char* chars = ALLOCATE(char, length + 1);
  memcpy(chars, a->chars, a->length);
  memcpy(chars + a->length, b->chars, b->length);
  chars[length] = '\0';

  ObjString* result = take_string(chars, length);
  push(OBJ_VAL(result));
}
static InterpretResult run(){
    CallFrame* frame = &vm.frames[vm.frameCount - 1];

  #define READ_BYTE() (*frame->ip++)
  #define READ_CONST() (frame->closure->function->chunk.constants.values[READ_BYTE()])
  #define READ_SHORT() \
          (frame->ip+=2, \
          (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))

  #define READ_STRING() AS_STRING(READ_CONST())
  #define BINARY_OP(val_type, op)\
          do {\
            if(!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))){\
              runtime_error("Operands must be numbers.");\
              return INTERPRET_RUNTIME_ERROR;\
            }\
            \
            double b = AS_NUMBER(pop());\
            double a = AS_NUMBER(pop());\
            push(val_type(a op b)); \
          } while(false)

  for(;;){
    #ifdef DEBUG_TRACE_EXECUTION
      printf("     ");
      for (Value* slot = vm.stack; slot < vm.stack_top; slot++) {
        /* code */
        printf("[ ");
        print_value(*slot);
        printf(" ]");
      }
      printf("\n");
      disassemble_instr(&frame->closure->function->chunk, (int)(frame->ip - frame->closure->function->chunk.code));
    #endif
    uint8_t instr;
    switch (instr = READ_BYTE()) {
      case OP_CONSTANT:{
        Value constant = READ_CONST();
        push(constant);
        break;
      }

      case OP_NIL: push(NIL_VAL); break;
      case OP_TRUE: push(BOOL_VAL(true)); break;
      case OP_FALSE: push(BOOL_VAL(false)); break;
      case OP_POP: pop(); break;
      case OP_GET_LOCAL:{
        uint8_t slot = READ_BYTE();
        push(frame->slots[slot]);
        break;
      }
      case OP_SET_LOCAL:{
        uint8_t slot = READ_BYTE();
        frame->slots[slot] = peek(0);
      }
      case OP_GET_GLOBAL:{//assuming load gl_var to stack
        ObjString* name = READ_STRING();
        Value value;
        if(!table_get(&vm.globals, name, &value)){
          runtime_error("Undefined variable '%s'.", name->chars);
          return INTERPRET_RUNTIME_ERROR;
        }
        push(value);
        break;
      }
      case OP_DEFINE_GLOBAL: {
        ObjString* name = READ_STRING();
        table_set(&vm.globals, name, peek(0));
        pop();
        break;
      }
      case OP_SET_GLOBAL:{
        ObjString* name = READ_STRING();
        if(table_set(&vm.globals, name, peek(0))){
          table_delete(&vm.globals,name);
          runtime_error("Undefined variable '%s'.", name->chars);
          return INTERPRET_RUNTIME_ERROR;
        }
        break;
      }
      case OP_GET_UPVALUE:{
          uint8_t slot = READ_BYTE();
          push(*frame->closure->upvalues[slot]->location);
          break;
      }
      case OP_SET_UPVALUE:{
          uint8_t slot = READ_BYTE();
          *frame->closure->upvalues[slot]->location = peek(0);
          break;
      }
      case OP_EQUAL: {
        Value b = pop();
        Value a = pop();
        push(BOOL_VAL(vals_equal(a,b)));
        break;
      }
      case OP_GREATER: BINARY_OP(BOOL_VAL, >); break;
      case OP_LESS: BINARY_OP(BOOL_VAL, <); break;
      case OP_ADD:{
        if(IS_STRING(peek(0)) && IS_STRING(peek(1))){
          concatenate();
        }else if(IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))){
          double b = AS_NUMBER(pop());
          double a = AS_NUMBER(pop());
          push(NUMBER_VAL(a+b));
        }else{
          runtime_error("Operands must be two numbers or two strings.");
          return INTERPRET_RUNTIME_ERROR;
        }
        break;
      }
      case OP_SUB:{
        BINARY_OP(NUMBER_VAL, -);
        break;
      }
      case OP_MUL:{
        BINARY_OP(NUMBER_VAL, *);
        break;
      }
      case OP_DIV:{
        BINARY_OP(NUMBER_VAL, /);
        break;
      }
      case OP_NOT:
        push(BOOL_VAL(is_falsey(pop())));
        break;
      case OP_NEGATE:{
        if(!IS_NUMBER(peek(0))){
          runtime_error("Operand must be a number.");
          return INTERPRET_RUNTIME_ERROR;
        }
        push(NUMBER_VAL(-AS_NUMBER(pop())));
        break;
      }
      case OP_PRINT:{
        print_value(pop());
        printf("\n");
        break;
      }

      case OP_JUMP: {
        uint16_t offset = READ_SHORT();
        frame->ip += offset;
        break;
      }

      case OP_JUMP_IF_FALSE: {
        uint16_t offset = READ_SHORT();
        if(is_falsey(peek(0))) frame->ip += offset;
        break;
      }
      case OP_LOOP: {
        uint16_t offset = READ_SHORT();
        frame->ip -= offset;
        break;
      }
        case OP_CALL: {
            int arg_count = READ_BYTE();
            if (!call_value(peek(arg_count), arg_count)) {
                return INTERPRET_RUNTIME_ERROR;
            }
            frame = &vm.frames[vm.frameCount - 1];
            break;
        }
        case OP_CLOSURE:{
            ObjFunction* function = AS_FUNCTION(READ_CONST());
            ObjClosure* closure = newClosure(function);
            push(OBJ_VAL(closure));
            for (int i = 0; i < closure->upvalue_count; ++i) {
                uint8_t is_local = READ_BYTE();
                uint8_t index = READ_BYTE();
                if(is_local){
                    closure->upvalues[i] = capture_upvalue(frame->slots + index);
                } else{
                    closure->upvalues[i] =closure->upvalues[index];
                }
            }
            break;
        }
        case OP_CLOSE_UPVALUE:
            close_upvalues(vm.stack_top - 1);
            pop();
            break;
      case OP_RETURN:{
          Value result = pop();
          close_upvalues(frame->slots);
          vm.frameCount--;
          if(vm.frameCount == 0){
              pop();
              return INTERPRET_OK;
          }
          vm.stack_top = frame->slots;
          push(result);
          frame = &vm.frames[vm.frameCount-1];
          break;
      }
    }
  }
  #undef READ_BYTE
  #undef READ_SHORT
  #undef READ_CONST
  #undef READ_STRING
  #undef BINARY_OP
}

InterpretResult interpret(const char* source){
//  Chunk chunk;
//  init_chunk(&chunk);
//
//  if(!compile(source, &chunk)){
//    free_chunk(&chunk);
//    return INTERPRET_COMPILE_ERROR;
//  }
//  vm.chunk = &chunk;
//  vm.ip = vm.chunk->code;
    ObjFunction* function = compile(source);
    if (function == NULL) return INTERPRET_COMPILE_ERROR;

    push(OBJ_VAL(function));
    ObjClosure* closure = newClosure(function);
    pop();
    push(OBJ_VAL(closure));
    call_value(OBJ_VAL(closure), 0);

    return run();
}
