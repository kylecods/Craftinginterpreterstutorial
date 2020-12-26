#ifndef croto_table_h
#define croto_table_h

#include "common.h"
#include "value.h"

//entry to the table
typedef struct{
  ObjString* key; // actual value stored in table
  Value value; //val is of type number (index)
} Entry;

typedef struct{//ratio of count to capacity gives loadfactor of table
  int count;
  int capacity;
  Entry* entries;//pointer to entry
}Table;

void init_table(Table* table);
void free_table(Table* table);
bool table_get(Table* table, ObjString* key, Value* value);
bool table_set(Table* table, ObjString* key, Value value);
bool table_delete(Table* table, ObjString* key);
void table_add_all(Table* from, Table* to);
ObjString* table_find_string(Table* table, const char* chars, int length, uint32_t hash);

#endif