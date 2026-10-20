# Fast syntax check (cl /Zs - no .obj written) of one or more Core / CoreTests .cpp files, with the same
# compiler flags and include paths as Core.vcxproj. Safe to run from several agents at once (writes nothing).
#
#   powershell -ExecutionPolicy Bypass -File tools\check.ps1 Core\Data\SystemData.cpp Core\Infor\IoMap.cpp
#
# Exit code 0 = no errors. Keep this file ASCII-only (Windows PowerShell 5.1 reads BOM-less scripts as ANSI).
param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Files)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$vcvars = 'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'
$vm = 'C:\Program Files\VisionMaster4.4.0\Development\V4.x'
$servo = (Resolve-Path (Join-Path $root '..\..\ServoControl\Cpp')).Path

if (-not $Files -or $Files.Count -eq 0) { Write-Host 'usage: check.ps1 <file.cpp> [...]'; exit 2 }

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

$flags = '/nologo /Zs /std:c++20 /utf-8 /EHs /W3 /permissive- /bigobj /wd4819 /wd4828 /DNOMINMAX /DWIN32_LEAN_AND_MEAN /D_CRT_SECURE_NO_WARNINGS /DUNICODE /D_UNICODE'
$cmd = "call `"$vcvars`" >nul 2>&1 && cl $flags $($inc -join ' ') $($src -join ' ')"
cmd /c $cmd
exit $LASTEXITCODE
