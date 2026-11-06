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

};

struct platform_button_state
{
    int32  HalfTransitionCount;
    bool32 EndedDown;
};

enum platform_key
{
    Key_Left  = 0x25,
    Key_Up    = 0x26,
    Key_Right = 0x27,
    Key_Down  = 0x28,
};

struct platform_gamepad_input
{
    bool32 IsConnected;
    uint16 Buttons; // XInput button bits.
    int16  LeftStickX, LeftStickY;
    int16  RightStickX, RightStickY;
    uint8  LeftTrigger, RightTrigger;
};

struct platform_input
{
    real32 dtForFrame;

    platform_button_state Keys[256]; // Virtual-key codes; letters are 'A'..'Z'.
    platform_button_state MouseButtons[5]; // Left, middle, right, X1, X2.
    int32 MouseX, MouseY, MouseZ;
    int32 LastMouseX, LastMouseY;
    platform_gamepad_input Gamepads[4];

    int32 RenderWidth;
    int32 RenderHeight;
    real32 UIScale;
};

struct game_sound_output_buffer
{
    int    SamplesPerSecond;
    int    SampleCount;
    int16* Samples;
};

#define GAME_UPDATE_AND_RENDER(name) void name(game_memory* Memory, const platform_input* Input, render_commands* RenderCommands)
typedef GAME_UPDATE_AND_RENDER(game_update_and_render);

#define GAME_GET_SOUND_SAMPLES(name) void name(game_memory* Memory, game_sound_output_buffer* SoundBuffer)
typedef GAME_GET_SOUND_SAMPLES(game_get_sound_samples);

#endif
