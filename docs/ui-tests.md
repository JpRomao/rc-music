# UI e presenter — variante exclusiva de testes

**Estado:** cobertura histórica promovida ao build raiz; M1 e release permanecem abertos.
**Data:** 2026-09-06.

A raiz agora executa os testes de presenter, contrato visual, repositório/modelo de fixture e Qt Quick, sem habilitar cenários na aplicação real. As fontes de suporte e testes foram movidas para [tests/ui](../tests/ui), e o consumidor histórico aponta para os mesmos arquivos. As assertions foram preservadas; o bootstrap Quick seleciona somente o nome do recurso de tradução correspondente à variante compilada.

## Uma implementação, duas compilações isoladas

A fábrica [rc_music_add_ui](../cmake/RcMusicUi.cmake) recebe alvo, suporte e diretório QML. Ela compila as mesmas fontes de apresentação, os mesmos 28 componentes QML, tradução e assets da raiz para duas composições:

| Composição | UI/plugin | Suporte e recursos de cenário |
|---|---|---|
| Aplicação real | `m0_qt_shell_ui` / `m0_qt_shell_uiplugin` | Ausentes, inclusive com testes ON |
| Testes UI | `rc_music_test_ui` / `rc_music_test_uiplugin` | `rc_music_ui_scenario_support`; JSONs somente nos consumidores de teste |

[tests/ui/CMakeLists.txt](../tests/ui/CMakeLists.txt) só é incluído com desktop e `BUILD_TESTING=ON`. O suporte reúne `FixtureRepository`, `FixtureDecoder`, `QueueListModel`, controles auxiliares de cenário/teste e o backend determinístico já existente. Não injeta esses componentes no core ou na UI de produção.

`RC_MUSIC_ENABLE_FIXTURE_SCENARIOS` altera o layout C++ de `PlayerPresenter`. Por isso, a macro é pública na variante de testes e acompanha seus consumidores, inclusive código gerado por moc/registro QML. Nenhum executável pode ligar simultaneamente as duas variantes ou consumir o cabeçalho com uma configuração diferente da biblioteca.

Ambas conservam a URI `MusicPlayer.M0`, mas têm alvos, plugins e diretórios de geração separados. O Quick recebe explicitamente o import root `<build>/tests/ui/qml`; a aplicação usa `<build>/qml`. Criar os targets em `tests/ui` também mantém o scanner QML fora da raiz inteira e dos caches. Isso é recompilação controlada de fonte única, não uma segunda implementação da UI.

## Fixtures privadas preservadas

[tests/ui/fixtures](../tests/ui/fixtures) contém somente três cópias textuais byte-idênticas, totalizando **591.252 bytes**:

| Arquivo | Bytes | SHA-256 |
|---|---:|---|
| `fixture.v1.json` | 8.615 | `03fd2a762758c524e226296a63575df5e3972a9a0ea09c1109bb641089a58464` |
| `queue.v1.json` | 582.271 | `a3c2e890f08885af1de319ed330f71a4b657aeaecd7bf7c640dc633095d55ef3` |
| `LICENSE.assets.txt` | 366 | `c77c86115e7da5fff31ab28c903e78a8a12f65f9b2792f7e0d137c458fae5b94` |

[RcMusicUiFixtures.cmake](../cmake/RcMusicUiFixtures.cmake) fixa esses valores e valida tamanho/hash no configure de testes, recusando ausência, diretório ou divergência. Não lê o manifesto ou protocolo comparativo. Os originais e seus registros autenticados permanecem intactos; não é necessário configurar o experimento para executar a suite raiz.

A [licença preservada](../tests/ui/fixtures/LICENSE.assets.txt), `LicenseRef-MusicPlayer-M0-Private`, autoriza uso, cópia e modificação apenas dentro deste projeto privado local e de seus experimentos; não autoriza redistribuição pública. Os dois SVGs continuam em `assets`, sem novas cópias: o contrato UI mantém suas assertions de tamanho/hash contra os recursos efetivamente embutidos.

Os JSONs são ligados por `m0_qt_fixture_data` somente aos testes que precisam dos recursos. Presenter/repositório também recebem caminhos explícitos para as cópias locais. A tradução pt-BR e os assets usam as mesmas fontes de produção; o Quick inicializa a tradução da sua própria variante.

A [promoção posterior das jornadas de processo](process-tests.md), com gates de build/testes validados, reutiliza essa mesma UI de teste em dois shells compilados da main compartilhada. Ambos preservam a ABI de cenário; somente `rc_music_fixture_test_shell` incorpora os JSONs, enquanto `rc_music_test_shell` usa áudio combinado sem esses recursos. Não é uma terceira implementação da UI nem instrumentação do executável real.

## Comandos e fronteira verificável

Os [comandos de desenvolvimento existentes](development-commands.md) já cobrem a promoção; não há novo wrapper:

```powershell
pwsh -NoProfile -File scripts/develop.ps1 test
pwsh -NoProfile -File scripts/develop.ps1 validate
```

Esses comandos usam `.tools/windows.psd1` por padrão; `-ToolchainFile tools/windows.example.psd1` seleciona o exemplo local. Para um check focado depois do configure/build ON, use `ctest --test-dir out/product/desktop-tests --output-on-failure --no-tests=error -R '^rc_music\.ui\.'`.

O novo teste [rc_music.ui.build-boundary](../tests/cmake/UiBuildBoundaryTests.cmake) examina os comandos alcançáveis dos executáveis real e Quick. Recusa suporte, macro, JSONs ou UI de cenário na aplicação; recusa a UI/plugin de produção no Quick; e exige uma compilação coerente do presenter, moc e registro de tipos para cada variante. Também verifica a separação dos diretórios usados pelos comandos RCC. Não infere independência pela ausência de um arquivo antigo: o conteúdo dos QRCs alcançáveis permanece uma revisão separada.

## Evidência do incremento

- Baseline histórico de presenter, contrato UI e Quick: **3/3** antes da promoção, em **34,70 s**. A extração mecânica não muda o comportamento de produção nem usa um Red artificial.
- Equivalência das fontes movidas: **34 arquivos byte-idênticos**; o 35º, `quick_main.cpp`, altera somente a seleção condicional do símbolo de tradução. As assertions C++/QML não mudaram. A fábrica preserva as fontes e opções do bloco original após parametrização de nomes/diretórios.
- A guarda de fronteira teve Red porque `rc_music_ui_quick_tests` ainda não existia, mantendo as verificações de produção aprovadas; Green com a composição isolada. A regressão do formatter teve Red **17 passagens/1 falha**, pela ausência de Quick no inventário, seguido de Green **18/18**.
- `validate` raiz passou **276/276** registros CTest em **88,93 s**, incluindo as quatro suites UI e o novo check de fronteira, além da cobertura raiz já existente de core, adapters e áudio.
- Contagens internas lidas no `LastTest.log` do build `out/product/m1-desktop-release-on-20260906`: repositório/modelo **41/41**, presenter **265/265**, contrato UI **29/29** e Quick **187/187**; todos sem falhas ou skips. Essas contagens QtTest não são somadas ao total de registros CTest.
- Formato real passou em **154 C++/52 QML**: o inventário QML agora inclui os 24 arquivos Quick promovidos, além dos 28 componentes da aplicação. Testes do runner **33/33** e formatter **18/18** passaram.
- Os dois alvos `qmllint`, de produção e da variante de testes, passaram na validação raiz.
- Produção OFF reconfigurada/compilada, **zero testes**, sem Qt Test/QuickTest no cache ou suporte UI de cenário no grafo. Sua aplicação conserva **246 comandos**, com as fontes reais e sem referências históricas/de teste.
- Revisão dos QRCs realmente alcançados no build ON: aplicação com quatro QRCs sob `src/.qt/rcc` (28 QML, dois SVGs, tradução e qmldir reais), sem JSON; Quick com cinco QRCs sob `tests/ui/.qt/rcc`, incluindo somente as duas cópias JSON de teste no recurso adicional. A API QML gerada de produção e teste tem SHA-256 idêntico: `e8534f10016ff84190bfde8da909d3c692d96794328db1d05750cabdb20887df`, preservando o baseline. Revisão independente sem achados P0–P2 remanescentes.
- Consumidor histórico recompilado com as fontes movidas: seleção **8/8** em **36,18 s**, cobrindo as quatro suites UI, bindings de diagnóstico/mídia/energia e startup combinado. Mantém seus inputs originais e o comportamento histórico; não é execução completa do experimento.
- Check final aprovado: formato **154 C++/52 QML**, runner **33/33**, formatter **18/18**, documentos **53 Markdown/83 requisitos/17 checks/76 etapas** e diff sem erros. O índice Git conserva os 34 movimentos em 100% de similaridade e somente o ajuste de tradução no Quick; nenhum processo de app/build/teste do workspace permaneceu ativo.

## Limites

Os testes UI usam `offscreen`, renderização software e estilo Basic, com backends/portas determinísticos para as jornadas. Isso preserva regressões de foco, teclado, acessibilidade, fila, preferências e recuperação, mas não comprova NVDA, DPI/alto contraste nativos, diálogo do sistema ou dispositivos físicos.

Não houve nova interação nativa, uso de áudio pessoal, benchmark, Linux, instalação/associação no host ou atualização de runtime/instalador neste recorte. A promoção não aprova release. O [incremento Windows posterior](windows-integration-tests.md) acrescenta os quatro bindings à variante e amplia o teste de fronteira, sem alterar esta evidência histórica de 276 registros. A [composição dos drivers instrumentados](process-tests.md) também tem gates de build/testes validados; smokes nativos, CI Windows e os gates de segurança, licenças e distribuição continuam separados no [plano](development-plan.md).
