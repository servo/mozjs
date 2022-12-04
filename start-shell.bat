@ECHO OFF

SETLOCAL ENABLEEXTENSIONS ENABLEDELAYEDEXPANSION

REM Reset some env vars.
SET CYGWIN=
SET INCLUDE=
SET LIB=
SET GITDIR=
SET MOZILLABUILD=%~dp0

REM mintty is available as an alternate terminal, but is not enabled by default due
REM to spurious newlines being added to copied text - see bug 1751500.
IF NOT DEFINED USE_MINTTY (
  SET USE_MINTTY=
)

FOR /F "tokens=* USEBACKQ" %%F IN (`where ssh 2^>NUL`) DO (
    IF NOT DEFINED EXTERNAL_TO_MOZILLABUILD_SSH_DIR (
        SET "EXTERNAL_TO_MOZILLABUILD_SSH_DIR='%%~dpF'"
    )
)

REM Start shell.
IF "%USE_MINTTY%" == "1" (
  REM Opt into "ConPTY" support, which enables usage of win32 console binaries when
  REM running from mintty
  SET MSYS=enable_pcon
  %MOZILLABUILD%msys2\msys2_shell.cmd -full-path %*
) ELSE (
  %MOZILLABUILD%msys2\msys2_shell.cmd -no-start -defterm -full-path %*
)

EXIT /B