# Publish the WinUI App to GitHub Releases for the in-app updater (Setting > System > SOFTWARE UPDATE, sca::AppUpdate).
#
#   tools\Publish-Release.ps1                                   version = today, yyyy.M.d (then .2, .3 ... if the tag exists)
#   tools\Publish-Release.ps1 -Version 2026.11.8 -Notes "Fix scan order"
#   tools\Publish-Release.ps1 -PackageOnly                      build + zip only, no commit / push / release
#
# Steps: clean tree on main -> write Core\SysConfig\AppVersion.h -> build Core, then the App into a fresh folder under
# %TEMP% (never the normal build tree, so the App may stay open) -> zip the build output without .pdb/.lib/.exp
# (+ .sha256) -> commit "Release vX" + tag vX -> push main and the tag to origin and github -> gh release create.
# Needs: VS 18 (MSBuild), the VisionMaster SDK, gh logged in (gh auth login).
# Keep this file ASCII-only (Windows PowerShell 5.1 reads BOM-less scripts as ANSI).
[CmdletBinding()]
param(
    [string]$Version,
    [string]$Notes = '',
    [switch]$PackageOnly
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$repoRoot = Split-Path -Parent $root
$ghRepo = 'tuan161290/ShieldCANAssyMC'
$msbuild = 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe'
$git = (Get-Command git -ErrorAction SilentlyContinue).Source
if (-not $git) { $git = 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\TeamFoundation\Team Explorer\Git\cmd\git.exe' }
$gh = (Get-Command gh -ErrorAction SilentlyContinue).Source
if (-not $gh) { $gh = 'C:\Program Files\GitHub CLI\gh.exe' }

function Run([string]$exe, [string[]]$arguments) {
    & $exe @arguments
    if ($LASTEXITCODE -ne 0) { throw "$([IO.Path]::GetFileName($exe)) $($arguments -join ' ') failed (exit $LASTEXITCODE)" }
}

# A native command whose stderr is redirected turns each stderr line into an error record; with 'Stop' that aborts
# the script (Windows PowerShell 5.1). Run such checks with 'Continue' and look at the exit code instead.
function Quiet([scriptblock]$block) {
    $saved = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try { & $block } finally { $ErrorActionPreference = $saved }
}

Push-Location $repoRoot
try {
    # 1. Clean tree on main, gh ready
    $branch = (& $git rev-parse --abbrev-ref HEAD).Trim()
    if ($branch -ne 'main') { throw "On branch '$branch' - switch to main first." }
    if (& $git status --porcelain) { throw 'Uncommitted changes - commit (or stash) them first.' }
    if (-not $PackageOnly) {
        Quiet { & $gh auth status *> $null }
        if ($LASTEXITCODE -ne 0) { throw 'GitHub CLI is not logged in - run: gh auth login' }
        if (-not ((& $git remote) -contains 'github')) { throw "No 'github' remote - git remote add github https://github.com/$ghRepo.git" }
    }

    # 2. Version
    Quiet { & $git fetch -q --tags origin 2>$null }
    $tags = @(& $git tag --list 'v*')
    if (-not $Version) {
        $base = (Get-Date).ToString('yyyy.M.d')
        $Version = $base
        $n = 2
        while ($tags -contains "v$Version") { $Version = "$base.$n"; $n++ }
    }
    if ($Version -notmatch '^\d+(\.\d+){1,3}$') { throw "Version must look like 2026.11.8 or 2026.11.8.2 (got '$Version')." }
    $tag = "v$Version"
    if ($tags -contains $tag) { throw "Tag $tag already exists." }
    Write-Host "== Release $tag"

    # 3. Version into the code
    $versionFile = Join-Path $root 'Core\SysConfig\AppVersion.h'
    $utf8 = New-Object Text.UTF8Encoding($false)
    $text = [IO.File]::ReadAllText($versionFile, $utf8)
    $text = [regex]::Replace($text, '#define SCA_APP_VERSION "[^"]*"', "#define SCA_APP_VERSION `"$Version`"")
    [IO.File]::WriteAllText($versionFile, $text, $utf8)

    # 4. Build Core (normal tree), then a private copy of the App into a fresh folder
    $work = Join-Path $env:TEMP "ShieldCanAssyRelease\$Version"
    if (Test-Path $work) { Remove-Item $work -Recurse -Force }
    New-Item -ItemType Directory -Force $work | Out-Null
    $common = @('-p:Configuration=Release', '-p:Platform=x64', '-m', '-nologo', '-v:m')
    Write-Host '== Build Core'
    Run $msbuild (@("$root\Core\Core.vcxproj") + $common)
    Write-Host '== Build App (fresh folder, takes a few minutes)'
    robocopy "$root\App" "$work\App" /E /XD obj 'Generated Files' /NFL /NDL /NJH /NJS /NP | Out-Null
    Run $msbuild (@("$work\App\ShieldCanAssy.vcxproj", '-restore', '-p:BuildProjectReferences=false', "-p:ScaRoot=$root",
                    "-p:ScaBuildDir=$work\build", "-p:ScaCoreLib=$root\build\x64\Release\lib\Core.lib") + $common)
    $out = "$work\build\x64\Release\App"
    if (-not (Test-Path "$out\ShieldCanAssy.exe") -or -not (Test-Path "$out\Update\Updater.ps1")) { throw "Build output incomplete: $out" }

    # 5. Package
    Write-Host '== Package'
    $pkg = "$work\pkg"
    robocopy $out $pkg /E /XF *.pdb *.lib *.exp *.ilk /NFL /NDL /NJH /NJS /NP | Out-Null
    $zipName = "ShieldCanAssy-$Version.zip"
    $zip = Join-Path $work $zipName
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::CreateFromDirectory($pkg, $zip, [IO.Compression.CompressionLevel]::Optimal, $false)
    $sha = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
    [IO.File]::WriteAllText("$zip.sha256", "$sha  $zipName`n", $utf8)
    Write-Host ("   {0}  {1:N1} MB  sha256 {2}" -f $zip, ((Get-Item $zip).Length / 1MB), $sha)

    if ($PackageOnly) {
        Write-Host "PackageOnly: $versionFile now says $Version (not committed). Revert with: git checkout -- `"$versionFile`""
        return
    }

    # 6. Commit, tag, push to both remotes
    Write-Host '== Commit, tag, push'
    if (& $git status --porcelain) {
        Run $git @('add', $versionFile)
        Run $git @('commit', '-q', '-m', "Release $tag")
    }
    Run $git @('tag', '-a', $tag, '-m', "Release $tag")
    Run $git @('push', '-q', 'origin', 'main', $tag)
    Run $git @('push', '-q', 'github', 'main', $tag)

    # 7. GitHub release with the zip and its checksum
    Write-Host '== GitHub release'
    if (-not $Notes) { $Notes = "Shield CAN Assy MC $Version" }
    Run $gh @('release', 'create', $tag, $zip, "$zip.sha256", '--repo', $ghRepo, '--title', "ShieldCanAssy $Version", '--notes', $Notes)
    Write-Host "Done: https://github.com/$ghRepo/releases/tag/$tag"
}
finally {
    Pop-Location
}
