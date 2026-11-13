#ifndef ZTALKBOX_H
#define ZTALKBOX_H

#include "xDynAsset.h"
#include "xIni.h"
#include "xScene.h"
#include "zNMECommon.h"
#include "zNPC.h"
#include "zTextBox.h"

struct xEntNPCAssetIN : xEntNPCAsset
{
    U32 navigation_mesh_id;
    U32 settings;
};

struct ztalkbox : xBase
{
    struct asset_type : xDynAsset
    {
        U32 dialog_box;
        U32 prompt_box;
        U32 quit_box;
        U8 trap : 8;
        U8 pause : 8;
        U8 allow_quit : 8;
        U8 trigger_pads : 8;
        U8 page : 8;
        U8 show : 8;
        U8 hide : 8;
        U8 audio_effect : 8;
        U32 teleport;
        struct
        {
            struct
            {
                U8 time : 8;
                U8 prompt : 8;
                U8 sound : 8;
                U8 event : 8;
            } type;
            F32 delay;
            S32 which_event;
        } auto_wait;
        struct
        {
            U32 skip;
            U32 noskip;
            U32 quit;
            U32 noquit;
            U32 yesno;
        } prompt;
    };

    enum answer_enum
    {
        ANSWER_CONTINUE,
        ANSWER_YES,
        ANSWER_NO
    };

    struct callback
    {
        callback()
        {
        }

        virtual void on_signal(U32)
        {
        }

        virtual void on_start()
        {
        }

        virtual void on_stop()
        {
        }

        virtual void on_answer(answer_enum answer)
        {
        }
    };

    struct
    {
        U8 visible : 1;
    } flag;
    const asset_type* asset;
    ztextbox* dialog_box;
    ztextbox* prompt_box;
    ztextbox* quit_box;
    struct
    {
        const char* skip;
        const char* noskip;
        const char* quit;
        const char* noquit;
        const char* yesno;
    } prompt;
    U32 triggerPads;
    zNPC::base* npc;

    static void init();
    static void load(xBase& data, xDynAsset& asset, size_t);
    static void load_settings(xIniFile& ini);
    static void update_all(xScene& s, F32 dt);
    static void render_all();
    static void reset_all();
    static void permit(U32 add_flags, U32 remove_flags);
    static ztalkbox* get_active();

    void load(const asset_type& a);
    void reset();
    void set_text(const char* s);
    void set_text(U32 id);
    void add_text(const char* s);
    void add_text(U32 id);
    void clear_text();
    void start_talk(const char* s, callback* cb, zNPC::base* npc);
    void start_talk(U32 text_id, callback* cb, zNPC::base* npc);
    void stop_talk();
    void stop_wait(U32 x);
    void show();
    void hide();
    void MasterTellSlaves(S32 isBeginning);
    void MasterLoveSlave(const xBase* slave, S32 starting);
};

#endif
