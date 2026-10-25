param([string]$Repo=(Split-Path -Parent $PSScriptRoot))
$ErrorActionPreference='Stop'
$out=Join-Path $Repo 'build-win/vr4all-ui-tests'
New-Item -ItemType Directory -Path $out -Force | Out-Null
$source=Get-Content -Raw -LiteralPath (Join-Path $Repo 'core/rend/vr/xr_host.cpp')
$gate=[regex]::Match($source,'if \(([^\r\n]+)\)\r?\n\s*hands::prepPresented\(\);')
if(-not $gate.Success){throw 'Missing actual preparation acknowledgement'}
$inputGate=[regex]::Match($source,'if \(([^\r\n]+)\)\r?\n\s*updateLightgun\(frameState.predictedDisplayTime, located\);')
$inputCondition=if($inputGate.Success){$inputGate.Groups[1].Value}else{'true'}
$cpp=@"
#include "vr4all_splash.h"
#include "survivor_prep.h"
#include <cassert>
#include <iostream>
#undef assert
#define assert(condition) do { if(!(condition)) { std::cerr << "FAIL " << #condition << "\n"; return 1; } } while(false)
int main() {
 bool submitted=true,render=true,ok=true,focused=true,showPanel=true,menuOpen=false,preparing=true;
 bool preparationActionsSampled=true,splashVisible=true;
 auto acknowledge=[&] {return $($gate.Groups[1].Value);};
 auto input=[&] {return $inputCondition;};
 vr::xr::LaunchSplash splash;
 vr::hands::SurvivorPreparation prep;
 prep.begin(true,{},true);
 int extracted=0;
 for(int frame=0;frame<300;++frame) {
  splashVisible=splash.active();
  if(acknowledge())prep.presented();
  prep.tick(false,[&](size_t,std::string&){++extracted;return true;});
  assert(extracted==0 && "Weapon preparation must wait for its own visible panel");
  assert(!input() && "VR input must wait until after the splash");
  splash.presented(std::chrono::milliseconds(10));
 }
 splashVisible=splash.active();
 assert(!splashVisible && input());
 assert(acknowledge());
 focused=false;assert(!acknowledge());focused=true;
 preparationActionsSampled=false;assert(!acknowledge());preparationActionsSampled=true;
 for(int frame=0;frame<9;++frame){if(acknowledge())prep.presented();prep.tick(false,[&](size_t,std::string&){++extracted;return true;});}
 assert(extracted==8 && prep.phase==vr::hands::SurvivorPreparation::Phase::Ready);
 std::cout << "PASS Survivor host gates: no preparation or input during splash; focused sampled preparation afterward\n";
}
"@
[IO.File]::WriteAllText((Join-Path $out 'survivor-splash-gate.cpp'),$cpp,[Text.UTF8Encoding]::new($false))
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs=& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$vcvars=Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
$batch=Join-Path $out 'compile.cmd'
@"
@echo off
call "$vcvars" >nul || exit /b 1
cl /nologo /EHsc /std:c++17 /I"$Repo\core\rend\vr" "$out\survivor-splash-gate.cpp" /Fo"$out\gate.obj" /Fe"$out\gate.exe" || exit /b 1
"$out\gate.exe"
"@ | Set-Content -LiteralPath $batch -Encoding ascii
& $env:ComSpec /d /c $batch
exit $LASTEXITCODE
