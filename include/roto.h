#ifndef file_roto_h
#define file_roto_h

typedef struct _rotoVM RotoVM;

typedef void *(*RotoReallocFn)(void* memory, size_t old_size, size_t new_size);


typedef enum{
	INTERPRET_OK,
	INTERPRET_COMPILE_ERROR,
	INTERPRET_RUNTIME_ERROR
}InterpretResult;


RotoVM* init_vm(RotoReallocFn reallocfn);
void free_vm(RotoVM* vm);
InterpretResult interpret(RotoVM* vm, const char* source);


#endif