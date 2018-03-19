@echo off
echo ----------------------------------------------- increment.bat script -----------------------------------------------

rem ========================================================================================
rem == This script automatically increments build number in "version.h" file.
rem == Instructions and more information:
rem == http://codeblog.vurdalakov.net/2017/04/autoincrement-build-number-in-arduino-ide.html
rem ========================================================================================

setlocal

set output=%1
echo output=%output%

set source=%2
echo source=%source%

set filename=%source%\version.h
echo filename=%filename%

md %2\backup
set sketch=%2\%3
echo sketch=%sketch%


for /f "delims=" %%x in (%filename%) do set define=%%x
echo %define%

for /f "tokens=1-3 delims= " %%a in ("%define%") do (
   set macro=%%b
   set version=%%c
)

echo macro=%macro%

set version=%version:~1,-1%
echo version=%version%

copy %2\*.bin %2\backup\*.bin_%version%



for /f "tokens=1-3 delims=. " %%a in ("%version%") do (
   set version=%%a.%%b
   set build=%%c
)

echo build=%build%

set /a build=%build%+1
echo build=%build%

set version=%version%.%build%
echo version=%version%

set line=#define %macro% "%version%"
echo %line%

echo filename=%filename%

set sketchbackup=%2\backup\%3_%version%
echo sketchbackup=%sketchbackup%

copy %sketch% %sketchbackup%
echo "backup created"

echo %line% > %filename%
type %filename%

set filename=%output%\sketch\version.h
echo filename=%filename%

echo %line% > %filename%
type %filename%

set curdir=%cd%
cd %source%

rem == Uncomment following line to commit updated version.h file to Git repository
rem == Uncomment second line to tag commit
rem git commit -am "Version %version%"
rem git tag %version%

cd %curdir%

echo ----------------------------------------------- increment.bat script -----------------------------------------------