@echo off
setlocal enabledelayedexpansion

set "INSTALL_DIR=%ProgramFiles%\CxxIME"
set "TSF_DLL=cxxime_tsf.dll"
set "SERVER_EXE=cxxime-server.exe"
set "PASS=0"
set "FAIL=0"
set "WARN=0"

echo(
echo  ====================================================
echo  CxxIME Installation Verification
echo  ====================================================
echo(

rem -- 1. Check files --
echo  [1/7] Checking installed files in %INSTALL_DIR%\bin\
echo         Expected: %TSF_DLL%, %SERVER_EXE%
echo(

if exist "%INSTALL_DIR%\bin\%TSF_DLL%" (
    echo         [PASS] %TSF_DLL% found in %INSTALL_DIR%\bin\
    set /a PASS+=1
) else (
    echo         [FAIL] %TSF_DLL% NOT found in %INSTALL_DIR%\bin\
    set /a FAIL+=1
)

if exist "%INSTALL_DIR%\bin\%SERVER_EXE%" (
    echo         [PASS] %SERVER_EXE% found in %INSTALL_DIR%\bin\
    set /a PASS+=1
) else (
    echo         [FAIL] %SERVER_EXE% NOT found in %INSTALL_DIR%\bin\
    set /a FAIL+=1
)

if exist "%INSTALL_DIR%\data\pinyin.dict.db" (
    echo         [PASS] pinyin.dict.db found in %INSTALL_DIR%\data\
    set /a PASS+=1
) else (
    echo         [WARN] pinyin.dict.db NOT found - IME requires dictionary to work
    set /a WARN+=1
)

if exist "%INSTALL_DIR%\data\default.json" (
    echo         [PASS] default.json found in %INSTALL_DIR%\data\
    set /a PASS+=1
) else (
    echo         [WARN] default.json NOT found
    set /a WARN+=1
)
echo(

rem -- 2. Check server process --
echo  [2/7] Checking server process
echo         Expected: %SERVER_EXE% running in Task Manager
echo(

tasklist /fi "imagename eq %SERVER_EXE%" 2>nul | find /i "%SERVER_EXE%" >nul 2>&1
if not errorlevel 1 (
    echo         [PASS] %SERVER_EXE% is running
    set /a PASS+=1
) else (
    echo         [FAIL] %SERVER_EXE% is NOT running
    echo         Hint: start manually with "%INSTALL_DIR%\bin\%SERVER_EXE%"
    set /a FAIL+=1
)
echo(

rem -- 3. Check CLSID registration --
echo  [3/7] Checking CLSID registry
echo         Key: HKCR\CLSID\{B7E1E5A2-8F3D-4A9C-B6E7-2C4D8F1A3B5E}
echo(

set "CLSID_KEY=CLSID\{B7E1E5A2-8F3D-4A9C-B6E7-2C4D8F1A3B5E}"
reg query "HKCR\%CLSID_KEY%" >nul 2>&1
if not errorlevel 1 (
    echo         [PASS] CLSID registered
    set /a PASS+=1
) else (
    echo         [FAIL] CLSID NOT registered
    set /a FAIL+=1
)

reg query "HKCR\%CLSID_KEY%\InprocServer32" >nul 2>&1
if not errorlevel 1 (
    echo         [PASS] InprocServer32 registered
    set /a PASS+=1
) else (
    echo         [FAIL] InprocServer32 NOT registered
    set /a FAIL+=1
)
echo(

rem -- 4. Check TIP registration --
echo  [4/7] Checking TIP registry
echo         Key: HKLM\SOFTWARE\Microsoft\CTF\TIP\{B7E1E5A2-...}
echo(

set "TIP_KEY=HKLM\SOFTWARE\Microsoft\CTF\TIP\{B7E1E5A2-8F3D-4A9C-B6E7-2C4D8F1A3B5E}"
reg query "%TIP_KEY%" >nul 2>&1
if not errorlevel 1 (
    echo         [PASS] TIP entry exists
    set /a PASS+=1
) else (
    echo         [FAIL] TIP entry NOT found
    set /a FAIL+=1
)

set "LP_KEY=%TIP_KEY%\LanguageProfile\0x00000804\{D4F2C7A1-9E6B-4D8A-A3F5-1B2C3D4E5F60}"
reg query "%LP_KEY%" >nul 2>&1
if not errorlevel 1 (
    echo         [PASS] LanguageProfile registered for zh-CN
    set /a PASS+=1
) else (
    echo         [FAIL] LanguageProfile NOT registered for zh-CN
    set /a FAIL+=1
)
echo(

rem -- 5. Check categories --
echo  [5/7] Checking TSF category registration
echo(

set "CAT_KEY=%TIP_KEY%\Category"

rem First, dump what actually exists under Category
echo         Enumerating: %CAT_KEY%
set "CAT_COUNT=0"
for /f "tokens=*" %%k in ('reg query "%CAT_KEY%" /s 2^>nul') do (
    set /a CAT_COUNT+=1
    echo           %%k
)
if !CAT_COUNT! EQU 0 (
    echo         No subkeys found under Category
)
echo(

set "CAT_OK=0"
set "TIP_CLSID={B7E1E5A2-8F3D-4A9C-B6E7-2C4D8F1A3B5E}"

rem RegisterCategory writes to Category\Category\{CAT_GUID}\{TIP_CLSID}
reg query "%CAT_KEY%\Category\{534C48C1-0607-4098-A521-4FC899C73E90}\%TIP_CLSID%" >nul 2>&1
if not errorlevel 1 (
    echo         [PASS] GUID_TFCAT_TIP_KEYBOARD
    set /a CAT_OK+=1
) else (
    echo         [FAIL] GUID_TFCAT_TIP_KEYBOARD missing
)

reg query "%CAT_KEY%\Category\{364215D9-75BC-11D7-A6EF-00065B84435C}\%TIP_CLSID%" >nul 2>&1
if not errorlevel 1 (
    echo         [PASS] GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER
    set /a CAT_OK+=1
) else (
    echo         [FAIL] GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER missing
)

reg query "%CAT_KEY%\Category\{7C6A82AE-B0D7-4F14-A745-14F28B009D61}\%TIP_CLSID%" >nul 2>&1
if not errorlevel 1 (
    echo         [PASS] GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT
    set /a CAT_OK+=1
) else (
    echo         [FAIL] GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT missing
)

if !CAT_OK! GEQ 2 (
    echo         [PASS] Key categories registered
    set /a PASS+=1
) else (
    echo         [FAIL] Categories missing - IME may not appear in input list
    set /a FAIL+=1
)
echo(

rem Also dump the full TIP key tree for diagnosis
echo         Full TIP key tree:
for /f "tokens=*" %%k in ('reg query "%TIP_KEY%" /s 2^>nul') do (
    echo           %%k
)
echo(

rem -- 6. Check auto-start --
echo  [6/7] Checking auto-start registry
echo         Key: HKLM\...\Run\CxxIMEServer
echo(

reg query "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Run" /v "CxxIMEServer" >nul 2>&1
if not errorlevel 1 (
    echo         [PASS] Auto-start configured
    set /a PASS+=1
) else (
    echo         [WARN] Auto-start not configured
    set /a WARN+=1
)
echo(

rem -- 7. Check registration log --
echo  [7/7] Checking registration log (%TEMP%\cxxime_register.log)
echo(

set "REG_LOG=%TEMP%\cxxime_register.log"
if exist "%REG_LOG%" (
    echo         Registration log contents:
    type "%REG_LOG%"
) else (
    echo         No registration log found - regsvr32 may not have run
)
echo(

rem -- Summary --
echo  ====================================================
echo  Results:  PASS=!PASS!  FAIL=!FAIL!  WARN=!WARN!
echo  ====================================================
echo(

if !FAIL! GTR 0 (
    echo  Installation has !FAIL! failure(s^).
    echo  Run install.bat as administrator, then run this script again.
) else (
    echo  All critical checks passed.
    echo(
    echo  Next steps:
    echo    1. Log off and log back in (or restart^)
    echo    2. Settings - Time and Language - Language - Chinese - Options
    echo    3. Add keyboard - Select "CxxIME"
    echo    4. Press Ctrl+Space or Win+Space to switch input method
)

echo(
pause
endlocal
