#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "chunk.h"
#include "compiler.h"
#include "scanner.h"
#include "memory.h"

#ifndef DEBUG_PRINT_CODE
#include "debug.h"
#endif

/*
expressions
expr :=  num | string|
        |expr ''+' expr //infix expression
        |expr '-' expr //infix expression
        |expr '*' expr //infix expression
        |expr '/' expr //infix expression
        |'(' expr ')' // prefix expression
        |'-' expr // prefix expression
  the idea is grammar can be represented using functions.

  ***tutorial language has the grammar*** //might emulate this
  declaration := class_decl
                | func_decl
                | var_decl
                |statement

  statement := expr_stmt
              | for_stmt
              | if_stmt
              |print_stmt
              |return_stmt
              |while_stmt
              |block
  block := '{' declaration* '}' ;
*/

typedef struct{
  /* data */
  Token current;
  Token previous;
  bool hadError;
  bool panicMode;
} Parser;

typedef enum{
  PREC_NONE,
  PREC_ASSIGNMENT, // =
  PREC_OR, // or
  PREC_AND, // and
  PREC_EQUALITY, // == !=
  PREC_COMPARISON, //< > <= >=
  PREC_BITWISE_OR, // |
  PREC_BITWISE_XOR, // ^
  PREC_BITWISE_AND, // &
  PREC_LEFT_SHIFT, // <<
  PREC_RIGHT_SHIFT, // >>
  PREC_TERM, // + -
  PREC_FACTOR, // * /
  PREC_UNARY, // ! -, pre ++, pre --
  PREC_CALL, // . () []
  PREC_PRIMARY

} Precedence;

typedef void (*ParseFn)(RotoVM* vm, bool can_assign);

typedef struct{
  ParseFn prefix;
  ParseFn infix;
  Precedence precedence;
}ParseRule;

typedef struct{
  Token name;
  int depth;
  bool is_captured;
}Local;

typedef struct{
    uint8_t index;
    bool is_local;
}Upvalue;
typedef enum {
   TYPE_FUNCTION,
   TYPE_INITIALIZER,
   TYPE_METHOD,
   TYPE_SCRIPT
}FunctionType;

typedef struct Compiler{
    struct Compiler* enclosing;
    ObjFunction* function;
    FunctionType type;
    Local locals[UINT8_COUNT];
    int local_count;
    Upvalue upvalues[UINT8_COUNT];
    int scope_depth;
}Compiler;

typedef struct ClassCompiler{
    struct ClassCompiler* enclosing;
    Token name;
    bool has_super_class;
}ClassCompiler;

Parser parser;
ClassCompiler* current_class = NULL;
Compiler* current = NULL;


static Chunk* current_chunk(){
  return &current->function->chunk;
}

static void error_at(Token *token, const char* message) {
  if(parser.panicMode) return;
  parser.panicMode = true;
  /* code */
  fprintf(stderr, "[line %d] ERROR",token->line);
  if (token->type == TOKEN_EOF) {
    /* code */
    fprintf(stderr, " at end");
  }else if (token->type == TOKEN_ERROR){

  }else{
    fprintf(stderr, " at '%.*s'\n",token->length, token->start);
  }
  fprintf(stderr, ": %s\n", message);
  parser.hadError = true;
}

static void error(const char* msg){
  error_at(&parser.previous, msg);
}

static void error_at_current(const char* message){
  error_at(&parser.current, message);
}
//consume the token and move to the next
static void advance(){
  parser.previous = parser.current;

  for(;;){
    parser.current = scan_token();
    if(parser.current.type != TOKEN_ERROR) break;

    error_at_current(parser.current.start);
  }
}

static void consume(TokenType type, const char *message){
  if(parser.current.type == type){
    advance();
    return;
  }
  error_at_current(message);
}
static bool check(TokenType type){
  return parser.current.type == type;
}
static bool match(TokenType type){
  if(!check(type)) return false;
  advance();
  return true;
}
static void emit_byte(RotoVM* vm, uint8_t byte) {
  /* code */
  write_chunk(vm,current_chunk(), byte, parser.previous.line);
}
static void emit_bytes(RotoVM* vm,uint8_t byte1, uint8_t byte2) {
  emit_byte(vm,byte1);
  emit_byte(vm,byte2);
}
static void emit_loop(RotoVM* vm,int loop_start){
  emit_byte(vm,OP_LOOP);

  int offset = current_chunk()->count - loop_start + 2;
  if(offset > UINT16_MAX) error("Loop body too large.");

  emit_byte(vm,(offset >> 8) & 0xff);
  emit_byte(vm,offset & 0xff);
}
static int emit_jmp(RotoVM* vm,uint8_t instruction){
  emit_byte(vm,instruction);
  emit_byte(vm,0xff);
  emit_byte(vm,0xff);
  return current_chunk()->count - 2;
}
static void emit_return(RotoVM* vm) {
    if(current->type == TYPE_INITIALIZER){
        emit_bytes(vm,OP_GET_LOCAL, 0);
    } else {
        emit_byte(vm,OP_NIL);
    }
    emit_byte(vm,OP_RETURN);
}
static uint8_t make_constant(RotoVM* vm,Value value){
  int constant = add_constant(vm,current_chunk(), value);
  if(constant > UINT8_MAX){
    error("Too many constants in one chunk");
    return 0;
  }
  return (uint8_t)constant;
}
static void emit_constant(RotoVM* vm,Value value) {
  emit_bytes(vm,OP_CONSTANT, make_constant(vm,value));
}

static void patch_jmp(int offset){
  // -2 to adjust for the bytecode for the jump offset itself
  int jump = current_chunk()->count - offset - 2;

  if(jump > UINT16_MAX){
    error("Too much code to jump over.");
  }
  current_chunk()->code[offset] = (jump >> 8) & 0xff;
  current_chunk()->code[offset + 1] = jump & 0xff;
}

static void init_compiler(RotoVM* vm,Compiler* compiler,FunctionType type){
    compiler->enclosing = current;
    compiler->function = NULL;
    compiler->type = type;
    compiler->local_count = 0;
    compiler->scope_depth = 0;
    compiler->function = newFunction(vm);

    current = compiler;

    if (type != TYPE_SCRIPT){
        current->function->name = copy_string(vm,parser.previous.start, parser.previous.length);
    }

    Local* local = &current->locals[current->local_count++];
    local->depth = 0;
    local->is_captured = false;
    if(type != TYPE_FUNCTION) {
        local->name.start = "this";
        local->name.length = 4;
    } else{
        local->name.start = "";
        local->name.length = 0;

    }
}


static ObjFunction* end_compiler(RotoVM* vm) {
    emit_return(vm);
    ObjFunction* function = current->function;
#ifndef DEBUG_PRINT_CODE
    if(!parser.hadError){
      disassembleChunk(current_chunk(), function->name != NULL
                        ? function->name->chars: "<script>");
    }

#endif
    current = current->enclosing;
    return function;
}

static void begin_scope(){
  current->scope_depth++;
}

static void end_scope(RotoVM* vm){
    current->scope_depth--;
  while (current->local_count > 0 && current->locals[current->local_count - 1].depth >
          current->scope_depth) {
      if (current->locals[current->local_count - 1].is_captured) {
            emit_byte(vm,OP_CLOSE_UPVALUE);
      }else{
          emit_byte(vm,OP_POP);//optimization use a pop instr with an operand to pop many at once
      }
    current->local_count--;
  }
}

//foward declaration // many like this is not good. bruh dont do this in own..
static void expression(RotoVM* vm);
static void statement(RotoVM* vm);
static void declaration(RotoVM* vm);
static void and_(RotoVM* vm,bool can_assign);
static ParseRule* get_rule(TokenType type);
static void parse_precedence(RotoVM* vm,Precedence precedence);
static uint8_t parse_variable(RotoVM* vm, const char* error_msg);
static void define_variable(RotoVM* vm,uint8_t global);
static void var_decl(RotoVM* vm);
static void mark_initialized();
static void fun_declaration(RotoVM* vm);
static uint8_t argument_list(RotoVM* vm);
static int resolve_local(Compiler* compiler, Token* name);
static int resolve_upvalue(Compiler* compiler, Token* name);
static void named_variable(RotoVM* vm,Token name, bool can_assign);
static void call(RotoVM* vm,bool can_assign);
static uint8_t identifier_constant(RotoVM* vm,Token* name);
static void decl_variable();
static void variable(RotoVM* vm,bool can_assign);
static bool identifiers_equal(Token* a, Token* b);
static void add_local(Token name);
static Token synthetic_token(const char* text);
static void class_declaration(RotoVM* vm);







static void expression(RotoVM* vm){
  parse_precedence(vm,PREC_ASSIGNMENT);
}

static void block(RotoVM* vm){
  while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
    declaration(vm);
  }

  consume(TOKEN_RIGHT_BRACE, "Expect '}' after block.");
}
static void function(RotoVM* vm,FunctionType type){
    Compiler compiler;
    init_compiler(vm,&compiler, type);
    begin_scope();

    //compile the parameter list
    consume(TOKEN_LEFT_PAREN,"Expect '(' after function name.");
    if(!check(TOKEN_RIGHT_PAREN)){
        do {
            current->function->arity++;
            if(current->function->arity > 255){
                error_at_current("Can't have more than 255 parameters.");
            }
            //parsing
            uint8_t paramConstant = parse_variable(vm,"Expect parameter name");
            define_variable(vm,paramConstant);
        } while (match(TOKEN_COMMA));
    }
    consume(TOKEN_RIGHT_PAREN,"Expect ')' after parameters.");

    //The body
    consume(TOKEN_LEFT_BRACE, "Expect '{' before function body.");
    block(vm);

    //Create the function object.
    ObjFunction* function = end_compiler(vm);
    emit_bytes(vm,OP_CLOSURE,make_constant(vm,OBJ_VAL(function)));

        for (int i = 0; i < function->upvalue_count; ++i) {
            emit_byte(vm,compiler.upvalues[i].is_local ? 1 : 0);
            emit_byte(vm,compiler.upvalues[i].index);
        }

}
static void method(RotoVM* vm){
    consume(TOKEN_IDENTIFIER, "Expect method name.");
    uint8_t constant = identifier_constant(vm, &parser.previous);
    FunctionType type = TYPE_METHOD;
    if (parser.previous.length == 4 && memcmp(parser.previous.start, "init", 4) == 0){
        type = TYPE_INITIALIZER;
    }
    function(vm,type);
    emit_bytes(vm,OP_METHOD, constant);
}
static void class_declaration(RotoVM* vm){
    consume(TOKEN_IDENTIFIER,"Expected class name.");
    Token class_name = parser.previous;
    uint8_t name_constant = identifier_constant(vm,&parser.previous);
    decl_variable();

    emit_bytes(vm,OP_CLASS, name_constant);
    define_variable(vm,name_constant);

    ClassCompiler classCompiler;
    classCompiler.name = parser.previous;
    classCompiler.has_super_class = false;
    classCompiler.enclosing = current_class;
    current_class = &classCompiler;

    if(match(TOKEN_LESS)){
        consume(TOKEN_IDENTIFIER,"Expect superclass name.");
        variable(vm,false);
        if(identifiers_equal(&class_name, &parser.previous)){
            error("A class can't inherit from itself.");
        }
        begin_scope();
        add_local(synthetic_token("super"));
        define_variable(vm,0);
        named_variable(vm,class_name,false);
        emit_byte(vm,OP_INHERIT);
        classCompiler.has_super_class = true;
    }

    named_variable(vm,class_name, false);
    consume(TOKEN_LEFT_BRACE, "Expect '{' before the class body.");
    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)){
        method(vm);
    }
    consume(TOKEN_RIGHT_BRACE, "Expect '}' after the class body.");
    emit_byte(vm,OP_POP);

    if (classCompiler.has_super_class){
        end_scope(vm);
    }
    current_class = current_class->enclosing;
}
static void fun_declaration(RotoVM* vm){
    uint8_t global = parse_variable(vm,"Expect function name.");
    mark_initialized();
    function(vm,TYPE_FUNCTION);
    define_variable(vm,global);
}
static void var_decl(RotoVM* vm) {
  uint8_t global = parse_variable(vm,"Expected variable name.");

  if(match(TOKEN_EQUAL)){
    expression(vm);
  }
  else{
    emit_byte(vm,OP_NIL);
  }
  consume(TOKEN_SEMICOLON, "Expected ';' after variable declaration.");

  define_variable(vm,global);
}

void expr_stmt(RotoVM* vm) {
  expression(vm);
  consume(TOKEN_SEMICOLON, "Expect ';' after expression.");
  emit_byte(vm,OP_POP);

}

static void for_stmt(RotoVM* vm) {

  //declaration part: var i = 0;
  begin_scope();
  consume(TOKEN_LEFT_PAREN, "Expect '(' after 'for'.");
  if (match(TOKEN_SEMICOLON)) {
    //no initializer
  }else if(match(TOKEN_VAR)){
    var_decl(vm);
  }else{
    expr_stmt(vm);
  }

  int loop_start = current_chunk()->count;

  //conditional part: eg. i < 3; ....
  int exit_jmp = -1;
  if(!match(TOKEN_SEMICOLON)){
    expression(vm);
    consume(TOKEN_SEMICOLON, "Expected ';' after loop condition.");

    //jump out of the loop if the condition is false
    exit_jmp = emit_jmp(vm,OP_JUMP_IF_FALSE);
    emit_byte(vm,OP_POP); //condition
  }

  //increment part: i++;
  if(!match(TOKEN_RIGHT_PAREN)){
    int body_jump = emit_jmp(vm,OP_JUMP);

    int increment_start = current_chunk()->count;
    expression(vm);
    emit_byte(vm,OP_POP);
    consume(TOKEN_RIGHT_PAREN, "Expected ')' after for clauses");

    emit_loop(vm,loop_start);
    loop_start = increment_start;
    patch_jmp(body_jump);

  }

  statement(vm);

  emit_loop(vm,loop_start);

  if(exit_jmp != -1){
    patch_jmp(exit_jmp);
    emit_byte(vm,OP_POP);
  }

  end_scope(vm);
}
/*
if_stmt := '(' expr; ')' '{' statement; '}' else '{' statement '}'

  VM
    condition
      false: JUMP then POP move to else
      true: OP_POP move to THEN
    THEN
      stmt then JUMP over else


*/

static void if_stmt(RotoVM* vm){
    consume(TOKEN_LEFT_PAREN, "Expected '(' after 'if'.");
    expression(vm);
    consume(TOKEN_RIGHT_PAREN,"Expected ')' after condition.");

    int then_jmp = emit_jmp(vm,OP_JUMP_IF_FALSE);
    emit_byte(vm,OP_POP);
    statement(vm);
    int else_jmp = emit_jmp(vm,OP_JUMP);
    patch_jmp(then_jmp);

    emit_byte(vm,OP_POP);
    if(match(TOKEN_ELSE)) statement(vm);
    patch_jmp(else_jmp);
}
//static void print_statement() {
//  expression();
//  consume(TOKEN_SEMICOLON, "Expect ';' after value.");
//  emit_byte(OP_PRINT);
//}

static void return_stmt(RotoVM* vm){
    if (current->type == TYPE_SCRIPT){
        error("Can't return from top-level code.");
    }
    if(match(TOKEN_SEMICOLON)){
        emit_return(vm);
    } else{
        if (current->type == TYPE_INITIALIZER){
            error("Can't return a value from an initializer.");
        }
        expression(vm);
        consume(TOKEN_SEMICOLON, "Expect ';' after return value.");
        emit_byte(vm,OP_RETURN);
    }
}

static void while_stmt(RotoVM* vm) {
  int loop_start = current_chunk()->count;

  consume(TOKEN_LEFT_PAREN, "Expected '(' after 'while'.");
  expression(vm);
  consume(TOKEN_RIGHT_PAREN, "Expected ')' after condition.");

  int exit_jmp = emit_jmp(vm,OP_JUMP_IF_FALSE);

  emit_byte(vm,OP_POP);
  statement(vm);

  emit_loop(vm,loop_start);

  patch_jmp(exit_jmp);
  emit_byte(vm,OP_POP);
}
static void synchronize() {
  parser.panicMode = false;

  while(parser.current.type != TOKEN_EOF){
    if(parser.previous.type == TOKEN_SEMICOLON) return;
    switch (parser.current.type) {
      case TOKEN_CLASS:
      case TOKEN_FUN:
      case TOKEN_VAR:
      case TOKEN_FOR:
      case TOKEN_IF:
      case TOKEN_WHILE:
      case TOKEN_RETURN:
        return;
      default: ;
    }
    advance();
  }
}
static void declaration(RotoVM* vm) {
    if(match(TOKEN_CLASS)){
        class_declaration(vm);
    }else if(match(TOKEN_FUN)){
        fun_declaration(vm);
    }else if(match(TOKEN_VAR)){
      var_decl(vm);
  }else{
    statement(vm);
  }
  if(parser.panicMode) synchronize();
}

// statement := expr_stmt
//             | for_stmt
//             | if_stmt
//             |print_stmt
//             |return_stmt
//             |while_stmt
//             |block

static void statement(RotoVM* vm) {
    if(match(TOKEN_FOR)){
         for_stmt(vm);
  }else if(match(TOKEN_IF)){
    if_stmt(vm);
  }else if(match(TOKEN_RETURN)){
      return_stmt(vm);
  }else if(match(TOKEN_WHILE)){
    while_stmt(vm);
  }else if(match(TOKEN_LEFT_BRACE)){
    begin_scope();
    block(vm);
    end_scope(vm);
  }else{
    expr_stmt(vm);
  }
}
static void grouping(RotoVM* vm,bool can_assign){
  expression(vm);
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression");
}


static void binary(RotoVM* vm,bool can_assign) {
  //remember the token
  TokenType operator_type = parser.previous.type;

  //compile the right operand
  ParseRule* rule = get_rule(operator_type);
  parse_precedence(vm,(Precedence)(rule->precedence + 1));

  //emit the operator instruction
  switch (operator_type) {
    case TOKEN_BANG_EQUAL: emit_bytes(vm,OP_EQUAL,OP_NOT); break;
    case TOKEN_EQUAL_EQUAL: emit_byte(vm,OP_EQUAL); break;
    case TOKEN_GREATER: emit_byte(vm,OP_GREATER); break;
    case TOKEN_GREATER_EQUAL: emit_bytes(vm,OP_LESS, OP_NOT); break;
    case TOKEN_LESS: emit_byte(vm,OP_LESS); break;
    case TOKEN_LESS_EQUAL: emit_bytes(vm,OP_GREATER, OP_NOT); break;
    case TOKEN_PIPE: emit_byte(vm,OP_BITWISE_OR); break;
    case TOKEN_CARET: emit_byte(vm,OP_BITWISE_XOR); break;
      case TOKEN_AMPERSAND: emit_byte(vm,OP_BITWISE_AND); break;
    case TOKEN_RIGHT_SHIFT: emit_byte(vm,OP_RIGHT_SHIFT); break;
    case TOKEN_LEFT_SHIFT: emit_byte(vm,OP_LEFT_SHIFT); break;
    case TOKEN_PLUS: emit_byte(vm,OP_ADD); break;
    case TOKEN_MINUS: emit_byte(vm,OP_SUB); break;
    case TOKEN_STAR: emit_byte(vm,OP_MUL); break;
    case TOKEN_SLASH: emit_byte(vm,OP_DIV); break;

  }
}

static void call(RotoVM* vm,bool can_assign){
    uint8_t arg_count = argument_list(vm);
    emit_bytes(vm,OP_CALL, arg_count);
}

static void list(RotoVM* vm,bool can_assign){
    int item_count = 0;
//    if(!check(TOKEN_RIGHT_BRACKET)){
        do {
            if(check(TOKEN_RIGHT_BRACKET)){
                break;
            }
//            parse_precedence(PREC_OR);
//            if (item_count == UINT8_COUNT){
//                error("Cannot have more than 256 items in a list literal.");
//            }
            expression(vm);
            item_count++;
        } while (match(TOKEN_COMMA));
//    }
    emit_bytes(vm,OP_BUILD_LIST,item_count);
    consume(TOKEN_RIGHT_BRACKET, "Expect ']' after list literal.");
//    if(item_count < 256){
//        emit_bytes(OP_BUILD_LIST,item_count);
//    } else{
//        emit_bytes(OP_WIDE,OP_BUILD_LIST);
//        emit_byte((uint8_t)(item_count >> 8));
//        emit_byte((uint8_t)item_count);
//    }

//    emit_byte(item_count);


}

static void subscript(RotoVM* vm,bool can_assign){
    parse_precedence(vm,PREC_OR);
    consume(TOKEN_RIGHT_BRACKET, "Expect ']' after index.");

    if(can_assign && match(TOKEN_EQUAL)){
        expression(vm);
        emit_byte(vm,OP_STORE_SUBSCR);
    } else{
        emit_byte(vm,OP_INDEX_SUBSCR);
    }
}


static void dot(RotoVM* vm, bool can_assign){
    consume(TOKEN_IDENTIFIER, "Expect property name after '.'.");
    uint8_t name = identifier_constant(vm,&parser.previous);
    if(can_assign && match(TOKEN_EQUAL)){
        expression(vm);
        emit_bytes(vm,OP_SET_PROPERTY, name);
    } else if(match(TOKEN_LEFT_PAREN)) {
        uint8_t arg_count = argument_list(vm);
        emit_bytes(vm,OP_INVOKE, name);
        emit_byte(vm,arg_count);
    }else{
        emit_bytes(vm,OP_GET_PROPERTY, name);
    }
}

static void literal(RotoVM* vm,bool can_assign) {
  switch (parser.previous.type) {
    case TOKEN_FALSE: emit_byte(vm,OP_FALSE); break;
    case TOKEN_NIL: emit_byte(vm,OP_NIL); break;
    case TOKEN_TRUE: emit_byte(vm,OP_TRUE); break;
    default:
      return;
  }
}
static void or_(RotoVM* vm,bool can_assign) {
  int else_jmp = emit_jmp(vm,OP_JUMP_IF_FALSE);
  int end_jump = emit_jmp(vm,OP_JUMP);

  patch_jmp(else_jmp);
  emit_jmp(vm,OP_POP);

  parse_precedence(vm,PREC_OR);
  patch_jmp(end_jump);
}
static void number(RotoVM* vm,bool can_assign) {

    // credit Dictu,https://github.com/dictu-lang/Dictu/blob/9fffbef4b19d0f0a8f0c2fa4ead70631b22d5c91/src/vm/compiler.c#L821
    char* buffer  = malloc(sizeof(char)*(parser.previous.length + 1));
    char* current = buffer;

    for (int i = 0; i < parser.previous.length; i++) {
        char c = parser.previous.start[i];

        if (c != '_'){
            *(current++) = c;
        }
    }
    *current = '\0';
//    double value = strtod(parser.previous.start, NULL);
    double value = strtod(buffer, NULL);
    emit_constant(vm,NUMBER_VAL(value));
    free(buffer);
}


static void string(RotoVM* vm, bool can_assign){
  emit_constant(vm,OBJ_VAL(copy_string(vm,parser.previous.start + 1, parser.previous.length - 2)));
}
static void named_variable(RotoVM* vm,Token name, bool can_assign) {
    uint8_t get_op, set_op;
    int arg = resolve_local(current, &name);

    if(arg != -1){
      get_op = OP_GET_LOCAL;
      set_op = OP_SET_LOCAL;
    }else if((arg = resolve_upvalue(current, &name)) != -1){
        get_op = OP_GET_UPVALUE;
        set_op = OP_SET_UPVALUE;

    }else{
      arg = identifier_constant(vm,&name);
      get_op = OP_GET_GLOBAL;
      set_op = OP_SET_GLOBAL;
    }

   if(can_assign && match(TOKEN_EQUAL)){
     expression(vm);
     emit_bytes(vm,set_op,(uint8_t)arg);
   }else{
     emit_bytes(vm,get_op,(uint8_t)arg);
   }
}
static void variable(RotoVM* vm,bool can_assign){
  named_variable(vm,parser.previous, can_assign);
}
static Token synthetic_token(const char* text){
    Token token;
    token.start = text;
    token.length = (int)strlen(text);
    return token;
}
static void super_(RotoVM* vm,bool can_assign){
    if(current_class == NULL){
        error("Can't use 'super' outside of a class");
    } else if(!current_class->has_super_class){
        error("Can't use 'super' in a class with no superclass.");
    }

    consume(TOKEN_DOT, "Expect '.' after 'super'.");
    consume(TOKEN_IDENTIFIER, "Expect superclass method name.");
    uint8_t name = identifier_constant(vm,&parser.previous);
    named_variable(vm,synthetic_token("this"), false);
    if(match(TOKEN_LEFT_PAREN)){
        uint8_t arg_count = argument_list(vm);
        named_variable(vm,synthetic_token("super"),false);
        emit_bytes(vm,OP_SUPER_INVOKE, name);
        emit_byte(vm,arg_count);
    } else{
        named_variable(vm,synthetic_token("super"), false);
        emit_bytes(vm,OP_GET_SUPER,name);
    }

}
static void this_(RotoVM* vm,bool can_assign){
    if (current_class == NULL){
        error("Can't use 'this' outside of a class.");
        return;
    }
   variable(vm,false);
}

static void unary(RotoVM* vm,bool can_assign){
  TokenType operator_type = parser.previous.type;
  //compile the operand ('-' expr)
  parse_precedence(vm,PREC_UNARY);

  switch (operator_type) {
    case TOKEN_BANG: emit_byte(vm,OP_NOT); break;
    case TOKEN_MINUS: emit_byte(vm,OP_NEGATE); break;
    default:
      return;
  }
}
ParseRule rules[] = {
  [TOKEN_LEFT_PAREN] = { grouping, call,    PREC_CALL},       // TOKEN_LEFT_PAREN
  { NULL,     NULL,    PREC_NONE },       // TOKEN_RIGHT_PAREN
  { NULL,     NULL,    PREC_NONE },      // TOKEN_LEFT_BRACE
  { NULL,     NULL,    PREC_NONE },      // TOKEN_RIGHT_BRACE
  { list,     subscript,    PREC_CALL },       // TOKEN_LEFT_BRACKET
  { NULL,     NULL,    PREC_NONE },       // TOKEN_RIGHT_BRACKET
  { NULL,     NULL,    PREC_NONE },       // TOKEN_COMMA
  { NULL,     dot,    PREC_CALL },       // TOKEN_DOT
  { unary,    binary,  PREC_TERM },       // TOKEN_MINUS
  { NULL,     binary,  PREC_TERM },       // TOKEN_PLUS
  { NULL,     NULL,    PREC_NONE },       // TOKEN_SEMICOLON
  { NULL,     binary,  PREC_FACTOR },     // TOKEN_SLASH
  { NULL,     binary,  PREC_FACTOR },     // TOKEN_STAR
  { unary,     NULL,    PREC_NONE },       // TOKEN_BANG
  { NULL,     binary,    PREC_BITWISE_AND},       // TOKEN_AMPERSAND
  { NULL,     binary,    PREC_BITWISE_OR},       // TOKEN_PIPE
  { NULL,     binary,    PREC_BITWISE_XOR},       // TOKEN_CARET
  { NULL,     binary,    PREC_LEFT_SHIFT},       // TOKEN_LEFT_SHIFT
  { NULL,     binary,    PREC_RIGHT_SHIFT},       // TOKEN_RIGHT_SHIFT
  { NULL,     binary,    PREC_EQUALITY },       // TOKEN_BANG_EQUAL
  { NULL,     NULL,    PREC_NONE },       // TOKEN_EQUAL
  { NULL,     binary,    PREC_EQUALITY },       // TOKEN_EQUAL_EQUAL
  { NULL,     binary,    PREC_COMPARISON },       // TOKEN_GREATER
  { NULL,     binary,    PREC_COMPARISON },       // TOKEN_GREATER_EQUAL
  { NULL,     binary,    PREC_COMPARISON },       // TOKEN_LESS
  { NULL,     binary,    PREC_COMPARISON },       // TOKEN_LESS_EQUAL
  { variable,     NULL,    PREC_NONE },       // TOKEN_IDENTIFIER
  { string,     NULL,    PREC_NONE },       // TOKEN_STRING
  { number,   NULL,    PREC_NONE },       // TOKEN_NUMBER
  { NULL,     and_,    PREC_AND },       // TOKEN_AND
  { NULL,     NULL,    PREC_NONE },       // TOKEN_CLASS
  { NULL,     NULL,    PREC_NONE },       // TOKEN_ELSE
  { literal,     NULL,    PREC_NONE },       // TOKEN_FALSE
  { NULL,     NULL,    PREC_NONE },       // TOKEN_FOR
  { NULL,     NULL,    PREC_NONE },       // TOKEN_FUN
  { NULL,     NULL,    PREC_NONE },       // TOKEN_IF
  { literal,     NULL,    PREC_NONE },       // TOKEN_NIL
  { NULL,     or_,    PREC_OR },       // TOKEN_OR
  { NULL,     NULL,    PREC_NONE },       // TOKEN_PRINT
  { NULL,     NULL,    PREC_NONE },       // TOKEN_RETURN
   { super_,     NULL,    PREC_NONE },       // TOKEN_SUPER
  { this_,     NULL,    PREC_NONE },       // TOKEN_THIS
  { literal,     NULL,    PREC_NONE },       // TOKEN_TRUE
  { NULL,     NULL,    PREC_NONE },       // TOKEN_VAR
  { NULL,     NULL,    PREC_NONE },       // TOKEN_WHILE
  { NULL,     NULL,    PREC_NONE },       // TOKEN_ERROR
  { NULL,     NULL,    PREC_NONE },       // TOKEN_EOF
};

static void parse_precedence(RotoVM* vm,Precedence precedence){
    advance();
    ParseFn prefix_rule = get_rule(parser.previous.type)->prefix;
    if(prefix_rule == NULL){
      error("Expect expression");
      return;
    }
    bool can_assign = precedence <= PREC_ASSIGNMENT;
    prefix_rule(vm,can_assign);

    while (precedence <= get_rule(parser.current.type)->precedence) {
      advance();
      ParseFn infix_rule = get_rule(parser.previous.type)->infix;
      infix_rule(vm,can_assign);
    }
    if(can_assign && match (TOKEN_EQUAL)){
      error("Invalid assignment target.");
    }
}
static uint8_t identifier_constant(RotoVM* vm,Token* name){
  return make_constant(vm,OBJ_VAL(copy_string(vm,name->start, name->length)));
}
static bool identifiers_equal(Token* a, Token* b){
  if(a->length != b->length) return false;

  return memcmp(a->start, b->start, a->length) == 0;
}

static int resolve_local(Compiler* compiler, Token* name){
  for(int i = compiler->local_count - 1; i >=0; i--){
    Local* local = &compiler->locals[i];
    if (identifiers_equal(name, &local->name)) {
      if (local->depth == -1) {
        error("Cannot read local variable in its own initializer.");
      }
        return i;
    }
  }

  return -1;
}
static int add_upvalue(Compiler* compiler, uint8_t index, bool is_local){
    int upvalue_count = compiler->function->upvalue_count;
    for (int i = 0; i < upvalue_count; i++){
        Upvalue* upvalue = &compiler->upvalues[i];
        if(upvalue->index == index && upvalue->is_local == is_local){
            return i;
        }

    }

    if(upvalue_count == UINT8_COUNT){
        error("Too many closure variables in function");
        return 0;
    }
    compiler->upvalues[upvalue_count].is_local = is_local;
    compiler->upvalues[upvalue_count].index = index;
    return compiler->function->upvalue_count++;
}

static int resolve_upvalue(Compiler* compiler, Token* name){
    if(compiler->enclosing == NULL) return -1;

    int local = resolve_local(compiler->enclosing, name);
    if (local != -1){
        compiler->enclosing->locals[local].is_captured = true;
        return add_upvalue(compiler, (uint8_t)local, true);
    }
    int upvalue = resolve_upvalue(compiler->enclosing, name);
    if(upvalue != -1){
        return add_upvalue(compiler, (uint8_t)upvalue, false);
    }

    return -1;
}
static void add_local(Token name) {
  if(current->local_count == UINT8_COUNT){
    error("Too many local variables in function.");
    return;
  }
  Local* local = &current->locals[current->local_count++];
  local->name = name;
  local->depth = -1;
  local->is_captured = false;
}

static void decl_variable(){
  //globals are implicitly declared
  if(current->scope_depth == 0) return;

  Token* name = &parser.previous;
  for (int i = current->local_count - 1; i >= 0; i--) {
    Local* local = &current->locals[i];

    if (local->depth != -1 && local->depth < current->scope_depth) {
      break;
    }
    if (identifiers_equal(name, &local->name)) {
      error("Variable with this name already declared in this scope.");
    }
  }
  add_local(*name);

}

static uint8_t parse_variable(RotoVM* vm,const char* error_msg){
  consume(TOKEN_IDENTIFIER, error_msg);

  decl_variable();
  if(current->scope_depth > 0) return 0;
  return identifier_constant(vm,&parser.previous);
}

static void mark_initialized() {
    if(current->scope_depth == 0) return;
  current->locals[current->local_count - 1].depth = current->scope_depth;
}

static void define_variable(RotoVM* vm,uint8_t global) {
  if(current->scope_depth > 0){
    mark_initialized();
    return;
  }
  emit_bytes(vm,OP_DEFINE_GLOBAL, global);
}

static uint8_t argument_list(RotoVM* vm){
    uint8_t arg_count = 0;
    if(!check(TOKEN_RIGHT_PAREN)){
        do {
            expression(vm);
            if (arg_count == 255){
                error("Can't have more than 255 arguments.");
            }
            arg_count++;
        } while (match(TOKEN_COMMA));
    }

    consume(TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");
    return arg_count;
}


/*
  left expr:
    false: JUMP over right expr value is left expr result
    true: POP move to right expr
  right expr value is result of right expr
*/
static void and_(RotoVM* vm,bool can_assign) {
   int end_jump = emit_jmp(vm,OP_JUMP_IF_FALSE);

   emit_byte(vm,OP_POP);
   parse_precedence(vm,PREC_AND);

   patch_jmp(end_jump);
}

static ParseRule* get_rule(TokenType type){
  return &rules[type];
}


ObjFunction* compile(RotoVM* vm, const char *source){
  init_scanner(source);
  Compiler compiler;
  init_compiler(vm,&compiler,TYPE_SCRIPT);

   parser.hadError = false;
   parser.panicMode = false;
   advance();

   while (!match(TOKEN_EOF)) {
     declaration(vm);
   }
   ObjFunction* function = end_compiler(vm);

   return parser.hadError ? NULL: function;
}

void mark_compiler_roots(RotoVM* vm){
    Compiler* compiler = current;
    while (compiler != NULL){
        mark_object(vm,(Obj*)compiler->function);
        compiler = compiler->enclosing;
    }
}