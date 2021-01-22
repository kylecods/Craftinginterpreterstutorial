#ifndef croto_compiler_h
#define croto_compiler_h

#include "object.h"
#include "lox.h"
#include "vm.h"

ObjFunction* compile(LoxVM* vm, const char* source);
void mark_compiler_roots();

#endif
