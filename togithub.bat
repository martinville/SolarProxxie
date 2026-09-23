@echo off
setlocal
cd /d "%~dp0"

set "TARGET=%~dp0togithub"
set "IDF_PYTHON=%~dp0.tools\espressif\python_env\idf5.4_py3.14_env\Scripts\python.exe"

echo [1/3] Building the current SolarProxxie firmware...
python tools\idf_local.py build
if errorlevel 1 (
    echo ERROR: Firmware build failed. Nothing was copied.
    exit /b 1
)

echo [2/3] Preparing the GitHub Pages and release firmware files...
"%IDF_PYTHON%" tools\prepare_pages.py
if errorlevel 1 (
    echo ERROR: Firmware packaging failed. Nothing was copied.
    exit /b 1
)

echo [3/3] Creating a clean GitHub upload folder...
if exist "%TARGET%" (
    rmdir /s /q "%TARGET%"
    if exist "%TARGET%" (
        echo ERROR: Could not remove "%TARGET%".
        exit /b 1
    )
)
mkdir "%TARGET%" || exit /b 1

for %%D in (.github docs main site tests tools web) do (
    robocopy "%%D" "%TARGET%\%%D" /E /XD __pycache__ private /XF *.pyc >nul
    if errorlevel 8 (
        echo ERROR: Failed while copying %%D.
        exit /b 1
    )
)

for %%F in (
    ".clang-format"
    ".gitignore"
    "CHANGELOG.md"
    "CMakeLists.txt"
    "GITHUB_PAGES.md"
    "LICENSE"
    "logo.svg"
    "NOTICE.md"
    "package-lock.json"
    "package.json"
    "partitions.csv"
    "README.md"
    "sdkconfig.defaults"
    "VERSION"
) do (
    copy /y "%%~F" "%TARGET%\" >nul || (
        echo ERROR: Failed while copying %%~F.
        exit /b 1
    )
)

copy /y "%~f0" "%TARGET%\" >nul || exit /b 1

set /p PROJECT_VERSION=<VERSION
echo.
echo READY
echo -----
echo Repository files:
echo   Upload the CONTENTS of "%TARGET%" to the repository root.
echo.
echo GitHub Release:
echo   Tag:   v%PROJECT_VERSION%
echo   Asset: "%TARGET%\site\firmware\SolarProxxie.bin"
echo.
echo IMPORTANT: The OTA file must be attached to a PUBLISHED GitHub Release.
echo Placing it only in the repository or GitHub Pages does not enable cloud update.
exit /b 0
