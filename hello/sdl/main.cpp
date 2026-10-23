// SDL3 shell for the same core: window + streaming texture + gamepad/touch/mouse input.
// XPLAT_FRAMES=N renders N frames then writes frame.bmp and exits (headless test mode).
#include "../core/game.hpp"
#include <SDL3/SDL.h>
#include <cstdlib>
#include <vector>
int main(int, char **) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) { SDL_Log("init: %s", SDL_GetError()); return 1; }
    const int W = 1280, H = 800; // Steam Deck native resolution
    SDL_Window *win; SDL_Renderer *ren;
    if (!SDL_CreateWindowAndRenderer("XPlat Hello", W, H, SDL_WINDOW_RESIZABLE, &win, &ren)) { SDL_Log("win: %s", SDL_GetError()); return 1; }
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, W, H);
    std::vector<uint32_t> buf(W * H); Framebuffer fb{buf.data(), W, H, W}; Game g;
    const char *fe = std::getenv("XPLAT_FRAMES"); int max_frames = fe ? std::atoi(fe) : -1;
    SDL_Log("SDL %d.%d.%d video=%s gamepads=%s", SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_MICRO_VERSION,
            SDL_GetCurrentVideoDriver(), SDL_HasGamepad() ? "yes" : "no");
    Uint64 last = SDL_GetTicksNS(); bool run = true;
    for (int frame = 0; run; ++frame) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) run = false;
            if (e.type == SDL_EVENT_GAMEPAD_ADDED) SDL_OpenGamepad(e.gdevice.which);
            if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) g.tap(e.button.x, e.button.y);
            if (e.type == SDL_EVENT_FINGER_DOWN) g.tap(e.tfinger.x * W, e.tfinger.y * H);
            if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && e.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH) g.tap(W / 2.f, H / 2.f);
        }
        Uint64 now = SDL_GetTicksNS(); g.update((now - last) / 1e9, W, H); last = now;
        g.render(fb);
        SDL_UpdateTexture(tex, nullptr, buf.data(), W * 4);
        SDL_RenderClear(ren); SDL_RenderTexture(ren, tex, nullptr, nullptr); SDL_RenderPresent(ren);
        if (max_frames > 0 && frame + 1 >= max_frames) {
            SDL_Surface *s = SDL_RenderReadPixels(ren, nullptr);
            if (s) { SDL_SaveBMP(s, "frame.bmp"); SDL_DestroySurface(s); SDL_Log("wrote frame.bmp"); }
            run = false;
        }
    }
    SDL_Quit(); return 0;
}
