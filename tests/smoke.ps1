$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$compiler = (Get-Command g++).Source
$out = Join-Path $root 'build\scanner-smoke.exe'
& $compiler -std=c++17 -O2 -Wall -Wextra -Wno-unused-function `
    (Join-Path $PSScriptRoot 'scanner_smoke.cpp') -o $out -lws2_32 -lcomctl32
if ($LASTEXITCODE -ne 0) { throw 'Could not compile the smoke harness.' }
& $out
if ($LASTEXITCODE -ne 0) { throw 'Scanner smoke check failed.' }
