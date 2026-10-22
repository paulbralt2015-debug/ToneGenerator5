name: Build Tone Generator 5

on:
  workflow_dispatch:
  push:
    branches: [main]
  pull_request:
    branches: [main]

jobs:
  build-windows:
    runs-on: windows-latest

    steps:
      - name: Recuperer le code
        uses: actions/checkout@v4

      - name: Installer Qt
        uses: jurplel/install-qt-action@v4
        with:
          version: '6.8.3'
          host: windows
          target: desktop
          arch: win64_msvc2022_64
          modules: qtmultimedia

      - name: Configurer le projet
        run: cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release

      - name: Compiler
        run: cmake --build build --config Release

      - name: Recuperer l'executable
        uses: actions/upload-artifact@v4
        with:
          name: ToneGenerator5-Windows
          path: build/ToneGenerator5.exe
          if-no-files-found: error
