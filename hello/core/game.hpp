// Shared, platform-free game core: logic + software rendering into an RGBA buffer.
#pragma once
#include <cstdint>
struct Framebuffer { uint32_t *px; int w, h, stride; }; // stride in pixels, ABGR (RGBA bytes)
struct Game {
    double t = 0, bx = 100, by = 100, vx = 220, vy = 170;
    void tap(float x, float y) { bx = x; by = y; }
    void update(double dt, int w, int h);
    void render(Framebuffer &fb) const;
};
