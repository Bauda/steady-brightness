#pragma once
#include "common.h"
constexpr wchar_t BrightnessSliderClass[]=L"SteadyBrightness.Slider.1";
struct SliderPalette { COLORREF background,track,accent,thumb,border,disabled; };
using SliderPaletteCallback=SliderPalette (*)();
bool RegisterBrightnessSlider(HINSTANCE instance,SliderPaletteCallback palette);
HWND CreateBrightnessSlider(HINSTANCE instance,HWND parent,UINT id,const wchar_t* name);
bool BrightnessSliderDragging(HWND window);
void CancelBrightnessSliderDrag(HWND window);
