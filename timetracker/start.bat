@echo off
rem Runs the app from this folder (e.g. on a shared drive) using PHP's built-in server.
rem Only this computer can reach it. Needs PHP 8.1+ with pdo_sqlite enabled.
rem If PHP is not installed, unzip PHP for Windows into a "php" folder next to this file.
pushd "%~dp0"
set PORT=8765
echo Timesheets running at http://127.0.0.1:%PORT%/  (close this window to stop)
start "" http://127.0.0.1:%PORT%/
if exist "%~dp0php\php.exe" (
    "%~dp0php\php.exe" -n -d extension_dir="%~dp0php\ext" -d extension=pdo_sqlite -S 127.0.0.1:%PORT% router.php
) else (
    php -S 127.0.0.1:%PORT% router.php
)
