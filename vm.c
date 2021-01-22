#include <stdio.h>
#include <stdarg.h>
#include <string.h>


#include "common.h"
#include "debug.h"
#include "vm.h"
#include "compiler.h"
#include "memory.h"
#include "native.h"




VM vm;//should be a pointer for multiple instances and dont make a it global variable

static void reset_stack() {
    /* code */
    vm.stack_top = vm.stack;
    vm.frameCount = 0;
    vm.open_upvalues = NULL;
}
void runtime_error(const char *format, ...){
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
        fprintf(stderr, "[line %d] in ", function->chunk.lines[instruction]);
        if(function->name == NULL) {
            fprintf(stderr, "script\n");
        } else{
            fprintf(stderr, "%s()\n", function->name->chars);
        }
    }

    CallFrame* frame = &vm.frames[vm.frameCount - 1];
    size_t instr = frame->ip - frame->closure->function->chunk.code - 1;
    int line = frame->closure->function->chunk.lines[instr];
    fprintf(stderr, "[line %d] in script\n", line);

    reset_stack();
}

//native methods
static Value length_method(int arg_count, Value* args){
    if(arg_count != 0){
        runtime_error("Expected no arguments but got %d.", arg_count);
        return NIL_VAL;
    }
    ObjList* list = AS_LIST(args[0]);
    return NUMBER_VAL(list->values.count);
}




static void define_ntv_methods(const char* name, NativeFn function) {
    push(OBJ_VAL(copy_string(name, (int)strlen(name))));
    push(OBJ_VAL(newNative(function)));
    table_set(&vm.listMethods, AS_STRING(vm.stack[0]),vm.stack[1]);
    pop();
    pop();
}

void init_vm(){
  reset_stack();
  vm.objects = NULL;
  vm.bytes_alocated = 0;
  vm.next_gc = 1024 * 1024;
  vm.gray_count = 0;
  vm.gray_capacity = 0;
  vm.gray_stack = NULL;

  vm.next_op_wide--;
  init_table(&vm.globals);
  init_table(&vm.strings);
  init_table(&vm.listMethods);
  vm.init_string = NULL;
  vm.init_string = copy_string("init",4);


  //native function definition
  define_all_natives();
  //native method definition
  define_ntv_methods("length", length_method);

}

void free_vm() {
  /* code */
  free_table(&vm.globals);
  free_table(&vm.strings);
  free_table(&vm.listMethods);
  vm.init_string = NULL;
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
            case OBJ_BOUND_METHOD:{
                ObjBoundMethod* bound = AS_BOUND_METHOD(callee);
                vm.stack_top[-arg_count - 1] = bound->receiver;
                return call(bound->method,arg_count);
            }
            case OBJ_CLASS:{
                ObjClass* klass = AS_CLASS(callee);
                vm.stack_top[-arg_count - 1] = OBJ_VAL(newInstance(klass));
                Value initializer;
                if(table_get(&klass->methods, vm.init_string,&initializer)){
                    return call(AS_CLOSURE(initializer), arg_count);
                }else if (arg_count != 0){
                    runtime_error("Expected 0 arguments but got %d.", arg_count);
                    return false;
                }
                return true;
            }
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
static bool call_native_methods(Value val, int arg_count){
    NativeFn native = AS_NATIVE(val);
    Value result = native(arg_count, vm.stack_top-arg_count-1);
    vm.stack_top -= arg_count + 1;
    push(result);
    return true;
}

void append_to_list(ObjList* list, Value value){
    write_val_array(&list->values,value);
}

void store_to_list(ObjList* list, int index, Value value){
    list->values.values[index] = value;
}
Value index_from_list(ObjList* list, int index){
    return list->values.values[index];
}
void delete_from_list(ObjList* list, int index){
    for (int i = index; i < list->values.count-1; i++) {
        list->values.values[i] = list->values.values[i+1];
    }
    list->values.values[list->values.count - 1] = NIL_VAL;
    list->values.count--;
}

bool is_valid_list_index(ObjList* list, int index){
    if (index < 0 || index > list->values.count - 1){
        return false;
    }
    return true;
}

static bool invoke_from_class(ObjClass* klass, ObjString* name, int arg_count){
    Value method;
    if (!table_get(&klass->methods, name, &method)){
        runtime_error("Undefined property '%s'.", name->chars);
        return false;
    }
    return call(AS_CLOSURE(method),arg_count);
}

static bool invoke(ObjString* name, int arg_count){
    Value receiver = peek(arg_count);
//    if(!IS_INSTANCE(receiver)){
//        runtime_error("Only instances have methods.");
//        return false;
//    }
    if(IS_LIST(receiver)){
        Value value;
        if(table_get(&vm.listMethods, name, &value)){
//            vm.stack_top[-arg_count - 1] = value;
            return call_native_methods(value,arg_count);
        }
    } else if(IS_INSTANCE(receiver)){
        ObjInstance* instance = AS_INSTANCE(receiver);
        Value value;
        if(table_get(&instance->fields, name, &value)){
            vm.stack_top[-arg_count - 1] = value;
            return call_value(value,arg_count);
        }
            return invoke_from_class(instance->klass, name, arg_count);
    } else{
        runtime_error("Only instances and lists have methods.");
        return false;
    }
}
static bool bind_method(ObjClass* klass, ObjString* name){
    Value method;
    if(!table_get(&klass->methods,name,&method)){
        runtime_error("Undefined property '%s'.",name->chars);
        return false;
    }
    ObjBoundMethod* bound = newBoundMethod(peek(0),AS_CLOSURE(method));
    pop();
    push(OBJ_VAL(bound));
    return true;
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

static void define_method(ObjString* name){
    Value method = peek(0);
    ObjClass* klass = AS_CLASS(peek(1));
    table_set(&klass->methods,name,method);
    pop();
}

//@fix
static bool is_falsey(Value value){
  return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static void concatenate(){
  ObjString* b = AS_STRING(peek(0));
  ObjString* a = AS_STRING(peek(1));

  int length = a->length + b->length;
  char* chars = ALLOCATE(char, length + 1);
  memcpy(chars, a->chars, a->length);
  memcpy(chars + a->length, b->chars, b->length);
  chars[length] = '\0';

  ObjString* result = take_string(chars, length);
  pop();
  pop();
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
  #define BINARY_OP(val_type, op,type)\
          do {\
            if(!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))){\
              runtime_error("Operands must be numbers.");\
              return INTERPRET_RUNTIME_ERROR;\
            }\
            \
            type b = AS_NUMBER(pop());\
            type a = AS_NUMBER(pop());\
            push(val_type(a op b)); \
          } while(false);       \

#define BITWISE_OP(val_type, op) \
    do{                          \
         if(!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))){\
              runtime_error("Operands must be numbers.");\
              return INTERPRET_RUNTIME_ERROR;\
         }                       \
                                 \
           int b = AS_NUMBER(pop());               \
           int a = AS_NUMBER(pop());              \
           push(val_type(a op b));\
    }while(false);

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

      case OP_GET_PROPERTY: {
          if (!IS_INSTANCE(peek(0))){
              runtime_error("Only instances have properties.");
              return INTERPRET_RUNTIME_ERROR;
          }

          ObjInstance* instance = AS_INSTANCE(peek(0));
          ObjString* name = READ_STRING();

          Value value;
          if(table_get(&instance->fields, name, &value)){
              pop(); //instance
              push(value);
              break;
          }
          if(!bind_method(instance->klass, name)){
              return INTERPRET_RUNTIME_ERROR;
          }
          break;

      }

      case OP_SET_PROPERTY:{
          if(!IS_INSTANCE(peek(1))){
              runtime_error("Only instances have fields.");
              return INTERPRET_RUNTIME_ERROR;
          }
          ObjInstance* instance = AS_INSTANCE(peek(1));
          table_set(&instance->fields, READ_STRING(), peek(0));

          Value value = pop();
          pop();
          push(value);
          break;
      }


      case OP_GET_SUPER:{
          ObjString* name = READ_STRING();
          ObjClass* superclass = AS_CLASS(pop());
          if(!bind_method(superclass, name)){
              return INTERPRET_RUNTIME_ERROR;
          }
          break;
      }

      case OP_EQUAL: {
        Value b = pop();
        Value a = pop();
        push(BOOL_VAL(vals_equal(a,b)));
        break;
      }
      case OP_GREATER: BINARY_OP(BOOL_VAL, >,double); break;
      case OP_LESS: BINARY_OP(BOOL_VAL, <, double); break;
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
        BINARY_OP(NUMBER_VAL, -,double);
        break;
      }
      case OP_MUL:{
        BINARY_OP(NUMBER_VAL, *, double);
        break;
      }
      case OP_DIV:{
        BINARY_OP(NUMBER_VAL, /, double);
        break;
      }
      case OP_BITWISE_AND:{
          BINARY_OP(NUMBER_VAL, &, int);
          break;
      }
        case OP_BITWISE_OR:{
            BINARY_OP(NUMBER_VAL, |, int);
            break;
        }
        case OP_BITWISE_XOR:{
            BINARY_OP(NUMBER_VAL, ^, int);
            break;
        }
        case OP_LEFT_SHIFT:{
            BINARY_OP(NUMBER_VAL, <<, int);
            break;
        }
        case OP_RIGHT_SHIFT:{
            BINARY_OP(NUMBER_VAL, >>,int);
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

        case OP_INVOKE: {
            ObjString* method = READ_STRING();
            int arg_count = READ_BYTE();
            if(!invoke(method, arg_count)){
                return INTERPRET_RUNTIME_ERROR;
            }
            frame = &vm.frames[vm.frameCount - 1];
            break;
        }
        case OP_BUILD_LIST:{
            ObjList* list = newList();
            uint8_t item_count = READ_BYTE();

            //add items to list
            push(OBJ_VAL(list));//for gc
            for (int i = item_count; i > 0; i--) {
                append_to_list(list, peek(i));
            }
            pop();

            //pop items from stack
            while (item_count-- > 0){
                pop();
            }
            push(OBJ_VAL(list));
            break;
        }
        case OP_WIDE:{
            vm.next_op_wide = 2;
            break;
        }
        case OP_INDEX_SUBSCR:{
            Value index = peek(0);
            Value list = peek(1);

            Value result;
            if(!IS_LIST(list)){
                runtime_error("Invalid type to index into.");
                return INTERPRET_RUNTIME_ERROR;
            }
            ObjList* list_ = AS_LIST(list);
            if(!IS_NUMBER(index)){
                runtime_error("List index is not a number.");
                return INTERPRET_RUNTIME_ERROR;
            }
            int index_ = AS_NUMBER(index);
            if(!is_valid_list_index(list_,index_)){
                runtime_error("List index out of range.");
                return INTERPRET_RUNTIME_ERROR;
            }
            result = index_from_list(list_, AS_NUMBER(index));
            pop();
            pop();
            push(result);
            break;
        }

        case OP_STORE_SUBSCR:{
            Value item = peek(0);
            Value index = peek(1);
            Value list = peek(2);

            if (!IS_LIST(list)){
                runtime_error("Cannot store value in a non-list.");
                return INTERPRET_RUNTIME_ERROR;
            }
            ObjList* list_ = AS_LIST(list);
            if (!IS_NUMBER(index)){
                runtime_error("List index is not a number.");
                return INTERPRET_RUNTIME_ERROR;
            }
            int index_ = AS_NUMBER(index);

            if (!is_valid_list_index(list_, index_)){
                runtime_error("Invalid list index.");
                return INTERPRET_RUNTIME_ERROR;
            }
            store_to_list(list_,index_, item);
            pop();
            pop();
            pop();
             push(item);
//            push(NIL_VAL);
            break;
        }
        case OP_SUPER_INVOKE:{
            ObjString* method = READ_STRING();
            int arg_count = READ_BYTE();
            ObjClass* superclass = AS_CLASS(pop());
            if(!invoke_from_class(superclass,method, arg_count)) {
                return INTERPRET_RUNTIME_ERROR;
            }
            frame = &vm.frames[vm.frameCount-1];
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
      case OP_CLASS:
          push(OBJ_VAL(newClass(READ_STRING())));
          break;


        case OP_INHERIT:{
            Value superclass = peek(1);
            if(!IS_CLASS(superclass)){
                runtime_error("Superclass must be a class.");
                return INTERPRET_RUNTIME_ERROR;
            }
            ObjClass* subclass = AS_CLASS(peek(0));
            table_add_all(&AS_CLASS(superclass)->methods,&subclass->methods);
            pop();//subclass
            break;
        }
        case OP_METHOD:
            define_method(READ_STRING());
            break;
    }
    if(vm.next_op_wide == 1){
        runtime_error("OP_WIDE used on an invalid opcode.");
        return INTERPRET_RUNTIME_ERROR;
    } else if(vm.next_op_wide == 2){
        vm.next_op_wide--;
    }
  }
  #undef READ_BYTE
  #undef READ_SHORT
  #undef READ_CONST
  #undef READ_STRING
  #undef BINARY_OP
  #undef BITWISE_OP
}

InterpretResult interpret(const char* source){
    ObjFunction* function = compile(source);
    if (function == NULL) return INTERPRET_COMPILE_ERROR;

    push(OBJ_VAL(function));
    ObjClosure* closure = newClosure(function);
    pop();
    push(OBJ_VAL(closure));
    call_value(OBJ_VAL(closure), 0);

    return run();
}
