param([ValidateSet('Debug','Release')][string]$Configuration='Release')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsRoot=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsRoot) { throw 'Установите Visual Studio с компонентами Desktop development with C++ и Windows SDK.' }
$cmake=Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
if (-not (Test-Path -LiteralPath $cmake)) { $cmake=(Get-Command cmake -ErrorAction Stop).Source }
& $cmake -S $projectRoot -B (Join-Path $projectRoot 'out') -G 'Visual Studio 18 2026' -A x64
if ($LASTEXITCODE) { throw 'Ошибка конфигурации CMake.' }
& $cmake --build (Join-Path $projectRoot 'out') --config $Configuration --parallel
if ($LASTEXITCODE) { throw 'Ошибка сборки.' }
$ctest=Join-Path (Split-Path $cmake -Parent) 'ctest.exe'
& $ctest --test-dir (Join-Path $projectRoot 'out') -C $Configuration --output-on-failure
if ($LASTEXITCODE) { throw 'Тесты не прошли.' }
if ($Configuration -eq 'Release') {
    & $cmake --install (Join-Path $projectRoot 'out') --config Release --prefix (Join-Path $projectRoot 'dist')
    if ($LASTEXITCODE) { throw 'Ошибка подготовки поставки.' }
}
