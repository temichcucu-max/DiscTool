# Third-Party Notices

This project is distributed under the MIT License (see `LICENSE`).

It uses the following third-party components. Every component must be listed
here before/at the moment it is added to the project.

## Allowed licenses

Only components under MIT, BSD-2-Clause, BSD-3-Clause, Apache-2.0, Zlib, or
Boost are permitted. Qt itself is the only LGPL component allowed.
GPL, AGPL, LGPL (other than Qt), and proprietary SDKs are not permitted.

## Components

### Qt 6.x

- **Copyright:** Copyright (c) The Qt Company Ltd. and contributors
- **License:** LGPL-3.0 (dynamic linking only)
- **Repository:** <https://www.qt.io> / <https://code.qt.io/cgit/qt/>
- **License text in project:** not bundled; see
  <https://www.gnu.org/licenses/lgpl-3.0.html>
- **Used modules:** Qt6::Core, Qt6::Gui, Qt6::Widgets
- **Notes:** linked dynamically. The exact version is resolved by
  `find_package(Qt6 REQUIRED COMPONENTS Widgets)` in `src/app/CMakeLists.txt`
  and is not pinned by the project. Static Qt linking is not used.

### Catch2 3.8.1

- **Copyright:** Copyright (c) Catch2 contributors
- **License:** BSL-1.0 (Boost Software License 1.0)
- **Repository:** <https://github.com/catchorg/Catch2>
- **License text in project:** not bundled; see
  <https://github.com/catchorg/Catch2/blob/v3.8.1/LICENSE.txt>
- **Used by:** `tests/` only. It is not linked into `disktool.exe` or any
  release binary. Fetched at configure time via `FetchContent` when no local
  Catch2 3 package is found.

<!--
Entry template — copy this block for each new dependency:

### <Library name> <version>

- **Copyright:** Copyright (c) <year> <author/rights holder>
- **License:** <MIT | BSD-2-Clause | BSD-3-Clause | Apache-2.0 | Zlib | Boost | LGPL-3.0 (Qt only)>
- **Repository:** <https://...>
- **License text in project:** <path/to/license/file, optional>
-->
