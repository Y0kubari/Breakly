# Сборка

## Инструменты

Windows 11 x64; Visual Studio 2026 с «Разработка классических приложений на C++», MSVC x64, Windows SDK 10.0.26100 или новее, CMake 4.3 (поддержка генератора VS 2026). В текущем окружении установлены MSVC 19.51.36257 и SDK 10.0.26100.0. Сеть во время сборки не нужна.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Configuration Debug
```

Скрипт находит Visual Studio через `vswhere`, использует её CMake, собирает и запускает CTest. Release копируется в `dist\Breakly.exe`; Debug находится в `out\Debug\Breakly.exe`. PDB и тесты остаются в `out`, в поставку не включаются.

MSVC Runtime скомпонован статически (`/MT`, Debug `/MTd`); необходимы только системные DLL Windows. Программа запускается без установленной Visual Studio, VC Redistributable или .NET. PE содержит манифест PerMonitorV2 и `asInvoker`; включены ASLR, DEP и Control Flow Guard. Release использует оптимизацию компоновки и удаление неиспользуемого кода. Никаких скачиваемых зависимостей CMake нет.

Для другого генератора вызовите CMake самостоятельно с отдельной выходной папкой:

```powershell
cmake -S . -B out-other -G "Visual Studio 17 2022" -A x64
cmake --build out-other --config Release --parallel
ctest --test-dir out-other -C Release --output-on-failure
cmake --install out-other --config Release --prefix dist-other
```

## Установка без администратора

Можно просто перенести `dist\Breakly.exe` в постоянную папку. Не перемещайте программу после включения автозапуска: запись содержит абсолютный путь; после переноса включите автозапуск заново. Подписанный установщик и цифровая подпись в эту поставку не входят.

Служебные параметры:

```powershell
.\dist\Breakly.exe --background
.\dist\Breakly.exe --background --diagnostics --data-dir "D:\Breakly-test-profile"
```

`--data-dir` изолирует настройки, статистику и mutex тестовой копии; элементы включения автозапуска в таком профиле отключены. `--diagnostics` однократно пишет время готовности трея в `startup.json`. Без этого параметра диагностических записей при каждом запуске нет.
