#include <stdio.h>
#include <stdarg.h>
#include <string.h>


#include "common.h"
#include "debug.h"
#include "vm.h"
#include "compiler.h"
#include "memory.h"
#include "native.h"




static void reset_stack(RotoVM* vm) {
    /* code */
    vm->stack_top = vm->stack;
    vm->frameCount = 0;
    vm->open_upvalues = NULL;
}
void runtime_error(RotoVM* vm,const char *format, ...){
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputs("\n", stderr);

    for (int i = vm->frameCount - 1; i >= 0; i--) {
        CallFrame* frame = &vm->frames[i];
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

    CallFrame* frame = &vm->frames[vm->frameCount - 1];
    size_t instr = frame->ip - frame->closure->function->chunk.code - 1;
    int line = frame->closure->function->chunk.lines[instr];
    fprintf(stderr, "[line %d] in script\n", line);

    reset_stack(vm);
}

//native methods
static Value length_method(RotoVM* vm, int arg_count, Value* args){
    if(arg_count != 0){
        runtime_error(vm,"Expected no arguments but got %d.", arg_count);
        return NIL_VAL;
    }
    ObjList* list = AS_LIST(args[0]);
    return NUMBER_VAL(list->values.count);
}




static void define_ntv_methods(RotoVM* vm, const char* name, NativeFn function) {
    push(vm, OBJ_VAL(copy_string(vm,name, (int)strlen(name))));
    push(vm, OBJ_VAL(newNative(vm,function)));
    table_set(vm,&vm->listMethods, AS_STRING(vm->stack[0]),vm->stack[1]);
    pop(vm);
    pop(vm);
}

RotoVM* init_vm(RotoReallocFn reallocfn){

    RotoVM *vm = reallocfn(NULL, 0, sizeof(RotoVM));

    reset_stack(vm);
    vm->objects = NULL;
    vm->bytes_alocated = 0;
    vm->next_gc = 1024 * 1024;
    vm->gray_count = 0;
    vm->gray_capacity = 0;
    vm->gray_stack = NULL;

    vm->next_op_wide--;
    init_table(&vm->globals);
    init_table(&vm->strings);
    init_table(&vm->listMethods);
    vm->init_string = NULL;
    vm->init_string = copy_string(vm,"init",4);


    //native function definition
    define_all_natives(vm);
    //native method definition
    define_ntv_methods(vm,"length", length_method);

    return vm;

}

void free_vm(RotoVM* vm) {
  /* code */
  free_table(vm,&vm->globals);
  free_table(vm,&vm->strings);
  free_table(vm,&vm->listMethods);
  vm->init_string = NULL;
  free_objects(vm);
}

void push(RotoVM* vm, Value value) {
  /* code */
  *vm->stack_top = value;
  vm->stack_top++;
}

Value pop(RotoVM* vm){
  vm->stack_top--;
  return *vm->stack_top;
}

static Value peek(RotoVM* vm, int distance){
  return vm->stack_top[-1 - distance];
}

static bool call(RotoVM* vm, ObjClosure* closure, int arg_count){
    if(arg_count != closure->function->arity) {
        runtime_error(vm,"Expected %d arguments but got %d.", closure->function->arity, arg_count);
        return false;
    }
    if (vm->frameCount == FRAMES_MAX){
        runtime_error(vm,"Stack overflow.");
        return false;
    }
    CallFrame* frame = &vm->frames[vm->frameCount++];
    frame->closure = closure;
    frame->ip = closure->function->chunk.code;

    frame->slots = vm->stack_top - arg_count - 1;
    return true;

}

static bool call_value(RotoVM* vm, Value callee, int arg_count) {
    if(IS_OBJ(callee)) {
        switch (OBJ_TYPE(callee)) {
            case OBJ_BOUND_METHOD:{
                ObjBoundMethod* bound = AS_BOUND_METHOD(callee);
                vm->stack_top[-arg_count - 1] = bound->receiver;
                return call(vm, bound->method,arg_count);
            }
            case OBJ_CLASS:{
                ObjClass* klass = AS_CLASS(callee);
                vm->stack_top[-arg_count - 1] = OBJ_VAL(newInstance(vm,klass));
                Value initializer;
                if(table_get(&klass->methods, vm->init_string,&initializer)){
                    return call(vm, AS_CLOSURE(initializer), arg_count);
                }else if (arg_count != 0){
                    runtime_error(vm,"Expected 0 arguments but got %d.", arg_count);
                    return false;
                }
                return true;
            }
            case OBJ_CLOSURE:
                return call(vm, AS_CLOSURE(callee), arg_count);

            case OBJ_NATIVE:{
                NativeFn native = AS_NATIVE(callee);
                Value result = native(vm,arg_count, vm->stack_top-arg_count);
                vm->stack_top -= arg_count + 1;
                push(vm, result);
                return true;
            }
            default:
                break;
        }
    }

    runtime_error(vm, "Can only call functions and classes.");
    return false;
}
static bool call_native_methods(RotoVM* vm,Value val, int arg_count){
    NativeFn native = AS_NATIVE(val);
    Value result = native(vm,arg_count, vm->stack_top-arg_count-1);
    vm->stack_top -= arg_count + 1;
    push(vm, result);
    return true;
}

void append_to_list(RotoVM* vm,ObjList* list, Value value){
    write_val_array(vm,&list->values,value);
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

static bool invoke_from_class(RotoVM* vm,ObjClass* klass, ObjString* name, int arg_count){
    Value method;
    if (!table_get(&klass->methods, name, &method)){
        runtime_error(vm, "Undefined property '%s'.", name->chars);
        return false;
    }
    return call(vm, AS_CLOSURE(method),arg_count);
}

static bool invoke(RotoVM* vm, ObjString* name, int arg_count){
    Value receiver = peek(vm,arg_count);
//    if(!IS_INSTANCE(receiver)){
//        runtime_error("Only instances have methods.");
//        return false;
//    }
    if(IS_LIST(receiver)){
        Value value;
        if(table_get(&vm->listMethods, name, &value)){
//            vm.stack_top[-arg_count - 1] = value;
            return call_native_methods(vm,value,arg_count);
        }
    } else if(IS_INSTANCE(receiver)){
        ObjInstance* instance = AS_INSTANCE(receiver);
        Value value;
        if(table_get(&instance->fields, name, &value)){
            vm->stack_top[-arg_count - 1] = value;
            return call_value(vm,value,arg_count);
        }
            return invoke_from_class(vm, instance->klass, name, arg_count);
    } else{
        runtime_error(vm,"Only instances and lists have methods.");
        return false;
    }
}
static bool bind_method(RotoVM* vm, ObjClass* klass, ObjString* name){
    Value method;
    if(!table_get(&klass->methods,name,&method)){
        runtime_error(vm,"Undefined property '%s'.",name->chars);
        return false;
    }
    ObjBoundMethod* bound = newBoundMethod(vm,peek(vm,0),AS_CLOSURE(method));
    pop(vm);
    push(vm, OBJ_VAL(bound));
    return true;
}
static ObjUpvalue* capture_upvalue(RotoVM* vm, Value* local){
    ObjUpvalue* prev_upvalue = NULL;
    ObjUpvalue* upvalue = vm->open_upvalues;
    while (upvalue != NULL && upvalue->location > local){
        prev_upvalue = upvalue;
        upvalue = upvalue->next;
    }
    if (upvalue != NULL && upvalue->location == local){
        return upvalue;
    }
    ObjUpvalue* created_upvalue = newUpvalue(vm,local);
    created_upvalue->next = upvalue;
    if(prev_upvalue == NULL){
        vm->open_upvalues = created_upvalue;
    } else{
        prev_upvalue->next = created_upvalue;
    }
    return created_upvalue;
}

static void close_upvalues(RotoVM* vm, Value* last){
    while (vm->open_upvalues != NULL && vm->open_upvalues->location >= last){
        ObjUpvalue* upvalue = vm->open_upvalues;
        upvalue->closed = *upvalue->location;
        upvalue->location = &upvalue->closed;
        vm->open_upvalues = upvalue->next;
    }
}

static void define_method(RotoVM* vm, ObjString* name){
    Value method = peek(vm,0);
    ObjClass* klass = AS_CLASS(peek(vm,1));
    table_set(vm,&klass->methods,name,method);
    pop(vm);
}

//@fix
bool is_falsey(Value value){
  return IS_NIL(value) ||(IS_BOOL(value) && !AS_BOOL(value));
}

static void concatenate(RotoVM* vm){
  ObjString* b = AS_STRING(peek(vm,0));
  ObjString* a = AS_STRING(peek(vm,1));

  int length = a->length + b->length;
  char* chars = ALLOCATE(vm,char, length + 1);
  memcpy(chars, a->chars, a->length);
  memcpy(chars + a->length, b->chars, b->length);
  chars[length] = '\0';

  ObjString* result = take_string(vm,chars, length);
  pop(vm);
  pop(vm);
  push(vm,OBJ_VAL(result));
}
static InterpretResult run(RotoVM* vm){
    CallFrame* frame = &vm->frames[vm->frameCount - 1];
//    register uint8_t* ip = frame->ip;

  #define READ_BYTE() (*frame->ip++)
  #define READ_CONST() (frame->closure->function->chunk.constants.values[READ_BYTE()])
  #define READ_SHORT() \
          (frame->ip+=2, \
          (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))

  #define READ_STRING() AS_STRING(READ_CONST())
  #define BINARY_OP(val_type, op,type)\
          do {\
            if(!IS_NUMBER(peek(vm,0)) || !IS_NUMBER(peek(vm,1))){\
              runtime_error(vm,"Operands must be numbers.");\
              return INTERPRET_RUNTIME_ERROR;\
            }\
            \
            type b = AS_NUMBER(pop(vm));\
            type a = AS_NUMBER(pop(vm));\
            push(vm,val_type(a op b)); \
          } while(false);       \

#define BITWISE_OP(val_type, op) \
    do{                          \
         if(!IS_NUMBER(peek(vm,0)) || !IS_NUMBER(peek(vm,1))){\
              runtime_error(vm,"Operands must be numbers.");\
              return INTERPRET_RUNTIME_ERROR;\
         }                       \
                                 \
           int b = AS_NUMBER(pop(vm));               \
           int a = AS_NUMBER(pop(vm));              \
           push(vm,val_type(a op b));\
    }while(false);

#ifdef COMPUTED_GOTO

    static void *dispatchTable[] = {
        #define OPCODE(name) &&op_##name,
        #include "opcodes.h"
        #undef OPCODE
    };

    #define INTERPRET_LOOP DISPATCH();
    #define CASE_CODE(name) op_##name

    #ifdef DEBUG_TRACE_EXECUTION
        #define DISPATCH()\
        do{\
            printf("     ");\
        for (Value* slot = vm->stack; slot < vm->stack_top; slot++) {\
            printf("[ ");\
            print_value(*slot);\
            printf(" ]");\
        }\
        printf("\n");\
        disassemble_instr(&frame->closure->function->chunk, (int)(frame->ip - frame->closure->function->chunk.code));\
        goto *dispatchTable[instruction = READ_BYTE()];\
        }while(false)
    #else
        #define DISPATCH()\
            do{\
                goto *dispatchTable[instr = READ_BYTE()];\
            }while(false)
    #endif

#else

#define INTERPRET_LOOP          \
        loop:                   \
            switch (instr = READ_BYTE())

#define DISPATCH() goto loop

#define CASE_CODE(name) case OP_##name

#endif

    uint8_t instr;
    INTERPRET_LOOP {
      CASE_CODE(CONSTANT):{
        Value constant = READ_CONST();
        push(vm,constant);
        DISPATCH();
      }

      CASE_CODE(NIL): push(vm,NIL_VAL); DISPATCH();
      CASE_CODE(TRUE): push(vm,BOOL_VAL(true)); DISPATCH();
      CASE_CODE(FALSE): push(vm,BOOL_VAL(false)); DISPATCH();
      CASE_CODE(POP): pop(vm); DISPATCH();
      CASE_CODE(GET_LOCAL):{
        uint8_t slot = READ_BYTE();
        push(vm,frame->slots[slot]);
        DISPATCH();
      }
      CASE_CODE(SET_LOCAL):{
        uint8_t slot = READ_BYTE();
        frame->slots[slot] = peek(vm,0);
      }
      CASE_CODE(GET_GLOBAL):{//assuming load gl_var to stack
        ObjString* name = READ_STRING();
        Value value;
        if(!table_get(&vm->globals, name, &value)){
          runtime_error(vm,"Undefined variable '%s'.", name->chars);
          return INTERPRET_RUNTIME_ERROR;
        }
        push(vm,value);
        DISPATCH();
      }
      CASE_CODE(DEFINE_GLOBAL): {
        ObjString* name = READ_STRING();
        table_set(vm,&vm->globals, name, peek(vm,0));
        pop(vm);
        DISPATCH();
      }
      CASE_CODE(SET_GLOBAL):{
        ObjString* name = READ_STRING();
        if(table_set(vm,&vm->globals, name, peek(vm,0))){
          table_delete(&vm->globals,name);
          runtime_error(vm,"Undefined variable '%s'.", name->chars);
          return INTERPRET_RUNTIME_ERROR;
        }
        DISPATCH();
      }
      CASE_CODE(GET_UPVALUE):{
          uint8_t slot = READ_BYTE();
          push(vm,*frame->closure->upvalues[slot]->location);
          DISPATCH();
      }
      CASE_CODE(SET_UPVALUE):{
          uint8_t slot = READ_BYTE();
          *frame->closure->upvalues[slot]->location = peek(vm,0);
          DISPATCH();
      }

      CASE_CODE(GET_PROPERTY): {
          if (!IS_INSTANCE(peek(vm,0))){
              runtime_error(vm,"Only instances have properties.");
              return INTERPRET_RUNTIME_ERROR;
          }

          ObjInstance* instance = AS_INSTANCE(peek(vm,0));
          ObjString* name = READ_STRING();

          Value value;
          if(table_get(&instance->fields, name, &value)){
              pop(vm); //instance
              push(vm,value);
              DISPATCH();
          }
          if(!bind_method(vm,instance->klass, name)){
              return INTERPRET_RUNTIME_ERROR;
          }
          DISPATCH();

      }

      CASE_CODE(SET_PROPERTY):{
          if(!IS_INSTANCE(peek(vm,1))){
              runtime_error(vm,"Only instances have fields.");
              return INTERPRET_RUNTIME_ERROR;
          }
          ObjInstance* instance = AS_INSTANCE(peek(vm,1));
          table_set(vm,&instance->fields, READ_STRING(), peek(vm,0));

          Value value = pop(vm);
          pop(vm);
          push(vm,value);
          DISPATCH();
      }


      CASE_CODE(GET_SUPER):{
          ObjString* name = READ_STRING();
          ObjClass* superclass = AS_CLASS(pop(vm));
          if(!bind_method(vm,superclass, name)){
              return INTERPRET_RUNTIME_ERROR;
          }
          DISPATCH();
      }

      CASE_CODE(EQUAL): {
        Value b = pop(vm);
        Value a = pop(vm);
        push(vm,BOOL_VAL(vals_equal(a,b)));
        DISPATCH();
      }
      CASE_CODE(GREATER): BINARY_OP(BOOL_VAL, >,double); DISPATCH();
      CASE_CODE(LESS): BINARY_OP(BOOL_VAL, <, double); DISPATCH();
      CASE_CODE(ADD):{
        if(IS_STRING(peek(vm,0)) && IS_STRING(peek(vm,1))){
          concatenate(vm);
        }else if(IS_NUMBER(peek(vm,0)) && IS_NUMBER(peek(vm,1))){
          double b = AS_NUMBER(pop(vm));
          double a = AS_NUMBER(pop(vm));
          push(vm,NUMBER_VAL(a+b));
        }else{
          runtime_error(vm,"Operands must be two numbers or two strings.");
          return INTERPRET_RUNTIME_ERROR;
        }
        DISPATCH();
      }
      CASE_CODE(SUB):{
        BINARY_OP(NUMBER_VAL, -,double);
        DISPATCH();
      }
      CASE_CODE(MUL):{
        BINARY_OP(NUMBER_VAL, *, double);
        DISPATCH();
      }
      CASE_CODE(DIV):{
        BINARY_OP(NUMBER_VAL, /, double);
        DISPATCH();
      }
      CASE_CODE(BITWISE_AND):{
          BINARY_OP(NUMBER_VAL, &, int);
          DISPATCH();
      }
        CASE_CODE(BITWISE_OR):{
            BINARY_OP(NUMBER_VAL, |, int);
            DISPATCH();
        }
        CASE_CODE(BITWISE_XOR):{
            BINARY_OP(NUMBER_VAL, ^, int);
            DISPATCH();
        }
        CASE_CODE(LEFT_SHIFT):{
            BINARY_OP(NUMBER_VAL, <<, int);
            DISPATCH();
        }
        CASE_CODE(RIGHT_SHIFT):{
            BINARY_OP(NUMBER_VAL, >>,int);
            DISPATCH();
        }
      CASE_CODE(NOT):
        push(vm,BOOL_VAL(is_falsey(pop(vm))));
        DISPATCH();
      CASE_CODE(NEGATE):{
        if(!IS_NUMBER(peek(vm,0))){
          runtime_error(vm,"Operand must be a number.");
          return INTERPRET_RUNTIME_ERROR;
        }
        push(vm,NUMBER_VAL(-AS_NUMBER(pop(vm))));
        DISPATCH();
      }

      CASE_CODE(JUMP): {
        uint16_t offset = READ_SHORT();
        frame->ip += offset;
        DISPATCH();
      }

      CASE_CODE(JUMP_IF_FALSE): {
        uint16_t offset = READ_SHORT();
        if(is_falsey(peek(vm,0))) frame->ip += offset;
        DISPATCH();
      }
      CASE_CODE(LOOP): {
        uint16_t offset = READ_SHORT();
        frame->ip -= offset;
        DISPATCH();
      }
        CASE_CODE(CALL): {
            int arg_count = READ_BYTE();
            if (!call_value(vm,peek(vm,arg_count), arg_count)) {
                return INTERPRET_RUNTIME_ERROR;
            }
            frame = &vm->frames[vm->frameCount - 1];
            DISPATCH();
        }

        CASE_CODE(INVOKE): {
            ObjString* method = READ_STRING();
            int arg_count = READ_BYTE();
            if(!invoke(vm,method, arg_count)){
                return INTERPRET_RUNTIME_ERROR;
            }
            frame = &vm->frames[vm->frameCount - 1];
            DISPATCH();
        }
        CASE_CODE(BUILD_LIST):{
            ObjList* list = newList(vm);
            uint8_t item_count = READ_BYTE();

            //add items to list
            push(vm,OBJ_VAL(list));//for gc
            for (int i = item_count; i > 0; i--) {
                append_to_list(vm,list, peek(vm,i));
            }
            pop(vm);

            //pop items from stack
            while (item_count-- > 0){
                pop(vm);
            }
            push(vm,OBJ_VAL(list));
            DISPATCH();
        }
        CASE_CODE(WIDE):{
            vm->next_op_wide = 2;
            DISPATCH();
        }
        CASE_CODE(INDEX_SUBSCR):{
            Value index = peek(vm,0);
            Value list = peek(vm,1);

            Value result;
            if(!IS_LIST(list)){
                runtime_error(vm,"Invalid type to index into.");
                return INTERPRET_RUNTIME_ERROR;
            }
            ObjList* list_ = AS_LIST(list);
            if(!IS_NUMBER(index)){
                runtime_error(vm,"List index is not a number.");
                return INTERPRET_RUNTIME_ERROR;
            }
            int index_ = AS_NUMBER(index);
            if(!is_valid_list_index(list_,index_)){
                runtime_error(vm,"List index out of range.");
                return INTERPRET_RUNTIME_ERROR;
            }
            result = index_from_list(list_, AS_NUMBER(index));
            pop(vm);
            pop(vm);
            push(vm,result);
            DISPATCH();
        }

        CASE_CODE(STORE_SUBSCR):{
            Value item = peek(vm,0);
            Value index = peek(vm,1);
            Value list = peek(vm,2);

            if (!IS_LIST(list)){
                runtime_error(vm,"Cannot store value in a non-list.");
                return INTERPRET_RUNTIME_ERROR;
            }
            ObjList* list_ = AS_LIST(list);
            if (!IS_NUMBER(index)){
                runtime_error(vm,"List index is not a number.");
                return INTERPRET_RUNTIME_ERROR;
            }
            int index_ = AS_NUMBER(index);

            if (!is_valid_list_index(list_, index_)){
                runtime_error(vm,"Invalid list index.");
                return INTERPRET_RUNTIME_ERROR;
            }
            store_to_list(list_,index_, item);
            pop(vm);
            pop(vm);
            pop(vm);
             push(vm,item);
//            push(NIL_VAL);
            DISPATCH();
        }
        CASE_CODE(SUPER_INVOKE):{
            ObjString* method = READ_STRING();
            int arg_count = READ_BYTE();
            ObjClass* superclass = AS_CLASS(pop(vm));
            if(!invoke_from_class(vm,superclass,method, arg_count)) {
                return INTERPRET_RUNTIME_ERROR;
            }
            frame = &vm->frames[vm->frameCount-1];
            DISPATCH();
        }


        CASE_CODE(CLOSURE):{
            ObjFunction* function = AS_FUNCTION(READ_CONST());
            ObjClosure* closure = newClosure(vm,function);
            push(vm,OBJ_VAL(closure));
            for (int i = 0; i < closure->upvalue_count; ++i) {
                uint8_t is_local = READ_BYTE();
                uint8_t index = READ_BYTE();
                if(is_local){
                    closure->upvalues[i] = capture_upvalue(vm,frame->slots + index);
                } else{
                    closure->upvalues[i] =closure->upvalues[index];
                }
            }
            DISPATCH();
        }
        CASE_CODE(CLOSE_UPVALUE):
            close_upvalues(vm,vm->stack_top - 1);
            pop(vm);
            DISPATCH();
      CASE_CODE(RETURN):{
          Value result = pop(vm);
          close_upvalues(vm,frame->slots);
          vm->frameCount--;
          if(vm->frameCount == 0){
              pop(vm);
              return INTERPRET_OK;
          }
          vm->stack_top = frame->slots;
          push(vm,result);
          frame = &vm->frames[vm->frameCount-1];
          DISPATCH();
      }
      CASE_CODE(CLASS):
          push(vm,OBJ_VAL(newClass(vm,READ_STRING())));
          DISPATCH();


        CASE_CODE(INHERIT):{
            Value superclass = peek(vm,1);
            if(!IS_CLASS(superclass)){
                runtime_error(vm,"Superclass must be a class.");
                return INTERPRET_RUNTIME_ERROR;
            }
            ObjClass* subclass = AS_CLASS(peek(vm,0));
            table_add_all(vm,&AS_CLASS(superclass)->methods,&subclass->methods);
            pop(vm);//subclass
            DISPATCH();
        }
        CASE_CODE(METHOD):
            define_method(vm,READ_STRING());
            DISPATCH();
    }

  #undef READ_BYTE
  #undef READ_SHORT
  #undef READ_CONST
  #undef READ_STRING
  #undef BINARY_OP
  #undef BITWISE_OP

  return INTERPRET_RUNTIME_ERROR;
}

InterpretResult interpret(RotoVM* vm,const char* source){
    ObjFunction* function = compile(vm,source);
    if (function == NULL) return INTERPRET_COMPILE_ERROR;

    push(vm,OBJ_VAL(function));
    ObjClosure* closure = newClosure(vm,function);
    pop(vm);
    push(vm,OBJ_VAL(closure));
    call_value(vm,OBJ_VAL(closure), 0);

    return run(vm);
}