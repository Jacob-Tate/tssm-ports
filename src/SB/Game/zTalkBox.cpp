#include "zTalkBox.h"

#include "containers.h"
#include "xEvent.h"
#include "xFont.h"
#include "xGroup.h"
#include "xIni.h"
#include "xMath.h"
#include "xMath3.h"
#include "xModel.h"
#include "xPad.h"
#include "xSnd.h"
#include "xString.h"
#include "xTextAsset.h"
#include "xstransvc.h"

#include "zBase.h"
#include "zEntPlayer.h"
#include "zGame.h"
#include "zGlobals.h"
#include "zMusic.h"
#include "zScene.h"

#include <string.h>
#include <types.h>

namespace
{
    enum state_enum
    {
        STATE_INVALID = -1,
        BEGIN_STATE,
        STATE_START,
        STATE_NEXT,
        STATE_WAIT,
        STATE_STOP,
        END_STATE,
        MAX_STATE = END_STATE
    };

    enum trigger_pads_enum
    {
        TP_NEVER,
        TP_TRAPPED,
        TP_ACTIVE,
        TP_FORCED_ACTIVE
    };

    enum query_enum
    {
        Q_SKIP,
        Q_YESNO
    };

    struct state_type;

    struct wait_context
    {
        struct
        {
            U8 time : 1;
            U8 prompt : 1;
            U8 sound : 1;
            U8 event : 1;
            U32 pad : 28;
        } type;
        U8 need;
        F32 delay;
        U32 event_mask;
        query_enum query;

        void reset_type()
        {
            *(U32*)&type = 0;
        }
    };

    struct trigger_pair
    {
        ztalkbox* origin;
        U32 event;
    };

    struct shared_type
    {
        S32 flags;
        U32 permit;
        ztalkbox* active;
        state_type* state;
        state_type* states[MAX_STATE];
        xtextbox::layout lt;
        S32 begin_jot;
        S32 end_jot;
        S32 page_end_jot;
        wait_context wait;
        wait_context auto_wait;
        U32 wait_event_mask;
        sound_queue<4> sounds;
        U8 allow_quit;
        U8 quitting;
        U8 delay_events;
        ztalkbox::callback* cb;
        fixed_queue<trigger_pair, 32> triggered;
        F32 volume;
        zNPC::base* speak_npc;
        U32 speak_player;
    };

    struct sound_context
    {
        enum action_enum
        {
            ACTION_SET,
            ACTION_PUSH,
            ACTION_POP
        };

        enum type_enum
        {
            TYPE_INVALID,
            TYPE_VOLUME,
            TYPE_TARGET,
            TYPE_ORIGIN
        };

        iSndGroupHandle soundgroup;
        struct
        {
            action_enum action : 16;
            type_enum type : 16;
        };
        union
        {
            struct
            {
                F32 left;
                F32 right;
            } volume;
            U32 target;
            xVec3 origin;
        };
        U32 speaker;
        U8 anim;
        U8 pad[3];
    };

    struct teleport_context
    {
        U8 use_loc;
        U8 use_yaw;
        xVec3 loc;
        F32 yaw;
    };

    struct signal_context
    {
        U32 flags;
    };

    struct TriggerPadsContext
    {
        U32 triggerPadTalkBoxID;
        U8 triggerPads;
    };

    struct location_asset : xDynAsset
    {
        xVec3 loc;

        static const char* type_name()
        {
            return "location";
        }
    };

    struct pointer_asset : xDynAsset
    {
        xVec3 loc;
        F32 yaw;
        F32 pitch;
        F32 roll;

        static const char* type_name()
        {
            return "pointer";
        }
    };

    shared_type shared;
    U32 new_tags_size = 12;
    F32 music_fade_delay = 0.5f;

    static void stop();

    static inline void speak_stop()
    {
        if (shared.speak_npc)
        {
            zEntEvent(shared.speak_npc, eEventNPCSpeakStop);
            shared.speak_npc = NULL;
        }

        if (shared.speak_player)
        {
            xglobals->___player_ent_dont_use_directly->SpeakStop();
            shared.speak_player = 0;
        }
    }

    static inline void trigger(U32 event)
    {
        if (shared.delay_events)
        {
            shared.triggered.push_back();
            trigger_pair& tp = shared.triggered.back();
            tp.origin = shared.active;
            tp.event = event;
        }
        else
        {
            zEntEvent(shared.active, shared.active, event);
        }
    }

    static inline U32* pad_pressed()
    {
        if ((shared.permit & 2) && shared.active && shared.active->flag.visible)
        {
            return &globals.pad0->pressed;
        }

        static U32 zero;
        zero = 0;
        return &zero;
    }

    static void trigger_pads(U32 pressed)
    {
        if (pressed & XPAD_BUTTON_UP)
        {
            trigger(eEventPadPressUp);
        }
        if (pressed & XPAD_BUTTON_DOWN)
        {
            trigger(eEventPadPressDown);
        }
        if (pressed & XPAD_BUTTON_LEFT)
        {
            trigger(eEventPadPressLeft);
        }
        if (pressed & XPAD_BUTTON_RIGHT)
        {
            trigger(eEventPadPressRight);
        }
        if (pressed & XPAD_BUTTON_START)
        {
            trigger(eEventPadPressStart);
        }
        if (pressed & XPAD_BUTTON_SELECT)
        {
            trigger(eEventPadPressSelect);
        }
        if (pressed & XPAD_BUTTON_R1)
        {
            trigger(eEventPadPressR1);
        }
        if (pressed & XPAD_BUTTON_R2)
        {
            trigger(eEventPadPressR2);
        }
        if (pressed & XPAD_BUTTON_L1)
        {
            trigger(eEventPadPressL1);
        }
        if (pressed & XPAD_BUTTON_L2)
        {
            trigger(eEventPadPressL2);
        }
        if (pressed & XPAD_BUTTON_X)
        {
            trigger(eEventPadPressX);
        }
        if (pressed & XPAD_BUTTON_O)
        {
            trigger(eEventPadPressO);
        }
        // The SQUARE and TRIANGLE bits raise each other's events (retail).
        if (pressed & XPAD_BUTTON_SQUARE)
        {
            trigger(eEventPadPressTriangle);
        }
        if (pressed & XPAD_BUTTON_TRIANGLE)
        {
            trigger(eEventPadPressSquare);
        }
    }

    static inline void flush_triggered()
    {
        while (!shared.triggered.empty())
        {
            trigger_pair tp = shared.triggered.front();
            shared.triggered.pop_front();
            zEntEvent(tp.origin, tp.origin, tp.event);
        }
    }

    static inline bool read_bool(const substr& s, bool def)
    {
        static const substr positive[6] = { { "yes", 3 },  { "y", 1 }, { "1", 1 },
                                            { "true", 4 }, { "t", 1 }, { "on", 2 } };
        // Retail bug: "no" has size 3 (it includes the terminator), so "no" never matches.
        static const substr negative[6] = { { "no", 3 },    { "n", 1 }, { "0", 1 },
                                            { "false", 5 }, { "f", 1 }, { "off", 3 } };

        if (def)
        {
            for (U32 i = 0; i < 6; i++)
            {
                if (icompare(s, negative[i]) == 0)
                {
                    return false;
                }
            }
            return true;
        }
        else
        {
            for (U32 i = 0; i < 6; i++)
            {
                if (icompare(s, positive[i]) == 0)
                {
                    return true;
                }
            }
            return false;
        }
    }

    static inline void reset_auto_wait()
    {
        const ztalkbox::asset_type& a = *shared.active->asset;

        shared.auto_wait.type.time = a.auto_wait.type.time;
        shared.auto_wait.type.prompt = a.auto_wait.type.prompt;
        shared.auto_wait.type.sound = a.auto_wait.type.sound;
        shared.auto_wait.type.event = a.auto_wait.type.event;
        shared.auto_wait.delay = a.auto_wait.delay;
        shared.auto_wait.need = false;

        if (a.auto_wait.which_event <= 0 || a.auto_wait.which_event >= 32)
        {
            shared.auto_wait.event_mask = 0xffffffff;
        }
        else
        {
            shared.auto_wait.event_mask = 1 << a.auto_wait.which_event;
        }

        shared.auto_wait.query = Q_SKIP;
    }

    static void load_wait_context(wait_context& c, const xtextbox::tag_entry_list& el)
    {
        static S32 v[32];

        c.reset_type();
        c.delay = 0.0f;

        xtextbox::tag_entry* e = xtextbox::find_entry(el, substr::create("wait", 4));
        if (e && xtextbox::read_list(*e, &c.delay, 1) == 1)
        {
            c.type.time = true;
        }

        xtextbox::tag_entry* prompt = xtextbox::find_entry(el, substr::create("prompt", 6));
        if (prompt)
        {
            c.type.prompt = true;

            if (prompt->op == '=' && prompt->args_size == 1 &&
                icompare(*prompt->args, substr::create("yesno", 5)) == 0)
            {
                c.query = Q_YESNO;
            }
            else
            {
                c.query = Q_SKIP;
            }
        }

        e = xtextbox::find_entry(el, substr::create("sound", 5));
        c.type.sound = (e != NULL);

        e = xtextbox::find_entry(el, substr::create("event", 5));
        if (e)
        {
            c.type.event = true;

            U32 r = xtextbox::read_list(*e, v, 32);
            if (r == 0)
            {
                c.event_mask = 0xffffffff;
            }
            else if (e->op == '=')
            {
                for (U32 i = 0; i < r; i++)
                {
                    if ((U32)v[i] < 32)
                    {
                        c.event_mask |= 1 << v[i];
                    }
                }

                if (c.event_mask & 1)
                {
                    c.event_mask = 0xffffffff;
                }
            }
        }
    }

    static void parse_tag_auto_wait(xtextbox::jot& j, const xtextbox&, const xtextbox& ctb,
                                    const xtextbox::split_tag& ti)
    {
        if (!shared.active)
        {
            return;
        }

        if (&shared.active->dialog_box->tb != &ctb)
        {
            return;
        }

        wait_context& c = *(wait_context*)j.context;
        j.context_size = sizeof(wait_context);

        xtextbox::tag_entry_list el = xtextbox::read_tag(ti.tag);
        xtextbox::tag_entry& e = *el.entries;

        if (e.op == '=' && e.args_size != 0 && !read_bool(*e.args, true))
        {
            c.reset_type();
            c.delay = 0.0f;
        }
        else
        {
            el.size--;
            el.entries++;
            load_wait_context(c, el);
        }

        shared.auto_wait = c;
    }

    static void reset_tag_auto_wait(xtextbox::jot& j, const xtextbox&, const xtextbox& ctb,
                                    const xtextbox::split_tag&)
    {
        if (!shared.active)
        {
            return;
        }

        if (&shared.active->dialog_box->tb != &ctb)
        {
            return;
        }

        wait_context& c = *(wait_context*)j.context;
        j.context_size = sizeof(wait_context);
        reset_auto_wait();
        c = shared.auto_wait;
    }

    static U8 trigger_auto_wait(const xtextbox::jot& j)
    {
        shared.auto_wait = *(const wait_context*)j.context;
        return true;
    }

    static void parse_tag_signal(xtextbox::jot& j, const xtextbox&, const xtextbox& ctb,
                                 const xtextbox::split_tag& ti)
    {
        if (!shared.active)
        {
            return;
        }

        if (&shared.active->dialog_box->tb != &ctb)
        {
            return;
        }

        signal_context& c = *(signal_context*)&j.context;
        xtextbox::tag_entry* e;
        c.flags = 0;

        xtextbox::tag_entry_list el = xtextbox::read_tag(ti.tag);

        if (el.entries->op != ':')
        {
            return;
        }

        S32 v[20];
        U32 r = xtextbox::read_list(*el.entries, v, 20);
        if (r == 0)
        {
            c.flags = 0x7fffffff;
        }
        else
        {
            for (U32 i = 0; i < r; i++)
            {
                if ((U32)v[i] < 20)
                {
                    c.flags |= 1 << v[i];
                }
            }
        }

        el.entries++;
        el.size--;

        e = xtextbox::find_entry(el, substr::create("need", 4));
        if (!e)
        {
            return;
        }

        if (e->op != '=' || e->args_size == 0 || !read_bool(*e->args, true))
        {
            c.flags |= 0x80000000;
        }
    }

    static U8 trigger_signal(const xtextbox::jot& j)
    {
        const signal_context& c = *(const signal_context*)&j.context;

        if (shared.quitting && !(c.flags & 0x80000000))
        {
            return true;
        }

        static const U32 signals[20] = {
            eEventTalkBox_OnSignal0,  eEventTalkBox_OnSignal1,  eEventTalkBox_OnSignal2,
            eEventTalkBox_OnSignal3,  eEventTalkBox_OnSignal4,  eEventTalkBox_OnSignal5,
            eEventTalkBox_OnSignal6,  eEventTalkBox_OnSignal7,  eEventTalkBox_OnSignal8,
            eEventTalkBox_OnSignal9,  eEventTalkBox_OnSignal10, eEventTalkBox_OnSignal11,
            eEventTalkBox_OnSignal12, eEventTalkBox_OnSignal13, eEventTalkBox_OnSignal14,
            eEventTalkBox_OnSignal15, eEventTalkBox_OnSignal16, eEventTalkBox_OnSignal17,
            eEventTalkBox_OnSignal18, eEventTalkBox_OnSignal19
        };

        for (U32 i = 0; i < 20; i++)
        {
            if (c.flags & (1 << i))
            {
                trigger(signals[i]);
            }
        }

        if (shared.cb)
        {
            shared.cb->on_signal(c.flags & 0x7fffffff);
        }

        return true;
    }

    static inline void load_sound_type(sound_context& c, const xtextbox::tag_entry_list& el)
    {
        xtextbox::tag_entry* e = xtextbox::find_entry(el, substr::create("volume", 6));
        if (e)
        {
            U32 r = xtextbox::read_list(*e, &c.volume.left, 2);
            if (r == 1)
            {
                c.volume.right = c.volume.left;
            }

            if (r != 0)
            {
                c.type = sound_context::TYPE_VOLUME;
                return;
            }
        }

        c.volume.right = c.volume.left = 1.0f;
        c.type = sound_context::TYPE_VOLUME;
    }

    static void parse_tag_sound(xtextbox::jot& j, const xtextbox&, const xtextbox& ctb,
                                const xtextbox::split_tag& ti)
    {
        if (!shared.active)
        {
            return;
        }

        if (&shared.active->dialog_box->tb != &ctb)
        {
            return;
        }

        sound_context& c = *(sound_context*)j.context;
        xtextbox::tag_entry* e;
        j.context_size = sizeof(sound_context);

        xtextbox::tag_entry_list el = xtextbox::read_tag(ti.tag);

        c.action = sound_context::ACTION_SET;

        e = xtextbox::find_entry(el, substr::create("action", 6));
        if (e && e->args_size != 0)
        {
            substr& action = *e->args;

            if (icompare(action, substr::create("add", 3)) == 0)
            {
                c.action = sound_context::ACTION_PUSH;
            }
            else if (icompare(action, substr::create("remove", 6)) == 0)
            {
                c.action = sound_context::ACTION_POP;
            }
        }

        e = el.entries;
        if (c.action != sound_context::ACTION_POP && e->op == ':' && e->args_size != 0)
        {
            c.soundgroup = xSndMgrGetSoundGroup(xStrHash(e->args->text, e->args->size));
        }
        else
        {
            c.soundgroup = (iSndGroupHandle)-1;
        }

        if (c.soundgroup != (iSndGroupHandle)-1 && c.action != sound_context::ACTION_POP)
        {
            load_sound_type(c, el);
        }

        c.speaker = 2;

        e = xtextbox::find_entry(el, substr::create("speaker", 7));
        if (e && e->args_size != 0)
        {
            substr& action = *e->args;

            if (icompare(action, substr::create("pc", 2)) == 0)
            {
                c.speaker = 1;
            }
            else if (icompare(action, substr::create("none", 4)) == 0)
            {
                c.speaker = 0;
            }
            else if (action.size != 0)
            {
                c.speaker = xStrHash(action.text, action.size);
            }
        }

        c.anim = 0;

        e = xtextbox::find_entry(el, substr::create("anim", 4));
        if (e && e->args_size != 0)
        {
            S32 anim = 0;
            xtextbox::read_list(*e, &anim, 1);
            c.anim = anim;
        }
    }

    static void reset_tag_sound(xtextbox::jot& j, const xtextbox&, const xtextbox& ctb,
                                const xtextbox::split_tag&)
    {
        if (!shared.active)
        {
            return;
        }

        if (&shared.active->dialog_box->tb != &ctb)
        {
            return;
        }

        sound_context& c = *(sound_context*)j.context;
        j.context_size = sizeof(sound_context);
        c.soundgroup = (iSndGroupHandle)-1;
        c.action = sound_context::ACTION_SET;
    }

    static U8 trigger_sound(const xtextbox::jot& j)
    {
        if (shared.quitting)
        {
            shared.sounds.clear();
            speak_stop();
            return true;
        }

        const sound_context& c = *(const sound_context*)j.context;

        switch (c.action)
        {
        case sound_context::ACTION_POP:
            if (shared.sounds.size() > 0)
            {
                shared.sounds.pop();
            }
            break;
        case sound_context::ACTION_SET:
            shared.sounds.clear();
            speak_stop();
            // fall through
        case sound_context::ACTION_PUSH:
        default:
        {
            if (c.soundgroup == (iSndGroupHandle)-1)
            {
                return true;
            }

            F32 fVolume = shared.volume * MAX(c.volume.left, c.volume.right);

            xSndMgrStopSounds(2, false);
            gSnd.m_uSndMgrFlags &= ~0x2;
            shared.sounds.push(xSndMgrPlay(c.soundgroup, 0, NULL, NULL, NULL, &fVolume, NULL));

            ztalkbox& talk = *shared.active;
            zNPC::base* npc = NULL;
            xEnt* player = NULL;

            switch (c.speaker)
            {
            case 0:
                break;
            case 1:
                player = xglobals->___player_ent_dont_use_directly;
                break;
            case 2:
                npc = talk.npc;
                break;
            default:
            {
                xBase* obj = zSceneFindObject(c.speaker);
                if (obj)
                {
                    if (obj->baseType == eBaseTypeNPC)
                    {
                        npc = (zNPC::base*)obj;
                    }
                    else if (obj->baseType == eBaseTypePlayer)
                    {
                        player = xglobals->___player_ent_dont_use_directly;
                    }
                }
                break;
            }
            }

            if (npc)
            {
                F32 params[4] = { c.anim - 1.0f };
                zEntEvent(NULL, 0, npc, eEventNPCSpeakStart, params, NULL, c.soundgroup, FE_NO);
                shared.speak_npc = npc;
            }
            else if (player)
            {
                xglobals->___player_ent_dont_use_directly->SpeakStart(c.soundgroup, c.anim - 1);
                shared.speak_player = 1;
            }
            break;
        }
        }

        return true;
    }

    static void parse_tag_pause(xtextbox::jot&, const xtextbox&, const xtextbox&,
                                const xtextbox::split_tag&)
    {
    }

    static void reset_tag_pause(xtextbox::jot&, const xtextbox&, const xtextbox&,
                                const xtextbox::split_tag&)
    {
    }

    static U8 trigger_pause(const xtextbox::jot&)
    {
        return true;
    }

    static void parse_tag_allow_quit(xtextbox::jot& j, const xtextbox&, const xtextbox&,
                                     const xtextbox::split_tag& ti)
    {
        U8& c = *(U8*)&j.context;
        c = ti.action.size != 1 || ti.action.text[0] != '=' || read_bool(ti.value, true);
    }

    static void reset_tag_allow_quit(xtextbox::jot& j, const xtextbox&, const xtextbox& ctb,
                                     const xtextbox::split_tag&)
    {
        if (!shared.active)
        {
            return;
        }

        if (&shared.active->dialog_box->tb != &ctb)
        {
            return;
        }

        *(U8*)&j.context = shared.active->asset->allow_quit != 0;
    }

    static U8 trigger_allow_quit(const xtextbox::jot& j)
    {
        shared.allow_quit = j.context != NULL;
        return true;
    }

    static void parse_tag_teleport(xtextbox::jot& j, const xtextbox&, const xtextbox&,
                                   const xtextbox::split_tag& ti)
    {
        if (!shared.active)
        {
            return;
        }

        teleport_context& c = *(teleport_context*)j.context;
        U32 id = shared.active->asset->teleport;

        bool has_value = ti.action.size == 1 && ti.action.text[0] == ':' && ti.value.size != 0;
        if (has_value)
        {
            id = xStrHash(ti.value.text, ti.value.size);
        }

        if (id == 0)
        {
            return;
        }

        c.use_yaw = false;
        c.use_loc = false;

        xDynAsset* a = (xDynAsset*)xSTFindAsset(id, NULL);
        if (!a)
        {
            return;
        }

        if (a->type == xStrHash(location_asset::type_name()))
        {
            location_asset& ta = *(location_asset*)a;
            c.use_loc = true;
            c.loc = ta.loc;
        }
        else if (a->type == xStrHash(pointer_asset::type_name()))
        {
            pointer_asset& ta = *(pointer_asset*)a;
            c.use_yaw = true;
            c.use_loc = true;
            c.loc = ta.loc;
            c.yaw = (PI / 180.0f) * ta.yaw;
        }

        j.context_size = sizeof(teleport_context);
    }

    static inline void move_player(const xVec3& loc)
    {
        xMat4x3* dst_mat = xModelGetFrame(xglobals->___player_ent_dont_use_directly->model);
        xEntFrame* out_frame = xglobals->___player_ent_dont_use_directly->frame;

        dst_mat->pos = out_frame->mat.pos = loc;
    }

    static inline void turn_player(F32 yaw)
    {
        xMat3x3& m = *xModelGetFrame(xglobals->___player_ent_dont_use_directly->model);
        xVec3 ang;
        xMat3x3GetEuler(&m, &ang);
        ang.x = yaw;
        xMat3x3Euler(&m, &ang);
    }

    static U8 trigger_teleport(const xtextbox::jot& j)
    {
        if (j.context_size != sizeof(teleport_context))
        {
            return true;
        }

        const teleport_context& c = *(const teleport_context*)j.context;

        if (c.use_loc)
        {
            move_player(c.loc);
        }

        if (c.use_yaw)
        {
            turn_player(c.yaw);
        }

        return true;
    }

    static void parse_tag_trap(xtextbox::jot& j, const xtextbox&, const xtextbox&,
                               const xtextbox::split_tag& ti)
    {
        U8& c = *(U8*)&j.context;
        c = ti.action.size != 1 || ti.action.text[0] != '=' || read_bool(ti.value, true);
    }

    static void reset_tag_trap(xtextbox::jot& j, const xtextbox&, const xtextbox& ctb,
                               const xtextbox::split_tag&)
    {
        if (!shared.active)
        {
            return;
        }

        if (&shared.active->dialog_box->tb != &ctb)
        {
            return;
        }

        *(U8*)&j.context = shared.active->asset->trap != 0;
    }

    static U8 trigger_trap(const xtextbox::jot& j)
    {
        if (j.context)
        {
            zEntPlayerControlOff(CONTROL_OWNER_TALK_BOX);
        }
        else
        {
            zEntPlayerControlOn(CONTROL_OWNER_TALK_BOX);
        }

        return true;
    }

    static void parse_tag_wait(xtextbox::jot& j, const xtextbox&, const xtextbox& ctb,
                               const xtextbox::split_tag& ti)
    {
        if (!shared.active)
        {
            return;
        }

        if (&shared.active->dialog_box->tb != &ctb)
        {
            return;
        }

        wait_context& c = *(wait_context*)j.context;
        j.context_size = sizeof(wait_context);

        xtextbox::tag_entry_list el = xtextbox::read_tag(ti.tag);

        if (el.size == 1 && el.entries->args_size == 0)
        {
            c = shared.auto_wait;
        }
        else
        {
            load_wait_context(c, el);
        }
    }

    static U8 trigger_wait(const xtextbox::jot& j)
    {
        const wait_context& c = *(const wait_context*)j.context;

        if (shared.quitting && shared.allow_quit && !c.need)
        {
            return true;
        }

        shared.wait = c;
        return false;
    }

    static U8 triggerTagShow(const xtextbox::jot& j)
    {
        ztalkbox* talkBoxToShow = (ztalkbox*)zSceneFindObject(*(const U32*)j.context);
        if (talkBoxToShow)
        {
            talkBoxToShow->show();
        }

        return true;
    }

    static void parseTagShow(xtextbox::jot& j, const xtextbox&, const xtextbox&,
                             const xtextbox::split_tag& splitTag)
    {
        if (!shared.active)
        {
            return;
        }

        if (splitTag.action.size != 1 || splitTag.action.text[0] != ':' || splitTag.value.size == 0)
        {
            return;
        }

        U32* id = (U32*)j.context;
        *id = xStrHash(splitTag.value.text, splitTag.value.size);
        j.context_size = sizeof(U32);
    }

    static U8 triggerTagTriggerPads(const xtextbox::jot& j)
    {
        ztalkbox* talkBoxTriggerPads = (ztalkbox*)zSceneFindObject(
            ((const TriggerPadsContext*)j.context)->triggerPadTalkBoxID);
        if (talkBoxTriggerPads)
        {
            talkBoxTriggerPads->triggerPads = TP_FORCED_ACTIVE;
        }

        return true;
    }

    static U8 triggerTagResetHide(const xtextbox::jot& j)
    {
        ztalkbox* talkBoxToResetHide = (ztalkbox*)zSceneFindObject(*(const U32*)j.context);
        if (talkBoxToResetHide)
        {
            talkBoxToResetHide->flag.visible = false;
            talkBoxToResetHide->prompt_box->deactivate();
            talkBoxToResetHide->quit_box->deactivate();
        }

        return true;
    }

    static void parseTagTriggerPads(xtextbox::jot& j, const xtextbox&, const xtextbox&,
                                    const xtextbox::split_tag& splitTag)
    {
        if (!shared.active)
        {
            return;
        }

        if (splitTag.action.size != 1 || splitTag.action.text[0] != ':' || splitTag.value.size == 0)
        {
            return;
        }

        const char* c;
        bool forceActiveTag = false;

        for (c = splitTag.value.text; c < splitTag.value.text + splitTag.value.size; c++)
        {
            if (*c == '.')
            {
                const char* forceActiveKeyword = "ForceActive";
                forceActiveTag = imemcmp(c + 1, forceActiveKeyword,
                                         MIN(splitTag.value.size, strlen(forceActiveKeyword))) == 0;
                break;
            }
        }

        if (forceActiveTag)
        {
            TriggerPadsContext* context = (TriggerPadsContext*)j.context;
            j.context_size = sizeof(TriggerPadsContext);
            U32 talkBoxTriggerPadsID = xStrHash(splitTag.value.text, c - splitTag.value.text);
            context->triggerPadTalkBoxID = talkBoxTriggerPadsID;
            context->triggerPads = forceActiveTag;
        }
    }

    static void parseTagResetHide(xtextbox::jot& j, const xtextbox&, const xtextbox&,
                                  const xtextbox::split_tag& splitTag)
    {
        if (!shared.active)
        {
            return;
        }

        if (splitTag.action.size != 1 || splitTag.action.text[0] != ':' || splitTag.value.size == 0)
        {
            return;
        }

        U32* id = (U32*)j.context;
        *id = xStrHash(splitTag.value.text, splitTag.value.size);
        j.context_size = sizeof(U32);
    }

    xtextbox::tag_type new_tags[] = {
        { { "allow_quit", 10 },
          parse_tag_allow_quit,
          reset_tag_allow_quit,
          (void*)trigger_allow_quit },
        { { "aq", 2 }, parse_tag_allow_quit, reset_tag_allow_quit, (void*)trigger_allow_quit },
        { { "auto_wait", 9 }, parse_tag_auto_wait, reset_tag_auto_wait, (void*)trigger_auto_wait },
        { { "pause", 5 }, parse_tag_pause, reset_tag_pause, (void*)trigger_pause },
        { { "resethide", 9 }, parseTagResetHide, NULL, (void*)triggerTagResetHide },
        { { "show", 4 }, parseTagShow, NULL, (void*)triggerTagShow },
        { { "signal", 6 }, parse_tag_signal, NULL, (void*)trigger_signal },
        { { "sound", 5 }, parse_tag_sound, reset_tag_sound, (void*)trigger_sound },
        { { "teleport", 8 }, parse_tag_teleport, NULL, (void*)trigger_teleport },
        { { "trap", 4 }, parse_tag_trap, reset_tag_trap, (void*)trigger_trap },
        { { "triggerpads", 11 }, parseTagTriggerPads, NULL, (void*)triggerTagTriggerPads },
        { { "wait", 4 }, parse_tag_wait, NULL, (void*)trigger_wait },
    };

    static inline void start_audio_effect(ztalkbox& talk)
    {
        // Retail keeps the one-time registration flag, but the debug tweaks it guarded are gone.
        static bool registered = false;

        if (!registered)
        {
            registered = true;
        }

        switch (talk.asset->audio_effect)
        {
        case 0:
            break;
        case 1:
            zMusicSetVolume(gDialogueMusicLevel, music_fade_delay);
            break;
        }
    }

    static inline void stop_audio_effect()
    {
        if (!shared.active)
        {
            return;
        }

        switch (shared.active->asset->audio_effect)
        {
        case 0:
            break;
        case 1:
            zMusicSetVolume(1.0f, music_fade_delay);
            break;
        }
    }

    static inline void deactivate()
    {
        stop_audio_effect();

        if (!shared.active)
        {
            return;
        }

        if (shared.active->prompt_box)
        {
            shared.active->prompt_box->deactivate();
        }

        shared.active = NULL;
    }

    static inline void activate(ztalkbox& t)
    {
        deactivate();

        shared.active = &t;

        if (t.dialog_box->flag.active)
        {
            t.dialog_box->deactivate();
        }

        if (t.prompt_box && t.flag.visible)
        {
            t.prompt_box->activate(true);
        }

        if (t.dialog_box)
        {
            t.dialog_box->flag.skipFrame = true;
        }

        start_audio_effect(t);
    }

    static inline bool is_wait_jot(const xtextbox::jot& j)
    {
        return j.tag && j.tag->parse_tag == parse_tag_wait;
    }

    static inline void update_prompts()
    {
        ztalkbox& active = *shared.active;

        if (active.prompt_box)
        {
            const char* message;

            if (shared.wait.type.prompt)
            {
                const char* queries[2] = { active.prompt.skip, active.prompt.yesno };
                message = queries[shared.wait.query];
            }
            else
            {
                message = active.prompt.noskip;
            }

            if (message)
            {
                active.prompt_box->set_text(message);
                if (active.flag.visible)
                {
                    active.prompt_box->activate(true);
                }
            }
            else
            {
                active.prompt_box->deactivate();
            }
        }

        if (active.quit_box)
        {
            if (shared.allow_quit && active.prompt.quit)
            {
                active.quit_box->set_text(active.prompt.quit);
                if (active.flag.visible)
                {
                    active.quit_box->activate(true);
                }
            }
            else if (!shared.allow_quit && active.prompt.noquit)
            {
                active.quit_box->set_text(active.prompt.noquit);
                if (active.flag.visible)
                {
                    active.quit_box->activate(true);
                }
            }
            else
            {
                active.quit_box->deactivate();
            }
        }
    }

    static inline void hide_prompts()
    {
        ztalkbox& active = *shared.active;

        if (active.prompt_box)
        {
            active.prompt_box->deactivate();
        }

        if (active.quit_box)
        {
            active.quit_box->deactivate();
        }
    }

    static inline U8 trigger_jot(const xtextbox::jot& j)
    {
        if (!j.tag)
        {
            return true;
        }

        if (j.tag->context)
        {
            return ((U8 (*)(const xtextbox::jot&))j.tag->context)(j);
        }

        return true;
    }

    static inline U8 trigger_jot(S32 index)
    {
        return trigger_jot(shared.lt.jots()[index]);
    }

    struct state_type
    {
        state_enum type;

        state_type(state_enum t)
        {
            type = t;
        }

        virtual void start()
        {
        }

        virtual void stop()
        {
        }

        virtual state_enum update(xScene& s, F32 dt) = 0;
    };

    struct start_state_type : state_type
    {
        start_state_type() : state_type(STATE_START)
        {
        }

        void start()
        {
            shared.page_end_jot = 0;
            shared.end_jot = 0;
            shared.begin_jot = 0;
            shared.wait.reset_type();
            shared.wait.type.time = true;
            shared.wait.delay = 0.0f;
        }

        void stop()
        {
        }

        state_enum update(xScene&, F32)
        {
            return STATE_NEXT;
        }
    };

    struct next_state_type : state_type
    {
        S32 prev_wait_jot;

        next_state_type() : state_type(STATE_NEXT)
        {
        }

        void start()
        {
            if (shared.end_jot == shared.page_end_jot)
            {
                xtextbox& tb = shared.active->dialog_box->tb;
                S32 size;
                S32 jots_size = shared.lt.jots_size();

                shared.begin_jot = shared.end_jot;

                tb.yextent(tb.bounds.h, size, shared.lt, shared.begin_jot, -1);

                if (size == 0 && jots_size > shared.begin_jot)
                {
                    size = 1;
                }

                shared.page_end_jot = shared.begin_jot + size;
            }

            while (shared.end_jot < shared.page_end_jot)
            {
                if (!trigger_jot(shared.end_jot++))
                {
                    break;
                }
            }

            if (shared.end_jot == shared.page_end_jot)
            {
                xtextbox::jot* prev = shared.lt.jots() + shared.end_jot - 1;

                if (prev->flag.page_break && shared.end_jot - 1 > shared.begin_jot)
                {
                    prev--;
                }

                if (!is_wait_jot(*prev))
                {
                    shared.wait = shared.auto_wait;
                }
            }
        }

        void stop()
        {
        }

        state_enum update(xScene&, F32)
        {
            if (shared.begin_jot == shared.page_end_jot)
            {
                return STATE_STOP;
            }

            return STATE_WAIT;
        }
    };

    struct wait_state_type : state_type
    {
        U8 answer_yes;

        wait_state_type() : state_type(STATE_WAIT)
        {
        }

        void start()
        {
            answer_yes = false;
            update_prompts();
        }

        void stop()
        {
            if (shared.wait.type.prompt && shared.wait.query == Q_YESNO)
            {
                U32 event = eEventTalkBox_OnNo;
                if (answer_yes)
                {
                    event = eEventTalkBox_OnYes;
                }
                trigger(event);
            }

            shared.wait.reset_type();
            shared.wait.type.time = true;
            shared.wait.delay = 0.0f;
            shared.wait.need = false;
        }

        state_enum update(xScene&, F32 dt)
        {
            U32* pressed = pad_pressed();

            if (shared.quitting)
            {
                if (shared.allow_quit && !shared.wait.need)
                {
                    return STATE_NEXT;
                }

                shared.quitting = false;
            }

            if (shared.wait.type.time)
            {
                shared.wait.delay -= dt;
                if (shared.wait.delay <= 0.0f)
                {
                    return STATE_NEXT;
                }
            }

            if (shared.wait.type.prompt)
            {
                switch (shared.wait.query)
                {
                case Q_YESNO:
                    if (*pressed & XPAD_BUTTON_X)
                    {
                        *pressed &= ~XPAD_BUTTON_X;
                        answer_yes = true;
                        return STATE_NEXT;
                    }

                    if (*pressed & XPAD_BUTTON_SQUARE)
                    {
                        *pressed &= ~XPAD_BUTTON_SQUARE;
                        return STATE_NEXT;
                    }
                    break;
                case Q_SKIP:
                default:
                    if (*pressed & XPAD_BUTTON_X)
                    {
                        *pressed &= ~XPAD_BUTTON_X;
                        return STATE_NEXT;
                    }
                    break;
                }
            }

            if (shared.allow_quit && (*pressed & XPAD_BUTTON_TRIANGLE))
            {
                shared.quitting = true;
                *pressed &= ~XPAD_BUTTON_TRIANGLE;
                return STATE_NEXT;
            }

            if (shared.wait.type.sound && !shared.sounds.playing())
            {
                return STATE_NEXT;
            }

            if (shared.wait.type.event && (shared.wait_event_mask & shared.wait.event_mask))
            {
                shared.wait_event_mask &= ~shared.wait.event_mask;
                return STATE_NEXT;
            }

            return STATE_WAIT;
        }
    };

    struct stop_state_type : state_type
    {
        stop_state_type() : state_type(STATE_STOP)
        {
        }

        void start()
        {
        }

        void stop()
        {
        }

        state_enum update(xScene&, F32)
        {
            return STATE_INVALID;
        }
    };

    static inline void stop_wait(ztalkbox& e, const F32* args, U32 args_size)
    {
        if (shared.active != &e)
        {
            return;
        }

        U32 mask = 0;

        for (U32 i = 0; i < args_size; i++)
        {
            U32 v = (U32)args[i];
            if (v != 0 && v < 32)
            {
                mask |= 1 << v;
            }
        }

        if (mask == 0)
        {
            mask = 0xffffffff;
        }

        e.stop_wait(mask);
    }

    static void cb_dispatch(xBase*, xBase* to, U32 event, const F32* argf, xBase*, U32)
    {
        ztalkbox& e = *(ztalkbox*)to;

        shared.delay_events = true;

        switch (event)
        {
        case eEventReset:
        case eEventSceneEnd:
            e.reset();
            break;
        case eEventInvisible:
        case eEventFastInvisible:
            e.hide();
            break;
        case eEventVisible:
        case eEventFastVisible:
            e.show();
            break;
        case eEventStartConversation:
            e.start_talk(argf == NULL ? 0 : *(const U32*)argf, NULL, NULL);
            flush_triggered();
            break;
        case eEventEndConversation:
            e.stop_talk();
            flush_triggered();
            break;
        case eEventTalkBox_StopWait:
            if (!argf)
            {
                stop_wait(e, NULL, 0);
            }
            else
            {
                stop_wait(e, argf, 4);
            }
            flush_triggered();
            break;
        case eEventSetText:
            if (argf)
            {
                e.set_text(*(const U32*)argf);
            }
            break;
        case eEventAddText:
            if (argf)
            {
                e.add_text(*(const U32*)argf);
            }
            break;
        case eEventClearText:
            e.clear_text();
            break;
        case eEventPadPressRight:
        case eEventPadPressLeft:
        case eEventOpenTBox:
        case eEventCloseTBox:
        case eEventTalkBox_OnSignal18:
        case eEventTalkBox_OnSignal19:
        case eEventTalkBox_OnStart:
        case eEventTalkBox_OnYes:
        case eEventTalkBox_OnNo:
            break;
        }

        shared.delay_events = false;
    }

    static inline const char* load_text(U32 id)
    {
        if (id == 0)
        {
            return NULL;
        }

        const char* text = xTextFindString(id, NULL);
        if (!text)
        {
            return NULL;
        }

        return text;
    }
} // namespace

void ztalkbox::load(const asset_type& a)
{
    xBaseInit(this, &a);
    baseType = eBaseTypeTalkBox;
    asset = &a;
    eventFunc = cb_dispatch;

    if (linkCount != 0)
    {
        link = (xLinkAsset*)(&a + 1);
    }

    dialog_box = (ztextbox*)zSceneFindObject(a.dialog_box);

    if (a.prompt_box == 0)
    {
        prompt_box = NULL;
    }
    else
    {
        prompt_box = (ztextbox*)zSceneFindObject(a.prompt_box);
    }

    if (a.quit_box == 0)
    {
        quit_box = NULL;
    }
    else
    {
        quit_box = (ztextbox*)zSceneFindObject(a.quit_box);
    }

    prompt.skip = load_text(a.prompt.skip);
    prompt.noskip = load_text(a.prompt.noskip);
    prompt.quit = load_text(a.prompt.quit);
    prompt.noquit = load_text(a.prompt.noquit);
    prompt.yesno = load_text(a.prompt.yesno);

    reset();
}

inline void ztalkbox::reset()
{
    if (asset->hide != 4)
    {
        flag.visible = true;

        if (shared.active == this)
        {
            deactivate();
        }
    }
    else
    {
        flag.visible = false;

        if (shared.active == this)
        {
            deactivate();
        }
    }

    triggerPads = asset->trigger_pads;
}

void ztalkbox::set_text(const char* s)
{
    ztextbox& d = *dialog_box;

    if (s)
    {
        d.set_text(s);
    }
    else
    {
        d.clear_text();
    }

    d.refresh();

    if (shared.active != this)
    {
        return;
    }

    if (shared.state)
    {
        shared.state->stop();
        shared.state = NULL;
    }

    shared.lt.refresh(d.tb, false);
    shared.state = shared.states[STATE_START];
    shared.state->start();
}

void ztalkbox::set_text(U32 id)
{
    if (id == 0)
    {
        return;
    }

    const char* text = xTextFindString(id, NULL);
    if (!text)
    {
        set_text((const char*)NULL);
        return;
    }

    set_text(text);
}

inline void ztalkbox::add_text(const char* s)
{
    dialog_box->add_text(s);

    if (shared.active == this)
    {
        shared.lt.refresh_end(dialog_box->tb);
    }
}

inline void ztalkbox::add_text(U32 id)
{
    if (id == 0)
    {
        return;
    }

    const char* text = xTextFindString(id, NULL);
    if (!text)
    {
        return;
    }

    add_text(text);
}

inline void ztalkbox::clear_text()
{
    set_text((const char*)NULL);
}

void ztalkbox::start_talk(const char* s, callback* cb, zNPC::base* npc)
{
    this->npc = npc;

    stop_talk();
    xSndMgrStopSounds(2, false);
    activate(*this);

    shared.cb = cb;
    shared.wait_event_mask = 0;
    reset_auto_wait();
    shared.allow_quit = asset->allow_quit != 0;
    shared.quitting = false;

    if (asset->trap)
    {
        zEntPlayerControlOff(CONTROL_OWNER_TALK_BOX);
    }
    else
    {
        zEntPlayerControlOn(CONTROL_OWNER_TALK_BOX);
    }

    MasterTellSlaves(1);
    update_prompts();

    ztextbox& d = *dialog_box;

    if (d.flag.active)
    {
        d.deactivate();
    }

    if (s)
    {
        d.set_text(s);
    }

    d.refresh();

    shared.lt.refresh(d.tb, false);
    shared.state = shared.states[STATE_START];
    trigger(eEventTalkBox_OnStart);

    if (cb)
    {
        cb->on_start();
    }

    shared.state->start();
}

void ztalkbox::start_talk(U32 text_id, callback* cb, zNPC::base* npc)
{
    if (text_id == 0)
    {
        start_talk((const char*)NULL, cb, npc);
        return;
    }

    const char* text = xTextFindString(text_id, NULL);
    if (text)
    {
        start_talk(text, cb, npc);
    }
}

void ztalkbox::stop_talk()
{
    if (shared.active == this)
    {
        stop();
    }
}

inline void ztalkbox::stop_wait(U32 x)
{
    if (shared.active == this)
    {
        shared.wait_event_mask |= x;
    }
}

namespace
{
    static inline void stop()
    {
        if (!shared.state)
        {
            return;
        }

        shared.state->stop();
        hide_prompts();
        shared.active->MasterTellSlaves(0);
        zEntPlayerControlOn(CONTROL_OWNER_TALK_BOX);
        trigger(eEventTalkBox_OnStop);

        if (shared.cb)
        {
            shared.cb->on_stop();
        }

        shared.state = NULL;
        deactivate();
        shared.sounds.clear();
        speak_stop();
    }
} // namespace

void ztalkbox::show()
{
    flag.visible = true;

    if (shared.active != this)
    {
        return;
    }

    if (prompt_box)
    {
        prompt_box->activate(true);
    }

    if (shared.allow_quit && prompt.quit && quit_box)
    {
        quit_box->activate(true);
    }
}

void ztalkbox::hide()
{
    flag.visible = false;

    if (prompt_box)
    {
        prompt_box->deactivate();
    }

    if (quit_box)
    {
        quit_box->deactivate();
    }
}

void ztalkbox::MasterTellSlaves(S32 isBeginning)
{
    for (S32 i = 0; i < linkCount; i++)
    {
        xLinkAsset* l = &link[i];

        if (l->dstEvent == eEventConnectToChild)
        {
            xBase* mychild = zSceneFindObject(l->dstAssetID);
            if (mychild)
            {
                MasterLoveSlave(mychild, isBeginning);
            }
        }
    }
}

void ztalkbox::MasterLoveSlave(const xBase* slave, S32 starting)
{
    switch (slave->baseType)
    {
    case eBaseTypeGroup:
    {
        xGroup* grp = (xGroup*)slave;
        U32 cnt = xGroupGetCount(grp);

        for (U32 i = 0; i < cnt; i++)
        {
            xBase* grpitem = xGroupGetItemPtr(grp, i);
            if (grpitem)
            {
                MasterLoveSlave(grpitem, starting);
            }
        }
        break;
    }
    case eBaseTypeNPC:
        return;
    }
}

void ztalkbox::load_settings(xIniFile& ini)
{
    shared.volume = xIniGetFloat(&ini, "talk_box.volume", 1.0f);
}

void ztalkbox::init()
{
    xtextbox::register_tags(new_tags, new_tags_size);

    static start_state_type start_state;
    shared.states[STATE_START] = &start_state;

    static next_state_type next_state;
    shared.states[STATE_NEXT] = &next_state;

    static wait_state_type wait_state;
    shared.states[STATE_WAIT] = &wait_state;

    static stop_state_type stop_state;
    shared.states[STATE_STOP] = &stop_state;

    reset_all();
}

void ztalkbox::load(xBase& data, xDynAsset& asset, size_t)
{
    ((ztalkbox&)data).load((const asset_type&)asset);
}

void ztalkbox::update_all(xScene& s, F32 dt)
{
    if (zGameIsPaused())
    {
        return;
    }

    while (shared.state)
    {
        shared.delay_events = true;

        state_enum newtype = shared.state->update(s, dt);
        if (newtype == shared.state->type)
        {
            break;
        }

        shared.state->stop();

        if (newtype == STATE_INVALID)
        {
            stop();
            break;
        }

        shared.state = shared.states[newtype];
        shared.state->start();
        shared.delay_events = false;
        flush_triggered();
    }

    if (shared.state && shared.active)
    {
        trigger_pads_enum tp = (trigger_pads_enum)shared.active->triggerPads;

        if ((tp == TP_ACTIVE && !globals.player.ControlOff) ||
            (tp == TP_TRAPPED && globals.player.ControlOff) || tp == TP_FORCED_ACTIVE)
        {
            trigger_pads(*pad_pressed());
        }
    }

    shared.delay_events = false;
    flush_triggered();
}

void ztalkbox::render_all()
{
    if (!(shared.permit & 1))
    {
        return;
    }

    if (!shared.state)
    {
        return;
    }

    if (!shared.active->flag.visible)
    {
        return;
    }

    ztextbox& d = *shared.active->dialog_box;

    if (d.flag.active)
    {
        d.deactivate();
    }

    if (d.flag.skipFrame)
    {
        d.flag.skipFrame = false;
        return;
    }

    if (d.flag.show_backdrop)
    {
        d.render_backdrop();
    }

    d.tb.render(shared.lt, shared.begin_jot, shared.end_jot);
}

void ztalkbox::reset_all()
{
    shared.flags = 0;
    shared.permit = 0xffffffff;
    shared.active = NULL;
    shared.state = NULL;
    shared.cb = NULL;
    shared.delay_events = false;
    shared.triggered.reset();
    shared.lt.clear();
    shared.speak_npc = NULL;
    shared.speak_player = 0;
}

ztalkbox* ztalkbox::get_active()
{
    return shared.active;
}

void ztalkbox::permit(U32 add_flags, U32 remove_flags)
{
    shared.permit &= ~remove_flags;
    shared.permit |= add_flags;
}
