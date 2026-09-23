@echo off
REM Standalone ModService test build (no Qt required).
REM Run from the BB_Launcher checkout root in a VS 2022 x64 environment, e.g.:
REM   "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
REM   tests_cpp\build_standalone.bat
setlocal
set SRC=%~dp0..
cl /nologo /std:c++20 /EHsc /W3 /Fe%~dp0modservice_test.exe %~dp0modservice_test.cpp %SRC%\modules\ModService.cpp
if errorlevel 1 exit /b 1
%~dp0modservice_test.exe
