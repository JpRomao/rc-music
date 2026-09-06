# Bootstrap real sem dados de demonstração

**Data:** 2026-09-06. **Escopo:** segundo recorte de M1.1, preparando M1.10; etapas globais ainda abertas.

**Continuação:** a [promoção gráfica posterior](desktop-build.md) também separa fontes e configure na raiz. As limitações de composição experimental abaixo descrevem estes recortes anteriores, não o estado atual do build desktop.

Depois do [core promovido](core-build.md), o executável combinado Windows inicia sem construir `FixtureBundle` nem carregar os JSONs congelados de apresentação/fila. Este incremento implementa parte de `NFR-STACK-002` e preserva os contratos de UI de M1.10; não é ainda um executável gráfico independente do experimento.

## Mudança e compatibilidade

- Os seis construtores reais de `PlayerPresenter` recebem portas, serviços e preferências diretamente. Somente os dois construtores históricos alocam o bundle; não se fabrica um bundle vazio para o modo real.
- Sem preferências carregadas, o presenter aplica `Preferences{}` do núcleo, incluindo volume 0,8 e mudo desligado, antes de instalar callbacks. Uma recusa de volume ou mudo impede uma inicialização incoerente. Preferências persistidas seguem o fluxo existente.
- `ShellUiContract(QObject*)` fornece os mesmos defaults visuais, traduções, temas e política de movimento. O construtor histórico conserva validações e overrides aceitos. O parâmetro QObject é explícito para preservar `final`/`QML_UNCREATABLE` e a compatibilidade do registro Qt, sem criar um construtor default para o QML.
- `m0_qt_shell_assets` conserva os dois SVGs. Os JSONs `data/fixture.v1.json` e `data/queue.v1.json` passam para `m0_qt_fixture_data`, ligado somente ao shell histórico sem áudio e aos testes que os consomem. O executável combinado não liga nem inicializa esse recurso, com testes ON ou OFF.
- Os testes reais de presenter, áudio, bindings e Quick usam a API sem bundle; cenários históricos e suas assertions permanecem. Não houve mudança de QML, algoritmos de fila/reprodução, schemas, marca RC Music ou identificadores `MusicPlayer`.

## Red, Green e revisão

1. Red das novas APIs: compilação dos testes falhou porque os construtores reais sem bundle e o contrato visual com QObject ainda não existiam.
2. Red de integração: o smoke de startup passou a exigir ausência dos dois JSONs em `QFile::exists`; o executável antigo ainda os embutia e o teste falhou (0/1 CTest, um caso Qt Test falho).
3. Green: construtores e recurso separado implementados, callsites reais migrados e registros históricos mantidos. O build identificou também a incompatibilidade entre um novo construtor default e a classe QML `final`; exigir QObject explícito resolveu-a sem afrouxar a restrição.
4. Revisão independente confirmou defaults/paletas equivalentes, dereferências do bundle restritas ao modo histórico, preservação de preferências e ausência do alvo de JSONs no link real. O fortalecimento da inicialização de mudo tem regressão de sucesso e de recusa do backend.

## Checks executados

- Release combinado em `out/m0/builds/qt-audio/combined-shell-20260829-01`: executável e seis alvos de teste afetados recompilados; CTest proporcional **10/10**, em 79,17 s. Inclui presenter, contrato visual, Quick, áudio real com corpus sintético, bindings de diagnóstico/plataforma, reinícios de preferências/sessão, instância única e startup.
- Contagens internas conferidas: presenter **265/265**, contrato visual **29/29** e Quick **187/187**, sem falhas ou skips nesses três alvos. Isso não representa execução da suíte CTest completa.
- Shell histórico sem áudio em `out/m0/builds/qt-shell/support-msvc-14.44-release`: Release recompilado e smokes de recuperação de erro/bandeja **2/2**, em 1,53 s, offscreen.
- Release combinado `BUILD_TESTING=OFF` em `out/m0/builds/qt-audio/m43-production-release-20260901`: rebuild aprovado e CTest lista zero testes. Auditoria Ninja dos 257 comandos transitivos do executável não encontrou `m0_qt_fixture_data`; o QRC de assets contém exatamente os dois SVGs. A auditoria inspeciona o grafo, não presume ausência por arquivos de build antigos.
- Probe do executável OFF oculto/offscreen, com ambiente de DLLs reconstruído do CTest e raiz GUID: `--m0-test-config-root` recusado com exit 1, stdout/stderr vazios e configuração não criada. O processo terminou sem intervenção; não houve interação com a janela.
- `all_qmllint` aprovado; `clang-format --dry-run --Werror` aprovado nos 11 C++ alterados. Nenhum QML mudou, portanto não se repetiu `qmlformat`. `git diff --check` aprovado.
- Validação documental aprovada: **49 Markdown, 83 requisitos, 17 checks e 76 etapas**. Documentos de estado/plano mantêm explícita a promoção gráfica ainda pendente.

Na sessão MSVC já preparada conforme o [ambiente existente](../experiments/m0/candidates/qt/README.md#ambiente-de-build-msvc-144), os checks focados podem ser repetidos quando houver mudança relevante:

```powershell
cmake --build out/m0/builds/qt-audio/combined-shell-20260829-01 --target m0_qt_presenter_tests m0_qt_shell_contract_tests m0_qt_shell m0_qt_quick_tests m0_qt_local_audio_integration_tests m0_qt_diagnostic_binding_tests m0_qt_shell_platform_binding_tests --parallel 4
ctest --test-dir out/m0/builds/qt-audio/combined-shell-20260829-01 --output-on-failure -R '^m0\.qt\.(presenter\.initial-load|shell-ui-contract|quick\.shell-states|audio\.local-integration|diagnostics\.binding|shell-platform-binding|shell-executable\.combined-startup|preferences\.restart|session\.restart|single-instance\.executable)$'
cmake --build out/m0/builds/qt-audio/combined-shell-20260829-01 --target all_qmllint --parallel 4
pwsh -NoProfile -File scripts/validate-docs.ps1
git diff --check
```

O comando pressupõe a árvore de build já configurada com os runners de processo existentes; não configura nem baixa SDK. Os testes usam fakes ou mídia sintética, sem os áudios pessoais do usuário.

## Limites e próximo recorte

No fechamento do segundo recorte (`8fea81a`), a dependência dos JSONs em runtime havia sido removida, mas `FixtureRepository.cpp` e `DeterministicPlaybackBackend.cpp` ainda apareciam no grafo OFF. A atualização abaixo trata essa dependência compilada. O configure continua autenticando inputs M0 e a unidade gráfica/adapters/QML ainda deve ser promovida para a raiz, preservando os testes históricos e uma implementação única.

Core, corpus, protocolo e harness autenticados não foram alterados. Não houve benchmark, A/B, Linux, interação nativa, instalação/registro no host, dependência nova ou rebuild de SDK. O instalador `a43faa1` permanece anterior a este incremento. Segurança/licenças, acessibilidade nativa, pacote e gates amplos de M1/M4/M5 continuam abertos.

## Atualização — compilação de produção sem cenários históricos

O terceiro recorte de M1.1 retira do executável combinado `BUILD_TESTING=OFF` os fontes de repositório de fixture, modelo de fila histórico e backend determinístico, suas bibliotecas/headers de suporte e os construtores/projeções exclusivas desses cenários. `LocalQueueListModel`, filas, navegação, persistência e adapters reais permanecem.

`RC_MUSIC_ENABLE_FIXTURE_SCENARIOS` é uma capacidade interna derivada pelo CMake: habilitada nos builds com testes ou no shell histórico sem áudio, nunca na composição combinada OFF. A definição é pública nos alvos de suporte/apresentação para que consumidores, AUTOMOC e registro QML compartilhem o mesmo layout C++. Não misturar objetos/bibliotecas ON e OFF; cada configuração usa diretório próprio. Essa é exclusão de suporte de teste, não uma segunda implementação do player ou um novo serviço de cenários no domínio.

A API QML permanece idêntica. Em produção, `usesFrozenFixture` é falso e as operações públicas históricas `loadFrozenQueue`, `skipUnavailable` e `advanceOneTick` ficam inertes. Os construtores e helpers C++ ligados ao backend de demonstração deixam de ser declarados/compilados. No ON, seus comportamentos, validações, defaults, ownership Qt e ordem de sinais são preservados; os getters reais usam as mesmas projeções existentes.

O novo [check de fronteira de produção](../scripts/Test-ProductionBoundary.ps1) inspeciona o grafo transitivo Ninja, verifica fontes/link reais e recusa fontes, biblioteca, include de suporte e macro históricos. Exige explicitamente Release combinado OFF e falha fechado se o cache/grafo não puder ser lido; não usa ausência de objetos antigos como evidência. O Red confirmou as três fontes históricas, `rc_music_test_support` e o include de suporte no grafo anterior. Após a mudança, o Green inspecionou 249 comandos sem essas dependências e o build Release OFF foi aprovado.

Checks de fechamento deste terceiro recorte:

- Release combinado ON recompilado e CTest proporcional **12/12**, em 84,11 s: os dez alvos do recorte anterior mais os bindings de mídia e energia do sistema. Presenter **265/265**, contrato visual **29/29** e Quick **187/187**, sem falhas/skips nesses três alvos; nenhuma assertion foi removida ou flexibilizada. Não foi executada a suíte completa.
- Release do shell histórico sem áudio recompilado; recuperação de erro e lifecycle de bandeja **2/2**, em 1,36 s, offscreen.
- Produção OFF recompilada e CTest com **zero testes**. As dependências registradas pelo Ninja dos objetos de `PlayerPresenter.cpp`, `ShellUiContract.cpp` e `main.cpp` estão `VALID` e não contêm os três headers históricos; headers reais/modelo local permanecem presentes.
- O executável OFF recompilado recusou `--m0-test-config-root` com exit 1, stdout/stderr vazios e raiz solicitada ausente, em probe GUID oculto/offscreen; terminou espontaneamente, sem processo residual ou configuração criada.
- Catálogos QML gerados ON/OFF idênticos byte a byte: 54.947 bytes, SHA-256 `e8534f10016ff84190bfde8da909d3c692d96794328db1d05750cabdb20887df`. Isso comprova a superfície declarativa, não substitui testes de comportamento ou acessibilidade nativa.
- `all_qmllint` ON/OFF aprovados; formato dos cinco C++ alterados e diffcheck aprovados. Nenhum QML foi alterado. Validação documental: **49 Markdown, 83 requisitos, 17 checks e 76 etapas**.
- Revisão independente sem achados P0–P2 no escopo de inicialização, navegação, exclusão condicional e propagação de layout; núcleo, testes existentes e inputs autenticados permaneceram intactos.

```powershell
pwsh -NoProfile -File scripts/Test-ProductionBoundary.ps1 -BuildDirectory out/m0/builds/qt-audio/m43-production-release-20260901
ctest --test-dir out/m0/builds/qt-audio/combined-shell-20260829-01 --output-on-failure -R '^m0\.qt\.(presenter\.initial-load|shell-ui-contract|quick\.shell-states|audio\.local-integration|diagnostics\.binding|shell-platform-binding|system-media-controls\.binding|system-power-events\.binding|shell-executable\.combined-startup|preferences\.restart|session\.restart|single-instance\.executable)$'
```

O próximo recorte é promover a composição gráfica/adapters/QML e separar seu configure dos caminhos/autenticação M0. O build ON continua contendo o suporte histórico de teste por intenção; não apresentá-lo como artefato de produção. Isso não fecha M1 nem gera instalador, distribuição ou prova de interação nativa.
