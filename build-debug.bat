@echo off
rem Debug build of the PC port. Optional arguments: the render backends and the
rem architecture (x64 is the default, x86 the other).
rem
rem     build-debug.bat
rem     build-debug.bat GL3
rem     build-debug.bat "D3D11;GL3" x86
rem
rem Puts tssm.exe and the DLLs it needs in bin\. See tools\pcbuild.bat.
call "%~dp0tools\pcbuild.bat" Debug %1 %2
