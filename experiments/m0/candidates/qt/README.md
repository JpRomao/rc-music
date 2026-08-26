# Candidato Qt

Shell experimental C++20/Qt Quick de M0.2. A stack continua apenas como candidata: este diretório não é o scaffold do produto e não aceita o ADR-0001.

## Estado

O primeiro incremento TDD materializa somente o core C++20 puro em `shell/`, com portas para playback e integração de plataforma. Ele cobre seek com preview/ack, preferências em memória e a decisão de close/tray por meio de fakes determinísticos.

Ainda estão pendentes o backend falso temporizado, o presenter Qt, a UI QML, a fila comum de 1.000 itens, os temas, o tray concreto e as validações manuais. Não há áudio real, rede, leitura de biblioteca pessoal nem dependência Qt no core.

## Build e testes do core

Execute primeiro o preflight autenticado na raiz do repositório. Depois, em um **Developer PowerShell for VS 2022** configurado para x64:

```powershell
$repo = (Resolve-Path .).Path
$source = Join-Path $repo 'experiments\m0\candidates\qt\shell'
$build = Join-Path $repo 'out\m0\builds\qt-shell\core-release'
$catch2 = Join-Path $repo 'out\m0\toolchains\Catch2-3.15.3'
$cmake = 'C:\Program Files\CMake\bin\cmake.exe'
$ctest = 'C:\Program Files\CMake\bin\ctest.exe'

& $cmake -S $source -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release "-DM0_CATCH2_SOURCE_DIR=$catch2"
& $cmake --build $build --target m0_qt_core_tests
& $ctest --test-dir $build -C Release --output-on-failure -R '^m0\.qt\.core\.'
```

O Catch2 precisa ser o source tree autenticado do preflight e o build permanece sob `out/m0`; nenhuma dependência é baixada durante configure ou build.
