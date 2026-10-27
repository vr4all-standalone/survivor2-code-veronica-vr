$ErrorActionPreference='Stop'
$repo=(Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$out=Join-Path $repo 'build-research/survivor-service-test'
New-Item -ItemType Directory -Path $out -Force|Out-Null
$source=[IO.File]::ReadAllText((Join-Path $repo 'core/rend/vr/survivor_native.cpp'))
$start=$source.IndexOf('bool handleTrap(');if($start -lt 0){throw 'Missing production trap handler'}
$begin=$source.IndexOf('{',$start);$depth=1;$end=$begin+1
while($depth -gt 0 -and $end -lt $source.Length){if($source[$end] -eq '{'){$depth++};if($source[$end] -eq '}'){$depth--};$end++}
[IO.File]::WriteAllText((Join-Path $out 'survivor_service_under_test.inc'),$source.Substring($start,$end-$start))
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$vcvars=Join-Path $vs 'VC/Auxiliary/Build/vcvars64.bat'
$batch=Join-Path $out 'build.cmd'
@"
@echo off
call "$vcvars" >nul || exit /b 1
cd /d "$out" || exit /b 1
cl /nologo /std:c++17 /EHsc /O2 /W3 /I"$repo/core" /I"$out" /Fesurvivor_service_test.exe "$PSScriptRoot/survivor_service_test.cpp" >build.log 2>&1
"@ | Set-Content -LiteralPath $batch -Encoding ascii
& $env:ComSpec /d /c $batch
if($LASTEXITCODE -ne 0){Get-Content -LiteralPath (Join-Path $out 'build.log');throw 'Service return test compilation failed'}
& (Join-Path $out 'survivor_service_test.exe')
exit $LASTEXITCODE
