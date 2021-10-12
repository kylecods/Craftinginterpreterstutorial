#ifndef file_compiler_h
#define file_compiler_h

#include "object.h"

ObjFunction* compile(RotoVM* vm,const char* source);
void mark_compiler_roots(RotoVM* vm);

#endif
