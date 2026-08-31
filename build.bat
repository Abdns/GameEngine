@echo off

set VSLANG=1033

if not exist build mkdir build
if not exist EngaAsset mkdir EngaAsset
pushd build

del *.pdb > NUL 2> NUL

if not exist CompiledShaders mkdir CompiledShaders
for %%f in (..\engine\source\Graphics\Vulkan\shaders\*.hlsl) do (
    "%VULKAN_SDK%\Bin\dxc.exe" -spirv -fvk-use-dx-layout -fspv-target-env=vulkan1.3 -T vs_6_0 -E VSMain "%%f" -Fo "CompiledShaders\%%~nf.vert.spv" || goto :failed
    "%VULKAN_SDK%\Bin\dxc.exe" -spirv -fvk-use-dx-layout -fspv-target-env=vulkan1.3 -T ps_6_0 -E PSMain "%%f" -Fo "CompiledShaders\%%~nf.frag.spv" || goto :failed
)

for %%f in (..\engine\source\Graphics\Vulkan\shaders\compute\*.hlsl) do (
    "%VULKAN_SDK%\Bin\dxc.exe" -spirv -fvk-use-dx-layout -fspv-target-env=vulkan1.3 -I ..\engine\source\Graphics\Vulkan\shaders -T cs_6_0 -E CSMain "%%f" -Fo "CompiledShaders\%%~nf.comp.spv" || goto :failed
)

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
 -DENGINE_INTERNAL=1^
 -DENGINE_SLOW=1^
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
 -I..\engine\source\PlatformApi^
 -I..\engine\source\Platform\win32\include^
 -I..\engine\source\Platform\win32\src^
 -I..\engine\source\Graphics\Vulkan^
 -I"%VULKAN_SDK%\Include"

set CommonLinkerFlags=-incremental:no^
 -opt:ref^
 -LIBPATH:"%VULKAN_SDK%\Lib"

cl %CommonCompilerFlags%^
 ..\tools\AssetBuilder\AssetBuilder.cpp^
 -FeAssetBuilder.exe^
 /link %CommonLinkerFlags%^
 gdi32.lib
if errorlevel 1 goto :failed
.\AssetBuilder.exe
if errorlevel 1 goto :failed

echo WAITING_FOR_PDB > lock.tmp
cl %CommonCompilerFlags%^
 -LD ..\engine\source\Game\src\Game.cpp^
 -FmGame.map^
 -FeGame.dll^
 /link %CommonLinkerFlags%^
 -PDB:game_%random%.pdb^
 -EXPORT:GameUpdateAndRender^
 -EXPORT:GameGetSoundSamples
set GameError=%ERRORLEVEL%
del lock.tmp
if not "%GameError%"=="0" goto :failed

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

popd
exit /b 0

:failed
echo.
echo BUILD FAILED
popd
exit /b 1
