#include "memory.h"
#include "vm.h"


void define_native(RotoVM* vm,const char* name, NativeFn function) {
    push(vm,OBJ_VAL(copy_string(vm,name, (int)strlen(name))));
    push(vm,OBJ_VAL(newNative(vm,function)));
    table_set(vm,&vm->globals, AS_STRING(vm->stack[0]),vm->stack[1]);
    pop(vm);
    pop(vm);
}



