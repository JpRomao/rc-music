# Regressão isolada de coordenadas FreeType

Experimento local Windows/MSVC de M0.2, sem Qt runtime, interface, áudio, instalação ou rede durante os testes. Não é parte do player nem comprova a segurança do pacote Qt. Usa a API pública `FT_Get_Var_Design_Coordinates` e uma fonte de teste válida, sem modificar seus bytes ou acessar estruturas privadas.

## Contrato e fixture

A [API FreeType](https://freetype.org/freetype2/docs/reference/ft2-multiple_masters.html#ft_get_var_design_coordinates) exige zerar as coordenadas solicitadas além do número de eixos. A fonte Qt `tests/auto/shared/resources/testfont_variable.ttf` tem um eixo `wght`, mínimo/default 400 e máximo 900. Os nove casos combinam:

- estado inicial, seleção explícita do default e seleção de peso 650;
- pedido de zero, uma ou duas coordenadas: abaixo, exatamente no limite e acima;
- valor do eixo selecionado, zero na coordenada extra e guardas intactas fora da região solicitada.

Cada caso abre uma nova face pela API real. Falhas não imprimem memória de coordenadas, conteúdo da fonte ou paths locais. O teste verifica também a versão do runtime contra o header fixado. O inventário Qt declara **2.14.3**, mas os headers dessa revisão e do SDK oficial identificam **2.14.2**. Esse conflito é preservado; não se altera a versão upstream para fazer o teste passar nem se descreve o vendor como uma release upstream pura.

A fonte pertence à suíte Qt e é usada pela alternativa GPL-3.0-only declarada no `REUSE.toml`; não entra na fixture comparativa M0.1 nem no pacote do player. FreeType é usado sob FTL, com seus notices adicionais. [NOTICE.txt](NOTICE.txt) registra as atribuições e a adaptação da lista de unidades C da receita Qt sob BSD-3-Clause. Nenhuma licença de redistribuição do projeto é decidida aqui.

## Entradas fixadas

| Entrada | Identidade |
|---|---|
| Qtbase fonte | `ef55f427f2c8b410d34f8a7681020a3000cf6866` |
| [Arquivo de fonte Qtbase](https://codeload.github.com/qt/qtbase/tar.gz/ef55f427f2c8b410d34f8a7681020a3000cf6866) | 67.782.484 bytes; SHA-256 `12b3a9ba00613180caea4da77a1ae6fc1ca054b8822b316ba3a05fb4414fba56` |
| `ttgxvar.c` original | `b5ca576f480437e59f84a31ba2b569c7aadd31871f608034f6ea576340b88e10` |
| `ttgxvar.c` após o patch oficial | `9126fee413fabeb2f9f78b60548065dda2a8365ef95da43cb2a0edc55813e346` |
| `include/freetype/freetype.h` da fonte | `28519f6bdbda31718a5ac2715ca0d3765b5594cad6ef20d0a817d780d6a4ae79` |
| `testfont_variable.ttf` | 28.528 bytes; `8ce9e27075d78bfc284afaa052e133166b5e89bd90498c12effe238c35a8c7a7` |
| [Patch oficial](https://github.com/freetype/freetype/commit/5a280ecde6f324de0d226261036e736e0cb49a71) / [cópia local](5a280ec.patch) | 1.705 bytes; `fae1c96cd94b9ed5aafc7424f4b8894f2e235b4869a72b3cdbb684dedcea9d1a` |

O patch mantém bytes e metadados upstream. A regra local `.gitattributes` preserva LF e permite o espaço e a linha de contexto final obrigatórios desta cópia unified diff, sem mudar checks dos arquivos C++.

O CMake verifica hashes da função, do header e da fixture, mas esses três anchors **não autenticam toda a árvore**. Antes de compilar, verificar o arquivo de fonte acima e extrair somente membros de arquivo/diretório sob o prefixo fixado, sem paths absolutos, `..`, links ou destinos existentes. O experimento deve preservar a árvore original e aplicar o patch somente a uma segunda árvore nova. Não usar fontes pessoais, o SDK congelado como destino ou uma árvore parcialmente atualizada.

## Build e execução

Use Developer PowerShell x64 com MSVC 14.44 e SDK 10.0.22621.0, como no [checkpoint MSVC](../../../../docs/m0/evidence/m0-2-msvc-installation.md). Esse é o ambiente do **teste independente**, não a configuração de reconstrução do Qt oficial. CMake 3.31.5 e Ninja 1.12.1 são os executores já autenticados pelo projeto. Não usar flags extras `CL`, `_CL_`, `LINK` ou `_LINK_`.

O exemplo pressupõe as árvores novas, já verificadas, do ensaio `ft50811-01`. Escolha um diretório de build ainda inexistente; não sobrescreva os logs Red ou um stage histórico.

```powershell
$ftRepo = (Resolve-Path .).Path
$ftHarness = Join-Path $ftRepo 'experiments/m0/security/freetype-regression'
$ftSource = Join-Path $ftRepo 'out/m0/ft50811-01/qtbase/src/3rdparty/freetype'
$ftFixture = Join-Path $ftRepo 'out/m0/ft50811-01/qtbase/tests/auto/shared/resources/testfont_variable.ttf'
$ftBuild = Join-Path $ftRepo 'out/m0/ft50811-01/reproduction-original-01'
if (Test-Path -LiteralPath $ftBuild) { throw 'Escolha um build novo.' }
$ftCMake = 'C:\Program Files\CMake\bin\cmake.exe'
$ftCTest = 'C:\Program Files\CMake\bin\ctest.exe'
$ftNinja = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
$ftCompiler = Join-Path $env:VCToolsInstallDir 'bin/Hostx64/x64/cl.exe'
& $ftCMake -S $ftHarness -B $ftBuild -G Ninja '-DCMAKE_BUILD_TYPE=Release' `
    '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON' "-DCMAKE_C_COMPILER=$ftCompiler" `
    "-DCMAKE_CXX_COMPILER=$ftCompiler" "-DCMAKE_MAKE_PROGRAM=$ftNinja" `
    "-DM0_FT_SOURCE_DIR=$ftSource" "-DM0_FT_FIXTURE_FILE=$ftFixture"
if ($LASTEXITCODE -ne 0) { throw 'Configure falhou.' }
& $ftCMake --build $ftBuild --parallel 2
if ($LASTEXITCODE -ne 0) { throw 'Build falhou.' }
& $ftCTest --test-dir $ftBuild --output-on-failure --output-junit "$ftBuild/result.junit.xml"
```

O vendor original deve falhar somente nos três casos de duas coordenadas (`ctest` exit 8), com `Excess coordinate is not zero`. Isso é o controle Red e não deve ser convertido em Green por `WILL_FAIL` ou relaxamento da expectativa. O patch e a reconstrução isolada precisam ser validados com os mesmos nove casos em um build separado.

O build usa 44 unidades C do vendor, a biblioteca estática e um executável de teste próprio. Usa o gzip interno padrão do FreeType, sem os defines Qt para zlib externo/PNG, sem HarfBuzz e sem Qt Core. Não é configuração binariamente equivalente ao QtGui; a passagem da regressão independente não autentica uma DLL Qt corrigida. Essa integração exige configuração/ABI, proveniência, inventário e gates próprios, mantendo o SDK e os pacotes anteriores intactos.
