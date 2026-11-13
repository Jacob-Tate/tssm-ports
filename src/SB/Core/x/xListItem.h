#ifndef XLISTITEM_H
#define XLISTITEM_H

#include <types.h>

template <class T> struct xListItem
{
    S32 flg_travFilter;
    T* next;
    T* prev;

    xListItem()
    {
        flg_travFilter = 0;
        prev = NULL;
        next = NULL;
    }
    ~xListItem();

    T* Next()
    {
        return this->next;
    }
    void Insert(T* list);
    T* RemHead(T** listhead);
    T* Head();

    void Remove()
    {
        if (next != NULL)
        {
            next->prev = prev;
        }

        if (prev != NULL)
        {
            prev->next = next;
        }

        next = NULL;
        prev = NULL;
    }
};

// Out-of-line template members (PS2 DWARF: Engine/Core/x/xListItem.cpp; retail calls them with bl and
// emits them weak, Insert/RemHead/Head, in the unit that instantiates them).
template <class T> void xListItem<T>::Insert(T* list)
{
    T* node = (T*)this;

    node->prev = list;
    node->next = list->next;

    if (list->next != NULL)
    {
        list->next->prev = node;
    }

    list->next = node;
}

template <class T> T* xListItem<T>::RemHead(T** listhead)
{
    if (*listhead == NULL)
    {
        return NULL;
    }

    T* oldhead = (*listhead)->Head();

    if (oldhead == NULL)
    {
        *listhead = NULL;
    }
    else
    {
        *listhead = oldhead->Next();
        oldhead->Remove();
    }

    return oldhead;
}

// Invented (stripped in every build): an out-of-class destructor like the other members.
// Only the dead-stripped goal destructors call it; its instantiation places
// __sinit_zWadNME_cpp at the end of .text (see zNMEHazard.cpp).
template <class T> xListItem<T>::~xListItem()
{
}

template <class T> T* xListItem<T>::Head()
{
    T* node = (T*)this;

    if (node == NULL)
    {
        return node;
    }

    while (node->prev != NULL)
    {
        node = node->prev;
    }

    return node;
}

#endif
