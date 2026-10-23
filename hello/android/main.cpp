// Thin Android shell: NativeActivity (no Java), software-blits the shared core into ANativeWindow.
#include "../core/game.hpp"
#include <android/native_activity.h>
#include <android/native_window.h>
#include <android/log.h>
#include <atomic>
#include <chrono>
#include <thread>
static Game g_game; static std::atomic<ANativeWindow *> g_win{nullptr}; static std::atomic<bool> g_run{false}; static std::thread g_thr;
static void loop() {
    auto last = std::chrono::steady_clock::now();
    while (g_run) {
        ANativeWindow *w = g_win.load(); if (!w) { std::this_thread::sleep_for(std::chrono::milliseconds(16)); continue; }
        ANativeWindow_Buffer b;
        if (ANativeWindow_lock(w, &b, nullptr) == 0) {
            auto now = std::chrono::steady_clock::now(); double dt = std::chrono::duration<double>(now - last).count(); last = now;
            g_game.update(dt, b.width, b.height);
            Framebuffer fb{static_cast<uint32_t *>(b.bits), b.width, b.height, b.stride}; g_game.render(fb);
            ANativeWindow_unlockAndPost(w);
        }
    }
}
static void onWindowCreated(ANativeActivity *, ANativeWindow *w) {
    ANativeWindow_setBuffersGeometry(w, 0, 0, WINDOW_FORMAT_RGBA_8888); g_win = w;
    if (!g_run) { g_run = true; g_thr = std::thread(loop); }
}
static void onWindowDestroyed(ANativeActivity *, ANativeWindow *) { g_win = nullptr; }
static void onDestroy(ANativeActivity *) { g_run = false; if (g_thr.joinable()) g_thr.join(); }
extern "C" JNIEXPORT void ANativeActivity_onCreate(ANativeActivity *a, void *, size_t) {
    __android_log_print(ANDROID_LOG_INFO, "xplat", "hello from shared core");
    a->callbacks->onNativeWindowCreated = onWindowCreated;
    a->callbacks->onNativeWindowDestroyed = onWindowDestroyed;
    a->callbacks->onDestroy = onDestroy;
}
