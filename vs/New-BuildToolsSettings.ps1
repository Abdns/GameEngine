# Export Environment > External Tools and Keyboard from Visual Studio first.
# ExistingToolCount is the number of entries in Tools > External Tools; it
# includes built-in tools, which may be absent from the exported XML.
# Example: .\New-BuildToolsSettings.ps1 -ExportedSettings backup.vssettings -ExistingToolCount 3
# Import the generated file through Tools > Import and Export Settings.
# Save source edits before invoking a tool. Automatic Save All is disabled so
# VSERunOnSave does not enqueue an extra Game.dll build for each saved file.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$ExportedSettings,
    [Parameter(Mandatory = $true)][ValidateRange(0, 24)][int]$ExistingToolCount,
    [string]$OutputFile = (Join-Path $PSScriptRoot '..\build\EngineBuildTools.vssettings')
)
$ErrorActionPreference = 'Stop'
[xml]$source = Get-Content -LiteralPath $ExportedSettings -Raw -Encoding UTF8
$toolsCategory = $source.SelectSingleNode('//Category[@RegisteredName="Environment_ExternalTools"]')
$keysCategory = $source.SelectSingleNode('//Category[@RegisteredName="Environment_KeyBindings"]')
if (!$toolsCategory -or !$keysCategory) { throw 'Export both External Tools and Keyboard settings first.' }
[xml]$output = '<UserSettings><ApplicationIdentity/><ToolsOptions/></UserSettings>'
$output.SelectSingleNode('/UserSettings/ApplicationIdentity').SetAttribute('version', $source.UserSettings.ApplicationIdentity.version)
$group = $output.UserSettings.AppendChild($output.ImportNode($toolsCategory.ParentNode, $false))
$toolRoot = $group.AppendChild($output.ImportNode($toolsCategory, $true)).SelectSingleNode('ExternalTools')
$keyboard = $group.AppendChild($output.ImportNode($keysCategory, $true)).SelectSingleNode('KeyboardShortcuts')
if (!$toolRoot -or !$keyboard) { throw 'The exported settings contain an unsupported format.' }
$userKeys = $keyboard.SelectSingleNode('UserShortcuts')
if (!$userKeys) { $userKeys = $keyboard.AppendChild($output.CreateElement('UserShortcuts')) }
$globalId = '{5EFC7975-14BC-11CF-9B2B-00AA00573819}'
$globalScope = @($keyboard.ScopeDefinitions.Scope | Where-Object { $_.ID -eq $globalId })
if ($globalScope.Count -ne 1) { throw 'The exported keyboard settings are missing the Global scope.' }
$titles = @('Engine: 1 Game', 'Engine: 2 Shaders', 'Engine: 3 Assets', 'Engine: 4 All')
$modes = @('game', 'shaders', 'assets', 'all')
$wrapperPath = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'Build.ps1'))
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$powershellPath = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
$nextIndex = $ExistingToolCount
foreach ($existingIndex in $toolRoot.SelectNodes('*/Index')) {
    if ([int]$existingIndex.InnerText -ge $ExistingToolCount) { throw 'ExistingToolCount does not match the exported tool indices.' }
}
for ($i = 0; $i -lt 4; $i++) {
    $existing = @($toolRoot.SelectNodes('UserCreatedTool') | Where-Object { $_.Title -eq $titles[$i] })
    if ($existing.Count -gt 1) { throw ('Duplicate Engine tool: ' + $titles[$i]) }
    if ($existing.Count -eq 1) {
        $index = [int]$existing[0].Index
        $tool = $existing[0]
    } else {
        $index = $nextIndex++
        $tool = $toolRoot.AppendChild($output.CreateElement('UserCreatedTool'))
    }
    if ($index -ge 24) { throw 'Visual Studio supports only 24 ExternalCommand keyboard commands.' }
    $command = 'Tools.ExternalCommand' + ($index + 1)
    $shortcut = 'Ctrl+Alt+' + ($i + 1)
    foreach ($key in $keyboard.SelectNodes('.//Shortcut')) {
        if (($key.InnerText -replace '\s', '') -ieq $shortcut -and $key.Command -ne $command) {
            throw ('Shortcut already assigned: ' + $shortcut + ' -> ' + $key.Command + '. Choose a free shortcut in Visual Studio first.')
        }
    }
    $properties = [ordered]@{
        Arguments = '-NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "' + $wrapperPath + '" -Mode ' + $modes[$i]
        CloseOnExit = 'true'; Command = $powershellPath; Index = [string]$index
        InitialDirectory = $projectRoot; IsGUIapp = 'false'
        NameID = '0'; Package = '{00000000-0000-0000-0000-000000000000}'
        PromptForArguments = 'false'; SaveAllDocs = 'false'; Title = $titles[$i]
        Unicode = 'true'; UseOutputWindow = 'true'; UseTaskList = 'false'
    }
    foreach ($name in $properties.Keys) {
        $property = $tool.SelectSingleNode($name)
        if (!$property) { $property = $tool.AppendChild($output.CreateElement($name)) }
        $property.InnerText = $properties[$name]
    }
    $key = @($userKeys.SelectNodes('Shortcut') | Where-Object { $_.Command -eq $command -and ($_.InnerText -replace '\s', '') -ieq $shortcut })
    if ($key.Count -eq 0) {
        $binding = $userKeys.AppendChild($output.CreateElement('Shortcut'))
        $binding.SetAttribute('Command', $command)
        $binding.SetAttribute('Scope', $globalScope[0].Name)
        $binding.InnerText = $shortcut
    }
    Write-Host ($shortcut + '  ' + $titles[$i] + '  (' + $command + ')')
}
$outputPath = [IO.Path]::GetFullPath($OutputFile)
if ($outputPath -eq (Resolve-Path -LiteralPath $ExportedSettings).Path) { throw 'Output must not overwrite the settings backup.' }
$null = [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($outputPath))
$writerSettings = New-Object System.Xml.XmlWriterSettings
$writerSettings.Indent = $true
$writerSettings.Encoding = New-Object System.Text.UTF8Encoding($false)
$writer = [System.Xml.XmlWriter]::Create($outputPath, $writerSettings)
try { $output.Save($writer) } finally { $writer.Dispose() }
Write-Host ('Created ' + $outputPath + '. Import only External Tools and Keyboard from this file.')
