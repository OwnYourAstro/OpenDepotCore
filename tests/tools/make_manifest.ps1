# SPDX-FileCopyrightText: 2026 Sarah Rose
# SPDX-License-Identifier: MIT

param(
    [string]$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
)

$ErrorActionPreference = 'Stop'
$utf8 = New-Object System.Text.UTF8Encoding($false)

function Get-Digest([string]$Path) {
    (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

$fixtureDir  = Join-Path $Root 'fixtures'
$expectedDir = Join-Path $Root 'expected'

$fixtures = Get-ChildItem -LiteralPath $fixtureDir -Filter *.cpg | Sort-Object Name
$rows = foreach ($f in $fixtures) {
    $base = $f.BaseName
    $exp = Get-ChildItem -LiteralPath $expectedDir -Filter "$base.*" | Sort-Object Name
    [ordered]@{
        file   = $f.Name
        bytes  = $f.Length
        sha256 = Get-Digest $f.FullName
        expected = @(
            foreach ($e in $exp) {
                [ordered]@{
                    file   = $e.Name
                    bytes  = $e.Length
                    sha256 = Get-Digest $e.FullName
                }
            }
        )
    }
}

$shared = foreach ($name in @('diff-xts2500-xts5000.txt', 'diff-xts2500-xts5000.json', 'models.txt')) {
    $p = Join-Path $expectedDir $name
    if (Test-Path -LiteralPath $p) {
        [ordered]@{
            file   = $name
            bytes  = (Get-Item -LiteralPath $p).Length
            sha256 = Get-Digest $p
        }
    }
}

$manifest = [ordered]@{
    schema    = 1
    generator = 'opendepot-cli m0-baseline: --create-new plus zone/channel edits'
    encoding  = 'utf-8, LF'
    fixtures  = @($rows)
    shared    = @($shared)
}

$outPath = Join-Path $fixtureDir 'MANIFEST.json'
$json = ($manifest | ConvertTo-Json -Depth 8) -replace "`r`n", "`n"
[System.IO.File]::WriteAllText($outPath, $json, $utf8)
Write-Output "wrote $outPath"
