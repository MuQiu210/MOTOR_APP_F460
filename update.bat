@echo off
setlocal

set "PORT=%~1"
set "BAUDRATE=%~2"
set "TIMEOUT=%~3"
set "ID=%~4"
set "IMAGE_BIN=%~5"

if "%BAUDRATE%"=="" set "BAUDRATE=921600"
if "%TIMEOUT%"=="" set "TIMEOUT=-1"
if "%ID%"=="" set "ID=1"

set "OTA_SCRIPT=%~dp0modbus_ota.py"
if "%IMAGE_BIN%"=="" set "IMAGE_BIN=%~dp0build\F460\MOTOR_APP_F460.bin"
if not exist "%IMAGE_BIN%" set "IMAGE_BIN=%~dp0build\F460\MOTOR_APP_F460.bin"

if not exist "%OTA_SCRIPT%" (
  echo OTA script not found: %OTA_SCRIPT%
  exit /b 1
)

if not exist "%IMAGE_BIN%" (
  echo BIN not found: %IMAGE_BIN%
  exit /b 1
)

if "%PORT%"=="" (
  python "%OTA_SCRIPT%" --firmware "%IMAGE_BIN%" --bps %BAUDRATE% --id %ID% --timeout %TIMEOUT%
) else (
  python "%OTA_SCRIPT%" --port "%PORT%" --firmware "%IMAGE_BIN%" --bps %BAUDRATE% --id %ID% --timeout %TIMEOUT%
)
