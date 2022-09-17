@ECHO OFF
CALL :cp32dlls bin\win32\release
CALL :cp32dlls bin\win32\debug
CALL :cp64dlls bin\x64\release
CALL :cp64dlls bin\x64\debug
EXIT /B 0

:cp32dlls
SETLOCAL
echo Copying 32bit dependencies to %1...
PUSHD ..
copy deps\SDL2-2.0.7\lib\win32\SDL2.dll %1 1>NUL
POPD
ENDLOCAL
EXIT /B

:cp64dlls
SETLOCAL
echo Copying 64bit dependencies to %1...
PUSHD ..
copy deps\SDL2-2.0.7\lib\x64\SDL2.dll %1 1>NUL
POPD
ENDLOCAL
EXIT /B