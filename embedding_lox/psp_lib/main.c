#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#include "common.h"
#include "chunk.h"
#include "debug.h"
#include "lox.h"
#include "vm.h"

static void repl(LoxVM* vm){
  char line[1024];
  for (;;){
    /* code */
    printf("> ");

    if(!fgets(line, sizeof(line), stdin)){
      printf("\n");
      break;
    }

    interpret(vm,line);
  }
}
//read binary file
static char* read_file(const char* path){
  FILE *file = fopen(path, "rb");

  if(file == NULL){
    fprintf(stderr, "Could not open file \"%s\".\n", path);
    exit(74);
  }

  fseek(file, 0L, SEEK_END);
  size_t file_size = ftell(file);
  rewind(file);

  char *buffer = (char*)malloc(file_size +1);

  if(buffer == NULL){
    fprintf(stderr, "Not enough memory to read \"%s\".\n",path);
    exit(74);
  }
  size_t bytes_read = fread(buffer, sizeof(char), file_size, file);

  if(bytes_read < file_size){
    fprintf(stderr, "Could not read file \"%s\".\n", path);
    exit(74);
  }

  buffer[bytes_read] = '\0';

  fclose(file);
  return buffer;
}


static void run_file(LoxVM* vm, const char* path){
  char *source = read_file(path);
  InterpretResult result = interpret(vm, source);

  free(source);

  if(result == INTERPRET_COMPILE_ERROR) exit(65);
  if(result == INTERPRET_RUNTIME_ERROR) exit(70);
}


//always declare in main, later will follow wren implementation of having default in vm.c..
static void* reallocate(void* memory, size_t old_size, size_t new_size){

	return realloc(memory, new_size);
}

int main(int argc, char **argv){
  LoxVM* vm = init_vm(reallocate);
  //Chunk ch;
  //init_chunk(&ch);
  

  //instruction test
  // write_chunk(&ch, OP_CONSTANT,123);
  // write_chunk(&ch, constant,123);
  //
  // write_chunk(&ch, OP_CONSTANT,123);
  // write_chunk(&ch, add_constant(&ch, 3.4),123);
  //
  // write_chunk(&ch, OP_ADD,123);
  //
  // write_chunk(&ch, OP_CONSTANT,123);
  // write_chunk(&ch, add_constant(&ch, 5.6),123);
  //
  // write_chunk(&ch, OP_DIV,123);
  //
  // write_chunk(&ch, OP_RETURN,123);
  //
  //

  if(argc == 1){
    repl(vm);
  }else if(argc == 2){
    run_file(vm, argv[1]);
  }else{
    fprintf(stderr, "Usage: croto [path]\n");
    exit(64);
  }
  free_vm(vm);
  // free_chunk(&ch);

  return 0;
}
