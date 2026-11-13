#ifndef XDEBUGTWEAK_H
#define XDEBUGTWEAK_H

// HI's debug tweak system (its own header, apart from xDebug.h, as in Ratatouille).
#include "xString.h"

#include <types.h>

extern char* tweak_blend_mode_labels[11];
extern const U32 tweak_blend_mode_values[11];
extern const U8 tweak_blend_mode_values8[11];

struct uint_data
{
    U32 value_def;
    U32 value_min;
    U32 value_max;
};

struct float_data
{
    F32 value_def;
    F32 value_min;
    F32 value_max;
};

struct bool_data
{
    U8 value_def;
};

struct select_data
{
    U32 value_def;
    U32 labels_size;
    char** labels;
    void* values;
};

struct flag_data
{
    U32 value_def;
    U32 mask;
};

struct raw_data
{
    U8 pad[16];
};

struct int_data
{
    S32 value_def;
    S32 value_min;
    S32 value_max;
};

struct tweak_callback;
struct tweak_info
{
    substr name;
    void* value;
    tweak_callback* cb;
    void* context;
    U8 type;
    U8 value_size;
    U16 flags;
    union
    {
        int_data int_context;
        uint_data uint_context;
        float_data float_context;
        bool_data bool_context;
        select_data select_context;
        flag_data flag_context;
        raw_data all_context;
    };
};

struct tweak_callback
{
    void (*on_change)(tweak_info&);
    void (*on_select)(tweak_info&);
    void (*on_unselect)(tweak_info&);
    void (*on_start_edit)(tweak_info&);
    void (*on_stop_edit)(tweak_info&);
    void (*on_expand)(tweak_info&);
    void (*on_collapse)(tweak_info&);
    void (*on_update)(tweak_info&);
    void (*convert_mem_to_tweak)(tweak_info&, void*);
    void (*convert_tweak_to_mem)(tweak_info&, void*);

    // The create helpers have no symbols (always inlined); names from Ratatouille's xDebugTweak.h.
    // create_select is inferred for the on_select/on_unselect pair. Their zero brace-initializers
    // are the seven 0x28 templates that open retail's .rodata in most wads.
    static tweak_callback create(void (*on_change)(tweak_info&), void (*on_select)(tweak_info&),
                                 void (*on_unselect)(tweak_info&),
                                 void (*on_start_edit)(tweak_info&),
                                 void (*on_stop_edit)(tweak_info&), void (*on_expand)(tweak_info&),
                                 void (*on_collapse)(tweak_info&), void (*on_update)(tweak_info&),
                                 void (*convert_mem_to_tweak)(tweak_info&, void*),
                                 void (*convert_tweak_to_mem)(tweak_info&, void*))
    {
        tweak_callback cb = { on_change,           on_select,    on_unselect,
                              on_start_edit,       on_stop_edit, on_expand,
                              on_collapse,         on_update,    convert_mem_to_tweak,
                              convert_tweak_to_mem };
        return cb;
    }

    static tweak_callback create_change(void (*on_change)(const tweak_info&))
    {
        tweak_callback cb = {};
        cb.on_change = (void (*)(tweak_info&))on_change;
        return cb;
    }

    static tweak_callback create_select(void (*on_select)(tweak_info&),
                                        void (*on_unselect)(tweak_info&))
    {
        tweak_callback cb = {};
        cb.on_select = on_select;
        cb.on_unselect = on_unselect;
        return cb;
    }

    static tweak_callback create_edit(void (*on_start_edit)(tweak_info&),
                                      void (*on_stop_edit)(tweak_info&))
    {
        tweak_callback cb = {};
        cb.on_start_edit = on_start_edit;
        cb.on_stop_edit = on_stop_edit;
        return cb;
    }

    static tweak_callback create_expand(void (*on_expand)(tweak_info&),
                                        void (*on_collapse)(tweak_info&))
    {
        tweak_callback cb = {};
        cb.on_expand = on_expand;
        cb.on_collapse = on_collapse;
        return cb;
    }

    static tweak_callback create_update(void (*on_update)(tweak_info&))
    {
        tweak_callback cb = {};
        cb.on_update = on_update;
        return cb;
    }

    static tweak_callback create_converter(void (*convert_mem_to_tweak)(tweak_info&, void*),
                                           void (*convert_tweak_to_mem)(tweak_info&, void*))
    {
        tweak_callback cb = {};
        cb.convert_mem_to_tweak = convert_mem_to_tweak;
        cb.convert_tweak_to_mem = convert_tweak_to_mem;
        return cb;
    }
};

void xDebugAddTweak(const char*, F32*, F32, F32, const tweak_callback*, void*, U32);
void xDebugRemoveTweak(const char*);

#endif
