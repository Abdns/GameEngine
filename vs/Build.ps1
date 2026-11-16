# Visual Studio External Tools must enable Unicode output for this wrapper.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('game', 'shaders', 'assets', 'all')]
    [string]$Mode,
    [switch]$NoLaunch
)
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = New-Object System.Text.UnicodeEncoding($false, $false)

try {
    $root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
    $buildDirectory = Join-Path $root 'build'
    $enginePath = Join-Path $buildDirectory 'Engine.exe'
    $buildScript = Join-Path $root 'build.bat'

    function Get-RunningEngine {
        Get-Process -Name Engine -ErrorAction SilentlyContinue |
            Where-Object { $_.Path -and $_.Path -ieq $enginePath }
    }

    $buildMode = $Mode
    if ($Mode -in @('game', 'all')) {
        $requiredFiles = @($enginePath, (Join-Path $root 'EngaAsset\assets.enga'))
        foreach ($shader in Get-ChildItem -LiteralPath (Join-Path $root 'engine\source\Graphics\Vulkan\shaders') -Filter '*.hlsl') {
            $requiredFiles += Join-Path $buildDirectory ('CompiledShaders\' + $shader.BaseName + '.vert.spv')
            $requiredFiles += Join-Path $buildDirectory ('CompiledShaders\' + $shader.BaseName + '.frag.spv')
        }
        $missingFiles = @($requiredFiles | Where-Object { !(Test-Path -LiteralPath $_ -PathType Leaf) })
        if ($Mode -eq 'game' -and $missingFiles.Count -gt 0) {
            [Console]::WriteLine('[game] Runtime files are missing. Performing one full build before starting the game.')
            $buildMode = 'all'
        }
    }
    if ($buildMode -eq 'all' -and @(Get-RunningEngine).Count -gt 0) {
        throw 'Close the running game before a full build (Ctrl+Alt+4). Use Ctrl+Alt+1 to rebuild and hot-reload Game.dll while playing.'
    }

    # cmd and its tools emit UTF-8; forward decoded text as UTF-16 to the VS
    # Output window. An explicit code page also preserves localized cmd errors.
    $start = New-Object System.Diagnostics.ProcessStartInfo
    $start.FileName = Join-Path $env:SystemRoot 'System32\cmd.exe'
    $start.Arguments = '/d /s /c "chcp 65001 >nul & call "' + $buildScript + '" ' + $buildMode + ' 2>&1"'
    $start.WorkingDirectory = $root
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.StandardOutputEncoding = New-Object System.Text.UTF8Encoding($false)
    $start.StandardErrorEncoding = $start.StandardOutputEncoding
    $build = New-Object System.Diagnostics.Process
    $build.StartInfo = $start
    try {
        $null = $build.Start()
        $errors = $build.StandardError.ReadToEndAsync()
        while ($null -ne ($line = $build.StandardOutput.ReadLine())) {
            [Console]::WriteLine($line)
        }
        $build.WaitForExit()
        $errorText = $errors.GetAwaiter().GetResult()
        if ($errorText) { [Console]::Write($errorText) }
        $result = $build.ExitCode
    } finally {
        $build.Dispose()
    }
    if ($result -ne 0) {
        [Console]::WriteLine('[build] Failed with exit code ' + $result + '. The game was not started.')
        exit $result
    }

    if ($Mode -in @('game', 'all')) {
        $requiredFiles += Join-Path $buildDirectory 'Game.dll'
        $missingFiles = @($requiredFiles | Where-Object { !(Test-Path -LiteralPath $_ -PathType Leaf) })
        if ($missingFiles.Count -gt 0) {
            throw ('Runtime files are still missing after the build: ' + ($missingFiles -join ', ') + '. Enable BUILD_SHADERS and BUILD_ASSETS in build.bat and run a full build.')
        }
    }

    if ($Mode -in @('game', 'all') -and !$NoLaunch) {
        if (@(Get-RunningEngine).Count -gt 0) {
            [Console]::WriteLine('[game] Already running. Game.dll will hot-reload automatically.')
        } else {
            # The game is the visible application requested by the user.
            $game = Start-Process -FilePath $enginePath -WorkingDirectory $buildDirectory -PassThru
            try {
                if ($game.WaitForExit(500)) {
                    throw ('Engine.exe exited during startup with exit code ' + $game.ExitCode + '.')
                }
                [Console]::WriteLine('[game] Started Engine.exe.')
            } finally {
                $game.Dispose()
            }
        }
    }
    exit 0
} catch {
    [Console]::WriteLine('[build] ' + $_.Exception.Message)
    exit 1
}
