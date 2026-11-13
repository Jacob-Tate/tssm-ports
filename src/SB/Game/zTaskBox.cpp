#include "zTaskBox.h"

#include "xEvent.h"
#include "xGroup.h"
#include "xTextAsset.h"
#include "xstransvc.h"

#include "zBase.h"
#include "zScene.h"
#include "zTalkBox.h"

#include <types.h>

ztaskbox::talk_callback* ztaskbox::tcb;

namespace
{
    struct
    {
        ztaskbox* active;
    } shared;
} // namespace

struct ztaskbox::talk_callback : ztalkbox::callback
{
    ztaskbox* task;
    ztalkbox::answer_enum answer;

    void reset(ztaskbox& task)
    {
        this->task = &task;
        this->answer = ztalkbox::ANSWER_CONTINUE;
    }

    void on_start()
    {
        task->on_talk_start();
    }

    void on_stop()
    {
        task->on_talk_stop(answer);
    }

    void on_answer(ztalkbox::answer_enum answer)
    {
        this->answer = answer;
    }
};

void ztaskbox::load(const asset_type& a)
{
    xBaseInit(this, &a);
    this->baseType = eBaseTypeTaskBox;
    this->asset = &a;
    this->eventFunc = cb_dispatch;
    if (this->linkCount != 0)
    {
        this->link = (xLinkAsset*)(&a + 1);
    }

    this->state = STATE_INVALID;
    if (a.enable)
    {
        this->flag.enabled = true;
        this->set_state(STATE_BEGIN);
        this->current = this;
    }
    else
    {
        this->flag.enabled = false;
        this->current = NULL;
    }

    this->flag.running = false;

    if (a.persistent)
    {
        this->baseFlags |= 2;
    }
}

void ztaskbox::read(xSerial& s)
{
    U8 state = this->state;
    s.Read(&state);
    set_state((state_enum)state);
}

void ztaskbox::write(xSerial& s)
{
    s.Write((U8)this->state);
}

void ztaskbox::start_talk(zNPC::base* npc)
{
    if (this->current == NULL)
    {
        return;
    }

    if (this->current != this)
    {
        this->current->set_callback(this->cb);
        this->current->start_talk(NULL);
        return;
    }

    if (!this->flag.enabled || this->state == STATE_INVALID)
    {
        return;
    }

    if (shared.active != NULL && shared.active != this)
    {
        shared.active->stop_talk();
    }

    ztalkbox* talk = (ztalkbox*)zSceneFindObject(this->asset->talk_box);
    if (talk == NULL)
    {
        return;
    }

    const char* text = get_text(this->asset->stages[this->state]);
    if (text == NULL)
    {
        return;
    }

    shared.active = this;
    tcb->reset(*this);
    talk->start_talk(text, tcb, npc);
}

void ztaskbox::stop_talk()
{
    if (this->current == NULL)
    {
        return;
    }

    if (this->current != this)
    {
        this->current->stop_talk();
        return;
    }

    if (this->flag.enabled == false || this->state == STATE_INVALID)
    {
        return;
    }

    if (shared.active != this)
    {
        return;
    }

    ztalkbox* talk = (ztalkbox*)zSceneFindObject(this->asset->talk_box);
    if (talk == NULL)
    {
        return;
    }

    talk->stop_talk();
    shared.active = NULL;
}

void ztaskbox::enable()
{
    if (!this->flag.enabled)
    {
        this->flag.enabled = true;
        set_state(STATE_BEGIN);
    }
}

void ztaskbox::disable()
{
    if (this->flag.enabled)
    {
        stop_talk();
        this->flag.enabled = false;
        set_state(STATE_INVALID);
    }
}

void ztaskbox::reset()
{
    disable();
    if (this->asset->enable)
    {
        enable();
    }
}

void ztaskbox::initiate()
{
    if (this->state == STATE_BEGIN)
    {
        set_state(STATE_DESCRIPTION);
    }
}

void ztaskbox::succeed()
{
    if (state == STATE_BEGIN || state == STATE_DESCRIPTION || state == STATE_REMINDER)
    {
        set_state(STATE_SUCCESS);
    }
}

void ztaskbox::fail()
{
    if (state == STATE_BEGIN || state == STATE_DESCRIPTION || state == STATE_REMINDER)
    {
        set_state(STATE_FAILURE);
    }
}

void ztaskbox::complete()
{
    if (this->flag.enabled)
    {
        this->state = STATE_INVALID;
        this->flag.enabled = false;
        zEntEvent(this, 0, this, eEventTaskBox_OnComplete, NULL, NULL, 0, FE_NO);
        this->current = (ztaskbox*)zSceneFindObject(this->asset->next_task);
        if (this->current != NULL)
        {
            return;
        }
    }
}

void ztaskbox::set_callback(callback* cb)
{
    this->cb = cb;
}

void ztaskbox::init()
{
    shared.active = NULL;

    static talk_callback tcb;
    ztaskbox::tcb = &tcb;
}

void ztaskbox::load(xBase& data, xDynAsset& asset, size_t num)
{
    ((ztaskbox&)data).load((asset_type&)asset);
}

bool ztaskbox::exists(state_enum state)
{
    U32 id = this->asset->stages[state];
    return id != 0 && xSTFindAsset(id, NULL) != NULL;
}

void ztaskbox::set_state(state_enum state)
{
    this->state = state;
    this->current = this;

    switch (state)
    {
    case STATE_BEGIN:
        if (!exists(state))
        {
            set_state(STATE_DESCRIPTION);
        }
        break;
    case STATE_DESCRIPTION:
        if (!exists(state))
        {
            set_state(STATE_REMINDER);
        }
        break;
    case STATE_REMINDER:
    case STATE_SUCCESS:
    case STATE_FAILURE:
        if (!exists(state))
        {
            set_state(STATE_END);
        }
        break;
    case STATE_END:
        if (!exists(state))
        {
            set_state(STATE_INVALID);
        }
        break;
    default:
        complete();
        break;
    }
}

U8 ztaskbox::is_running() const
{
    if (shared.active != NULL)
    {
        return shared.active->flag.running;
    }
    return false;
}

void ztaskbox::on_talk_stop(ztalkbox::answer_enum answer)
{
    this->flag.running = false;

    switch (this->state)
    {
    case STATE_DESCRIPTION:
        set_state(STATE_REMINDER);
        break;
    case STATE_SUCCESS:
        set_state(STATE_END);
        break;
    case STATE_FAILURE:
        if (this->asset->retry)
        {
            set_state(STATE_DESCRIPTION);
        }
        else
        {
            set_state(STATE_END);
        }
        break;
    case STATE_BEGIN:
    case STATE_REMINDER:
    case STATE_END:
        break;
    default:
        if (this->asset->loop)
        {
            set_state(STATE_BEGIN);
        }
        break;
    }

    if (this->cb != NULL)
    {
        this->cb->on_talk_stop();
    }
}

const char* ztaskbox::get_text(U32 id)
{
    xBase* b = zSceneFindObject(id);
    if (b != NULL)
    {
        if (b->baseType != eBaseTypeGroup)
        {
            return NULL;
        }
        id = ((xGroup*)b)->get_any();
    }

    if (id == 0)
    {
        return NULL;
    }

    const char* text = xTextFindString(id, NULL);
    if (text == NULL)
    {
        return NULL;
    }
    return text;
}

void ztaskbox::cb_dispatch(xBase*, xBase* to, U32 event, const F32*, xBase*, U32)
{
    ztaskbox& e = *(ztaskbox*)to;

    switch (event)
    {
    case eEventReset:
        e.reset();
        break;
    case eEventStartConversation:
        e.start_talk(NULL);
        break;
    case eEventEndConversation:
        e.stop_talk();
        break;
    case eEventTaskBox_Initiate:
        e.initiate();
        break;
    case eEventTaskBox_SetSuccess:
        e.succeed();
        break;
    case eEventTaskBox_SetFailure:
        e.fail();
        break;
    case eEventTaskBox_OnAccept:
    case eEventTaskBox_OnDecline:
    case eEventTaskBox_OnComplete:
        break;
    }
}
