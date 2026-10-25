@echo off
setlocal
set "ROOT=%~dp0.."
set "OUT=%ROOT%\build-research\survivor-prep-test"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%i"
if not defined VSROOT exit /b 1
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
if not exist "%OUT%" mkdir "%OUT%" || exit /b 1
pushd "%OUT%" || exit /b 1
cl /nologo /std:c++17 /EHsc /O2 /W3 /I"%ROOT%\core" /I"%ROOT%\core\deps\glm" /Fesurvivor_prep_test.exe "%ROOT%\survivor2-vr\survivor_prep_test.cpp" "%ROOT%\core\rend\vr\hands_build.cpp" >build.log 2>&1
if errorlevel 1 (type build.log & popd & exit /b 1)
survivor_prep_test.exe
set "RESULT=%ERRORLEVEL%"
popd
exit /b %RESULT%

