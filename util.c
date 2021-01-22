#include "memory.h"
#include "vm.h"


void define_native(const char* name, NativeFn function) {
    push(OBJ_VAL(copy_string(name, (int)strlen(name))));
    push(OBJ_VAL(newNative(function)));
    table_set(&vm.globals, AS_STRING(vm.stack[0]),vm.stack[1]);
    pop();
    pop();
}



