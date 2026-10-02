/*
    SDL_winrt_main_NonXAML.cpp, placed in the public domain by David Ludwig  3/13/14
*/

#define SDL_WINRT_METADATA_FILE_AVAILABLE 1
#include "SDL_main.h"
#include <wrl.h>

#ifndef SDL_WINRT_METADATA_FILE_AVAILABLE
#ifndef __cplusplus_winrt
#error SDL_winrt_main_NonXAML.cpp must be compiled with /ZW, otherwise build errors due to missing .winmd files can occur.
#endif
#endif

#ifdef _MSC_VER
#pragma warning(disable : 4447)
#pragma comment(lib, "runtimeobject.lib")
#endif

int CALLBACK WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    return SDL_WinRTRunApp(SDL_main, NULL);
}
