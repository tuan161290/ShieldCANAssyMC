# Shield CAN Assy MC updater. The App (Setting > System > SOFTWARE UPDATE) downloads a GitHub release zip, checks its
# SHA-256, copies this script to %TEMP%\ShieldCanAssyUpdate and starts it, then closes itself (sca::AppUpdate).
#
#   1. wait for the App to exit (120 s, else nothing is changed)
#   2. unpack the zip next to it
#   3. back up every file it is about to replace into <AppDir>\_Update\backup-<old>-<time>, then copy the new files
#   4. start the new App and watch it for 20 s
# Any failure after step 2 puts the backup back and starts the old App again. The package holds only build output,
# so _RunData (config, models, logs) and the VisionMaster runtime folders next to the exe are never touched.
# Keep this file ASCII-only (Windows PowerShell 5.1 reads BOM-less scripts as ANSI).
param(
    [Parameter(Mandatory = $true)][int]$ProcessId,
    [Parameter(Mandatory = $true)][string]$Zip,
    [Parameter(Mandatory = $true)][string]$AppDir,
    [string]$NewVersion = '?',
    [string]$OldVersion = '?',
    [string]$LogFile = ''
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
try { $Host.UI.RawUI.WindowTitle = "Shield CAN Assy MC - update $OldVersion -> $NewVersion" } catch {}

function Log([string]$message) {
    $line = (Get-Date -Format 'yyyy-MM-dd HH:mm:ss') + '  ' + $message
    Write-Host $line
    if ($LogFile) { try { Add-Content -LiteralPath $LogFile -Value $line -Encoding UTF8 } catch {} }
}

function Finish([int]$code) {
    Log 'This window closes in 10 s.'
    Start-Sleep -Seconds 10
    exit $code
}

$exe = Join-Path $AppDir 'ShieldCanAssy.exe'
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$work = Join-Path (Split-Path -Parent $Zip) "stage-$NewVersion-$stamp"
$backupRoot = Join-Path $AppDir '_Update'
$backup = Join-Path $backupRoot "backup-$OldVersion-$stamp"
$added = New-Object System.Collections.Generic.List[string]

function Restore {
    Log "Putting back $OldVersion from $backup"
    if (Test-Path -LiteralPath $backup) {
        Get-ChildItem -LiteralPath $backup -Recurse -File | ForEach-Object {
            $rel = $_.FullName.Substring($backup.Length + 1)
            Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $AppDir $rel) -Force
        }
    }
    foreach ($rel in $added) { Remove-Item -LiteralPath (Join-Path $AppDir $rel) -Force -ErrorAction SilentlyContinue }
}

function StartOld {
    if (Test-Path -LiteralPath $exe) {
        Start-Process -FilePath $exe -WorkingDirectory $AppDir | Out-Null
        Log "Started $OldVersion again"
    }
}

Log "Update $OldVersion -> $NewVersion"
Log "App folder: $AppDir"
Log 'Do not switch off the PC until this window says done.'

# 1. Wait for the App
$old = Get-Process -Id $ProcessId -ErrorAction SilentlyContinue
if ($old) {
    Log 'Waiting for the App to close...'
    if (-not $old.WaitForExit(120000)) {
        Log 'The App did not close within 120 s - update cancelled, nothing changed.'
        Finish 1
    }
}
Start-Sleep -Seconds 2

# 2. Unpack
try {
    [IO.Compression.ZipFile]::ExtractToDirectory($Zip, $work)
} catch {
    Log "Cannot unpack $Zip : $($_.Exception.Message) - update cancelled, nothing changed."
    StartOld
    Finish 1
}
if (-not (Test-Path -LiteralPath (Join-Path $work 'ShieldCanAssy.exe'))) {
    Log 'The package has no ShieldCanAssy.exe - update cancelled, nothing changed.'
    StartOld
    Finish 1
}
$files = @(Get-ChildItem -LiteralPath $work -Recurse -File)
Log "Package: $($files.Count) files"

# 3. Back up, then copy
try {
    foreach ($f in $files) {
        $rel = $f.FullName.Substring($work.Length + 1)
        $dst = Join-Path $AppDir $rel
        if (Test-Path -LiteralPath $dst) {
            $b = Join-Path $backup $rel
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $b) | Out-Null
            Copy-Item -LiteralPath $dst -Destination $b -Force
        } else {
            $added.Add($rel)
        }
    }
    Log "Backup of $OldVersion : $backup"
    foreach ($f in $files) {
        $dst = Join-Path $AppDir ($f.FullName.Substring($work.Length + 1))
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $dst) | Out-Null
        Copy-Item -LiteralPath $f.FullName -Destination $dst -Force
    }
    Log 'New files copied'
} catch {
    Log "Copy failed: $($_.Exception.Message)"
    Restore
    StartOld
    Finish 1
}

# 4. Start the new App and watch it
$new = Start-Process -FilePath $exe -WorkingDirectory $AppDir -PassThru
Log "Started $NewVersion (pid $($new.Id)), watching 20 s..."
Start-Sleep -Seconds 20
if ($new.HasExited) {
    Log "$NewVersion exited at once (code $($new.ExitCode)) - rolling back"
    Restore
    StartOld
    Finish 1
}

Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $Zip -Force -ErrorAction SilentlyContinue
# Keep the two newest backups (one is about 100 MB).
Get-ChildItem -LiteralPath $backupRoot -Directory -Filter 'backup-*' |
    Sort-Object LastWriteTime -Descending | Select-Object -Skip 2 |
    ForEach-Object { Remove-Item -LiteralPath $_.FullName -Recurse -Force -ErrorAction SilentlyContinue }
Log "Update to $NewVersion done. $OldVersion kept in $backup"
Finish 0
