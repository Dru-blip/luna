#include "runtime/worklist.h"

#include <stdlib.h>

void worklist_enqueue(worklist_t* worklist, void* object) {
    worklist_item_t* item = malloc(sizeof(worklist_item_t));

    item->object = object;
    item->next = nullptr;

    if (!worklist->head) {
        worklist->head = worklist->tail = item;
    } else {
        worklist->tail->next = item;
        worklist->tail = item;
    }
}

void* worklist_dequeue(worklist_t* worklist) {
    worklist_item_t* item = worklist->head;
    if (!item) return nullptr;
    void* object = item->object;
    worklist->head = item->next;
    if (!worklist->head) {
        worklist->tail = nullptr;
    }
    free(item);
    return object;
}
