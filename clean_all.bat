@echo off
title Neverlose.cc - Clean All

echo ========================================
echo  Cleaning all build outputs
echo ========================================

echo Removing build directories...
rmdir /s /q build 2>nul

echo Removing output directories...
rmdir /s /q outputs 2>nul

echo Removing temporary files...
del /s /q *.exp *.lib *.pdb *.ilk *.obj *.log 2>nul

echo.
echo Clean complete!
pause
