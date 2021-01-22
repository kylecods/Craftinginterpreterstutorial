//psp standard
#include <pspkernel.h>
#include <pspdebug.h>
#include <pspctrl.h>
#include <stdlib.h>
#include <stdio.h>
//#include <pspdisplay.h>

//lox includes
#include "common.h"
#include "chunk.h"
#include "debug.h"
#include "vm.h"
#include "lox.h"

#include "lib_lox.h"

//ease..
#define printf pspDebugScreenPrintf

#define VERS 1
#define REVS 0
PSP_MODULE_INFO("LOXTEST", PSP_MODULE_USER, VERS, REVS);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER);

int exit_callback(int arg1, int arg2, void* common){
	sceKernelExitGame();
	return 0;
}

int CallbackThread(SceSize args, void* argp){
	int cbid = sceKernelCreateCallback("Exit Callback", exit_callback, NULL);
	sceKernelRegisterExitCallback(cbid);
	sceKernelSleepThreadCB();
	return 0;

}

int SetupCallbacks(void){
	int thid = sceKernelCreateThread("update_thread", CallbackThread, 0x11, 0xFA0, 0, 0);
	if(thid >=0){
		sceKernelStartThread(thid, 0, 0);
	}
	return thid;
}

static void* reallocp(void* memory, size_t old_size, size_t new_size){
	return realloc(memory, new_size);
}

static char* read_file(const char* path){

	FILE* file = fopen(path, "rb");
	if(file == NULL){
		fprintf(stderr , "Could not open file \"%s\".\n", path);
		//@fix error code...
		printf("File could not be opened");
	}
	fseek(file, 0L, SEEK_END);
	size_t file_size = ftell(file);
	rewind(file);

	char* buffer = (char*)malloc(file_size + 1);
	
	if(buffer == NULL){
		fprintf(stderr, "Not enough memory to read \"%s\".\n", path);
		printf("Not enough memory.");
	}
	size_t  bytes_read = fread(buffer, sizeof(char), file_size, file);
	if(bytes_read < file_size){
		fprintf(stderr, "Could not read file \"%s\".\n", path);
		printf("Could not read file.");
	}
	buffer[bytes_read] = '\0'; 
	fclose(file);
	return buffer;
}

static void run_code(LoxVM* vm, char* code){
	InterpretResult result = interpret(vm, code);
	//free(source);
	if(result == INTERPRET_COMPILE_ERROR) printf("Compile Error\n");
	if(result == INTERPRET_RUNTIME_ERROR) printf("Runtime Error\n");

}

static void run_file(LoxVM* vm, const char* path){
	char* source = read_file(path);
	InterpretResult result = interpret(vm, source);
	free(source);
	if(result == INTERPRET_COMPILE_ERROR) printf("Compile Error");
	if(result == INTERPRET_RUNTIME_ERROR) printf("Runtime Error");
}	

int main(){
	LoxVM* vm = init_vm(reallocp);
	SetupCallbacks();
	pspDebugScreenInit();
	//char* code = "var a = \"hehe boy!! love this!!\"; printp(a);";
	const char* path = "umd0:/test.rt";
	//while(true){
	run_file(vm, path);
	//}	
	free_vm(vm);
	return 0; 

}
