@echo off

setlocal EnableDelayedExpansion



ver | find "Windows" >nul 2>nul

if errorlevel 1 (

    echo This script must be run on Windows.

    exit /b 1

)



set "SCRIPT_DIR=%~dp0"

pushd "%SCRIPT_DIR%" >nul



set "DOWNLOAD_DIR=%SCRIPT_DIR%Tools\build-tools"

if not exist "%DOWNLOAD_DIR%" (

    mkdir "%DOWNLOAD_DIR%"

)



set "VSWHERE_SYSTEM=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"

if exist "%VSWHERE_SYSTEM%" (

    set "VSWHERE=%VSWHERE_SYSTEM%"

) else (

    set "VSWHERE=%DOWNLOAD_DIR%\vswhere.exe"

    if not exist "%VSWHERE%" (

        echo Downloading vswhere.exe...

        powershell -NoProfile -ExecutionPolicy Bypass -Command "Invoke-WebRequest -UseBasicParsing -Uri 'https://github.com/microsoft/vswhere/releases/latest/download/vswhere.exe' -OutFile '%VSWHERE%'" || goto :fail

    )

)



set "NUGET=%DOWNLOAD_DIR%\nuget.exe"

if not exist "%NUGET%" (

    echo Downloading nuget.exe...

    powershell -NoProfile -ExecutionPolicy Bypass -Command "Invoke-WebRequest -UseBasicParsing -Uri 'https://dist.nuget.org/win-x86-commandline/latest/nuget.exe' -OutFile '%NUGET%'" || goto :fail

)



where dotnet >nul 2>nul

if errorlevel 1 (

    echo The .NET SDK (6.0 or newer) is required but was not found in PATH.

    goto :fail

)



set "MSBUILD="

for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.Component.MSBuild -property installationPath`) do (

    set "VSINSTALL=%%i"

)

if defined VSINSTALL (

    set "MSBUILD=%VSINSTALL%\MSBuild\Current\Bin\MSBuild.exe"

)

if not defined MSBUILD if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\BuildTools\MSBuild\Current\Bin\MSBuild.exe" (

    set "MSBUILD=%ProgramFiles(x86)%\Microsoft Visual Studio\2019\BuildTools\MSBuild\Current\Bin\MSBuild.exe"

)

if not defined MSBUILD (

    for /f "delims=" %%i in ('where msbuild') do (

        set "MSBUILD=%%i"

        goto :have_msbuild

    )

)

:have_msbuild

if not defined MSBUILD if exist "%VSINSTALL%\MSBuild\Current\Bin\MSBuild.exe" (

    set "MSBUILD=%VSINSTALL%\MSBuild\Current\Bin\MSBuild.exe"

)

set "VSBT_INSTALL=%DOWNLOAD_DIR%\vsbuildtools"

set "VSBT_MSBUILD=%VSBT_INSTALL%\MSBuild\Current\Bin\MSBuild.exe"

if not defined MSBUILD if exist "%VSBT_MSBUILD%" (

    set "MSBUILD=%VSBT_MSBUILD%"

)

if not defined MSBUILD (

    set "VSBT_BOOTSTRAPPER=%DOWNLOAD_DIR%\vs_BuildTools.exe"

    if not exist "%VSBT_BOOTSTRAPPER%" (

        echo Downloading Visual Studio Build Tools bootstrapper...

        powershell -NoProfile -ExecutionPolicy Bypass -Command "Invoke-WebRequest -UseBasicParsing -Uri 'https://aka.ms/vs/17/release/vs_BuildTools.exe' -OutFile '%VSBT_BOOTSTRAPPER%'" || goto :fail

    )

    echo Installing required Visual Studio Build Tools workload. This may take several minutes...

    "%VSBT_BOOTSTRAPPER%" --quiet --wait --norestart --nocache --installPath "%VSBT_INSTALL%" --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended || goto :fail

    if exist "%VSBT_MSBUILD%" (

        set "MSBUILD=%VSBT_MSBUILD%"

    )

)

if not defined MSBUILD (

    echo Could not locate or install MSBuild. Please install Visual Studio Build Tools manually.

    goto :fail

)



echo Using MSBuild at "%MSBUILD%"



"%NUGET%" restore WhisperCpp.sln || goto :fail



"%MSBUILD%" ComputeShaders\ComputeShaders.vcxproj -m -p:Configuration=Release -p:Platform=x64 || goto :fail

"%MSBUILD%" ThirdParty\RNNoise\RNNoise.vcxproj -m -p:Configuration=Release -p:Platform=x64 || goto :fail



dotnet restore Tools\CompressShaders\CompressShaders.csproj || goto :fail

dotnet build Tools\CompressShaders\CompressShaders.csproj -c Release || goto :fail

dotnet run --no-build --project Tools\CompressShaders\CompressShaders.csproj -c Release || goto :fail



"%MSBUILD%" WhisperCpp.sln -m -t:WhisperDesktop -p:Configuration=Release -p:Platform=x64 || goto :fail

"%MSBUILD%" WhisperPS\WhisperPS.csproj -m -p:Configuration=Release || goto :fail



set "OUTPUT_ROOT=%SCRIPT_DIR%Release"

set "CLIENT_DIR=%OUTPUT_ROOT%\NativeClient"

if exist "%CLIENT_DIR%" rmdir /s /q "%CLIENT_DIR%"

mkdir "%CLIENT_DIR%" || goto :fail



copy /y x64\Release\WhisperDesktop.exe "%CLIENT_DIR%" >nul || goto :fail

copy /y x64\Release\Whisper.dll "%CLIENT_DIR%" >nul || goto :fail

copy /y Whisper\Utils\LZ4\LICENSE "%CLIENT_DIR%\lz4.txt" >nul || goto :fail



set "ZIP_PATH=%OUTPUT_ROOT%\WhisperDesktop.zip"

if exist "%ZIP_PATH%" del /q "%ZIP_PATH%"

powershell -NoProfile -ExecutionPolicy Bypass -Command "Compress-Archive -Path '%CLIENT_DIR%\*' -DestinationPath '%ZIP_PATH%' -Force" || goto :fail



echo.

echo Native client Release build complete.

echo Output folder: %CLIENT_DIR%

echo Archive: %ZIP_PATH%



popd >nul

exit /b 0



:fail

popd >nul

echo Build failed.

exit /b 1

