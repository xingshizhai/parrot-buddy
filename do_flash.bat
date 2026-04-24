@echo off
cd /d D:\Work\esp32\projects\parrot-buddy
call D:\Work\esp32\projects\parrot-buddy\build_esp32.ps1 -ActivateOnly
idf.py -p COM3 flash > flash_out.txt 2>&1
echo EXIT_CODE: %ERRORLEVEL%
