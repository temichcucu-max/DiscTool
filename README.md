# DiscTool

Мультитул для работы с диском: поиск файлов, анализ занятого места и очистка.
Сейчас работают вкладки «Поиск» (фоновое сканирование, прогресс, отмена),
«Анализ диска» (дерево папок и squarified treemap) и «Очистка» (поиск пустых
файлов и папок с безопасным удалением в Корзину).

## Требования

- Windows 10/11 x64
- Visual Studio 2022 (или новее) с рабочей нагрузкой
  **«Разработка классических приложений на C++»** (MSVC, Windows SDK)
- CMake **3.21+**
- Qt **6.5+** (сборка MSVC 2019/2022 64-bit, например `msvc2019_64`)

Qt должен быть собран под MSVC: MinGW-сборки не подходят.

## Проверенная среда

Конфигурация, на которой проект собирался и проходил тесты:

- Windows 10 x64 (10.0.19045)
- Visual Studio 2026 (18.10.3), MSVC 19.51 (toolset v14.51), Windows SDK 10.0.26100
- CMake 4.4.4
- Qt 6.7.3 `msvc2019_64` — основная проверенная сборка Qt
- Qt 6.5.3 `msvc2019_64` — также установлена в системе

Пакетов `msvc2022_64` в системе нет. Qt собирается MSVC 2019, но ABI
совместим с более новыми MSVC (v14x), поэтому связка работает. Генератор
пресета не фиксирован: CMake выбирает установленную версию Visual Studio
(здесь — Visual Studio 18 2026).

## Тесты

Тесты используют Catch2 3 (BSL-1.0). Сначала выполняется
`find_package(Catch2 3 CONFIG QUIET)`; если пакет не найден, Catch2
скачивается через `FetchContent` (v3.8.1) при конфигурации — нужен доступ
к GitHub.

```powershell
cmake --build --preset windows-msvc-debug
ctest --test-dir build/windows-msvc-debug --output-on-failure
```

Для многоконфигурационного генератора Visual Studio можно явно указать
конфигурацию: `ctest --test-dir build/windows-msvc-debug -C Debug --output-on-failure`.


## Настройка Qt

Жёсткого пути к Qt в проекте нет. CMake найдёт Qt, если указать его одним из
способов:

1. Переменная окружения `QT_ROOT` (самый простой вариант для PowerShell):

   ```powershell
   $env:QT_ROOT = "C:\Qt\6.7.3\msvc2019_64"
   ```

   Корневой `CMakeLists.txt` добавит её в `CMAKE_PREFIX_PATH`.

2. Через `CMAKE_PREFIX_PATH`:

   ```powershell
   cmake --preset windows-msvc-debug -DCMAKE_PREFIX_PATH="C:/Qt/6.7.3/msvc2019_64"
   ```

3. Через `Qt6_DIR`:

   ```powershell
   cmake --preset windows-msvc-debug -DQt6_DIR="C:/Qt/6.7.3/msvc2019_64/lib/cmake/Qt6"
   ```

4. Переменная окружения `CMAKE_PREFIX_PATH` — CMake читает её сам.

## Сборка

Пресеты: `windows-msvc-debug` и `windows-msvc-release`. Генератор — Visual
Studio по умолчанию (подхватывается установленная версия); архитектура x64.

```powershell
# Конфигурация
cmake --preset windows-msvc-debug

# Сборка
cmake --build --preset windows-msvc-debug

# Release
cmake --preset windows-msvc-release
cmake --build --preset windows-msvc-release
```

Готовый бинарник:

```text
build/windows-msvc-debug/bin/Debug/disktool.exe
build/windows-msvc-release/bin/Release/disktool.exe
```

## Запуск

При запуске из проводника или консоли Windows должна находить DLL Qt. Есть
два варианта:

```powershell
# 1. Добавить bin Qt в PATH на время сессии
$env:PATH = "C:\Qt\6.7.3\msvc2019_64\bin;$env:PATH"
.\build\windows-msvc-debug\bin\Debug\disktool.exe

# 2. Разложить зависимости рядом с exe (для передачи/проверки)
& "C:\Qt\6.7.3\msvc2019_64\bin\windeployqt.exe" --debug `
    .\build\windows-msvc-debug\bin\Debug\disktool.exe
```

Qt линкуется **динамически**; статическая сборка Qt не поддерживается, так как
проект распространяется под MIT и использует Qt по LGPL-3.0.

## Структура

```text
DiscTool/
├── CMakeLists.txt
├── CMakePresets.json
├── vcpkg.json              # пустой манифест на будущее
├── include/disktool/core/  # version.h, scanner.h, tree_builder.h, treemap.h, cleanup.h
├── src/core/               # disktool_core — статическая библиотека без Qt
├── src/app/                # disktool — Qt Widgets (tabs/, models/, widgets/)
├── resources/              # .qrc (пока пустой)
└── tests/                  # тесты Catch2 для core
```

`disktool_core` не зависит от Qt — это чистое C++-ядро (сканер файлов,
построение дерева папок и раскладка treemap), которое используется приложением
и тестами.
Сканер синхронный, без внутренних потоков; UI запускает его через QtConcurrent, а отмена работает через `cancelToken`.
Анализ диска строит иерархию из плоского `ScanResult` (`buildTree`), сортирует
папки по суммарному размеру (`sortBySizeDescending`) и вычисляет прямоугольники
squarified treemap (`squarifiedTreemap`).
Очистка использует ту же иерархию (`findEmptyEntries`): пустые файлы и верхние
пустые папки удаляются только через `QFile::moveToTrash`, то есть в Корзину
Windows; `std::filesystem::remove`/`remove_all` в UI не используются.

## Лицензия

Собственный код — MIT, см. [LICENSE](LICENSE). Сторонние компоненты и их
лицензии перечислены в [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
