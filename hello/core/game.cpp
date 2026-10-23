#include "game.hpp"
#include <cmath>
static inline uint32_t rgba(int r, int g, int b) { return 0xff000000u | (b << 16) | (g << 8) | r; }
void Game::update(double dt, int w, int h) {
    t += dt; bx += vx * dt; by += vy * dt;
    if (bx < 30 || bx > w - 30) vx = -vx;
    if (by < 30 || by > h - 30) vy = -vy;
}
void Game::render(Framebuffer &fb) const {
    for (int y = 0; y < fb.h; ++y) {
        uint32_t *row = fb.px + y * fb.stride;
        for (int x = 0; x < fb.w; ++x) {
            int c = ((x / 32 + y / 32) & 1) ? 40 : 60;
            double dx = x - bx, dy = y - by;
            row[x] = (dx * dx + dy * dy < 900) ? rgba(255, 180 + int(60 * std::sin(t)), 0) : rgba(c, c, c + 30);
        }
    }
}
