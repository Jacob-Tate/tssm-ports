#include "xMath2.h"

// g_O2, g_X2 and g_Y2 (const xVec2, PS2 DWARF of xWad3) are unreferenced and absent from the
// GameCube binary (dead-stripped), so they are not defined here.

const xMat3x2 g_I2 = { { 1.0f, 0.0f }, { 0.0f, 1.0f }, { 0.0f, 0.0f } };

template <> const basic_rect<F32> basic_rect<F32>::m_Null = { 0.0f, 0.0f, 0.0f, 0.0f };
template <> const basic_rect<F32> basic_rect<F32>::m_Unit = { 0.0f, 0.0f, 1.0f, 1.0f };
