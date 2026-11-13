#ifndef CONTAINERS_H
#define CONTAINERS_H

#include <types.h>

#include "xMemMgr.h"

#include <stddef.h>

struct tier_queue_allocator
{
    struct block_data
    {
        U8 prev;
        U8 next;
        U16 flags;
        void* data;
    };

    block_data* blocks;
    U32 _unit_size;
    U32 _block_size;
    U32 _block_size_shift;
    U32 _max_blocks;
    U32 _max_blocks_shift;
    U8 head;

    void init(size_t unit_size, size_t block_size, size_t max_blocks)
    {
        _unit_size = (unit_size + 3) & ~3;
        _block_size_shift = log2_ceil(block_size);
        _block_size = 1 << _block_size_shift;
        _max_blocks_shift = log2_ceil(max_blocks);
        _max_blocks = 1 << _max_blocks_shift;
        blocks = (block_data*)xMemAlloc(gActiveHeap, _max_blocks * sizeof(block_data), 0);

        for (size_t i = 0, end = _max_blocks; i < end; i++)
        {
            blocks[i].data = NULL;
        }

        clear();

        // Allocate the data of the first 31 blocks up front, then give the blocks back.
        U8 arrayTempAllocs[31];

        for (S32 i = 0; i < 31; i++)
        {
            arrayTempAllocs[i] = alloc_block();
        }

        for (S32 i = 0; i < 31; i++)
        {
            free_block(arrayTempAllocs[i]);
        }
    }

    void clear()
    {
        head = 0;

        size_t imask = _max_blocks - 1;

        for (size_t i = 0, end = _max_blocks; i < end; i++)
        {
            blocks[i].prev = (i - 1) & imask;
            blocks[i].next = (i + 1) & imask;
        }
    }

    static size_t log2_ceil(size_t v)
    {
        size_t power = 0;

        while (v > 1)
        {
            v >>= 1;
            power++;
        }

        return power;
    }

    bool full() const
    {
        return head == blocks[head].next;
    }

    U8 alloc_block()
    {
        U8 index = head;
        block_data& block = blocks[index];

        head = block.next;

        blocks[block.prev].next = block.next;
        blocks[block.next].prev = block.prev;

        if (block.data == NULL)
        {
            block.data = alloc_block_data();
        }

        return index;
    }

    void free_block(U8 index)
    {
        block_data& block = blocks[index];

        block.next = head;
        block.prev = blocks[head].prev;

        blocks[block.prev].next = index;
        blocks[block.next].prev = index;

        head = index;
    }

    U32 block_size() const
    {
        return _block_size;
    }

    U32 block_size_shift() const
    {
        return _block_size_shift;
    }

    U32 mod_block_size(U32 i) const
    {
        return i & (_block_size - 1);
    }

    void* get_block(U32 at) const
    {
        return blocks[at].data;
    }

    void* alloc_block_data() const
    {
        return xMemAlloc(gActiveHeap, _block_size * _unit_size, 0);
    }
};

template <class T> struct tier_queue
{
    struct iterator
    {
        U32 it;
        tier_queue<T>* owner;

        T& operator*() const
        {
            return owner->get_at(it);
        }

        T* operator->() const
        {
            return &operator*();
        }

        iterator& operator+=(S32 i)
        {
            it = owner->wrap_index(it + i);
            return *this;
        }

        iterator operator+(S32 i) const
        {
            iterator c = *this;
            c += i;
            return c;
        }

        iterator& operator-=(S32 i)
        {
            return *this += -i;
        }

        iterator operator-(S32 i) const
        {
            iterator c = *this;
            c -= i;
            return c;
        }

        bool operator==(const iterator& other) const
        {
            return it == other.it;
        }
    };

    U32 first;
    U32 _size;
    U32 wrap_mask;
    tier_queue_allocator* alloc;
    U8 blocks[32];

    void init(tier_queue_allocator& allocator)
    {
        first = 0;
        _size = 0;
        wrap_mask = allocator.block_size() * 32 - 1;
        alloc = &allocator;
    }

    bool front_full() const
    {
        if (!alloc->full())
        {
            return false;
        }

        return alloc->mod_block_size(first) == 0;
    }

    void clear()
    {
        U32 i = get_block(first);
        U32 end = wrap_block(i + get_block(alloc->block_size() + _size - 1));

        while (i != end)
        {
            alloc->free_block(blocks[i]);
            i = wrap_block(i + 1);
        }

        first = _size = 0;
    }

    U32 wrap_block(U32 i) const
    {
        return i % 256;
    }

    U32 get_block(U32 i) const
    {
        return i >> alloc->block_size_shift();
    }

    U32 wrap_index(U32 i) const
    {
        return i & wrap_mask;
    }

    U32 size() const
    {
        return _size;
    }

    bool empty() const
    {
        return size() == 0;
    }

    T& get_at(U32 i) const
    {
        T* data = (T*)alloc->get_block(blocks[get_block(i)]);
        U32 unit_index = alloc->mod_block_size(i);
        return data[unit_index];
    }

    T& operator[](S32 i)
    {
        return get_at(wrap_index(first + i));
    }

    iterator create_iterator(U32 i) const
    {
        iterator it;
        it.it = i;
        it.owner = (tier_queue<T>*)this;
        return it;
    }

    iterator begin() const
    {
        return create_iterator(first);
    }

    iterator end() const
    {
        return create_iterator(wrap_index(first + _size));
    }

    static iterator dummy_iterator()
    {
        iterator it;
        it.it = 0;
        it.owner = NULL;
        return it;
    }

    T& front()
    {
        return *begin();
    }

    T& back()
    {
        return *(end() - 1);
    }

    void push_front()
    {
        _size++;

        U32 oldfirst = first;
        first = wrap_index(first - 1);

        if (!alloc->mod_block_size(oldfirst))
        {
            U32 block_index = get_block(first);
            blocks[block_index] = alloc->alloc_block();
        }
    }

    void pop_back()
    {
        if (_size <= 1)
        {
            clear();
            return;
        }

        _size--;

        U32 i = wrap_index(first + _size);

        if (alloc->mod_block_size(i) == 0)
        {
            alloc->free_block(blocks[get_block(i)]);
        }
    }
};

template <class T> struct static_queue
{
    U32 _first;
    U32 _size;
    U32 _max_size;
    U32 _max_size_mask;
    T* _buffer;

    struct iterator
    {
        U32 _it;
        static_queue<T>* _owner;

        T& operator*() const
        {
            return _owner->_buffer[_it];
        }

        bool operator==(const iterator& c) const
        {
            return _it == c._it;
        }

        bool operator!=(const iterator& c) const
        {
            return !(*this == c);
        }

        iterator& operator++()
        {
            *this += 1;
            return *this;
        }

        iterator& operator+=(S32 i)
        {
            _it = _owner->mod_max_size(_it + i);
            return *this;
        }
    };

    void init(U32 size)
    {
        U32 shift = 0;

        size++;

        while (size > 1)
        {
            size >>= 1;
            shift++;
        }

        _max_size = 1 << shift;
        _max_size_mask = _max_size - 1;
        _buffer = (T*)xMemAlloc(gActiveHeap, _max_size * sizeof(T), 0);
        clear();
    }

    void clear()
    {
        _size = 0;
        _first = 0;
    }

    U32 size() const
    {
        return _size;
    }

    bool empty() const
    {
        return size() == 0;
    }

    U32 max_size() const
    {
        return _max_size - 1;
    }

    bool full() const
    {
        return size() == max_size();
    }

    U32 mod_max_size(U32 index) const
    {
        return index & _max_size_mask;
    }

    iterator create_iterator(U32 index) const
    {
        iterator it;
        it._it = index;
        it._owner = const_cast<static_queue<T>*>(this);
        return it;
    }

    iterator begin() const
    {
        return create_iterator(_first);
    }

    T& front()
    {
        iterator it = begin();
        return *it;
    }

    T& push_front()
    {
        _size++;
        _first = mod_max_size(_first - 1);
        return front();
    }

    void pop_back()
    {
        _size--;
    }

    iterator end() const
    {
        return create_iterator(mod_max_size(_first + _size));
    }

    void erase(const iterator& first, const iterator& last)
    {
        if (first._it == _first)
        {
            U32 tail = _first + _size;
            _first = last._it;
            _size = mod_max_size(tail - _first);
        }
        else
        {
            U32 diff = mod_max_size(last._it - first._it);
            _size -= diff;
        }
    }
};

struct pool_list_base
{
    struct node_base
    {
        node_base* prev;
        node_base* next;
    };

    S32 _size;
    S32 _max_size;
    node_base head;
    node_base tail;
    node_base* stack;
    void* buffer;

    S32 size() const
    {
        return _size;
    }

    S32 max_size() const
    {
        return _max_size;
    }

    bool full() const
    {
        return size() == max_size();
    }

    node_base* erase(node_base* it);
    void reset(S32 node_size);
    void create(S32 node_size, S32 max_size, void* buffer);
    void push_back();

    node_base* alloc()
    {
        node_base* ret = stack;
        stack = ret->next;
        return ret;
    }

    void free(node_base* n)
    {
        n->next = stack;
        stack = n;
    }
};

template <class T, S32 N> struct pool_list : pool_list_base
{
    struct node_type
    {
        node_type* prev;
        node_type* next;
        T value;
    };

    void create(void* buffer)
    {
        pool_list_base::create(sizeof(node_type), N, buffer);
    }

    void reset()
    {
        pool_list_base::reset(sizeof(node_type));
    }

    void erase(T& value)
    {
        pool_list_base::erase((node_base*)((U8*)&value - offsetof(node_type, value)));
    }

    T& back()
    {
        return ((node_type*)tail.prev)->value;
    }
};

template <class T, U32 N> struct fixed_queue
{
    U32 _first;
    U32 _last;
    T _buffer[N + 1];

    struct iterator
    {
        U32 _it;
        fixed_queue<T, N>* _owner;

        T& operator*() const
        {
            return _owner->_buffer[_it];
        }

        bool operator!=(const iterator& other) const
        {
            return _it != other._it;
        }

        iterator* operator+=(S32 value)
        {
            value = _it + value;
            _it = (value + N + 1) % (N + 1);
            return this;
        }

        iterator* operator-=(S32 value)
        {
            iterator* tmp = operator+=(-value);
            return tmp;
        }

        iterator* operator--()
        {
            *this -= 1;
            return this;
        }

        iterator operator-(S32 value) const
        {
            iterator tmp = *this;
            tmp -= value;
            return tmp;
        }

        iterator* operator++()
        {
            *this += 1;
            return this;
        }
    };

    void reset()
    {
        clear();
    }
    void clear()
    {
        _first = _last = 0;
    }
    T& front()
    {
        iterator it = begin();
        return *it;
    }
    void pop_front()
    {
        _first = (_first + 1) % (N + 1);
    }
    void push_front()
    {
        _first = (_first + N) % (N + 1);
    }
    void push_front(const T& data)
    {
        push_front();
        T& new_front = front();
        new_front = data;
    }
    void push_back()
    {
        _last = (_last + 1) % (N + 1);
    }
    U32 max_size() const
    {
        return N;
    }
    bool full() const
    {
        return size() == max_size();
    }
    T& back()
    {
        iterator it = end() - 1;
        return *it;
    }
    void pop_back()
    {
        _last = (_last + N) % (N + 1);
    }
    bool empty() const
    {
        return _last == _first;
    }
    U32 size() const
    {
        return (_last + (N + 1) - _first) % (N + 1);
    }
    // No symbol in TSSM (always inlined; cruise_bubble's state_camera_survey uses it).
    T& operator[](S32 index)
    {
        return _buffer[(N + 1 + _first + index) % (N + 1)];
    }

    iterator create_iterator(U32 initial_location) const
    {
        iterator it;
        it._it = initial_location;
        it._owner = const_cast<fixed_queue<T, N>*>(this);
        return it;
    }

    iterator begin() const
    {
        return create_iterator(_first);
    }

    iterator end() const
    {
        return create_iterator(_last);
    }
};

#endif
