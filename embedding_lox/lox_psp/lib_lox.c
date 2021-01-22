#include <pspkernel.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspctrl.h>


#include "lib_lox.h"

#include "lox.h"
#include "memory.h"
#include "object.h"
#include "vm.h"

#define printf pspDebugScreenPrintf

static SceCtrlData binput;

//input
static Value psp_readin(LoxVM* vm, int arg_count, Value* args){
	//sceCtrlSetSamplingCycle(0);
	//sceCtrlSetSamplingMode(PSP_CTRL_MODE_DIGITAL);
	sceCtrlReadBufferPositive(&binput,1);
	return NIL_VAL;
}

static Value psp_up(LoxVM* vm, int arg_count, Value* args){
	return BOOL_VAL(binput.Buttons & PSP_CTRL_UP);
}
static Value psp_down(LoxVM* vm, int arg_count, Value* args){
	return BOOL_VAL(binput.Buttons & PSP_CTRL_DOWN);
}

static Value psp_left(LoxVM* vm, int arg_count, Value* args){
	return BOOL_VAL(binput.Buttons & PSP_CTRL_LEFT);
}

static Value psp_right(LoxVM* vm, int arg_count, Value* args){
	return BOOL_VAL(binput.Buttons & PSP_CTRL_RIGHT);
}

static Value psp_triangle(LoxVM* vm, int arg_count, Value* args){
	return BOOL_VAL(binput.Buttons & PSP_CTRL_TRIANGLE);
}
static Value psp_circle(LoxVM* vm, int arg_count, Value* args){
	return BOOL_VAL(binput.Buttons & PSP_CTRL_CIRCLE);
}
static Value psp_cross(LoxVM* vm, int arg_count, Value* args){
	return BOOL_VAL(binput.Buttons & PSP_CTRL_CROSS);
}

static Value psp_square(LoxVM* vm, int arg_count, Value* args){
	return BOOL_VAL(binput.Buttons & PSP_CTRL_SQUARE);
}
static Value psp_start(LoxVM* vm, int arg_count, Value* args){
	return BOOL_VAL(binput.Buttons & PSP_CTRL_START);
}
static Value psp_select(LoxVM* vm, int arg_count, Value* args){
	return BOOL_VAL(binput.Buttons & PSP_CTRL_SELECT);
}
static Value psp_ltrigger(LoxVM* vm, int arg_count, Value* args){
	return BOOL_VAL(binput.Buttons & PSP_CTRL_LTRIGGER);
}
static Value psp_rtrigger(LoxVM* vm, int arg_count, Value* args){
	return BOOL_VAL(binput.Buttons & PSP_CTRL_RTRIGGER);
}
static Value psp_analogX(LoxVM* vm, int arg_count, Value* args){
	return NUMBER_VAL(((double)binput.Lx -122.5) / 122.5);
}
static Value psp_analogY(LoxVM* vm, int arg_count, Value* args){
	return NUMBER_VAL(((double)binput.Ly -122.5) / 122.5);
}
//graphics

//debug print
static Value psp_print(LoxVM* vm, int arg_count, Value* args){

	for(int i = 0; i < arg_count; i++){
		Value value = args[i];
		switch(IS_STRING(value)){
			case 1:	
				printf("%s%s", AS_CSTRING(value), "\n");
				break;
			case 0:
				printf("%g%s", AS_NUMBER(value), "\n");
				break;
		}
	}

	return NIL_VAL;
}


const char* native_names[] = {
	"printp",
	"read",
	"up",
	"down",
	"left",
	"right",
	"triangle",
	"circle",
	"cross",
	"square",
	"start",
	"select",
	"analogX",
	"analogY",
	"ltrigger",
	"rtrigger"
};

NativeFn native_funcs[] = {
	psp_print,
	psp_readin,
	psp_up,
	psp_down,
	psp_left,
	psp_right,
	psp_triangle,
	psp_circle,
	psp_cross,
	psp_square,
	psp_start,
	psp_select,
	psp_analogX,
	psp_analogY,
	psp_ltrigger,
	psp_rtrigger
};


void define_all_natives(LoxVM* vm){
	for(uint8_t i = 0; i < sizeof(native_names) / sizeof(native_names[0]); i++){
		define_native(vm, native_names[i], native_funcs[i]);
	}
}
