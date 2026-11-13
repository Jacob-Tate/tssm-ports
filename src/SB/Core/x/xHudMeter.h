#ifndef XHUDMETER_H
#define XHUDMETER_H

#include "xHud.h"
#include "xSnd.h"
#include "xHudModel.h"
#include "zUI.h"
#include "xHudText.h"
#include "zHud.h"
#include "iModel.h"

#include <stdio.h>

struct ztextbox;

namespace xhud
{

    struct meter_asset : asset
    {
        F32 start_value;
        F32 min_value;
        F32 max_value;
        F32 increment_time;
        F32 decrement_time;
        struct _class_0
        {
            U32 start_increment;
            U32 increment;
            U32 start_decrement;
            U32 decrement;
        } sound;

        static const char* type_name()
        {
            return "hud:meter";
        }
    };

    // Layout from the PS2 DWARF (differs from BfBB: inc_value/inc_accel, no pitch).
    struct meter_widget : widget
    {
        meter_asset& res;
        F32 value;
        F32 min_value;
        F32 max_value;
        F32 inc_value;
        F32 end_value;
        F32 inc_accel;
        F32 ping_delay;
        sound_queue<4> pings;

        meter_widget(const meter_asset& a);
        // Never called (dead-stripped, like widget::~widget()), but its out-of-line copy places the
        // weak meter_widget::update where retail has it, right after unit_meter_widget::load.
        ~meter_widget()
        {
        }
        void updater(F32 dt);
        void set_value_immediate(F32 v);
        void set_value(F32 v);

        virtual U32 type() const;
        virtual bool is(U32 id) const;
        virtual void update(F32 dt)
        {
            updater(dt);
        }
    };

    struct font_context
    {
        U32 id;
        S32 justify;
        F32 w;
        F32 h;
        F32 space;
        F32 drop_x;
        F32 drop_y;
        xColor_tag c;
        xColor_tag drop_c;
    };

    struct font_meter_asset : meter_asset
    {
        font_context font;

        static const char* type_name()
        {
            return "hud:meter:font";
        }
    };

    struct font_meter_widget : meter_widget
    {
        font_context font;
        font_context start_font;
        S32 precision;
        char buffer[12];
        S32 last_value;
        xfont xf;

        font_meter_widget(const font_meter_asset& a);
        static void load(xBase& data, xDynAsset& asset, size_t);

        virtual void destroy();
        virtual U32 type() const;
        virtual bool is(U32 id) const;
        virtual void update(F32 dt);
        virtual void render();
    };

    struct model_info
    {
        U32 id;
        xVec3 loc;
        xVec3 size;
    };

    struct unit_meter_asset : meter_asset
    {
        model_info model[2];
        xVec3 offset;
        U32 fill_forward;

        static const char* type_name()
        {
            return "hud:meter:unit";
        }
    };

    struct unit_meter_widget : meter_widget
    {
        unit_meter_asset res;
        // Indexed [unit][which]. The PS2 DWARF dump shows [2][6], but the 8-byte unit stride
        // in unit_meter_widget::update and KrabbyPattyMeter::setup proves [6][2].
        xModelInstance* model[6][2];
        F32 anim_time;

        unit_meter_widget(const unit_meter_asset& a);
        static void load(xBase& data, xDynAsset& asset, size_t);

        virtual void destroy();
        virtual U32 type() const;
        virtual bool is(U32 id) const;
        virtual void setup();
        virtual void update(F32 dt);
        virtual void render();
    };

} // namespace xhud

// TSSM HUD elements live in namespace zHud (see the zHud:: symbols). Virtual order per the
// __vt__Q24zHud* tables; CW puts hud_element's vtable pointer at its first virtual (0x14, after
// _next), as zPlayerSB::Reset's virtual calls through the embedded meters show.
namespace zHud
{
    // zHud, not xhud: the PS2 symbol of TextureScroller::setup mangles it as Q24zHud10eDirection.
    enum eDirection
    {
        eDIRECTION_X,
        eDIRECTION_Y
    };

    struct TextureScroller
    {
        xhud::model_widget* m_pModelWidget;
        U16* m_aMoveIndices;
        U16 m_moveCount;
        eDirection m_dir : 8;
        U8 m_pad1;
        F32 m_prevOffset;
        F32 m_finalOffset;
        F32 m_initialOffset;

        TextureScroller();
        void setup(xhud::model_widget* pModelWidget, RwTexCoords low, RwTexCoords high,
                   F32 initialOffset, F32 finalOffset, eDirection dir);
        void set(F32 moveOffset);

        // No symbol on GC (always inlined into set and setup).
        void shiftUVs(F32 shiftAmount)
        {
            if (!(shiftAmount >= -0.00001f && shiftAmount <= 0.00001f))
            {
                S32 numVerts;
                RwTexCoords* pTex =
                    iModelLockAndGetTextureCoords(m_pModelWidget->model->Data, &numVerts);

                if (m_dir == eDIRECTION_X)
                {
                    for (U32 i = 0; i < m_moveCount; i++)
                    {
                        pTex[m_aMoveIndices[i]].u += shiftAmount;
                    }
                }
                else
                {
                    for (U32 i = 0; i < m_moveCount; i++)
                    {
                        pTex[m_aMoveIndices[i]].v += shiftAmount;
                    }
                }

                iModelUnlock(m_pModelWidget->model->Data);
            }
        }
    };

    struct ModelAndFont : hud_element
    {
        xhud::model_widget* modelWidget;
        xhud::font_meter_widget* fontMeter;
        U32* valueToWatch;
        U32 lastValue;
        U8 okToAutoHide;

        ModelAndFont()
        {
            modelWidget = NULL;
            fontMeter = NULL;
            valueToWatch = NULL;
            lastValue = 0;
            okToAutoHide = TRUE;
        }

        void setup();
        void update(F32 dt);
        void show(bool autohide);
        void hide();
        virtual void setup(const char* modelName, const char* fontName);
        virtual void reset();
        virtual void enable();
        virtual void disable();
    };

    struct ModelAndFontHideOnTextBox : ModelAndFont
    {
        U8 delayedShow;
        U8 hidden;

        static const char* includedTextBoxNames[7];
        static ztextbox* includedTextBoxes[7];

        void update(F32 dt);
        void show(bool autohide);
        void setup(const char* modelName, const char* fontName);
        // static: zUICustomUpgradeTutorialContoller::PreUpdate calls it without a this pointer.
        static U8 IsIncludedTextboxActive();
    };

    template <class T> struct BarMeter : hud_element
    {
        xhud::model_widget* bar;
        xhud::model_widget* barNotch;
        xhud::font_meter_widget* barMeter;
        F32 sizeOfActualBar;
        xVec3 notchLocation;
        xVec3 fontLocation;
        xhud::text_widget* minValueText;
        xhud::text_widget* maxValueText;
        TextureScroller* textureScroller;
        xhud::widget* barPartsWidgets[5];
        xLightKit* hudLightKit;
        T defaultMinValue;
        T defaultMaxValue;
        T* valueToWatch;
        T* minValueToWatch;
        T* maxValueToWatch;
        T lastValue;
        U8 firstTimeUpdate;

        BarMeter()
        {
            bar = NULL;
            barNotch = NULL;
            sizeOfActualBar = 0.0f;
            maxValueText = NULL;
            minValueText = NULL;
            lastValue = 0;
            valueToWatch = NULL;
            minValueToWatch = NULL;
            maxValueToWatch = NULL;
            hudLightKit = NULL;
            firstTimeUpdate = 1;
            defaultMinValue = 0;
            defaultMaxValue = 100;
            minValueToWatch = &defaultMinValue;
            maxValueToWatch = &defaultMaxValue;
        }

        void setup()
        {
        }
        // No symbol on GC (always inlined); the name is ours. ManlinessMeter::setup reads its bar
        // parts through it, as retail's register use needs.
        xhud::widget* GetBarPart(S32 index)
        {
            return barPartsWidgets[index];
        }
        // No symbol on GC (always inlined); the names are ours. The Xbox build keeps them out of
        // line as two members of the meter, one per text widget, called at the end of update. The
        // U32 versions are specializations (below the class).
        void SetMinValueText(T value)
        {
            if (minValueText != NULL)
            {
                char buffer[256];
                sprintf(buffer, "{c=ffffe600}%d", value);
                minValueText->set_text(buffer);
            }
        }
        void SetMaxValueText(T value)
        {
            if (maxValueText != NULL)
            {
                char buffer[256];
                sprintf(buffer, "{c=ffffe600}%d", value);
                maxValueText->set_text(buffer);
            }
        }
        // Defined in the class (as in the PS2 line table) and inlined into ManlinessMeter::update.
        // It is right at CW's inline size limit: writing the pointer tests as "!= NULL" makes it
        // too big.
        void update(F32 dt)
        {
            T range;
            T diff;
            F32 notchStep;

            if (valueToWatch && minValueToWatch && maxValueToWatch && textureScroller &&
                (lastValue != *valueToWatch || firstTimeUpdate))
            {
                firstTimeUpdate = 0;
                lastValue = *valueToWatch;

                range = *maxValueToWatch - *minValueToWatch;
                diff = *valueToWatch - *minValueToWatch;

                textureScroller->set((F32)diff / (F32)range);

                if (barNotch != NULL)
                {
                    notchStep = NSCREENX(diff * (sizeOfActualBar / range));
                    barNotch->rc.loc.x = notchLocation.x + notchStep;
                    barNotch->start_rc.loc.x = barNotch->rc.loc.x;
                }

                // notchStep is only set when there is a notch widget (as in the original).
                if (barMeter != NULL)
                {
                    barMeter->set_value(*valueToWatch);
                    barMeter->rc.loc.x = fontLocation.x + notchStep;
                    barMeter->start_rc.loc.x = barMeter->rc.loc.x;
                }

                SetMinValueText(*minValueToWatch);
                SetMaxValueText(*maxValueToWatch);
            }
        }
        virtual void reset()
        {
            for (S32 i = 0; i < 5; i++)
            {
                if (barPartsWidgets[i])
                {
                    barPartsWidgets[i]->clear_motives();
                }
            }
        }
        void hide()
        {
            for (S32 i = 0; i < 5; i++)
            {
                if (barPartsWidgets[i])
                {
                    barPartsWidgets[i]->hide(xhud::widget::BOTTOM, true, true);
                }
            }
        }
        void show(bool autohide)
        {
            for (S32 i = 0; i < 5; i++)
            {
                if (barPartsWidgets[i])
                {
                    barPartsWidgets[i]->show(xhud::widget::BOTTOM, true, true);
                }
            }
        }
    };

    template <> inline void BarMeter<U32>::SetMinValueText(U32 value)
    {
        if (minValueText != NULL)
        {
            char buffer[256];
            sprintf(buffer, "{c=ffffe600}%u", value);
            minValueText->set_text(buffer);
        }
    }

    template <> inline void BarMeter<U32>::SetMaxValueText(U32 value)
    {
        if (maxValueText != NULL)
        {
            char buffer[256];
            sprintf(buffer, "{c=ffffe600}%u", value);
            maxValueText->set_text(buffer);
        }
    }

    struct Combo
    {
        char comboText[64];
        bool (*callback)(xhud::widget&, xhud::motive&, F32);
    };

    // Animation timers and state shared by the ManlinessMeter combo text effects. Not in the
    // PS2 DWARF; names and sizes from the retail symbols (Time: 0x40 bytes, one per combo).
    struct ComboGlobals
    {
        static F32 Time[16];
        static U32 Combo15Count;
    };

    struct ManlinessMeter : BarMeter<U32>
    {
        xhud::model_widget* upgradeIcon;
        xhud::text_widget* manlinessPointsComboText;
        xhud::text_widget* manlinessPointsComboPointsText;
        U32 lastManlinessPoints;
        U32 minManlinessPoints;
        U32 maxManlinessPoints;
        U32 lastMaxManlinessPoints;
        U32 lastUnusedSkillPoints;
        U8 firstTimeUpdate;
        char nextUpgrade[16];
        xhud::widget* widgets[8];
        TextureScroller manlinessTextureScroller;
        iSndHandle upgradeSound;
        iSndGroupHandle upgradeSoundGroupHandle;

        static Combo combos[16];
        static U32 comboIndex;
        static U32 lastComboIndex;

        // Member functions follow the original source order (the PS2 DWARF lists them
        // reversed); reset is the key function that emits the vtable.
        ManlinessMeter();
        void reset();
        void setup();
        void SetImmediateValue(U32 value);
        void update(F32 dt);
        void show(bool autohide);
        void hide();
        static void ClearText(xhud::widget& w, xhud::motive&);
        static void HideAndClearText(xhud::widget& w, xhud::motive&);
        static bool Combo3Effect(xhud::widget& w, xhud::motive& m, F32 dt);
        static bool Combo4Effect(xhud::widget& w, xhud::motive& m, F32 dt);
        static bool Combo5Effect(xhud::widget& w, xhud::motive& m, F32 dt);
        static bool Combo6Effect(xhud::widget& w, xhud::motive& m, F32 dt);
        static bool Combo7Effect(xhud::widget& w, xhud::motive& m, F32 dt);
        static bool Combo8Effect(xhud::widget& w, xhud::motive& m, F32 dt);
        static bool Combo9Effect(xhud::widget& w, xhud::motive& m, F32 dt);
        static bool Combo10Effect(xhud::widget& w, xhud::motive& m, F32 dt);
        static bool Combo11Effect(xhud::widget& w, xhud::motive& m, F32 dt);
        static bool Combo12Effect(xhud::widget& w, xhud::motive& m, F32 dt);
        static bool Combo13Effect(xhud::widget& w, xhud::motive& m, F32 dt);
        static bool Combo14Effect(xhud::widget& w, xhud::motive& m, F32 dt);
        static bool Combo15Effect(xhud::widget& w, xhud::motive& m, F32 dt);
    };

    struct KrabbyPattyMeter : hud_element
    {
        xhud::unit_meter_widget* krabbyPattyMeter;
        U32 lastHealth;
        U32 lastMaxHealth;
        U8 firstTime;

        // reset is the key function that emits the vtable.
        KrabbyPattyMeter();
        virtual void reset();
        void setup();
        void update(F32 dt);
        void show(bool autohide);
        void hide();
    };

    struct TreasureChest : ModelAndFontHideOnTextBox
    {
        TreasureChest();
        void setup();
    };

    struct GoofyGooberMeter : ModelAndFontHideOnTextBox
    {
        zUIText* autoSave;

        GoofyGooberMeter();
        void setup();
        void update(F32 dt);
        void show(bool autohide);
    };

    struct NitroMeter : ModelAndFontHideOnTextBox
    {
        NitroMeter();
        void setup();
        void update(F32 dt);
        void show(bool autohide);
    };

} // namespace zHud

#endif
