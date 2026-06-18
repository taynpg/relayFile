@echo off
set BinRoot=C:\Qt\6.10.3\msvc2022_64\bin\windeployqt.exe
cmake -B..\build-default -S ..\ -A x64 -DRELEASE_MARK=ON
cmake --build ..\build-default --config Release
%BinRoot% ..\build-default\bin\Release\relayFileGui.exe
pause