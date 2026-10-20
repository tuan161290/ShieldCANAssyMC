# Compile + link a console exe from a list of source files, with the same flags/includes as Core.vcxproj.
# For a quick private test of a group of files without building the whole solution. Every caller uses its
# OWN -Out folder (the .obj files land next to the exe), so several agents can run this at once.
#
#   powershell -ExecutionPolicy Bypass -File tools\build-exe.ps1 -Out C:\tmp\t\test.exe Core\Base\Text.cpp my_main.cpp
#
# -CoreLibs: also link the prebuilt Core.lib + ServoControl.lib (Release) and the system/VisionMaster libs they
#            need; then pass only files that are NOT already in Core.lib.
# Keep this file ASCII-only (Windows PowerShell 5.1 reads BOM-less scripts as ANSI).
[CmdletBinding(PositionalBinding = $false)]
param(
    [Parameter(Mandatory = $true)][string]$Out,
    [string[]]$Libs = @(),
    [switch]$CoreLibs,
    [Parameter(ValueFromRemainingArguments = $true)][string[]]$Files
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$vcvars = 'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'
$vm = 'C:\Program Files\VisionMaster4.4.0\Development\V4.x'
$servo = (Resolve-Path (Join-Path $root '..\..\ServoControl\Cpp')).Path

$outDir = Split-Path -Parent $Out
New-Item -ItemType Directory -Force $outDir | Out-Null

$inc = @(
    "$root\Core",
    "$root\ThirdParty",
    "$root\Tests\CoreTests",
    $servo,
    "$vm\Includes"
) | ForEach-Object { "/I `"$_`"" }

$src = $Files | ForEach-Object {
    $p = if ([System.IO.Path]::IsPathRooted($_)) { $_ } else { Join-Path $root $_ }
    "`"$p`""
}

$linkArgs = @($Libs | ForEach-Object { "`"$_`"" })
if ($CoreLibs) {
    $linkArgs += "`"$root\build\x64\Release\lib\Core.lib`""
    $linkArgs += "`"$servo\build\x64\Release\lib\ServoControl.lib`""
    $linkArgs += "/LIBPATH:`"$vm\Libraries\win64\C`""
    $linkArgs += 'ws2_32.lib winmm.lib iMVS-6000PlatformSDK.lib delayimp.lib'
    $linkArgs += '/DELAYLOAD:iMVS-6000PlatformSDK.dll /IGNORE:4199'
}

$flags = '/nologo /std:c++20 /utf-8 /EHs /W3 /permissive- /bigobj /wd4819 /wd4828 /MD /O2 /DNOMINMAX /DWIN32_LEAN_AND_MEAN /D_CRT_SECURE_NO_WARNINGS /DUNICODE /D_UNICODE'
$link = "/link /OUT:`"$Out`" $($linkArgs -join ' ')"
$cmd = "call `"$vcvars`" >nul 2>&1 && cl $flags /Fo`"$outDir\\`" $($inc -join ' ') $($src -join ' ') $link"
cmd /c $cmd
exit $LASTEXITCODE
