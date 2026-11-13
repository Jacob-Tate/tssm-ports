#include "containers.h"

#include "xMemMgr.h"

void pool_list_base::push_back()
{
    node_base* n = alloc();
    n->prev = tail.prev;
    n->next = &tail;
    n->next->prev = n;
    n->prev->next = n;
    _size++;
}

void pool_list_base::create(S32 node_size, S32 max_size, void* buffer)
{
    if (!buffer)
    {
        this->buffer = xMemAlloc(gActiveHeap, node_size * max_size, 0);
    }
    else
    {
        this->buffer = buffer;
    }

    _max_size = max_size;
    reset(node_size);
}

void pool_list_base::reset(S32 node_size)
{
    U8* mem;
    U8* end_mem;

    _size = 0;
    head.next = &tail;
    head.prev = NULL;
    tail.prev = &head;
    tail.next = NULL;
    stack = (node_base*)buffer;

    mem = (U8*)buffer;
    end_mem = mem + (_max_size - 1) * node_size;
    while (mem != end_mem)
    {
        ((node_base*)mem)->next = (node_base*)(mem + node_size);
        mem += node_size;
    }
    ((node_base*)mem)->next = NULL;
}

pool_list_base::node_base* pool_list_base::erase(node_base* it)
{
    node_base* next = it->next;
    node_base* prev = it->prev;

    next->prev = prev;
    prev->next = next;

    free(it);

    _size--;

    return next;
}
