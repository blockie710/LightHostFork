@echo off  
call "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"  
cd c:\Users\bhogl\source\repos\blockie710\LightHostFork  
msbuild NovaHost.sln /p:Configuration=Release /p:Platform=x64 
