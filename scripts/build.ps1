$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$Build = Join-Path $Root "build"
cmake -S $Root -B $Build -DCMAKE_BUILD_TYPE=Release -DRATES_BUILD_TESTS=ON
cmake --build $Build --config Release --parallel
ctest --test-dir $Build -C Release --output-on-failure
Write-Host "Built rates_cli in $Build"
