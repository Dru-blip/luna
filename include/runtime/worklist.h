#pragma once


typedef struct worklist_item {
    void* object;
    struct worklist_item* next;
} worklist_item_t;

typedef struct worklist {
    worklist_item_t* head;
    worklist_item_t* tail;
} worklist_t;

void worklist_enqueue(worklist_t* worklist, void* object);
void* worklist_dequeue(worklist_t* worklist);
