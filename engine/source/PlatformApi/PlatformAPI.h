#ifndef PLATFORMAPI_H
#define PLATFORMAPI_H

#include "Types.h"
#include "Strings.h"
#include "RenderCommands.h"

#define PLATFORM_READ_ENTIRE_FILE(name)  file_data name(const char *Filename)
typedef PLATFORM_READ_ENTIRE_FILE(platform_read_entire_file);

#define PLATFORM_FREE_FILE_MEMORY(name)  void name(void *Memory)
typedef PLATFORM_FREE_FILE_MEMORY(platform_free_file_memory);

#if ENGINE_INTERNAL
#define DEBUG_PLATFORM_WRITE_ENTIRE_FILE(name) bool32 name(char* Filename, uint32 MemorySize, void* Memory)
typedef DEBUG_PLATFORM_WRITE_ENTIRE_FILE(debug_platform_write_entire_file);
#endif


struct game_memory
{
    bool32 IsInitialized;

    uint64 PermanentStorageSize;
    void*  PermanentStorage;

    platform_read_entire_file* PlatformReadEntireFile;
    platform_free_file_memory* PlatformFreeFileMemory;


#if ENGINE_INTERNAL
    debug_platform_write_entire_file* DEBUGPlatformWriteEntireFile;
#endif

    // Optional process-local GI validation startup; zero preserves normal startup.
    bool32 GiValidationRun;
    uint32 GiStartupScene;
    uint32 GiStartupView;
};

struct game_button_state
{
    int    HalfTransitionCount;
    bool32 EndedDown;
};

struct game_controller_input
{
    bool32 IsConnected;
    bool32 IsAnalog;

    real32 StickAverageX;
    real32 StickAverageY;

    union
    {
        game_button_state Buttons[6];
        struct
        {
            game_button_state Up;
            game_button_state Down;
            game_button_state Left;
            game_button_state Right;
            game_button_state LeftShoulder;
            game_button_state RightShoulder;
        };
    };
};

struct game_input
{
    real32 dtForFrame;

    game_button_state MouseButtons[5];
    int32 MouseX;
    int32 MouseY;
    int32 MouseZ;
    int32 LastMouseX;
    int32 LastMouseY;

    int32 RenderWidth;
    int32 RenderHeight;

    game_controller_input Controllers[5];
};

struct game_sound_output_buffer
{
    int    SamplesPerSecond;
    int    SampleCount;
    int16* Samples;
};

#define GAME_UPDATE_AND_RENDER(name) void name(game_memory* Memory, game_input* Input, render_commands* RenderCommands)
typedef GAME_UPDATE_AND_RENDER(game_update_and_render);

#define GAME_GET_SOUND_SAMPLES(name) void name(game_memory* Memory, game_sound_output_buffer* SoundBuffer)
typedef GAME_GET_SOUND_SAMPLES(game_get_sound_samples);

#endif
