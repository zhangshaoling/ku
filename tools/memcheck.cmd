@echo off
REM Simple M4 leak loop: run the C VM N times and report zero leakage.
setlocal EnableDelayedExpansion
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1
cd /d "%~dp0\.."
if not exist dao\dao_core.exe (
    echo dao_core.exe not found; building...
    cl.exe /c /W3 /O2 /utf-8 /DSQLITE_ENABLE_FTS5 dao\dao_core.c /Fo:dao_core.obj
    cl.exe /c /W0 /O2 /utf-8 /DSQLITE_ENABLE_FTS5 vendor\sqlite3.c /Fo:sqlite3.obj
    link.exe dao_core.obj sqlite3.obj kernel32.lib user32.lib ws2_32.lib rpcrt4.lib /OUT:dao\dao_core.exe
    del /q dao_core.obj sqlite3.obj
)
set DAO_GC_STATS=1
set FAIL=0
for /L %%i in (1,1,5) do (
    echo --- memcheck iteration %%i ---
    for /f "tokens=*" %%a in ('dao\dao_core.exe --bootstrap demos\frontend_bootstrap.kub.json demos\golden_path.ku 2^>^&1 ^| findstr /i "leak=0"') do (
        echo %%a
    )
    dao\dao_core.exe demos\golden_path.ku 2>&1 | findstr /r /c:"leak=[1-9]" && (
        echo FAIL: leak detected
        set FAIL=1
    )
)
if "%FAIL%"=="1" (
    echo MEMCHECK_FAILED
    exit /b 1
)
echo MEMCHECK_OK
exit /b 0