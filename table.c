#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"

#define TABLE_MAX_LOAD 0.75

void init_table(Table* table){
  table->count = 0;
  table->capacity = -1;
  table->entries = NULL;
}

void free_table(RotoVM* vm,Table* table){
  FREE_ARRAY(vm,Entry, table->entries, table->capacity + 1);
  init_table(table);
}

static Entry* find_entry(Entry* entries, int capacity, ObjString* key){
  uint32_t index = key->hash & capacity;
  Entry* tombstone = NULL;
  for (;;) {
    Entry* entry = &entries[index];

    if(entry->key == NULL){
      if (IS_NIL(entry->value)) {
        /* code */
        return tombstone != NULL ? tombstone : entry;
      }else{
        //we found a tombstone
        if(tombstone == NULL) tombstone = entry;
      }
    }else if(entry->key == key){
      //found the key
      return entry;
    }

    index = (index + 1) & capacity;
  }
}

bool table_get(Table* table, ObjString* key, Value* value){
  if(table->count == 0) return false;

  Entry* entry = find_entry(table->entries, table->capacity, key);
  if(entry->key == NULL) return false;

  *value = entry->value;
  return true;
}

static void adjust_capacity(RotoVM* vm,Table* table, int capacity) {
  Entry* entries = ALLOCATE(vm,Entry, capacity + 1);
  for (int i = 0; i <= capacity; i++) {
    entries[i].key = NULL;
    entries[i].value = NIL_VAL;
  }

  table->count = 0;
  for (int i = 0; i <= table->capacity; i++) {
    Entry* entry = &table->entries[i];
    if(entry->key == NULL) continue;

    Entry* dest = find_entry(entries, capacity, entry->key);
    dest->key = entry->key;
    dest->value = entry->value;
    table->count++;
  }
  FREE_ARRAY(vm,Entry, table->entries, table->capacity + 1);
  table->entries = entries;
  table->capacity = capacity;
}

bool table_set(RotoVM* vm,Table* table, ObjString* key, Value value){
  if(table->count + 1 > (table->capacity + 1) * TABLE_MAX_LOAD){
    int capacity = GROW_CAPACITY(table->capacity + 1) - 1;
    adjust_capacity(vm,table, capacity);
  }
  Entry* entry = find_entry(table->entries, table->capacity, key);

  bool is_new_key = entry->key == NULL;


  if(is_new_key && IS_NIL(entry->value)) table->count++;

  entry->key = key;
  entry->value = value;

  return is_new_key;
}

bool table_delete(Table* table, ObjString* key){
  if(table->count == 0) return false;
  //find entry
  Entry* entry = find_entry(table->entries, table->capacity, key);
  if(entry->key == NULL) return false;

  //place tombstone in the entry
  entry->key = NULL;
  entry->value = BOOL_VAL(true);
}

void table_add_all(RotoVM* vm,Table* from, Table* to) {
  for (int i = 0; i <= from->capacity; i++) {
    Entry* entry = &from->entries[i];
    if(entry->key != NULL){
      table_set(vm,to, entry->key, entry->value);
    }
  }
}

ObjString* table_find_string(Table* table, const char* chars, int length ,uint32_t hash){
  if(table->count == 0) return NULL;

  uint32_t index = hash & table->capacity;

  for(;;){
    Entry* entry = &table->entries[index];

    if(entry->key == NULL){
      //stop if we find an empty non-tombstone entry
      if(IS_NIL(entry->value)) return NULL;
    }else if(entry->key->length == length && entry->key->hash == hash &&
              memcmp(entry->key->chars, chars, length) == 0){
       return entry->key;
    }
    index = (index + 1) & table->capacity;
  }
}
void table_remove_white(Table* table){
    for (int i = 0; i <= table->capacity; i++) {
        Entry* entry = &table->entries[i];
        if (entry->key != NULL && !entry->key->obj.is_marked){
            table_delete(table, entry->key);
        }
    }
}

void mark_table(RotoVM* vm,Table* table){
    for(int i = 0; i <= table->capacity; i++){
        Entry* entry = &table->entries[i];
        mark_object(vm,(Obj*)entry->key);
        mark_value(vm,entry->value);
    }
}
