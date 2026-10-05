@echo off
REM %~dpn0 expands %0 to a combination of Drive, Path, and Name (i.e.
REM it removes the .bat extension from this script's full path)
%PYTHON3% %~dpn0 %*
