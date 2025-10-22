#pragma once

// switch to unrolled linked list
struct worklist_item {
    struct lu_object* object;
    // struct lu_object** items[16];
    struct worklist_item* next;
};

struct worklist {
    struct worklist_item* head;
    struct worklist_item* tail;
};

void worklist_enqueue(struct worklist* worklist, struct lu_object* object);
struct lu_object* worklist_dequeue(struct worklist* worklist);
