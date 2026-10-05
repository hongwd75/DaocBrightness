param([switch]$Verify, [switch]$Fullscreen)
$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio 2022 C++ 개발 도구가 필요합니다.' }
$builder = & $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
if (-not $builder) { throw 'MSBuild를 찾지 못했습니다.' }
& $builder (Join-Path $PSScriptRoot 'DaocBrightness.sln') /m /p:Configuration=Release /p:Platform=Win32 /v:minimal
if ($LASTEXITCODE -ne 0) { throw 'Release 빌드 실패' }
if ($Verify) {
    & (Join-Path $PSScriptRoot 'Release\SettingsTests.exe')
    if ($LASTEXITCODE -ne 0) { throw '밝기 설정 저장 및 복원 검증 실패' }
    & (Join-Path $PSScriptRoot 'Release\LanguageTests.exe')
    if ($LASTEXITCODE -ne 0) { throw '표시 언어 검증 실패' }
    & (Join-Path $PSScriptRoot 'Release\PayloadTests.exe') (Join-Path $PSScriptRoot 'dist\DaocBrightness.exe') (Join-Path $PSScriptRoot 'Release\DaocBrightnessHook.dll')
    if ($LASTEXITCODE -ne 0) { throw '단일 EXE 내장 모듈 검증 실패' }
    & (Join-Path $PSScriptRoot 'Release\ProcessScanTests.exe')
    if ($LASTEXITCODE -ne 0) { throw '프로세스 감지 검증 실패' }
    $tester = Join-Path $PSScriptRoot 'Release\RenderTests.exe'
    $cases = @(@(), @('--hook-dx9'), @('--hook-dx10'))
    if ($Fullscreen) { $cases += @(@('--fullscreen'), @('--hook-dx9','--fullscreen'), @('--hook-dx10','--fullscreen')) }
    foreach ($case in $cases) {
        & $tester @case
        if ($LASTEXITCODE -ne 0) { throw ('검증 실패: ' + ($case -join ' ')) }
    }
}
