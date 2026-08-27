# Candidato Qt

Shell experimental C++20/Qt Quick de M0.2. A stack continua apenas como candidata: este diretório não é o scaffold do produto e não aceita o ADR-0001.

## Estado

Os incrementos TDD materializados em `shell/` incluem o core C++20 puro, com portas para playback e integração de plataforma, o backend manual determinístico e o suporte Qt isolado para carregar a fixture congelada e expor a fila em um `QAbstractListModel`. O modelo nasce vazio e aceita exatamente uma carga não vazia por reset, preservando a mesma identidade. O presenter `QObject` começa no estado vazio, preserva valores opcionais por propriedades `has…`, aplica explicitamente todos os defaults tipados e publica de forma atômica a fila e o snapshot falso derivados da fixture. Ele oferece as ações de transporte e preferências e agenda um único tick preciso de 200 ms somente durante reprodução ou enquanto um seek aguarda ack/rejeição. O core cobre toggle play/pause, saltos com clamp, seek com preview/ack, preferências em memória e a decisão de close/tray. O backend reproduz os snapshots de 200 ms, transporte, clamp/pausa no fim e ack/rejeição de seek sem relógio ou áudio reais. O adapter Qt autentica os dois arquivos antes do parse, ancora seus bytes na cadeia protocolo → manifesto e projeta em tipos explícitos o contrato de apresentação, preferências, estados, detalhes, assets e fila, preservando ordem, nullabilidade e casos hostis. A seam de decodificação sem hash existe somente sob `src/` para testes semânticos causais e não faz parte da API pública.

A primeira fatia visual cria uma `ApplicationWindow` 960 × 640 responsiva até o mínimo 720 × 520, mostrando apenas o estado vazio congelado. Um módulo QML estático recebe uma instância controlada de `ShellUiContract`: propriedades tipadas, constantes e somente leitura, sem filesystem, backend ou API genérica exposta ao QML. O botão “Abrir áudio” possui foco visível, alvo de 44 px, mouse, Space, Return e ação acessível convergindo para um único sinal. Fixture, fila e SVGs são validados na configuração e fechados no executável sob aliases `qrc`; o bootstrap não incorpora caminhos do checkout. Qt Test verifica o inventário da boundary e os hashes/tamanhos dos recursos finais, enquanto Quick Test cobre layout, texto, imagem, foco, teclado, mouse e semântica acessível.

Ainda estão pendentes os estados tocando/erro, controles persistentes, slider, fila visual, preferências, aplicação dos temas, tray concreto, pacote, medições e validações manuais de acessibilidade. Não há áudio real, rede, leitura de biblioteca pessoal nem dependência Qt no core.

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

## Build e testes com suporte Qt

Para validar também o repository da fixture, o modelo da fila e a primeira janela QML, use a mesma shell x64 e a toolchain Qt autenticada. Os caminhos-fonte da fixture, do manifesto e do protocolo são fechados pelo próprio scaffold e não são parâmetros substituíveis; o executável consome cópias autenticadas em recursos `qrc`:

```powershell
$repo = (Resolve-Path .).Path
$source = Join-Path $repo 'experiments\m0\candidates\qt\shell'
$build = Join-Path $repo 'out\m0\builds\qt-shell\support-release'
$catch2 = Join-Path $repo 'out\m0\toolchains\Catch2-3.15.3'
$qt = Join-Path $repo 'out\m0\toolchains\qt-6.11.2-msvc2022_64'
$cmake = 'C:\Program Files\CMake\bin\cmake.exe'
$ctest = 'C:\Program Files\CMake\bin\ctest.exe'

& $cmake -S $source -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release "-DM0_CATCH2_SOURCE_DIR=$catch2" -DM0_QT_SHELL_SUPPORT=ON "-DCMAKE_PREFIX_PATH=$qt"
& $cmake --build $build
& $cmake --build $build --target m0_qt_shell_ui_qmllint
& $ctest --test-dir $build -C Release --output-on-failure
```

Configure com `M0_QT_SHELL_SUPPORT=OFF` — o padrão — continua compilando e testando o core sem localizar Qt nem ler a fixture.
