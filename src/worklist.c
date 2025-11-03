#include "worklist.h"

#include <stdlib.h>
#include "value.h"

void worklist_enqueue(struct worklist* worklist, struct lu_object* object) {
    struct worklist_item* item = malloc(sizeof(struct worklist_item));

    item->object = object;
    item->next = nullptr;

    if (!worklist->head) {
        worklist->head = worklist->tail = item;
    } else {
        worklist->tail->next = item;
        worklist->tail = item;
    }
}

struct lu_object* worklist_dequeue(struct worklist* worklist) {
    struct worklist_item* item = worklist->head;
    if (!item)
        return nullptr;
    void* object = item->object;
    worklist->head = item->next;
    if (!worklist->head) {
        worklist->tail = nullptr;
    }
    free(item);
    return object;
}
