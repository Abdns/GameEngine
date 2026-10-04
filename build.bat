@echo off
setlocal

rem Settings: 1 = enabled, 0 = disabled.
rem HOT_RELOAD controls "build.bat on-save", called by Visual Studio on Ctrl+S.
rem Ctrl+S requires the VSERunOnSave extension and the root .vserunonsave file.
rem Changing other settings requires a full build and engine restart.
set "HOT_RELOAD=1"
set "ENGINE_INTERNAL=1"
set "ENGINE_SLOW=1"
rem Shaders and assets are rebuilt only during a full build.
set "BUILD_SHADERS=1"
set "BUILD_ASSETS=1"

rem No argument: full build. "game": DLL only. "on-save": DLL if enabled.
set "BuildMode=full"
if "%~1"=="" goto :mode_ready
if /i "%~1"=="game" (
    set "BuildMode=game"
    goto :mode_ready
)
if /i "%~1"=="on-save" (
    if not "%HOT_RELOAD%"=="1" exit /b 0
    set "BuildMode=game"
    goto :mode_ready
)
echo Usage: build.bat [game ^| on-save]
exit /b 2

:mode_ready
set "BuildRoot=%~dp0"
if not exist "%BuildRoot%build\" mkdir "%BuildRoot%build"
if not exist "%BuildRoot%build\" exit /b 1
set "LockAttempts=0"

rem An open file handle serializes saves and full builds; exit releases it.
rem The guard file stays on disk. Its existence does not mean a build is active.
:acquire_lock
set "BuildEntered="
(
    call :locked_build 9>"%BuildRoot%build\build.guard"
) 2>nul
set "BuildResult=%ERRORLEVEL%"
if defined BuildEntered exit /b %BuildResult%
set /a LockAttempts+=1 >nul
if %LockAttempts% geq 60 (
    echo Another build is still running. Try again when it finishes.
    exit /b 1
)
ping 127.0.0.1 -n 2 >nul
goto :acquire_lock

:locked_build
set "BuildEntered=1"
rem Restore diagnostics while hiding only failed lock-acquisition messages.
call :build 2>&1
exit /b %ERRORLEVEL%

:build
set "VSLANG=1033"
call :setup_compiler
if errorlevel 1 exit /b 1
pushd "%BuildRoot%build"
if errorlevel 1 exit /b 1

set "WindowsSDKVersion="
for /f "tokens=2*" %%A in ('reg query "HKLM\SOFTWARE\Microsoft\Windows Kits\Installed Roots" /v KitsRoot10 2^>nul') do set "WindowsSdkDir=%%B"
if not defined WindowsSdkDir set "WindowsSdkDir=%ProgramFiles(x86)%\Windows Kits\10\"
if not defined WindowsSdkDir set "WindowsSdkDir=%ProgramFiles%\Windows Kits\10\"
if not "%WindowsSdkDir:~-1%"=="\" set "WindowsSdkDir=%WindowsSdkDir%\"
for /f "delims=" %%V in ('dir /b /ad /o-n "%WindowsSdkDir%Include" 2^>nul') do (
    if not defined WindowsSDKVersion set "WindowsSDKVersion=%%V\"
)
if not defined WindowsSDKVersion (
    echo Windows SDK headers not found under "%WindowsSdkDir%Include"
    goto failed
)
set "UniversalCRTSdkDir=%WindowsSdkDir%"
set "INCLUDE=%WindowsSdkDir%Include\%WindowsSDKVersion%ucrt;%WindowsSdkDir%Include\%WindowsSDKVersion%um;%WindowsSdkDir%Include\%WindowsSDKVersion%shared;%WindowsSdkDir%Include\%WindowsSDKVersion%winrt;%WindowsSdkDir%Include\%WindowsSDKVersion%cppwinrt;%INCLUDE%"
set "LIB=%WindowsSdkDir%Lib\%WindowsSDKVersion%ucrt\x64;%WindowsSdkDir%Lib\%WindowsSDKVersion%um\x64;%LIB%"
set "LIBPATH=%WindowsSdkDir%Lib\%WindowsSDKVersion%ucrt\x64;%WindowsSdkDir%Lib\%WindowsSDKVersion%um\x64;%LIBPATH%"

if /i "%BuildMode%"=="game" goto :compiler_flags
if not "%BUILD_SHADERS%"=="1" goto :compiler_flags
if not exist CompiledShaders mkdir CompiledShaders
for %%f in (..\engine\source\Graphics\Vulkan\shaders\*.hlsl) do (
    "%VULKAN_SDK%\Bin\dxc.exe" -spirv -fvk-use-dx-layout -fspv-target-env=vulkan1.3 -T vs_6_0 -E VSMain "%%f" -Fo "CompiledShaders\%%~nf.vert.spv" || goto :failed
    "%VULKAN_SDK%\Bin\dxc.exe" -spirv -fvk-use-dx-layout -fspv-target-env=vulkan1.3 -T ps_6_0 -E PSMain "%%f" -Fo "CompiledShaders\%%~nf.frag.spv" || goto :failed
)

:compiler_flags
set CommonCompilerFlags=-MTd^
 -nologo^
 -Gm-^
 -GR-^
 -EHa-^
 -Od^
 -Oi^
 -WX^
 -W4^
 -wd4201^
 -wd4100^
 -wd4189^
 -wd4505^
 -wd4211^
 -DENGINE_INTERNAL=%ENGINE_INTERNAL%^
 -DENGINE_SLOW=%ENGINE_SLOW%^
 -DENGINE_WIN32=1^
 -DVK_USE_PLATFORM_WIN32_KHR^
 -DUNICODE^
 -D_UNICODE^
 -FC^
 -Z7^
 -I..\shared^
 -I..\engine\source^
 -I..\engine\source\Physics^
 -I..\engine\source\Assets^
 -I..\engine\source\Game\include^
 -I..\engine\source\Game\src^
 -I..\engine\source\Game\src\UI^
 -I..\engine\source\PlatformApi^
 -I..\engine\source\Platform\win32\include^
 -I..\engine\source\Platform\win32\src^
 -I..\engine\source\Graphics\Vulkan^
 -I"%VULKAN_SDK%\Include"

set CommonLinkerFlags=-incremental:no^
 -opt:ref^
 -LIBPATH:"%VULKAN_SDK%\Lib"

if /i "%BuildMode%"=="game" goto :game
if not "%BUILD_ASSETS%"=="1" goto :game
if not exist "%BuildRoot%EngaAsset\" mkdir "%BuildRoot%EngaAsset"
cl %CommonCompilerFlags%^
 ..\tools\AssetBuilder\AssetBuilder.cpp^
 -FeAssetBuilder.exe^
 /link %CommonLinkerFlags%
if errorlevel 1 goto :failed
.\AssetBuilder.exe
if errorlevel 1 goto :failed

:game
rem Publish a complete DLL only after a successful compile and link.
echo WAITING_FOR_PDB > lock.tmp
del Game_build.dll >nul 2>nul
cl %CommonCompilerFlags%^
 -LD ..\engine\source\Game\src\Game.cpp^
 -FmGame.map^
 -FeGame_build.dll^
 /link %CommonLinkerFlags%^
 -PDB:game_%random%_%random%.pdb^
 -IMPLIB:Game.lib^
 -EXPORT:GameUpdateAndRender^
 -EXPORT:GameGetSoundSamples
if errorlevel 1 goto :failed
move /y Game_build.dll Game.dll >nul
if errorlevel 1 goto :failed
del lock.tmp >nul 2>nul
if /i "%BuildMode%"=="game" goto :success

cl %CommonCompilerFlags%^
 ..\engine\source\Platform\win32\Program.cpp^
 -FmEngine.map^
 -FeEngine.exe^
 /link %CommonLinkerFlags%^
 /SUBSYSTEM:CONSOLE^
 /ENTRY:WinMainCRTStartup^
 user32.lib^
 gdi32.lib^
 winmm.lib^
 dsound.lib^
 dxguid.lib^
 Xinput.lib^
 vulkan-1.lib
if errorlevel 1 goto :failed

:success
popd
exit /b 0

:failed
del lock.tmp Game_build.dll >nul 2>nul
echo.
echo BUILD FAILED
popd
exit /b 1

:setup_compiler
if /i "%VSCMD_ARG_TGT_ARCH%"=="x64" (
    where cl >nul 2>nul
    if not errorlevel 1 exit /b 0
)
set "VSWhere=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWhere%" (
    echo Visual Studio C++ tools not found: missing vswhere.exe.
    exit /b 1
)
set "VSPath="
for /f "usebackq delims=" %%V in (`"%VSWhere%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPath=%%V"
if not defined VSPath (
    echo Install the Visual Studio Desktop development with C++ workload.
    exit /b 1
)
call "%VSPath%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
exit /b 0
