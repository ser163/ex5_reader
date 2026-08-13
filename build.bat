@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not exist build mkdir build
if not exist bin mkdir bin
if not exist bin\plugins mkdir bin\plugins
echo === [1/3] CLI ex5reader.exe (x64) ===
cl /nologo /std:c++17 /EHsc /O2 /utf-8 /I third_party src\main.cpp src\ex5book.cpp third_party\miniz.c third_party\miniz_tdef.c third_party\miniz_tinfl.c third_party\miniz_zip.c third_party\sqlite3.c /Fo:build\ /Fe:bin\ex5reader.exe /link bcrypt.lib shell32.lib
if errorlevel 1 exit /b 1
echo === [2/3] GUI ex5reader_gui.exe (x64) ===
rc /nologo /fo build\app.res src\app.rc
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /O2 /utf-8 /DUNICODE /D_UNICODE /I third_party src\gui_main.cpp src\gui_reader.cpp src\gui_panel.cpp src\gui_dialogs.cpp src\gui_style.cpp src\gui_plugin.cpp src\ex5book.cpp third_party\miniz.c third_party\miniz_tdef.c third_party\miniz_tinfl.c third_party\miniz_zip.c third_party\sqlite3.c /Fo:build\ /Fe:bin\ex5reader_gui.exe /link /SUBSYSTEM:WINDOWS build\app.res user32.lib gdi32.lib gdiplus.lib ole32.lib shell32.lib comdlg32.lib comctl32.lib bcrypt.lib advapi32.lib
if errorlevel 1 exit /b 1
echo === [3/3] Plugin demo bin\plugins\demo_stats.dll ===
cl /nologo /std:c++17 /EHsc /O2 /utf-8 /LD plugins\demo\plugin_demo.cpp /Fo:build\ /Fe:bin\plugins\demo_stats.dll /link user32.lib
if errorlevel 1 exit /b 1
echo === Build OK ===
