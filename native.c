#include <stdlib.h>
#include "value.h"
#include "memory.h"
#include "native.h"
#include "vm.h"



static Value clock_native(int arg_count, Value* args) {
    return NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
}
static Value sin_native(int arg_count, Value* args){
    double d = AS_NUMBER(*args);
    return NUMBER_VAL((double)sin(d));
}

static Value floor_native(int arg_count, Value* args){
    double d = AS_NUMBER(*args);
    return NUMBER_VAL((double)floor(d));
}

static Value strlen_native(int arg_count, Value* args){
    char *f = AS_CSTRING(*args++);
    return NUMBER_VAL((double)strlen(f));
}
static Value print_native(int arg_count,Value* args){
    //pass in list of arguments and print them out on one line
    //if exhausted the number of arguments skip a line
    if(arg_count == 0){
        printf("\n");
        return NIL_VAL;
    }
    for (int i = 0; i < arg_count; i++){
        Value value = args[i];
        print_value(value);
        if (i == arg_count-1){
            printf("\n");
        }
    }

    return NIL_VAL;
}
static Value append_native(int arg_count, Value* args){
    if(arg_count != 2 || !IS_LIST(args[0])){
        runtime_error("Expected 2 arguments but got %d.", arg_count);
        return NIL_VAL;
    }
    ObjList* list = AS_LIST(args[0]);
    Value item = args[1];
    append_to_list(list,item);
    return NIL_VAL;
}
static Value lenList_native(int arg_count, Value* args){
    if(arg_count != 1 || !IS_LIST(args[0])){
        runtime_error("Expected 1 arguments but got %d.", arg_count);
        return NIL_VAL;
    }
    ObjList* list = AS_LIST(args[0]);
    return NUMBER_VAL(list->values.count);
}

static Value delete_native(int arg_count, Value* args){
    if(arg_count != 2 || !IS_LIST(args[0]) || !IS_NUMBER(args[1])){
        runtime_error("Expected 3 arguments but got %d.", arg_count);
        return NIL_VAL;
    }
    ObjList* list = AS_LIST(args[0]);
    int index = AS_NUMBER(args[1]);
    if (!is_valid_list_index(list,index)){
        runtime_error("Invalid list index.");
        return NIL_VAL;
    }
    delete_from_list(list,index);
    return NIL_VAL;
}
static Value readin_native(int arg_count, Value* args){
    if(arg_count != 1){
        runtime_error("Expected 1 argument but got %d.", arg_count);
        return NIL_VAL;
    }
    Value input = args[0];

    if(!IS_STRING(input)){
        runtime_error("Expected a string as the argument.");
        return NIL_VAL;
    }
    printf("%s", AS_CSTRING(input));

    int current_size = 140;
    char* line = ALLOCATE(char, current_size);

    if(line == NULL){
        runtime_error("Memory error on input()!");
        return NIL_VAL;
    }

    int c = EOF;
    int length = 0;
    while ((c = getchar()) != '\n' && c != EOF){
        line[length++] = (char)c;

        if(length + 1 == current_size){
            int old_size = current_size;
            current_size = GROW_CAPACITY(current_size);
            line = GROW_ARRAY(line, char, old_size, current_size);

            if(line == NULL){
                printf("Unable to allocate memory.\n");
                exit(71);
            }

        }

    }
    if (length != current_size){
        line = GROW_ARRAY(line,char, current_size,length +1);
    }
    line[length] = '\0';

    return OBJ_VAL(take_string(line,length));
}

void define_all_natives(){
    char *nativeNames[] = {
            "clock",
            //"sin",
            "floor",
            "strlength",
            "print",
            "input",
            "append",
            "len",
            "delete",

    };

    NativeFn nativeFunctions[] = {
            clock_native,
            //sin_native,
            floor_native,
            strlen_native,
            print_native,
            readin_native,
            append_native,
            lenList_native,
            delete_native,

    };


    for (uint8_t i = 0; i < sizeof(nativeNames) / sizeof(nativeNames[0]); ++i) {
        define_native(nativeNames[i], nativeFunctions[i]);
    }
}

