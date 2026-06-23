@echo off
set BinRoot=C:\Qt\6.10.3\msvc2022_64\bin\windeployqt.exe
cmake -B..\build-dev -S ..\ -A x64
cmake --build ..\build-dev --config Release
%BinRoot% ..\build-dev\bin\Release\relayFileGui.exe
pause