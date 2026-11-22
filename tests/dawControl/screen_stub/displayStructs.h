#pragma once
#include <cstdint>
struct strFont
{
    uint8_t data[256];
    int16_t handle;
    uint16_t width;
    uint16_t height;
};
extern strFont fonts[];
