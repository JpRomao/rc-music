# Fundação Aurora — checkpoint nativo de 2026-11-15

## Escopo entregue

Implementação incremental do [visual aprovado](../superpowers/specs/2026-11-15-aurora-shell-design.md), conforme o [plano da primeira frente](../superpowers/plans/2026-11-15-aurora-native-foundation.md) e o [ADR-0018](../architecture/decisions/0018-aurora-and-lidarr-removal.md):

- retirada do cliente, binding, painel, composição e testes exclusivos do Lidarr;
- aparência Aurora clara/escura, papel próprio de navegação, alto contraste e fontes nativas;
- navegação lateral Agora/Fila com a fila real e transporte persistente, sem recriar ou comandar o áudio ao navegar;
- acesso preservado aos painéis existentes de playlists, downloads e preferências; eles não são apresentados como novas páginas já migradas;
- textos QML de letras/playlists centralizados em `LyricsText.qml` e `PlaylistsText.qml`, com reuso de `DownloadText.qml` e `QueueText.qml`.

A mudança dos catálogos conserva os textos, parâmetros, identidade das faixas, consentimentos e valores persistidos `original`, `romanized`, `translation` e `off`. O componente interno de entrada em `DownloadLyricsPanel` passa a se chamar `LyricsInput` para não sombrear o catálogo `LyricsText`. Nenhuma alteração de schema, provider, sandbox, pin, ACK ou política de rede faz parte desta frente.

## Checks executados na retomada

Ambiente: worktree vinculada `out/product/brave-letras`, Qt 6.11.2, MSVC/toolset 14.44, Windows SDK 10.0.22621.0, Ninja/Release. Os testes visuais usam offscreen/software/Basic, fontes Windows e warnings fatais. Dados e processos de teste são isolados, sem músicas ou configuração pessoais.

| Check | Resultado observado |
| --- | --- |
| Red do gate original `essentialQmlTextIsCentralizedAndExtractable` | 2 casos Qt passaram, 1 falhou pelo catálogo legado; falha preservada |
| Red da classificação técnica e tradução QML real | 8 passaram, 15 falharam; inclui catálogo ausente e dados técnicos confundidos com rótulos |
| Contrato Qt completo após a correção | **56 passaram, 0 falharam, 0 skips**, incluindo o gate antes vermelho e instâncias reais dos catálogos/painéis sob tradutor |
| Quick completo | **432 passaram, 0 falharam, 0 skips** |
| CTest dos consumidores | **4/4**: Quick, fronteira UI, inicialização combinada e lifecycle de bandeja; os dois drivers de processo têm 3 casos Qt cada, incluindo lifecycle |
| Build Release ON | Produto, duas variantes de shell instrumentado, contrato e Quick construídos |
| QML lint de produção e teste | Zero warnings no fechamento |
| Formato do delta i18n | 1 C++ e 11 QML aprovados |
| Extração real `lupdate` | 137 mensagens de `LyricsText` e 39 de `PlaylistsText` extraídas |
| Preservação mecânica | Cinco painéis equivalentes ao estado anterior após desfazer apenas as substituições de catálogo, inserções de instância e renomeação interna; textos e lógica preservados |

Comandos principais, a partir da raiz da worktree e com o ambiente MSVC/Qt registrado:

```text
cmake --build out/product/dl-settings-tests --parallel 4 --target rc_music_ui_contract_tests rc_music_ui_quick_tests rc_music_test_ui_qmllint m0_qt_shell_ui_qmllint
out/product/dl-settings-tests/tests/ui/rc_music_ui_contract_tests.exe -o -,txt
cmake --build out/product/dl-settings-tests --parallel 4 --target m0_qt_shell rc_music_test_shell rc_music_fixture_test_shell
ctest --test-dir out/product/dl-settings-tests --output-on-failure -R "^rc_music\.(ui\.(quick|build-boundary)|process\.(combined-startup|tray-lifecycle))$"
```

Os comandos diretos Qt exigem o mesmo `ENVIRONMENT`/`ENVIRONMENT_MODIFICATION` do registro CTest. O prefixo Qt de destino é o SDK Network qualificado, não uma DLL encontrada incidentalmente no PATH. As receitas/recibos completos da rodada estão em `.superpowers/sdd/2026-11-15-aurora-native-foundation/resume-i18n/`, fora do produto versionado. O log completo original do Quick foi preservado de `Testing/Temporary/LastTest.log`, pois o JUnit limitou a saída a 1024 bytes; não houve rerun para reparar o relatório.

## Evidência anterior e falhas preservadas

O pacote `review-v3` anterior foi reconciliado por seus 46 hashes antes da edição. O parecer independente original e suas disposições continuam associados ao snapshot realmente revisado; não são renomeados como aprovação das fontes posteriores.

A rodada anterior 58 teve 5/6 CTest, com o gate i18n vermelho; este documento registra a correção causal e o Green posterior, sem apagar aquela falha. Na retomada, duas tentativas de compilar o novo teste falharam por dependência Qml não declarada e construtor explícito; não contam como Red comportamental. Uma primeira tentativa com os novos catálogos expôs o sombreamento por `LyricsText` inline e terminou sob warning fatal; a renomeação para `LyricsInput` corrigiu a causa. O lint também detectou uma referência de catálogo incorreta na fila, corrigida antes do Green final. Todos esses logs foram preservados.

Não foram repetidos por hábito os testes de presenter, core, rede, codecs ou segurança. As capturas claras/escuras e os testes anteriores de alto contraste/escala permanecem evidência histórica delimitada, não novas execuções desta rodada.

## Revisão independente do delta i18n

Parecer técnico sem achados Critical, Important ou Minor, vinculado ao manifest SHA-256 `8158b999ab247d62ad0154576db4a492824948c3a889a0a307989c6f9f431818` e ao diff `570f9136fdf753b5a527f839b01a0074f713f31ae95f8e77379942ab1230e1dd`. Os 14 after-hashes continuam iguais às fontes que receberam o parecer. A revisão leu fontes, integração e evidência existente; não executou novamente os testes e não requalifica a revisão Aurora anterior nem aprova instalador/release. As exclusões foram mantidas nos respectivos gates: providers/aquisição, dados pessoais, segurança, codecs/budgets, novos idiomas, acessibilidade física, Linux, instalação e encerramento global.

## Empacotamento privado e limites

O mantenedor autorizou commit local e geração de instalador para teste, **sem instalação**. A preparação deve preservar a edição `CompleteExperimental` e o perfil corrente de `setup-youtube-speed-4efd8ba`, sem regredir o resolver ou a extensão YouTube 0.1.5 para o candidato noaudio anterior. Main continua 0.1.0. Novos hosts recebem novos recibos de build; recibos antigos não podem ser transplantados para paths novos.

Associações permanecem OFF, sem matriz de formatos presumida. Ferramentas e preparação das extensões são opções separadas, inicialmente desmarcadas. Gerar o setup não habilita nem qualifica a aquisição pública e não autoriza instalar hosts, alterar Registro ou carregar extensões no Brave. O pacote foi gerado e conferido: [registro do instalador](../windows-installer.md#setup-aurora-de-d4137db). Setup privado de 93.052.834 bytes, SHA-256 `30ca2923f1b28f3abec2c42ef76b94ccde976756dbf0381c6276688fa1d12804`, em `out/product/aurora-installer-d4137db89c99/installer/RCMusic-Setup.exe` da worktree. O runtime válido `runtime-r2` tem 718 arquivos autenticados; smoke isolado passou na recusa de hooks, startup relocado e nos 17 cenários sintéticos de áudio (19 casos Qt, zero falhas/skips). A inspeção estática conferiu 739 membros e os 735 hashes de payload, mais o marcador gerado. O código/receita de geração é `d4137db`; o suplemento documental posterior não altera esses bytes. Os pacotes históricos não foram reconstruídos para auditoria.

A primeira montagem preservou um runtime parcial sem manifesto por resolução incorreta do Git num filho .NET. A invocação foi corrigida pelo PATH do Git nativo após o DevShell; a coleta válida ocorreu no destino novo `runtime-r2`, sem nova compilação completa. Uma tentativa intermediária foi recusada por outputs do build curto não ignorados; foram isolados por `.gitignore` local ao próprio build, sem ocultar fontes/WIP ou mudar a política compartilhada. Falhas e logs permanecem registrados.

## Não executado / não concluído

- Suíte global do produto, benchmarks ou nova qualificação de codecs/providers/sandbox.
- Instalação, upgrade, uninstall, Registro, associações, assinatura ou publicação.
- Aceite humano da UI Qt, NVDA, DPI físico/multimonitor, Linux ou máquina limpa.
- Jornada pessoal Brave → download → publicação/importação, audição ou arquivos pessoais.
- Biblioteca permanente, busca interna por novo provider e migração das demais superfícies do plano diretor.

Persistem os dois minors da revisão nativa anterior: qualificação/fallback Linux da fonte de símbolos e assert dedicado para falha de I/O no harness opt-in de capturas. Isso não impede o checkpoint Windows, mas não pode ser apresentado como cobertura executada. Este recorte não conclui M1, a release 0.1 ou as onze fases da reformulação.
