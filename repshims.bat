@echo off

if "%SCOOP%"=="" set SCOOP=%USERPROFILE%\scoop

for %%x in (%SCOOP%\shims\*.exe) do (
  echo Replacing %%x by new shim.
  copy /B shim.exe %%x >NUL
)
