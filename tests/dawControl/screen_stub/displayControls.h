#pragma once
// Host stub of the display control base (only what dawScreen.cpp uses).
#include <cstdint>
#include "displayStructs.h"
typedef enum { controlIdDawScreen = 100, controlIdQtrlScreen = 101, controlIdQ4Screen = 102, controlIdSotiScreen = 103 } control_id_t;
enum { controlStyleShow = 1 };
struct strControlProperties
{
    const char* text = nullptr;
    const uint32_t* colors = nullptr;
    uint32_t style = 0;
    uint16_t x = 0, y = 0, w = 0, h = 0;
    int value = 0;
    void* data = nullptr;
};
class cDisplayControl
{
  public:
    cDisplayControl(control_id_t, uint8_t) {}
    virtual ~cDisplayControl() {}
    virtual void update() = 0;
    virtual void setStyle(uint32_t) = 0;
    virtual void setText(const char*) = 0;
    virtual void setValue(int) = 0;
    virtual void setColors(const uint32_t*) = 0;
    virtual void setData(void*) = 0;
  protected:
    uint32_t style = 0;
    const char* text = nullptr;
    int value = 0;
    uint8_t colorsCount = 0;
    const uint32_t* colors = nullptr;
};
