#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "chunk.h"
#include "compiler.h"
#include "scanner.h"
#include "object.h"

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
  PREC_TERM, //+ -
  PREC_FACTOR, // * /
  PREC_UNARY, // ! -
  PREC_CALL, // . ()
  PREC_PRIMARY
} Precedence;

typedef void (*ParseFn)(bool can_assign);

typedef struct{
  ParseFn prefix;
  ParseFn infix;
  Precedence precedence;
}ParseRule;

typedef struct{
  Token name;
  int depth;
}Local;
typedef enum {
   TYPE_FUNCTION,
   TYPE_NATIVE,//@change
   TYPE_SCRIPT
}FunctionType;

typedef struct Compiler{
    struct Compiler* enclosing;
    ObjFunction* function;
    ObjNative* native; //@change
    FunctionType type;
    Local locals[UINT8_COUNT];
    int local_count;
    int scope_depth;
}Compiler;

Parser parser;
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
static void emit_byte(uint8_t byte) {
  /* code */
  write_chunk(current_chunk(), byte, parser.previous.line);
}
static void emit_bytes(uint8_t byte1, uint8_t byte2) {
  emit_byte(byte1);
  emit_byte(byte2);
}
static void emit_loop(int loop_start){
  emit_byte(OP_LOOP);

  int offset = current_chunk()->count - loop_start + 2;
  if(offset > UINT16_MAX) error("Loop body too large.");

  emit_byte((offset >> 8) & 0xff);
  emit_byte(offset & 0xff);
}
static int emit_jmp(uint8_t instruction){
  emit_byte(instruction);
  emit_byte(0xff);
  emit_byte(0xff);
  return current_chunk()->count - 2;
}
static void emit_return() {
    emit_byte(OP_NIL);
    emit_byte(OP_RETURN);
}
static uint8_t make_constant(Value value){
  int constant = add_constant(current_chunk(), value);
  if(constant > UINT8_MAX){
    error("Too many constants in one chunk");
    return 0;
  }
  return (uint8_t)constant;
}
static void emit_constant(Value value) {
  emit_bytes(OP_CONSTANT, make_constant(value));
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

static void init_compiler(Compiler* compiler,FunctionType type){
    compiler->enclosing = current;
    compiler->function = NULL;
    compiler->native = NULL;
    compiler->type = type;
    compiler->local_count = 0;
    compiler->scope_depth = 0;
    compiler->function = newFunction();

    current = compiler;

    if (type != TYPE_SCRIPT){
        current->function->name = copy_string(parser.previous.start, parser.previous.length);
    }

    Local* local = &current->locals[current->local_count++];
    local->depth = 0;
    local->name.length = 0;
    local->name.start = "";
}


static ObjFunction* end_compiler() {
    emit_return();
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

static void end_scope(){
    current->scope_depth--;
  while (current->local_count > 0 && current->locals[current->local_count - 1].depth >
          current->scope_depth) {
    emit_byte(OP_POP);//optimization use a pop instr with an operand to pop many at once
    current->local_count--;
  }
}

//foward declaration // many like this is not good.
static void expression();
static void statement();
static void declaration();
static void and_(bool can_assign);
static ParseRule* get_rule(TokenType type);
static void parse_precedence(Precedence precedence);
static uint8_t parse_variable(const char* error_msg);
static void define_variable(uint8_t global);
static void var_decl();
static void mark_initialized();
static void fun_declaration();
static uint8_t argument_list();
static int resolve_local(Compiler* compiler, Token* name);
static void named_variable(Token name, bool can_assign);
static void call(bool can_assign);
static uint8_t identifier_constant(Token* name);






static void expression(){
  parse_precedence(PREC_ASSIGNMENT);
}

static void block(){
  while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
    declaration();
  }

  consume(TOKEN_RIGHT_BRACE, "Expect '}' after block.");
}
static void function(FunctionType type){
    Compiler compiler;
    init_compiler(&compiler, type);
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
            uint8_t paramConstant = parse_variable("Expect parameter name");
            define_variable(paramConstant);
        } while (match(TOKEN_COMMA));
    }
    consume(TOKEN_RIGHT_PAREN,"Expect ')' after parameters.");

    //The body
    consume(TOKEN_LEFT_BRACE, "Expect '{' before function body.");
    block();

    //Create the function object.
    ObjFunction* function = end_compiler();
    emit_bytes(OP_CONSTANT,make_constant(OBJ_VAL(function)));

}
static void fun_declaration(){
    uint8_t global = parse_variable("Expect function name.");
    mark_initialized();
    function(TYPE_FUNCTION);
    define_variable(global);
}
static void var_decl() {
  uint8_t global = parse_variable("Expected variable name.");

  if(match(TOKEN_EQUAL)){
    expression();
  }else{
    emit_byte(OP_NIL);
  }
  consume(TOKEN_SEMICOLON, "Expected ';' after variable declaration.");

  define_variable(global);
}

void expr_stmt() {
  expression();
  consume(TOKEN_SEMICOLON, "Expect ';' after expression.");
  emit_byte(OP_POP);

}

static void for_stmt() {

  //declaration part: var i = 0;
  begin_scope();
  consume(TOKEN_LEFT_PAREN, "Expect '(' after 'for'.");
  if (match(TOKEN_SEMICOLON)) {
    //no initializer
  }else if(match(TOKEN_VAR)){
    var_decl();
  }else{
    expr_stmt();
  }

  int loop_start = current_chunk()->count;

  //conditional part: eg. i < 3; ....
  int exit_jmp = -1;
  if(!match(TOKEN_SEMICOLON)){
    expression();
    consume(TOKEN_SEMICOLON, "Expected ';' after loop condition.");

    //jump out of the loop if the condition is false
    exit_jmp = emit_jmp(OP_JUMP_IF_FALSE);
    emit_byte(OP_POP); //condition
  }

  //increment part: i++;
  if(!match(TOKEN_RIGHT_PAREN)){
    int body_jump = emit_jmp(OP_JUMP);

    int increment_start = current_chunk()->count;
    expression();
    emit_byte(OP_POP);
    consume(TOKEN_RIGHT_PAREN, "Expected ')' after for clauses");

    emit_loop(loop_start);
    loop_start = increment_start;
    patch_jmp(body_jump);

  }

  statement();

  emit_loop(loop_start);

  if(exit_jmp != -1){
    patch_jmp(exit_jmp);
    emit_byte(OP_POP);
  }

  end_scope();
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

static void if_stmt(){
    consume(TOKEN_LEFT_PAREN, "Expected '(' after 'if'.");
    expression();
    consume(TOKEN_RIGHT_PAREN,"Expected ')' after condition.");

    int then_jmp = emit_jmp(OP_JUMP_IF_FALSE);
    emit_byte(OP_POP);
    statement();
    int else_jmp = emit_jmp(OP_JUMP);
    patch_jmp(then_jmp);

    emit_byte(OP_POP);
    if(match(TOKEN_ELSE)) statement();
    patch_jmp(else_jmp);
}
static void print_statement() {
  expression();
  consume(TOKEN_SEMICOLON, "Expect ';' after value.");
  emit_byte(OP_PRINT);
}

static void return_stmt(){
    if (current->type == TYPE_SCRIPT){
        error("Can't return from top-level code.");
    }
    if(match(TOKEN_SEMICOLON)){
        emit_return();
    } else{
        expression();
        consume(TOKEN_SEMICOLON, "Expect ';' after return value.");
        emit_byte(OP_RETURN);
    }
}

static void while_stmt() {
  int loop_start = current_chunk()->count;

  consume(TOKEN_LEFT_PAREN, "Expected '(' after 'while'.");
  expression();
  consume(TOKEN_RIGHT_PAREN, "Expected ')' after condition.");

  int exit_jmp = emit_jmp(OP_JUMP_IF_FALSE);

  emit_byte(OP_POP);
  statement();

  emit_loop(loop_start);

  patch_jmp(exit_jmp);
  emit_byte(OP_POP);
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
static void declaration() {
    if(match(TOKEN_FUN)){
        fun_declaration();
    }else if(match(TOKEN_VAR)){
      var_decl();
  }else{
    statement();
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

static void statement() {
  if (match(TOKEN_PRINT)) {
    print_statement();
  }else if(match(TOKEN_FOR)){
    for_stmt();
  }else if(match(TOKEN_IF)){
    if_stmt();
  }else if(match(TOKEN_RETURN)){
      return_stmt();
  }else if(match(TOKEN_WHILE)){
    while_stmt();
  }else if(match(TOKEN_LEFT_BRACE)){
    begin_scope();
    block();
    end_scope();
  }else{
    expr_stmt();
  }
}
static void grouping(bool can_assign){
  expression();
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression");
}


static void binary(bool can_assign) {
  //remember the token
  TokenType operator_type = parser.previous.type;

  //compile the right operand
  ParseRule* rule = get_rule(operator_type);
  parse_precedence((Precedence)(rule->precedence + 1));

  //emit the operator instruction
  switch (operator_type) {
    case TOKEN_BANG_EQUAL: emit_bytes(OP_EQUAL,OP_NOT); break;
    case TOKEN_EQUAL_EQUAL: emit_byte(OP_EQUAL); break;
    case TOKEN_GREATER: emit_byte(OP_GREATER); break;
    case TOKEN_GREATER_EQUAL: emit_bytes(OP_LESS, OP_NOT); break;
    case TOKEN_LESS: emit_byte(OP_LESS); break;
    case TOKEN_LESS_EQUAL: emit_bytes(OP_GREATER, OP_NOT); break;
    case TOKEN_PLUS: emit_byte(OP_ADD); break;
    case TOKEN_MINUS: emit_byte(OP_SUB); break;
    case TOKEN_STAR: emit_byte(OP_MUL); break;
    case TOKEN_SLASH: emit_byte(OP_DIV); break;
  }
}

static void call(bool can_assign){
    uint8_t arg_count = argument_list();
    emit_bytes(OP_CALL, arg_count);
}

static void literal(bool can_assign) {
  switch (parser.previous.type) {
    case TOKEN_FALSE: emit_byte(OP_FALSE); break;
    case TOKEN_NIL: emit_byte(OP_NIL); break;
    case TOKEN_TRUE: emit_byte(OP_TRUE); break;
    default:
      return;
  }
}
static void or_(bool can_assign) {
  int else_jmp = emit_jmp(OP_JUMP_IF_FALSE);
  int end_jump = emit_jmp(OP_JUMP);

  patch_jmp(else_jmp);
  emit_jmp(OP_POP);

  parse_precedence(PREC_OR);
  patch_jmp(end_jump);
}
static void number(bool can_assign) {
    double value = strtod(parser.previous.start, NULL);
    emit_constant(NUMBER_VAL(value));
}


static void string(bool can_assign){
  emit_constant(OBJ_VAL(copy_string(parser.previous.start + 1, parser.previous.length - 2)));
}
static void named_variable(Token name, bool can_assign) {
    uint8_t get_op, set_op;
    int arg = resolve_local(current, &name);

    if(arg != -1){
      get_op = OP_GET_LOCAL;
      set_op = OP_SET_LOCAL;
    }else{
      arg = identifier_constant(&name);
      get_op = OP_GET_GLOBAL;
      set_op = OP_SET_GLOBAL;
    }

   if(can_assign && match(TOKEN_EQUAL)){
     expression();
     emit_bytes(set_op,(uint8_t)arg);
   }else{
     emit_bytes(get_op,(uint8_t)arg);
   }
}
static void variable(bool can_assign){
  named_variable(parser.previous, can_assign);
}

static void unary(bool can_assign){
  TokenType operator_type = parser.previous.type;
  //compile the operand ('-' expr)
  parse_precedence(PREC_UNARY);

  switch (operator_type) {
    case TOKEN_BANG: emit_byte(OP_NOT); break;
    case TOKEN_MINUS: emit_byte(OP_NEGATE); break;
    default:
      return;
  }
}
ParseRule rules[] = {
  [TOKEN_LEFT_PAREN] = { grouping, call,    PREC_CALL},       // TOKEN_LEFT_PAREN
  { NULL,     NULL,    PREC_NONE },       // TOKEN_RIGHT_PAREN
  { NULL,     NULL,    PREC_NONE },      // TOKEN_LEFT_BRACE
  { NULL,     NULL,    PREC_NONE },       // TOKEN_RIGHT_BRACE
  { NULL,     NULL,    PREC_NONE },       // TOKEN_COMMA
  { NULL,     NULL,    PREC_NONE },       // TOKEN_DOT
  { unary,    binary,  PREC_TERM },       // TOKEN_MINUS
  { NULL,     binary,  PREC_TERM },       // TOKEN_PLUS
  { NULL,     NULL,    PREC_NONE },       // TOKEN_SEMICOLON
  { NULL,     binary,  PREC_FACTOR },     // TOKEN_SLASH
  { NULL,     binary,  PREC_FACTOR },     // TOKEN_STAR
  { unary,     NULL,    PREC_NONE },       // TOKEN_BANG
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
  { NULL,     NULL,    PREC_NONE },       // TOKEN_SUPER
  { NULL,     NULL,    PREC_NONE },       // TOKEN_THIS
  { literal,     NULL,    PREC_NONE },       // TOKEN_TRUE
  { NULL,     NULL,    PREC_NONE },       // TOKEN_VAR
  { NULL,     NULL,    PREC_NONE },       // TOKEN_WHILE
  { NULL,     NULL,    PREC_NONE },       // TOKEN_ERROR
  { NULL,     NULL,    PREC_NONE },       // TOKEN_EOF
};

static void parse_precedence(Precedence precedence){
    advance();
    ParseFn prefix_rule = get_rule(parser.previous.type)->prefix;
    if(prefix_rule == NULL){
      error("Expect expression");
      return;
    }
    bool can_assign = precedence <= PREC_ASSIGNMENT;
    prefix_rule(can_assign);

    while (precedence <= get_rule(parser.current.type)->precedence) {
      advance();
      ParseFn infix_rule = get_rule(parser.previous.type)->infix;
      infix_rule(can_assign);
    }
    if(can_assign && match (TOKEN_EQUAL)){
      error("Invalid assignment target.");
    }
}
static uint8_t identifier_constant(Token* name){
  return make_constant(OBJ_VAL(copy_string(name->start, name->length)));
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
static void add_local(Token name) {
  if(current->local_count == UINT8_COUNT){
    error("Too many local variables in function.");
    return;
  }
  Local* local = &current->locals[current->local_count++];
  local->name = name;
  local->depth = -1;
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

static uint8_t parse_variable(const char* error_msg){
  consume(TOKEN_IDENTIFIER, error_msg);

  decl_variable();
  if(current->scope_depth > 0) return 0;
  return identifier_constant(&parser.previous);
}

static void mark_initialized() {
    if(current->scope_depth == 0) return;
  current->locals[current->local_count - 1].depth = current->scope_depth;
}

static void define_variable(uint8_t global) {
  if(current->scope_depth > 0){
    mark_initialized();
    return;
  }
  emit_bytes(OP_DEFINE_GLOBAL, global);
}

static uint8_t argument_list(){
    uint8_t arg_count = 0;
    if(!check(TOKEN_RIGHT_PAREN)){
        do {
            expression();
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
static void and_(bool can_assign) {
   int end_jump = emit_jmp(OP_JUMP_IF_FALSE);

   emit_byte(OP_POP);
   parse_precedence(PREC_AND);

   patch_jmp(end_jump);
}

static ParseRule* get_rule(TokenType type){
  return &rules[type];
}


ObjFunction* compile(const char *source){
  init_scanner(source);
  Compiler compiler;
  init_compiler(&compiler,TYPE_SCRIPT);

   parser.hadError = false;
   parser.panicMode = false;
   advance();

   while (!match(TOKEN_EOF)) {
     declaration();
   }
   ObjFunction* function = end_compiler();

   return parser.hadError ? NULL: function;
}
