[CmdletBinding(SupportsShouldProcess)]
param(
    [ValidateNotNullOrEmpty()]
    [string]$PortablePath
)

$ErrorActionPreference = 'Stop'
$pluginName = 'rmtpserver'
$dllName = "$pluginName.dll"
$source = Join-Path $PSScriptRoot $pluginName
if (!(Test-Path -LiteralPath $source -PathType Container)) {
    $source = Join-Path $PSScriptRoot "dist\$pluginName"
}

$files = @(
    'data\locale\en-US.ini',
    'data\locale\zh-CN.ini',
    'data\mediamtx\mediamtx.exe',
    'data\mediamtx\LICENSE',
    $dllName
)
foreach ($file in $files) {
    if (!(Test-Path -LiteralPath (Join-Path $source $file) -PathType Leaf)) {
        throw "Missing $file. Extract the full installation ZIP or build the plugin first."
    }
}

if ($PortablePath) {
    $portableRoot = (Get-Item -LiteralPath $PortablePath).FullName
    if (!(Test-Path -LiteralPath (Join-Path $portableRoot 'bin\64bit\obs64.exe') -PathType Leaf)) {
        throw 'Specify the portable OBS root directory that contains bin\64bit\obs64.exe.'
    }
    $pluginsRoot = Join-Path $portableRoot 'plugins'
} else {
    $pluginsRoot = Join-Path $env:ProgramData 'obs-studio\plugins'
}

$destination = Join-Path $pluginsRoot $pluginName
if ((Get-Item -LiteralPath $source).FullName -eq $destination) {
    Write-Host "Already installed in $destination"
    return
}
if (!$PSCmdlet.ShouldProcess($destination, 'Install rmtpserver')) {
    return
}

try {
    $oldDll = Join-Path $destination $dllName
    if (Test-Path -LiteralPath $oldDll -PathType Leaf) {
        Remove-Item -LiteralPath $oldDll -Force
        Write-Host "Removed previous $dllName"
    }
    foreach ($file in $files) {
        $target = Join-Path $destination $file
        [void][System.IO.Directory]::CreateDirectory((Split-Path -Parent $target))
        Copy-Item -LiteralPath (Join-Path $source $file) -Destination $target -Force
    }
    $legacyDestination = Join-Path $pluginsRoot 'obs-network-relay'
    if ((Test-Path -LiteralPath $legacyDestination -PathType Container) -and
        ((Get-Item -LiteralPath $legacyDestination).FullName -ne $destination)) {
        Remove-Item -LiteralPath $legacyDestination -Recurse -Force
        Write-Host "Removed legacy obs-network-relay plugin from $legacyDestination"
    }
} catch [System.UnauthorizedAccessException] {
    throw "Access denied to $destination. Close OBS and retry from an administrator terminal."
} catch [System.IO.IOException] {
    throw "Could not copy the plugin. Close OBS before installing or updating. $($_.Exception.Message)"
}

Write-Host "Installed in $destination"
Write-Host 'Open OBS and select Tools > rmtpserver.'
