# Núcleo de produto — primeiro recorte de M1.1

**Estado:** núcleo promovido; aplicação gráfica ainda em composição experimental.
**Data:** 2026-09-05.

O [ADR-0005](architecture/decisions/0005-windows-product-stack.md) autoriza reutilizar o C++ existente. O CMake da raiz agora compila fontes próprias, sem encaminhar para o experimento. Não é ainda o build/distribuição completo do aplicativo e não encerra M1.1, M1.2 ou M1.

## Fronteiras e fonte única

| Unidade | Fontes e dependências |
|---|---|
| `rc_music_contracts` | Headers públicos em `include/m0/domain`, `application` e `ports`; C++20, sem Qt |
| `rc_music_core` | Onze fontes em `src/domain` e `src/application`; somente contratos puros |
| `rc_music_workers` | Cinco adapters `SingleWorker*` em `src/adapters`; core e Threads; descoberta Windows isolada neste alvo, fora do domínio |
| `rc_music_test_support` | Backend determinístico em `tests/support`; solicitado apenas pelos testes ou shell histórico de fixture |
| `rc_music_core_tests` | Dezoito fontes em `tests/core`; mesmos 224 casos Catch2, sem `QCoreApplication` |
| Consumidor Qt existente | `m0_qt_core` é uma interface sobre os alvos acima; adapters Qt/TagLib, presenter, QML e bootstrap ainda ficam em `experiments/m0/candidates/qt/shell` |

Os 67 arquivos C++ foram movidos, não copiados: hashes após normalizar finais de linha em LF permaneceram iguais. A `.clang-format` também foi movida para a raiz, preservando a regra herdada pelo shell. Namespace/include `m0`, dados e locks `MusicPlayer` e marca visível RC Music permanecem intactos.

`BUILD_TESTING=OFF` na raiz não pede Catch2, Qt, testes, corpus, protocolo ou fixture. Com testes ON, o caminho absoluto de Catch2 3.15.3 é explícito e a versão é conferida; não há download automático. Usar o source tree já verificado no preflight não torna o harness uma dependência de execução do core.

`Test-M0Layout.ps1` e a suíte agregada M0 que inclui `Contract.Tests.ps1` mantêm a proibição pré-M0.10 de `src`/`CMakeLists.txt` na raiz. Esses comandos pertencem ao snapshot histórico, não aos gates da promoção. O módulo, inventário e protocolo autenticados não foram editados ou reancorados. A independência do core é verificada pelo build da exportação mínima descrito abaixo, não por relaxar o contrato experimental.

O shell histórico ainda solicita o backend determinístico mesmo com testes OFF, pois seu presenter mantém o modo de fixture. Separar `FixtureBundle`, recursos e bootstrap real é o próximo recorte; mover o core não demonstra essa separação nem autoriza chamar o executável histórico de produto independente.

## Build Windows

Na raiz do repositório, use PowerShell 7 x64 e as ferramentas já provisionadas no [ambiente MSVC](../experiments/m0/candidates/qt/README.md#ambiente-de-build-msvc-144). Estes caminhos correspondem ao host validado; não instalam ferramentas:

```powershell
$rcVsRoot = 'C:\Program Files\Microsoft Visual Studio\18\BuildTools'
Import-Module (Join-Path $rcVsRoot 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $rcVsRoot -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64 -vcvars_ver=14.44 -winsdk=10.0.22621.0'
$rcCompiler = Join-Path $rcVsRoot 'VC\Tools\MSVC\14.44.35207\bin\HostX64\x64\cl.exe'
$rcNinja = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
$rcCatch2 = Join-Path (Resolve-Path .).Path 'out\m0\toolchains\Catch2-3.15.3'

cmake -S . -B out/product/core-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON "-DCMAKE_CXX_COMPILER=$rcCompiler" "-DCMAKE_MAKE_PROGRAM=$rcNinja" "-DRC_MUSIC_CATCH2_SOURCE_DIR=$rcCatch2"
if ($LASTEXITCODE -ne 0) { throw 'Configure do core falhou.' }
cmake --build out/product/core-release --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Build do core falhou.' }
ctest --test-dir out/product/core-release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Testes do core falharam.' }

cmake -S . -B out/product/core-no-tests -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF "-DCMAKE_CXX_COMPILER=$rcCompiler" "-DCMAKE_MAKE_PROGRAM=$rcNinja"
if ($LASTEXITCODE -ne 0) { throw 'Configure OFF falhou.' }
cmake --build out/product/core-no-tests --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Build OFF falhou.' }
ctest --test-dir out/product/core-no-tests -N
```

O baseline exige CMake 3.31 ou superior, C++20 e `/W4 /WX /permissive-` em MSVC. A execução local usou MSVC 19.44.35228.0/toolset 14.44.35207 e Windows SDK 10.0.22621.0. `out/product` é ignorado; não versionar builds/toolchains. Para alterações apenas no core, não repetir checks QML ou benchmarks sem motivo. O [check de formato](../experiments/m0/candidates/qt/README.md#checks-de-formato-c-e-qml) cobre ambas as árvores C++.

## Evidência deste recorte

- Baseline antes da movimentação: CTest do core legado **224/224**.
- Nova raiz ON em `out/product/m1-core-release-20260905`: configure/build Release e CTest **224/224**, em 3,66 s.
- Independência OFF: uma exportação contendo somente `CMakeLists.txt`, `cmake/`, `include/` e `src/` compilou em `out/product/m1-core-standalone-20260905/build`. Não havia `experiments/` ou `tests/` nessa fonte. Os 16 comandos de compilação não contêm referências Qt/Catch2/fixture/experimento/backend determinístico; CTest registra zero testes.
- Equivalência dos 68 arquivos movidos verificada por SHA-256 normalizado em LF; formato dos 67 C++ aprovado.
- Proteção de proveniência do runtime: regressão Red **21 passagens/17 falhas**, seguida de Green **38/38**. Fontes de produto staged, unstaged e untracked passam a impedir manifesto com origem declarada limpa; a lista cobre raiz CMake, `cmake`, `include`, `src` e `tests`, além dos três inputs históricos.
- Consumidor Qt em `out/m0/builds/qt-audio/combined-shell-20260829-01`: rebuild Release aprovado; seleção CTest **271/271**, em 95,72 s (224 core, 35 metadata e 12 alvos de bindings, arquivos, presenter, contrato UI, Quick, startup e integração local). Presenter **263/263**, contrato UI **25/25** e Quick **187/187**, sem falhas/skips. Não foi executada a suíte CTest inteira nem interação nativa.
- Consumidor OFF em `out/m0/builds/qt-audio/m43-production-release-20260901`: rebuild aprovado, zero testes e recusa de `--m0-test-config-root` com exit 1, streams vazios e raiz solicitada ausente. O probe isolado/offscreen precisou receber também as entradas `ENVIRONMENT_MODIFICATION` de PATH do CTest: a tentativa inicial sem elas falhou no loader por DLL ausente, antes do aplicativo; isso não é Red comportamental.
- Revisão independente de CMake e caminhos sem achados restantes no escopo; validação documental **48 Markdown, 83 requisitos, 17 checks e 76 etapas** e `git diff --check` aprovados.

A seleção integrada pode ser repetida na sessão de build preparada, somente após mudança relevante:

```powershell
ctest --test-dir out/m0/builds/qt-audio/combined-shell-20260829-01 --output-on-failure -R '^m0\.qt\.(core\.|metadata\.|presenter\.|quick\.|shell-ui-contract$|shell-executable\.combined-startup$|audio\.local-integration$|shell-platform$|shell-platform-binding$|audio-file-locator$|audio-files-picker$|system-media-controls\.binding$|system-power-events\.binding$|diagnostics\.binding$)'
pwsh -NoProfile -File experiments/m0/candidates/qt/windows-installer/tests/WindowsRuntimePlan.Tests.ps1
pwsh -NoProfile -File scripts/validate-docs.ps1
git diff --check
```

Esse recorte não altera áudio, interface, dados, dependências ou identidade. Não atualiza o instalador a43faa1 e não executa instalação, associação, Computer Use, A/B, Linux ou benchmarks. Segurança/licenças, acessibilidade nativa, pacote e demais gates de conclusão permanecem abertos.
