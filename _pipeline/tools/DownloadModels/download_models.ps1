<#
.SYNOPSIS
  AI model downloader for this folder (ComfyUI layout). Double-click download_models.cmd to run it.

  The list of files and their download addresses is models.json (next to this script):
    "files"  : { "path": "folder/file", "url": "https://...", "gated": true (optional: needs a sign-in token) }
    "ignore" : wildcard patterns of files that are never treated as models (placeholders, configs ...)

  Menu:
    1  Download   - shows the target folder, scans it, skips (and lists) the files already there, asks
                    whether to download the missing ones (creates the folder if needed), then asks whether
                    to download the files that already existed again and replace them.
    2  Find new   - lists the files in the target folder that are not in models.json yet, and lets you add
                    each one with its download address (checked online; the size is compared).

  Downloads go to "<file>.part" first and are renamed when complete; running again resumes them.
  Gated repositories need a Hugging Face read token: accept the licence on the model page, then set
  HF_TOKEN (setx HF_TOKEN hf_xxx) or paste the token when asked.
#>
param([string]$Target, [string]$Manifest)

$ErrorActionPreference = 'Continue'
if (-not $Manifest) { $Manifest = Join-Path $PSScriptRoot 'models.json' }
$curl = Join-Path $env:WINDIR 'System32\curl.exe'
$script:token = $env:HF_TOKEN

# ================================================================== models.json
function Read-Manifest {
    if (-not (Test-Path -LiteralPath $Manifest)) { return [pscustomobject]@{ Ignore = @(); Files = New-Object System.Collections.Generic.List[object] } }
    $j = [IO.File]::ReadAllText($Manifest) | ConvertFrom-Json
    $files = New-Object System.Collections.Generic.List[object]
    foreach ($f in @($j.files)) {
        $files.Add([pscustomobject]@{ Rel = $f.path.Replace('/', '\'); Url = $f.url; Gated = [bool]$f.gated })
    }
    return [pscustomobject]@{ Ignore = @($j.ignore); Files = $files }
}

# One entry per line, so the file stays easy to read and to edit by hand.
function Save-Manifest($m) {
    $lines = foreach ($it in $m.Files) {
        $o = [ordered]@{ path = $it.Rel.Replace('\', '/'); url = $it.Url }
        if ($it.Gated) { $o.gated = $true }
        '    ' + (ConvertTo-Json $o -Compress)
    }
    $ig = ($m.Ignore | ForEach-Object { '    ' + (ConvertTo-Json ([string]$_)) }) -join ",`n"
    $json = "{`n  `"ignore`": [`n$ig`n  ],`n  `"files`": [`n" + ($lines -join ",`n") + "`n  ]`n}`n"
    [IO.File]::WriteAllText($Manifest, $json, (New-Object Text.UTF8Encoding $false))
}

# ================================================================== helpers
function Format-Size([double]$b) {
    if ($b -ge 1GB) { '{0:N2} GB' -f ($b / 1GB) } elseif ($b -ge 1MB) { '{0:N1} MB' -f ($b / 1MB) } else { '{0:N0} KB' -f ($b / 1KB) }
}

# Asks Y (all) / N (none) / numbers like "1,3,5-7"; returns the chosen items.
function Select-Items([object[]]$pool, [string]$question) {
    while ($true) {
        $a = ([string](Read-Host "$question  [Y = all / N = none / numbers e.g. 1,3,5-7]")).Trim()
        if ($a -match '^(y|yes|a|all)$') { return $pool }
        if ($a -eq '' -or $a -match '^(n|no)$') { return @() }
        $picked = New-Object System.Collections.Generic.SortedSet[int]; $bad = $false
        foreach ($tok in $a -split '[,\s]+' | Where-Object { $_ }) {
            if ($tok -match '^(\d+)-(\d+)$') { $from = [int]$Matches[1]; $to = [int]$Matches[2] }
            elseif ($tok -match '^\d+$') { $from = $to = [int]$tok }
            else { $bad = $true; break }
            foreach ($n in $from..$to) { if ($n -ge 1 -and $n -le $pool.Count) { [void]$picked.Add($n - 1) } else { $bad = $true } }
        }
        if (-not $bad) { return @($picked | ForEach-Object { $pool[$_] }) }
        Write-Host "  Not understood, try again." -ForegroundColor Yellow
    }
}

function Show-List([object[]]$pool, [string]$root, [switch]$WithSize) {
    $n = 0
    foreach ($it in $pool) {
        $n++
        $extra = if ($WithSize) { '  ' + (Format-Size (Get-Item -LiteralPath (Join-Path $root $it.Rel)).Length) } else { '' }
        $g = if ($it.Gated) { '  [sign-in]' } else { '' }
        Write-Host ('  {0,3}. {1}{2}{3}' -f $n, $it.Rel, $extra, $g)
    }
}

function Get-AuthArgs([string]$url) {
    if ($script:token -and $url -like 'https://huggingface.co/*') { return @('-H', "Authorization: Bearer $($script:token)") }
    return @()
}

# HEAD request (following redirects): the last status code and that response's size.
function Test-Url([string]$url) {
    $auth = @(Get-AuthArgs $url)
    $h = & $curl -sIL --max-time 30 @auth $url 2>$null
    $code = 0; $size = $null
    foreach ($line in $h) {
        if ($line -match '^HTTP/\S+\s+(\d+)') { $code = [int]$Matches[1]; $size = $null }
        elseif ($line -match '^content-length:\s*(\d+)') { $size = [int64]$Matches[1] }
    }
    return [pscustomobject]@{ Code = $code; Size = $size }
}

function Get-Files([object[]]$todo, [string]$root, [bool]$replace) {
    $failed = @(); $i = 0
    if (($todo | Where-Object Gated) -and -not $script:token) {
        Write-Host "`nSome files are in gated repositories and need a Hugging Face read token" -ForegroundColor Yellow
        Write-Host "(accept the licence on the model page first). Leave empty to skip them." -ForegroundColor Yellow
        $script:token = ([string](Read-Host 'HF token')).Trim()
    }
    foreach ($it in $todo) {
        $i++
        $dest = Join-Path $root $it.Rel
        $part = "$dest.part"
        Write-Host ("`n[{0}/{1}] {2}" -f $i, $todo.Count, $it.Rel) -ForegroundColor Cyan
        Write-Host "        $($it.Url)" -ForegroundColor DarkGray
        if ($it.Gated -and -not $script:token) { Write-Host '        skipped: needs HF token' -ForegroundColor Yellow; $failed += $it; continue }
        New-Item -ItemType Directory -Force (Split-Path -Parent $dest) | Out-Null
        $cargs = @('-L', '--fail', '--retry', '3', '-C', '-', '-o', $part) + @(Get-AuthArgs $it.Url)
        & $curl @cargs $it.Url
        $code = $LASTEXITCODE
        # 33 = the .part is already complete (server refuses the range): keep it.
        if (($code -eq 0 -or $code -eq 33) -and (Test-Path -LiteralPath $part)) {
            Move-Item -LiteralPath $part -Destination $dest -Force
            Write-Host ("        OK  {0}{1}" -f (Format-Size (Get-Item -LiteralPath $dest).Length), $(if ($replace) { '  (replaced)' } else { '' })) -ForegroundColor Green
        } else {
            Write-Host "        FAILED (curl exit $code). A partial .part file is kept; run again to resume." -ForegroundColor Red
            $failed += $it
        }
    }
    return $failed
}

# ================================================================== 1. download
function Invoke-Download([string]$root) {
    $m = Read-Manifest
    $items = $m.Files
    if (-not $items.Count) { Write-Host "models.json has no files yet. Use 2 (Find new) to add some." -ForegroundColor Yellow; return }
    if (-not (Test-Path -LiteralPath $root)) { Write-Host "(the target folder does not exist yet; it will be created)" }

    $existing = @($items | Where-Object { Test-Path -LiteralPath (Join-Path $root $_.Rel) })
    $missing  = @($items | Where-Object { -not (Test-Path -LiteralPath (Join-Path $root $_.Rel)) })

    Write-Host "`nAlready in the target folder (skipped for now): $($existing.Count)" -ForegroundColor Green
    if ($existing) { Show-List $existing $root -WithSize }
    Write-Host "`nMissing: $($missing.Count)" -ForegroundColor Yellow
    if ($missing) { Show-List $missing $root }

    $failed = @()
    if ($missing) {
        $todo = @(Select-Items $missing "`nDownload the missing files?")
        if ($todo) {
            if (-not (Test-Path -LiteralPath $root)) { New-Item -ItemType Directory -Force $root | Out-Null; Write-Host "Created $root" }
            $failed += @(Get-Files $todo $root $false)
            Write-Host "`nMissing files: download finished." -ForegroundColor Green
        }
    } else { Write-Host "`nNothing is missing." -ForegroundColor Green }

    if ($existing) {
        Write-Host "`nThese files existed before this run:" -ForegroundColor Cyan
        Show-List $existing $root -WithSize
        $todo = @(Select-Items $existing "Download them again and REPLACE the original files?")
        if ($todo) { $failed += @(Get-Files $todo $root $true) }
    }

    Write-Host ''
    if ($failed) {
        Write-Host "Not downloaded ($($failed.Count)):" -ForegroundColor Red
        $failed | ForEach-Object { Write-Host "  $($_.Rel)" }
    } else { Write-Host 'Download: done.' -ForegroundColor Green }
}

# ================================================================== 2. find new files, add them with an address
function Invoke-FindNew([string]$root) {
    if (-not (Test-Path -LiteralPath $root)) { Write-Host "The target folder does not exist." -ForegroundColor Yellow; return }
    $m = Read-Manifest
    $known = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    foreach ($it in $m.Files) { [void]$known.Add($it.Rel) }

    Write-Host "`nScanning $root ..."
    $new = @(Get-ChildItem -LiteralPath $root -Recurse -File -Force -ErrorAction SilentlyContinue | ForEach-Object {
        $rel = $_.FullName.Substring($root.Length).TrimStart('\')
        $slash = $rel.Replace('\', '/')
        $skip = $known.Contains($rel)
        if (-not $skip) { foreach ($p in $m.Ignore) { if ($slash -like $p) { $skip = $true; break } } }
        if (-not $skip) { [pscustomobject]@{ Rel = $rel; Size = $_.Length; Gated = $false } }
    })

    if (-not $new) { Write-Host "No new files: every file in the folder is in models.json (or ignored)." -ForegroundColor Green; return }
    Write-Host "`nNew files, not in models.json: $($new.Count)" -ForegroundColor Yellow
    $n = 0; foreach ($it in $new) { $n++; Write-Host ('  {0,3}. {1}  {2}' -f $n, $it.Rel, (Format-Size $it.Size)) }

    $todo = @(Select-Items $new "`nAdd download addresses for which files?")
    $added = 0
    foreach ($it in $todo) {
        Write-Host "`n$($it.Rel)  ($(Format-Size $it.Size))" -ForegroundColor Cyan
        while ($true) {
            $url = ([string](Read-Host '  Download address (Enter = skip)')).Trim().Trim('"')
            if (-not $url) { Write-Host '  skipped'; break }
            if ($url -notmatch '^https?://') { Write-Host '  It must start with http:// or https://' -ForegroundColor Yellow; continue }
            # A Hugging Face page link (/blob/) is turned into the file link (/resolve/).
            $url = $url -replace '^(https://huggingface\.co/.+?)/blob/', '$1/resolve/'
            if ($url -ne $url.Split('?')[0] -and $url -match '\?download=true$') { $url = $url.Split('?')[0] }

            Write-Host '  checking ...' -ForegroundColor DarkGray
            $t = Test-Url $url
            $gated = $false
            if ($t.Code -eq 401 -or $t.Code -eq 403) {
                Write-Host "  The server needs a sign-in (HTTP $($t.Code)); it is saved as [sign-in]." -ForegroundColor Yellow
                $gated = $true
            } elseif ($t.Code -lt 200 -or $t.Code -ge 400) {
                Write-Host "  The address does not work (HTTP $($t.Code))." -ForegroundColor Red
                if (([string](Read-Host '  Save it anyway? [y/N]')).Trim() -notmatch '^y') { continue }
            } elseif ($null -ne $t.Size -and $t.Size -ne $it.Size) {
                Write-Host "  Size differs: online $(Format-Size $t.Size) ($($t.Size) bytes), local $(Format-Size $it.Size) ($($it.Size) bytes)." -ForegroundColor Yellow
                if (([string](Read-Host '  Save it anyway? [y/N]')).Trim() -notmatch '^y') { continue }
            } else {
                Write-Host "  OK$(if ($null -ne $t.Size) { ', same size' })" -ForegroundColor Green
            }
            $m.Files.Add([pscustomobject]@{ Rel = $it.Rel; Url = $url; Gated = $gated })
            Save-Manifest $m                # saved at once, so nothing is lost if the window is closed
            $added++
            Write-Host '  added to models.json' -ForegroundColor Green
            break
        }
    }
    Write-Host "`nFind new: $added file(s) added to models.json." -ForegroundColor Green
}

# ================================================================== main
Write-Host '=== AI model downloader ===' -ForegroundColor White
Write-Host "List: $Manifest ($((Read-Manifest).Files.Count) files)"
$default = if ($Target) { $Target } else { $PSScriptRoot }
Write-Host "`nTarget folder: $default" -ForegroundColor Cyan
$in = ([string](Read-Host 'Press Enter to use it, or type another folder')).Trim().Trim('"')
$root = [IO.Path]::GetFullPath($(if ($in) { $in } else { $default })).TrimEnd('\')
Write-Host "Target: $root"

while ($true) {
    Write-Host "`n----------------------------------------"
    Write-Host '  1  Download (skip existing, download missing, optionally replace)'
    Write-Host '  2  Find new files not in models.json and add their download address'
    Write-Host '  Q  Quit'
    $c = ([string](Read-Host 'Choose')).Trim()
    switch -Regex ($c) {
        '^1$'       { Invoke-Download $root }
        '^2$'       { Invoke-FindNew $root }
        '^(q|)$'    { return }
        default     { Write-Host 'Choose 1, 2 or Q.' -ForegroundColor Yellow }
    }
}
