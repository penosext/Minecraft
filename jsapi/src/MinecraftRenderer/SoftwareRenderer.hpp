#pragma once

#include <cstdint>

// ARGB32 像素（uint32 顺序 A<<24 | R<<16 | G<<8 | B，小端内存为 BGRA）
static inline uint32_t ARGB(uint8_t a, uint8_t r, uint8_t g, uint8_t b)
{
    return (uint32_t(a) << 24) | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
}

// argb 必须容纳 w*h 个像素；相机角度单位为度。
void renderMinecraftFrame(uint32_t* argb, int w, int h, int frame,
                          float yaw = 45.0f, float pitch = 30.0f, int blockIndex = 0);
