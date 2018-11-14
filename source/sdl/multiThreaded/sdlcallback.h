#ifndef __SDL_CALLBACK_H__
#define __SDL_CALLBACK_H__

#ifdef BOXEDWINE_MULTI_THREADED

struct SdlCallback {
    SDL_Event sdlEvent;
    BOXEDWINE_CONDITION cond;
    std::function<U32()> pfn;
    U32 result;
    struct SdlCallback* next;
};

U32 sdlDispatch(std::function<U32()> p);

#define DISPATCH_MAIN_THREAD_BLOCK_BEGIN sdlDispatch([=]() -> U32 {
#define DISPATCH_MAIN_THREAD_BLOCK_BEGIN_RETURN return sdlDispatch([=]() -> U32 {
#define DISPATCH_MAIN_THREAD_BLOCK_END return 0;});
#else
#define DISPATCH_MAIN_THREAD_BLOCK_BEGIN
#define DISPATCH_MAIN_THREAD_BLOCK_BEGIN_RETURN
#define DISPATCH_MAIN_THREAD_BLOCK_END
#endif

#endif