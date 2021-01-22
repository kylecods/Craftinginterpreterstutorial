#ifndef croto_lox_h
#define croto_lox_h

typedef struct LoxVM LoxVM;


//explict memory management
typedef void *(*LoxReallocFn)(void* memory, size_t old_size, size_t new_size);


typedef enum{
	INTERPRET_OK,
	INTERPRET_COMPILE_ERROR,
	INTERPRET_RUNTIME_ERROR
}InterpretResult;


LoxVM* init_vm(LoxReallocFn reallocfn);
void free_vm(LoxVM* vm);
InterpretResult interpret(LoxVM* vm, const char* source);


#endif


