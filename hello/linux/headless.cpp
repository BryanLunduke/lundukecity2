// Host-side check: same core, render 60 frames, write a PPM of the last one.
#include "../core/game.hpp"
#include <cstdio>
#include <vector>
int main() {
    const int W = 320, H = 240; std::vector<uint32_t> buf(W * H);
    Framebuffer fb{buf.data(), W, H, W}; Game g;
    for (int i = 0; i < 60; ++i) { g.update(1.0 / 60, W, H); g.render(fb); }
    FILE *f = std::fopen("frame.ppm", "wb"); std::fprintf(f, "P6 %d %d 255\n", W, H);
    for (uint32_t p : buf) { unsigned char c[3] = {(unsigned char)p, (unsigned char)(p >> 8), (unsigned char)(p >> 16)}; std::fwrite(c, 1, 3, f); }
    std::fclose(f); std::printf("ok ball=(%.1f,%.1f)\n", g.bx, g.by); return 0;
}
