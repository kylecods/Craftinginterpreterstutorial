#ifndef croto_compiler_h
#define croto_compiler_h

#include "object.h"

ObjFunction* compile(const char* source);
void mark_compiler_roots();

#endif
