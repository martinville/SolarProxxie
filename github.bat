@echo off
setlocal

rem Build a clean GitHub upload folder beside this script.
cd /d "%~dp0"
set "TARGET=%~dp0togithub"

if exist "%TARGET%" (
    echo Removing the previous togithub folder...
    rmdir /s /q "%TARGET%"
    if exist "%TARGET%" (
        echo ERROR: Could not remove "%TARGET%".
        exit /b 1
    )
)

mkdir "%TARGET%" || exit /b 1

echo Copying project source and GitHub Pages files...
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

echo.
echo GitHub upload folder is ready:
echo %TARGET%
echo.
echo Upload the CONTENTS of togithub to the root of the SolarProxxie repository.
exit /b 0
