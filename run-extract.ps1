# run-extract.ps1
# Batch-extract Stellaris .sav files into JSON snapshots.
#
# Default behavior: scan .\save for .sav files, parse any that haven't been
# parsed before (per .\output\.parsed-manifest.json), and write JSON snapshots
# to .\output named after the save (2347.11.08.sav -> 2347.11.08.json).
#
# The decompressed gamestate is deleted after a successful parse.
#
# Usage:
#   .\run-extract.ps1                 # parse new saves only
#   .\run-extract.ps1 -Force          # re-parse everything
#   .\run-extract.ps1 -SaveDir other  # alternate save folder
#   .\run-extract.ps1 -File foo.sav   # parse a single save (still respects manifest)

[CmdletBinding()]
param(
    [string]$SaveDir   = "save",
    [string]$OutputDir = "output",
    [string]$Exe       = "stellaris_extract.exe",
    [string]$File      = $null,        # parse just this one .sav (path or name)
    [switch]$Force                     # ignore manifest, re-parse everything
)

$ErrorActionPreference = "Stop"

# Resolve paths relative to the script's own directory so this works no
# matter where it's invoked from.
$ScriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$SaveDir    = Join-Path $ScriptRoot $SaveDir
$OutputDir  = Join-Path $ScriptRoot $OutputDir
$ExePath    = Join-Path $ScriptRoot $Exe
$Manifest   = Join-Path $OutputDir ".parsed-manifest.json"

# ---------- sanity checks ----------

if (-not (Test-Path $ExePath)) {
    Write-Error "Extractor not found: $ExePath"
    exit 1
}
if (-not (Test-Path $SaveDir)) {
    Write-Error "Save directory not found: $SaveDir"
    exit 1
}
if (-not (Test-Path $OutputDir)) {
    New-Item -ItemType Directory -Path $OutputDir | Out-Null
}

# ---------- manifest load/save ----------
#
# The manifest is a JSON object mapping save filename -> record:
#   {
#     "2347.11.08.sav": {
#       "parsed_at":   "2026-04-29T12:34:56Z",
#       "size_bytes":  1234567,
#       "output_file": "2347.11.08.json"
#     }
#   }
# We key on filename (not path) so moving the save folder doesn't invalidate
# the manifest. Size is recorded so a future "re-parse if changed" mode is
# trivial to add.

function Load-Manifest {
    if (Test-Path $Manifest) {
        try {
            $raw = Get-Content -Raw -Path $Manifest
            return $raw | ConvertFrom-Json -AsHashtable
        } catch {
            Write-Warning "Manifest unreadable, starting fresh: $_"
        }
    }
    return @{}
}

function Save-Manifest($data) {
    $data | ConvertTo-Json -Depth 5 | Set-Content -Path $Manifest -Encoding UTF8
}

# ---------- core: process one save ----------

function Process-Save {
    param(
        [System.IO.FileInfo]$Save,
        [hashtable]$ManifestData
    )

    $name     = $Save.Name                                 # 2347.11.08.sav
    $stem     = [System.IO.Path]::GetFileNameWithoutExtension($name)  # 2347.11.08
    $jsonOut  = Join-Path $OutputDir ($stem + ".json")
    # Use a unique temp filename so concurrent runs don't collide.
    $tmpGs    = Join-Path $OutputDir (".tmp-" + $stem + "-" + [guid]::NewGuid().ToString("N").Substring(0,8) + ".gamestate")

    Write-Host ""
    Write-Host "=== $name ===" -ForegroundColor Cyan

    # 1. Decompress the .sav (it's a zip; gamestate is one entry inside)
    Write-Host "    extracting gamestate..." -NoNewline
    try {
        # Open the zip and pull only the 'gamestate' entry. We avoid
        # Expand-Archive because it requires .zip extension and extracts
        # everything; ZipFile lets us pick one entry directly.
        Add-Type -AssemblyName System.IO.Compression.FileSystem | Out-Null
        $zip = [System.IO.Compression.ZipFile]::OpenRead($Save.FullName)
        try {
            $entry = $zip.Entries | Where-Object { $_.Name -eq "gamestate" } | Select-Object -First 1
            if (-not $entry) { throw "no 'gamestate' entry inside $name" }
            [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $tmpGs, $true)
        } finally {
            $zip.Dispose()
        }
    } catch {
        Write-Host " FAILED" -ForegroundColor Red
        Write-Warning "    $_"
        return $false
    }
    $gsBytes = (Get-Item $tmpGs).Length
    Write-Host (" {0:N0} bytes" -f $gsBytes) -ForegroundColor DarkGray

    # 2. Run the extractor
    Write-Host "    running extractor..."
    $proc = Start-Process -FilePath $ExePath `
                          -ArgumentList @("`"$tmpGs`"", "`"$jsonOut`"") `
                          -NoNewWindow -Wait -PassThru
    if ($proc.ExitCode -ne 0) {
        Write-Host "    extractor failed (exit $($proc.ExitCode))" -ForegroundColor Red
        # Leave the gamestate on disk so the user can debug
        Write-Warning "    decompressed gamestate kept at: $tmpGs"
        return $false
    }

    # 3. Delete the decompressed gamestate (success path only)
    Remove-Item $tmpGs -Force -ErrorAction SilentlyContinue

    # 4. Update the manifest
    $ManifestData[$name] = @{
        parsed_at   = (Get-Date).ToUniversalTime().ToString("yyyy-MM-ddTHH:mm:ssZ")
        size_bytes  = $Save.Length
        output_file = ($stem + ".json")
    }

    Write-Host "    -> $jsonOut" -ForegroundColor Green
    return $true
}

# ---------- main ----------

$manifestData = Load-Manifest

# Decide which saves to consider
if ($File) {
    $target = $File
    if (-not [System.IO.Path]::IsPathRooted($target)) {
        # Allow either "foo.sav" (assume in $SaveDir) or a relative path
        $candidate = Join-Path $SaveDir $target
        if (Test-Path $candidate) { $target = $candidate }
        else { $target = Join-Path $ScriptRoot $target }
    }
    if (-not (Test-Path $target)) { Write-Error "File not found: $File"; exit 1 }
    $saves = @(Get-Item $target)
} else {
    $saves = @(Get-ChildItem -Path $SaveDir -Filter "*.sav" -File | Sort-Object Name)
}

if ($saves.Count -eq 0) {
    Write-Host "No .sav files found in $SaveDir" -ForegroundColor Yellow
    exit 0
}

# Filter against manifest unless -Force
$todo = @()
$skipped = 0
foreach ($s in $saves) {
    if (-not $Force -and $manifestData.ContainsKey($s.Name)) {
        $skipped++
        continue
    }
    $todo += $s
}

Write-Host ""
Write-Host "Found $($saves.Count) save(s); $skipped already parsed; $($todo.Count) to do."
if ($Force) { Write-Host "(-Force: ignoring manifest)" -ForegroundColor Yellow }

$ok = 0; $fail = 0
foreach ($s in $todo) {
    if (Process-Save -Save $s -ManifestData $manifestData) { $ok++ } else { $fail++ }
    # Save manifest after each success so a crash mid-batch doesn't lose progress
    Save-Manifest $manifestData
}

Write-Host ""
Write-Host "Done. $ok succeeded, $fail failed, $skipped skipped." -ForegroundColor Cyan
if ($fail -gt 0) { exit 1 }