#include <time.h>


#include "common.h"
#include "lox.h"
#include "native.h"
#include "object.h"
#include "vm.h"

//just hook up native.h and vm.h and can define custom functions...

static Value clock_native(LoxVM* vm, int arg_count, Value* args){
	return NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);

}




const char* native_names[] = {
	"clock"
};

NativeFn native_funcs [] = {
	clock_native
};

void define_all_natives(LoxVM* vm){

	for(uint8_t i = 0; i < sizeof(native_names) / sizeof(native_names[0]); i++){
		define_native(vm, native_names[i], native_funcs[i]);
	}

}
