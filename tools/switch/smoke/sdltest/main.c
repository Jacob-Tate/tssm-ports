#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_opengl.h>
int main(int argc, char** argv) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) return 1;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_Window* w = SDL_CreateWindow("tssm", 1280, 720, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    SDL_GLContext c = w ? SDL_GL_CreateContext(w) : NULL;
    SDL_Log("video=%s audio=%s gl=%p", SDL_GetCurrentVideoDriver(), SDL_GetCurrentAudioDriver(), (void*)c);
    typedef void (*clr_t)(float,float,float,float); typedef void (*cl_t)(unsigned);
    clr_t cc = (clr_t)SDL_GL_GetProcAddress("glClearColor"); cl_t cl = (cl_t)SDL_GL_GetProcAddress("glClear");
    for (int run = 1; run;) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) run = 0;
            if (e.type == SDL_EVENT_GAMEPAD_ADDED) SDL_OpenGamepad(e.gdevice.which);
            if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && e.gbutton.button == SDL_GAMEPAD_BUTTON_START) run = 0;
        }
        if (c && cc && cl) { cc(0.9f, 0.8f, 0.1f, 1.f); cl(0x4000); SDL_GL_SwapWindow(w); }
    }
    SDL_Quit();
    return 0;
}
