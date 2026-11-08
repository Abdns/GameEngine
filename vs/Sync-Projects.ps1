[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$namespace = 'http://schemas.microsoft.com/developer/msbuild/2003'

function Save-XmlIfChanged($Document, [string]$Path) {
    $settings = New-Object System.Xml.XmlWriterSettings
    $settings.Indent = $true
    $settings.NewLineChars = "`r`n"
    $settings.Encoding = New-Object System.Text.UTF8Encoding($false)
    $buffer = New-Object IO.MemoryStream
    $writer = [Xml.XmlWriter]::Create($buffer, $settings)
    try { $Document.Save($writer); $writer.Flush(); $content = [Text.Encoding]::UTF8.GetString($buffer.ToArray()) }
    finally { $writer.Dispose(); $buffer.Dispose() }
    if (!(Test-Path -LiteralPath $Path) -or [IO.File]::ReadAllText($Path) -cne $content) {
        [IO.File]::WriteAllText($Path, $content, $settings.Encoding)
        Write-Output ('[vs] Updated ' + [IO.Path]::GetFileName($Path))
    }
}

foreach ($projectName in @('Engine', 'AssetBuilder')) {
    $projectPath = Join-Path $PSScriptRoot ($projectName + '.vcxproj')
    [xml]$project = Get-Content -LiteralPath $projectPath -Raw
    $roots = if ($projectName -eq 'Engine') { @('engine\source', 'shared') } else { @('tools\AssetBuilder', 'shared', 'assets') }
    $files = @($roots | ForEach-Object {
        Get-ChildItem -LiteralPath (Join-Path $projectRoot $_) -File -Recurse |
            Where-Object { $_.FullName -notmatch '[\\/]\.vs[\\/]' -and ($projectName -eq 'AssetBuilder' -or $_.Extension -in @('.cpp', '.c', '.h', '.hpp', '.inl', '.hlsl')) }
    })
    $files += @(Get-Item -LiteralPath (Join-Path $projectRoot 'build.bat'), (Join-Path $projectRoot '.vserunonsave'))
    $files += @(Get-ChildItem -LiteralPath $PSScriptRoot -File | Where-Object { $_.Extension -in @('.ps1', '.cmd', '.md') })
    if (Test-Path -LiteralPath (Join-Path $projectRoot 'EngaAsset\assets.enga')) { $files += Get-Item -LiteralPath (Join-Path $projectRoot 'EngaAsset\assets.enga') }
    $entries = @($files | Sort-Object FullName -Unique | ForEach-Object {
        $relative = $_.FullName.Substring($projectRoot.Length + 1)
        $isUnit = if ($projectName -eq 'Engine') { $relative -in @('engine\source\Game\src\Game.cpp', 'engine\source\Platform\win32\Program.cpp') } else { $relative -eq 'tools\AssetBuilder\AssetBuilder.cpp' }
        $kind = if ($isUnit) { 'ClCompile' } elseif ($_.Extension -in @('.cpp', '.c', '.h', '.hpp', '.inl')) { 'ClInclude' } else { 'None' }
        [PSCustomObject]@{ Include = '..\' + $relative; Filter = [IO.Path]::GetDirectoryName($relative); Kind = $kind }
    })
    # Included .cpp files belong to the unity translation unit, not separate compiles.
    foreach ($item in @($project.SelectNodes('//*[local-name()="ItemGroup"]/*[local-name()="ClCompile" or local-name()="ClInclude" or local-name()="None"]'))) {
        $null = $item.ParentNode.RemoveChild($item)
    }
    foreach ($group in @($project.SelectNodes('//*[local-name()="ItemGroup"]'))) {
        if (!$group.HasChildNodes) { $null = $group.ParentNode.RemoveChild($group) }
    }
    $fileGroup = $project.CreateElement('ItemGroup', $namespace)
    $fileGroup.SetAttribute('Label', 'GeneratedFiles')
    foreach ($entry in $entries) {
        $item = $project.CreateElement($entry.Kind, $namespace)
        $item.SetAttribute('Include', $entry.Include)
        $null = $fileGroup.AppendChild($item)
    }
    $null = $project.DocumentElement.InsertBefore($fileGroup, $project.DocumentElement.LastChild)

    [xml]$filters = '<Project ToolsVersion="4.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003" />'
    $filterGroup = $filters.CreateElement('ItemGroup', $namespace)
    $filterNames = @($entries | ForEach-Object {
        $folder = $_.Filter
        while ($folder) { $folder; $folder = [IO.Path]::GetDirectoryName($folder) }
    } | Sort-Object -Unique)
    foreach ($filterName in $filterNames) {
        $filter = $filters.CreateElement('Filter', $namespace)
        $filter.SetAttribute('Include', $filterName)
        $null = $filterGroup.AppendChild($filter)
    }
    $null = $filters.DocumentElement.AppendChild($filterGroup)
    $items = $filters.CreateElement('ItemGroup', $namespace)
    foreach ($entry in $entries) {
        $item = $filters.CreateElement($entry.Kind, $namespace)
        $item.SetAttribute('Include', $entry.Include)
        if ($entry.Filter) {
            $filter = $filters.CreateElement('Filter', $namespace)
            $filter.InnerText = $entry.Filter
            $null = $item.AppendChild($filter)
        }
        $null = $items.AppendChild($item)
    }
    $null = $filters.DocumentElement.AppendChild($items)
    Save-XmlIfChanged $project $projectPath
    Save-XmlIfChanged $filters ($projectPath + '.filters')
}
