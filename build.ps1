$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildDir = Join-Path $repoRoot 'build'

if (-not (Get-Command g++ -ErrorAction SilentlyContinue)) {
    throw 'A 64-bit MinGW-w64 g++ toolchain is required.'
}

cmake -S $repoRoot -B $buildDir -G 'MinGW Makefiles'
cmake --build $buildDir --config Release
