@echo off
rem Release build of the PC port. Optional arguments: the render backends and
rem the architecture (x64 is the default, x86 the other).
rem
rem     build-release.bat
rem     build-release.bat GL3
rem     build-release.bat D3D9 x86
rem
rem Puts tssm.exe and the DLLs it needs in bin\. See tools\pcbuild.bat.
call "%~dp0tools\pcbuild.bat" Release %1 %2
