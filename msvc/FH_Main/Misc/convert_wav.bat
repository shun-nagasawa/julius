@echo off
setlocal EnableDelayedExpansion

:: --- 設定 ---
set "REF_DIR=%~dp0ref"
set "EXPORT_DIR=%~dp0export"
set "PREFIX=Convert_"
set "FILELIST=%~dp0filelist.txt"

:: --- export フォルダ作成 ---
if not exist "%EXPORT_DIR%" (
    mkdir "%EXPORT_DIR%"
)

:: --- filelist.txt 初期化 ---
if exist "%FILELIST%" del "%FILELIST%" 2>nul

:: --- 変換ループ ---
for %%F in ("%REF_DIR%\*.wav") do (
    set "BASENAME=%%~nF"
    set "OUTFILE=%EXPORT_DIR%\!PREFIX!!BASENAME!.wav"

    echo [%date% %time%] Convert Start: %%~nxF !OUTFILE!
    :: 16-bit PCM, 16000Hz, モノラルに変換
    sox.exe "%%~fF" -b 16 -r 16000 -c 1 "!OUTFILE!"

    if errorlevel 1 (
        echo [%date% %time%] LOG: Convert Fail: %%~nxF
    ) else (
        :: filelist.txt には Misc/export 以下のパスで出力
        echo [%date% %time%] LOG: Convert Success: %%~nxF
        echo Misc/export/!PREFIX!!BASENAME!.wav>>"%FILELIST%"
    )
)

echo [%date% %time%] Complete Comvert
endlocal
