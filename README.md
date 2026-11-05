# DiscTool

Мультитул для работы с диском: поиск файлов, анализ занятого места и очистка.

## Скачать

Готовый портативный архив — **`DiscTool-0.1.0-win64.zip`** (Windows 10/11 x64).
Он лежит в корне проекта, а при публикации прикрепляется к разделу **Releases**.

1. Распакуйте архив в любую папку (например, `C:\Tools\DiscTool`).
2. Запустите `disktool.exe`.

Устанавливать Qt, Visual Studio, CMake или что-либо ещё не нужно: все
необходимые DLL Qt и плагины (`platforms/`, `imageformats/`, `styles/` и др.)
уже лежат рядом с исполняемым файлом. Приложение запросит права Администратора
через UAC — они нужны для быстрого MFT-сканирования NTFS.

> Файл не подписан цифровой подписью, поэтому SmartScreen может показать
> предупреждение: «Подробнее» → «Выполнить в любом случае».

## Возможности

- **«Поиск»** — фоновое сканирование папки, живой фильтр по имени, прогресс и
  отмена. При старте автоматически сканирует `C:\`; есть быстрый режим
  **MFT (USN Journal)**, который включается автоматически при наличии прав
  Администратора. Результаты появляются на экране во время скана.
  ПКМ по строке: «Открыть», «Открыть путь», «Копировать полное имя», «Свойства».
- **«Анализ диска»** — дерево папок с размерами и squarified treemap
  (обычный сканер, чтобы размеры были точными; дерево обновляется на лету).
- **«Очистка»** — поиск пустых файлов и папок с безопасным асинхронным
  удалением в Корзину Windows через `IFileOperation` (без
  `std::filesystem::remove`/`remove_all`), с прогресс-баром и отменой.
- **Темы оформления** — «Вид → Тема»: системная, светлая или темная. Выбор
  сохраняется в `settings.ini` (`QStandardPaths::AppConfigLocation`) и
  применяется при следующем запуске.

## Запуск от имени Администратора

Приложение собрано с UAC-манифестом `requireAdministrator`: при запуске
Windows покажет запрос прав. Это нужно для прямого чтения MFT/USN на NTFS.
Без повышенных прав MFT недоступен; приложение вежливо предупредит и выполнит
обычный скан. Стандартный скан, анализ и очистка работают и без elevation.

## Сборка из исходников

### Требования

- Windows 10/11 x64
- Visual Studio 2022 (или новее) с рабочей нагрузкой
  **«Разработка классических приложений на C++»** (MSVC, Windows SDK)
- CMake **3.21+**
- Qt **6.5+** (сборка MSVC 2019/2022 64-bit, например `msvc2019_64`)

Qt должен быть собран под MSVC: MinGW-сборки не подходят.

### Настройка Qt

Жёсткого пути к Qt в проекте нет. Самый простой вариант — переменная окружения
`QT_ROOT` (PowerShell):

```powershell
$env:QT_ROOT = "C:\Qt\6.7.3\msvc2019_64"
```

Корневой `CMakeLists.txt` добавит её в `CMAKE_PREFIX_PATH`. Альтернативы:

```powershell
# через CMAKE_PREFIX_PATH
cmake --preset windows-msvc-debug -DCMAKE_PREFIX_PATH="C:/Qt/6.7.3/msvc2019_64"

# через Qt6_DIR
cmake --preset windows-msvc-debug -DQt6_DIR="C:/Qt/6.7.3/msvc2019_64/lib/cmake/Qt6"
```

Переменную `CMAKE_PREFIX_PATH` CMake читает и из окружения.

### Debug

```powershell
$env:QT_ROOT = "C:\Qt\6.7.3\msvc2019_64"
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug --config Debug
```

Готовый бинарник: `build/windows-msvc-debug/bin/Debug/disktool.exe`.
Для запуска из консоли добавьте `%QT_ROOT%\bin` в `PATH`.

### Release и портативная сборка

```powershell
$env:QT_ROOT = "C:\Qt\6.7.3\msvc2019_64"
cmake --preset windows-msvc-release
cmake --build --preset windows-msvc-release --config Release
```

После линковки `disktool` автоматический пост-сборочный шаг
(`src/app/deploy_qt.cmake`) находит `windeployqt.exe` в `$ENV{QT_ROOT}/bin`
и раскладывает runtime рядом с exe:

```
windeployqt --release --no-translations --no-system-d3d-compiler --no-opengl-sw
```

Шаг выполняется только для конфигурации **Release**, поэтому Debug-сборка
не засоряется release-версиями Qt DLL.

Результат — самодостаточная папка `build/windows-msvc-release/bin/Release/`:
`disktool.exe`, `Qt6*.dll`, `platforms/qwindows.dll` и остальные плагины.
Из неё собирается ZIP:

```powershell
Compress-Archive -Path .\build\windows-msvc-release\bin\Release\* `
    -DestinationPath .\DiscTool-0.1.0-win64.zip -Force
```

### Тесты

Тесты используют Catch2 3 (BSL-1.0). Сначала выполняется
`find_package(Catch2 3 CONFIG QUIET)`; если пакет не найден, Catch2
скачивается через `FetchContent` (v3.8.1) при конфигурации — нужен доступ
к GitHub.

```powershell
cmake --build --preset windows-msvc-debug
ctest --test-dir build/windows-msvc-debug -C Debug --output-on-failure
```

Для Release — то же с `windows-msvc-release -C Release`.

## Проверенная среда

Конфигурация, на которой проект собирался и проходил тесты:

- Windows 10 x64 (10.0.19045)
- Visual Studio 2026 (18.10.3), MSVC 19.51 (toolset v14.51), Windows SDK 10.0.26100
- CMake 4.4.4
- Qt 6.7.3 `msvc2019_64` — основная проверенная сборка Qt
- Qt 6.5.3 `msvc2019_64` — также установлена в системе

Пакетов `msvc2022_64` в системе нет. Qt собирается MSVC 2019, но ABI
совместим с более новыми MSVC (v14x), поэтому связка работает. Генератор
пресета не фиксирован: CMake выбирает установленную версию Visual Studio.

## Структура проекта

```text
DiscTool/
├── CMakeLists.txt              # корневая конфигурация, поиск Qt, тесты
├── CMakePresets.json           # пресеты Debug/Release (MSVC x64)
├── README.md
├── LICENSE                     # MIT для собственного кода
├── THIRD_PARTY_NOTICES.md      # лицензии сторонних компонентов
├── vcpkg.json                  # пустой манифест на будущее
├── DiscTool-0.1.0-win64.zip    # готовый портативный архив
│
├── include/disktool/core/      # публичный API ядра (без Qt)
│   ├── version.h
│   ├── scanner.h
│   ├── mft_scanner.h
│   ├── tree_builder.h
│   ├── treemap.h
│   └── cleanup.h
│
├── src/
│   ├── core/                   # disktool_core — статическая библиотека без Qt
│   │   ├── CMakeLists.txt
│   │   ├── version.cpp
│   │   ├── scanner.cpp
│   │   ├── mft_scanner.cpp
│   │   ├── tree_builder.cpp
│   │   ├── treemap.cpp
│   │   └── cleanup.cpp
│   │
│   └── app/                    # disktool — Qt Widgets
│       ├── CMakeLists.txt      # сборка exe, иконка, windeployqt POST_BUILD
│       ├── deploy_qt.cmake     # запуск windeployqt для Release
│       ├── disktool.rc.in      # шаблон .rc с иконкой exe
│       ├── main.cpp
│       ├── MainWindow.h
│       ├── MainWindow.cpp
│       ├── FormatUtils.h
│       ├── ScanJob.h           # выбор MFT / обычного сканера в фоне
│       ├── Theme.h
│       ├── Theme.cpp           # светлая/темная/системная тема + QSettings
│       ├── TrashWorker.h
│       ├── TrashWorker.cpp     # IFileOperation, батчи, прогресс, отмена
│       ├── models/
│       │   ├── DirTreeModel.h
│       │   ├── DirTreeModel.cpp
│       │   ├── FileTableModel.h
│       │   └── FileTableModel.cpp
│       ├── tabs/
│       │   ├── SearchTab.h
│       │   ├── SearchTab.cpp
│       │   ├── AnalysisTab.h
│       │   ├── AnalysisTab.cpp
│       │   ├── CleanupTab.h
│       │   └── CleanupTab.cpp
│       └── widgets/
│           ├── TreemapWidget.h
│           └── TreemapWidget.cpp
│
├── resources/
│   ├── resources.qrc           # иконки в Qt-ресурсах
│   ├── app.svg                 # исходная иконка
│   ├── app.png                 # растровая версия для окна
│   └── app.ico                 # иконка exe/окна (16–256 px)
│
└── tests/
    ├── CMakeLists.txt
    ├── scanner_tests.cpp
    ├── mft_scanner_tests.cpp
    ├── tree_builder_tests.cpp
    ├── treemap_tests.cpp
    └── cleanup_tests.cpp
```

`disktool_core` не зависит от Qt — это чистое C++-ядро (сканер файлов,
построение дерева папок, раскладка treemap и поиск пустых записей), которое
используется приложением и тестами. Сканер синхронный, без внутренних потоков;
UI запускает его через `QtConcurrent`, а отмена работает через `cancelToken`.

Анализ диска строит иерархию из плоского `ScanResult` (`buildTree`), сортирует
папки по суммарному размеру (`sortBySizeDescending`) и вычисляет прямоугольники
squarified treemap (`squarifiedTreemap`).

Очистка использует ту же иерархию (`findEmptyEntries`): пустые файлы и верхние
пустые папки удаляются только через `QFile::moveToTrash`, то есть в Корзину
Windows; `std::filesystem::remove`/`remove_all` в UI не используются.

Быстрый поиск (`MftScanner`) открывает том через `\\.\C:` и перечисляет записи
USN (`FSCTL_ENUM_USN_DATA`), а полные пути восстанавливает по
`ParentFileReferenceNumber`. Для не-NTFS, несуществующего диска или процесса
без прав Администратора возвращается `success = false`, и `SearchTab`
автоматически повторяет скан обычным `Scanner`.

Сканеры сообщают найденные записи пачками через `ScanOptions::onBatch` /
`MftScanOptions::onBatch`. UI складывает их в потокобезопасную очередь и
обновляет модель не чаще ~10 раз в секунду, поэтому большие тома не блокируют
интерфейс.

Удаление в «Очистке» выполняет `TrashWorker` в фоновом потоке через
`IFileOperation` пачками по 500 путей. `FOF_ALLOWUNDO` отправляет элементы в
Корзину; прогресс приходит сигналами, отмена реализована возвратом `E_ABORT`
из `IFileOperationProgressSink` (в Windows SDK у `IFileOperation` нет метода
`Abort()`).

## Как внести вклад

1. Сделайте форк и отдельную ветку под изменение.
2. Соберите Debug и прогоните `ctest` — все тесты должны быть зелёными.
3. Держите `disktool_core` свободным от Qt и добавляйте тесты Catch2 на каждую
   новую функцию ядра.
4. Удаление пользовательских файлов — только через Корзину
   (`QFile::moveToTrash`); не добавляйте прямые `remove`/`remove_all`.
5. Не добавляйте новых сторонних зависимостей без обсуждения: проект
   распространяется под MIT, Qt используется динамически по LGPL-3.0.
6. Оформляйте PR с кратким описанием изменения и способа проверки.

## Лицензия

Собственный код — MIT, см. [LICENSE](LICENSE). Сторонние компоненты и их
лицензии перечислены в [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
