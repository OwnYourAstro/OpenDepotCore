# SPDX-FileCopyrightText: 2026 Sarah Rose
# SPDX-License-Identifier: MIT

param(
    [string]$LegacyRoot = $env:LEGACY_OPENDEPOT
)

$ErrorActionPreference = 'Stop'
if (-not $LegacyRoot) { throw 'Set LEGACY_OPENDEPOT to the legacy OpenDepot checkout.' }

$cli = Join-Path $LegacyRoot 'build\vs\Release\opendepot-cli.exe'
if (-not (Test-Path -LiteralPath $cli)) { throw "Missing legacy CLI: $cli" }

$root = Split-Path -Parent $PSScriptRoot
$fixtures = Join-Path $root 'fixtures'
$expected = Join-Path $root 'expected'
$stage = Join-Path $root '_stage'
New-Item -ItemType Directory -Force -Path $fixtures, $expected, $stage | Out-Null

$utf8 = New-Object System.Text.UTF8Encoding($false)
function Save([string]$Name, [string]$Text) {
    [System.IO.File]::WriteAllText((Join-Path $expected $Name), ($Text -replace "`r`n", "`n"), $utf8)
}
function Invoke-Cli([string[]]$CliArgs) { (& $cli @CliArgs | Out-String) }

& $cli --create-new (Join-Path $stage 'xts2500-800.cpg') H46UCF9PW6BN TEST000001 | Out-Null
& $cli --create-new (Join-Path $stage 'xts5000-800.cpg') H18UCC9PW5AN TEST000002 | Out-Null
& $cli --create-new (Join-Path $stage 'mt1500-800.cpg') H67UCC9PW5AN TEST000003 | Out-Null

& $cli --set-zone-name (Join-Path $stage 'xts2500-800.cpg') (Join-Path $stage 'xts2500-edited.cpg') 1 'CORE TEST' | Out-Null
& $cli --set-channel-name (Join-Path $stage 'xts2500-edited.cpg') (Join-Path $stage 'xts2500-edited.cpg') 1 1 'CH TEST' | Out-Null
& $cli --set-channel-freq (Join-Path $stage 'xts2500-edited.cpg') (Join-Path $stage 'xts2500-edited.cpg') 1 1 851.0125 806.0125 | Out-Null

$names = @('xts2500-800', 'xts5000-800', 'mt1500-800', 'xts2500-edited')
foreach ($name in $names) {
    $src = Join-Path $stage "$name.cpg"
    Copy-Item -LiteralPath $src -Destination (Join-Path $fixtures "$name.cpg") -Force
    Save "$name.verify.txt"    (Invoke-Cli @('--verify', $src))
    Save "$name.records.txt"   (Invoke-Cli @('--records', $src))
    Save "$name.zones.txt"     (Invoke-Cli @('--zones', $src))
    Save "$name.report.txt"    (Invoke-Cli @('--report', $src))
    Save "$name.report.json"   (Invoke-Cli @('--report', $src, '--json'))
    Save "$name.flashcode.txt" (Invoke-Cli @('--flashcode', $src))
    Save "$name.dump.txt"      (Invoke-Cli @('--dump', $src))
    Save "$name.doctor.txt"    (Invoke-Cli @('--doctor', $src))
}

$xts2500 = Join-Path $stage 'xts2500-800.cpg'
$xts5000 = Join-Path $stage 'xts5000-800.cpg'
Save 'diff-xts2500-xts5000.txt'  (Invoke-Cli @('--diff', $xts2500, $xts5000))
Save 'diff-xts2500-xts5000.json' (Invoke-Cli @('--diff', $xts2500, $xts5000, '--json'))
Save 'models.txt'                (Invoke-Cli @('--models'))

Remove-Item -LiteralPath $stage -Recurse -Force
& (Join-Path $PSScriptRoot 'make_manifest.ps1')
