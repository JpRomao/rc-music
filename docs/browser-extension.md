# Extensão principal Brave/Chromium

**Estado:** cliente, biblioteca Qt Core de recepção e host/transporte Native Messaging Windows opcionais implementados. A configuração de produção do host fica vazia/desabilitada e o player de produção continua sem ingresso/serviço remoto ativo. Não há extensão/host instalados, download remoto ativado, engine interna ou prova no Brave real; não é uma entrega funcional instalada nem extensão publicada.

Contratos: [ADR-0006](architecture/decisions/0006-browser-editions-and-extensions.md), [ADR-0007](architecture/decisions/0007-main-extension-boundary.md), [ameaças e limites](security/browser-extension-threat-model.md); R1.7/FR-051–053. YouTube segue extensão independente, ainda não implementada.

## Implementação do cliente

O diretório [extensions/chromium/main](../extensions/chromium/main/manifest.json) contém o pacote-fonte MV3, sem build npm, bibliotecas externas, rede própria ou engine embarcada. Não foi carregado no Brave do usuário nem incluído nos instaladores. O playback e os dados `MusicPlayer` permanecem intactos; o cliente será compartilhado pelas edições Lite/Completa quando a ponte comum estiver integrada.

- Abrir o popup permite inspecionar temporariamente a página principal autorizada. Áudios, vídeos, elementos `source` e links MP3/MP4/M4A geram candidatos deduplicados, não downloads.
- Enquanto o popup está aberto, alterações DOM/eventos de mídia atualizam a lista de forma limitada. Ao fechar ou navegar, observadores/timers são encerrados. Reinjeção não duplica a infraestrutura. Um único listener inerte de conexão permanece no mundo isolado até o documento ser descartado, sem observação, coleta ou timer na ausência do popup.
- Lista com checkboxes, seleção individual/múltipla até 20, confirmação de uso autorizado, contador, truncamento e mensagens pt-BR. Títulos são texto literal; não renderizar HTML, previews remotos ou query strings da página.
- Somente “Baixar selecionados” produz pedido. O popup verifica aba, documento e URL atuais antes de enviar; novos itens não entram na seleção automaticamente e remoção/mudança relevante invalida consentimento.
- `policy.mjs` valida URLs/schema/limites; `native-client.mjs` abre o canal fixo somente no envio, valida resposta correlacionada e encerra sem retry automático. Nenhum comando arbitrário, path de saída, cookie ou header atravessa o contrato.
- Host ausente, resposta malformada, timeout e desconexão aparecem como falha/recebimento não confirmado. ACK significa apenas pedido aceito; conclusão/conversão/importação pertencem ao serviço futuro do app.

## Protocolo nativo de desenvolvimento

Host planejado `com.rcmusic.media`, não registrado. Pedido v1:

```json
{
  "schemaVersion": 1,
  "type": "download.request",
  "requestId": "01234567-89ab-4cde-8fab-0123456789ab",
  "items": [{ "url": "https://media.example.test/audio.mp3", "title": "Áudio", "kind": "audio" }]
}
```

`kind` é dica `audio`/`video`, não prova de codec ou segurança. O contrato pretende saída MP3; o serviço futuro precisa validar bytes e converter áudio de MP4, sem renomear extensão. O pedido não envia URL da página, histórico ou seleção não autorizada.

Resposta aceita tem exatamente `schemaVersion: 1`, `type: "download.response"`, mesmo `requestId`, `status: "accepted"` e `jobIds` distintos (um por item). IDs de job usam 1–80 caracteres ASCII alfanuméricos/`_`/`-`. Rejeição usa `status: "rejected"` e `code` de `unsupported_source`, `invalid_request`, `busy`, `not_available` ou `limit_exceeded`, sem `jobIds` ou texto livre. A decisão do lote é atômica; não existe aceitação parcial silenciosa.

Receber timeout ou desconexão não prova que o host não aceitou o pedido. Não reenviar automaticamente. A futura fila do app deverá permitir consulta e cancelamento e deduplicar IDs, sobrevivendo ao fechamento do popup. `OpenSources` continua estritamente local e não recebe este JSON.

## Checks locais

### Cliente

Usar Node.js já disponível (validado com v24.20.0); sem `npm install` ou downloads:

```powershell
node --test tests/extensions/*.test.mjs
Get-ChildItem extensions/chromium/main/*.mjs, extensions/chromium/main/*.js | ForEach-Object { node --check $_.FullName; if ($LASTEXITCODE -ne 0) { throw 'Falha de sintaxe da extensão' } }
pwsh -NoProfile -File scripts/validate-docs.ps1
pwsh -NoProfile -File scripts/validate-traceability.ps1
git diff --check
```

O runner de extensão é separado dos comandos C++: Node é ferramenta de desenvolvimento deste componente opcional, não dependência de compilação ou execução da Lite. Alterar a extensão exige seus checks acima. Nenhum QML mudou neste incremento; lint/testes de UI nativa e instalação continuam gates separados.

### Biblioteca nativa Windows

O alvo opcional `rc_music_browser_integration` compila [codec](../src/adapters/NativeDownloadCodec.cpp), [framing](../src/adapters/NativeMessageFrame.cpp), [sessão](../src/adapters/NativeDownloadSession.cpp) e o model de jobs com Qt Core 6.11.2 já provisionado. A [coordenação de downloads](download-jobs.md) implementa a porta pura `DownloadSubmission`: aceita ou recusa o lote inteiro e devolve IDs, sem baixar/converter sincronicamente. A integração de sessão usa esse serviço real nos testes, mas execução/importação ainda são falsas e não há conexão instalada.

`RC_MUSIC_BUILD_BROWSER_INTEGRATION` é OFF por padrão e não depende da opção desktop. O alvo não está ligado ao player, não distribui engine, não registra host e não instala arquivos. Com testes OFF não exige Qt Test; o core com ambas opções OFF continua sem Qt. Isso preserva a composição leve, mas ainda não prova os futuros pacotes Lite/Completa.

Na sessão MSVC descrita em [build do core](core-build.md#build-windows), com ferramentas locais:

```powershell
$rcSource = (Resolve-Path .).Path
$rcQt = Join-Path $rcSource 'q/f75eb9bea8280844c6d1a2b3f4/sdk'
$rcCatch2 = Join-Path $rcSource 'out/m0/toolchains/Catch2-3.15.3'
$rcNinja = 'C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe'
cmake -S . -B out/product/browser-integration-tests -G Ninja -DCMAKE_BUILD_TYPE=Release -DRC_MUSIC_BUILD_BROWSER_INTEGRATION=ON -DRC_MUSIC_BUILD_DESKTOP=OFF -DBUILD_TESTING=ON "-DCMAKE_PREFIX_PATH=$rcQt" "-DRC_MUSIC_CATCH2_SOURCE_DIR=$rcCatch2" "-DCMAKE_MAKE_PROGRAM=$rcNinja"
if ($LASTEXITCODE -ne 0) { throw 'Configure da ponte falhou' }
cmake --build out/product/browser-integration-tests --target rc_music_NativeDownloadCodec_tests rc_music_NativeMessageFrame_tests rc_music_NativeDownloadSession_tests rc_music_NativeDownloadService_tests rc_music_DownloadJobsModel_tests --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Build da ponte falhou' }
ctest --test-dir out/product/browser-integration-tests -R '^rc_music.browser.' --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Testes da ponte falharam' }
```

O CTest configura PATH e saída Qt dos cinco executáveis; não precisa de janela/áudio/rede. O filtro é obrigatório se só esses alvos foram compilados, pois o configure também registra os testes do core e do serviço puro. O build independente sem testes usa outro diretório, `-DBUILD_TESTING=OFF`, e alvo `rc_music_browser_integration`. Para incluir a suite Catch2 de coordenação, usar os [checks de jobs](download-jobs.md#checks-locais-windows).

### Host e transporte Native Messaging Windows

`RC_MUSIC_BUILD_NATIVE_MESSAGING` é **OFF por padrão**, Windows-only e independente do desktop. Quando ON, inclui a biblioteca de recepção acima e o alvo separado [rc_music_native_messaging](../cmake/RcMusicNativeMessaging.cmake), com Qt Network 6.11.2 para `QLocalServer`/`QLocalSocket`; não adiciona Qt Network ao alvo Qt Core `rc_music_browser_integration`, engine, HTTP server ou workflow remoto ao player. O executável [rc_music_native_host](../src/app/native_host_main.cpp) usa o mesmo codec/sessão v1 e encaminha admissão assíncrona ao dono persistente dos downloads, sem executar o serviço ou acessar seus stores.

- **Configuração confiável:** `RC_MUSIC_NATIVE_MAIN_ORIGIN` e `RC_MUSIC_NATIVE_ENDPOINT` são configuração C++ gerada no build, nunca JSON, argumentos, ambiente ou página. Ambos vazios produzem host desabilitado, com exit 2 antes de `QCoreApplication`, stdin/stdout ou conexão local; configuração parcial é recusada. A origem deve ter exatamente `chrome-extension://` + 32 letras `a`–`p` + `/`; o nome dedicado aceita somente 1–96 caracteres ASCII alfanuméricos/`_`/`-`.
- **Invocação:** a origem crua de `argv` deve coincidir exatamente com a configurada. Só é permitido um argumento adicional `--parent-window=` seguido de uint64 decimal canônico, sem sinal, espaços, zeros iniciais ou overflow. Esse valor é metadado inerte, não HWND validado nem autenticação do Brave.
- **IPC privado, distinto de OpenSources:** preâmbulo fixo de **61 bytes** — oito bytes ASCII `RCMDLIPC`, um byte de versão `1` e a origem exata de 52 bytes — seguido do mesmo schema/frame v1 uint32 little-endian, com JSON reserializado estritamente a partir do DTO tipado por `encodeNativeDownloadRequest`. O lote e as strings de URL/query/título são preservados exatamente; bytes JSON originais, whitespace e ordem original das chaves não são preservados. O dono usa `UserAccessOption`, backlog/conexões limitados a oito, buffers limitados antes de ler e prazos totais sem renovação por fragmentos. Valida origem/schema antes de encaminhar a `AsyncDownloadSubmission`; `Pending` não gera mensagem nem ACK. Somente o controller durável pode tornar aceitação observável após `Saved` correspondente.
- **Observação e saída:** o cliente conecta uma única vez, sem retry, autolaunch do player ou reenvio. Uma resposta só é reconhecida após EOF do peer, com corpo de até **4.096 bytes**, frame único e schema/UUID/quantidade de jobs exatos. Resposta parcial, extra, contraditória, timeout ou falha após escrita produzem recebimento incerto, não ACK/rejeição inventados. EOF/shutdown fecham observadores e ingresso, não cancelam jobs do dono; `stopped` exige callbacks/recursos drenados. A composição deve fechar ingresso antes do executor e manter controller/stores vivos até a parada reconhecida.
- **I/O do host:** stdin/stdout são handles Win32 binários `FILE_TYPE_PIPE`, sem CRT/texto, logs ou flush em stdout. O leitor exclusivo consome um snapshot limitado a corpo de 256 KiB + prefixo de quatro bytes + um byte de excesso, recusando snapshots maiores antes de alocar; nunca publica snapshot parcial. O relógio da sessão é amostrado imediatamente **depois** de `readAvailable`, antes de validar/admitir, para não usar um instante anterior à leitura e admitir após o deadline. Um writer privado conserva seu buffer, tem prazo de 2 s e repete `CancelSynchronousIo` até parada física. Um watchdog independente de **15 s** cobre leitura/teardown que não coopere com cancelamento e termina **somente o próprio processo do host** com exit 2, nunca o player, Brave ou outro PID.

Limitar o pipe ao usuário e comparar a origem não autentica outro processo do mesmo usuário nem prova lançamento pelo browser. A [reserva nativa abaixo](#reserva-nativa-do-nome-do-pipe) exige primeira instância e conserva o nome; identidade instalada, disponibilidade entre usuários, propriedade/upgrade/registro do manifest e o formato real de HWND/`--parent-window` do Brave continuam gates. Não habilitar a configuração de produção para contorná-los. Lite/Completa usam a mesma futura composição protegida; navegação interna e YouTube permanecem separados.

#### Exclusividade cooperativa do dono

[WindowsNativeDownloadLease](../src/adapters/WindowsNativeDownloadLease.hpp) reserva a identidade antes de `QtNativeDownloadServer::listen`, sem alterar a eleição do player/`OpenSources`. O nome é `Global\RCMusic.NativeDownloadOwner.v1.` seguido de SHA-256 sobre SID textual do `TokenUser` em UTF-8, NUL e endpoint ASCII em minúsculas. Origem da extensão, edição, perfil e CWD não entram na chave: um mesmo usuário/endpoint não deve admitir lotes em donos concorrentes por mudar esses campos. O namespace Global evita que sessões Windows do mesmo usuário obtenham reservas distintas. [Namespaces de objetos Windows](https://learn.microsoft.com/en-us/windows/win32/termserv/kernel-object-namespaces).

A reserva exige objeto recém-criado por `CreateMutexExW`, flags zero, handle não herdável e DACL protegida com direitos completos somente para usuário e SYSTEM. `ERROR_ALREADY_EXISTS`, colisão com outro tipo de objeto ou erro recusam prontamente; qualquer handle aberto é fechado. Não há espera, `ReleaseMutex`, ownership de thread ou aquisição recursiva: a exclusividade depende da criação atômica e da retenção do handle. [CreateMutexExW](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-createmutexexw).

Falha real de `listen` libera a reserva da tentativa. Se listen teve êxito e um callback o fechou reentrantemente, a reserva permanece até destruir o servidor. Depois de `listen` bem-sucedido, `beginShutdown` e `stopped` fecham/drenam o ingresso, mas **não** liberam a reserva: ela dura até a destruição do servidor. A composição mantém esse objeto vivo até controller, stores e workers emprestados pararem; liberação antecipada permitiria outro dono enquanto o anterior ainda grava ou processa jobs. Morte do processo fecha seus handles pelo Windows, sem marker de arquivo para remover. O objeto desaparece somente após o último handle fechar; um observador que o mantenha aberto pode continuar impedindo nova reserva.

O marker é cooperativo e seu hash não é segredo. A reserva nativa complementar abaixo recusa pipe preexistente, mas processo hostil do mesmo usuário ainda pode precriar recursos para negar disponibilidade ou criar outra instância sem FIRST. A chave por usuário também não resolve disponibilidade entre usuários no namespace global dos pipes, cujos nomes são case-insensitive. `UserAccessOption` restringe acesso, mas `QLocalServer` no Windows permite múltiplos listeners de mesmo nome; instalação/namespace/autenticação continuam gates. [Nomes de pipes Windows](https://learn.microsoft.com/en-us/windows/win32/ipc/pipe-names), [QLocalServer 6.11.2](https://doc.qt.io/qt-6.11/qlocalserver.html).

#### Reserva nativa do nome do pipe

Em 2026-11-03, [WindowsNativeDownloadPipeReservation](../src/adapters/WindowsNativeDownloadPipeReservation.hpp) passou a criar uma instância Win32 com `FILE_FLAG_FIRST_PIPE_INSTANCE` depois do marker e antes de `QLocalServer::listen`. Pipe preexistente, mesmo raw/ilimitado e sem marker, recusa startup sem dispatch. A DACL protegida concede direitos somente ao usuário atual e SYSTEM, com handles não herdáveis e `PIPE_REJECT_REMOTE_CLIENTS`. Atributos byte/duplex/overlapped, instâncias ilimitadas, buffers zero e timeout 3000 coincidem com Qt 6.11.2. [CreateNamedPipe](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-createnamedpipea).

Um cliente privado conecta imediatamente à primeira instância por `CreateFileW`, sem wait/retry; ambos os PIDs consultados devem ser o próprio processo antes de publicar a reserva. A âncora permanece ocupada por esse cliente, portanto os clientes legítimos alcançam as instâncias servidas pelo Qt. Nenhum read/write/ConnectNamedPipe/flush é iniciado no par privado; não há OVERLAPPED pendente para cancelar. Listener/conexões fecham antes do par, e este antes do marker. `beginShutdown`/`stopped` conservam nome e marker até a destruição; falha de startup anterior à admissão libera a tentativa. Os oito slots de sessões e os limites/ACK existentes permanecem.

O cliente do host também usa uma única chamada nativa `CreateFileW` e adota o HANDLE no `QLocalSocket` com reader já limitado; não usa o loop síncrono `WaitNamedPipe(5000)` de `connectToServer`. Busy/ausência antes da escrita retorna `NotAvailable` prontamente, sem retry/autolaunch. SQOS anônimo impede impersonation por esse handle; falha após possível escrita continua incerta, com framing/schema/EOF e ACK durável preservados.

Essa reserva prova primeira criação e retenção do nome, não autenticação do Brave ou de peers locais. O PID duplo valida somente o par privado. Direitos de escrita do usuário também permitem `FILE_CREATE_PIPE_INSTANCE`; outro processo desse usuário pode criar instância adicional ou manter handle que adie liberação. Sem identidade instalada/autenticação e namespace revisados, a produção continua desabilitada. [Direitos de pipes](https://learn.microsoft.com/en-us/windows/win32/ipc/named-pipe-security-and-access-rights).

TDD: os quatro casos novos do transporte tiveram **3 passagens/3 falhas** incluindo init/cleanup: startup indevido, nome liberado em stopped e bloqueio observado de **5015 ms**. Oito conexões/nona recusada/reposição já passaram no Red. Reserva stub teve **2 passagens/23 falhas**; processo real **6 passagens/1 falha** confirmou a liberação prematura do nome. Green: transporte **35/35**, reserva/DACL **25/25**, owner/processos **7/7**. A seleção completa native+codec passou **10/10 CTest**, sem skips, incluindo host/processos **32/32**. A composição instrumentada host → Main/painel/stores passou separadamente **1/1 CTest / 3/3 Qt Test**. Logs em `out/product/test-runs/native-pipe-reservation-20261103/`. Herança e falhas das consultas PID têm revisão da fonte; não são testes externos de impersonation ou de falha nativa injetada. O caso antigo chamado `failedListen...` agora recusa na reserva, sem comprovar falha do listen Qt posterior.

Checks reproduzíveis na sessão MSVC do [core](core-build.md#build-windows), usando os caminhos locais acima e outro cache. A identidade de produção é explicitamente vazia; o CMake cria **outro executável de teste**, com identidade sintética compilada e endpoint próprio derivado do cache, sem overrides no host de produção:

```powershell
$rcSource = (Resolve-Path .).Path
$rcQt = Join-Path $rcSource 'q/f75eb9bea8280844c6d1a2b3f4/sdk'
$rcCatch2 = Join-Path $rcSource 'out/m0/toolchains/Catch2-3.15.3'
$rcNinja = 'C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe'
$rcFormat = 'C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/Llvm/x64/bin/clang-format.exe'
Get-ChildItem src/adapters/NativeHostLaunch.*, src/adapters/NativeDownloadCodec.*, src/adapters/NativeDownloadIpcProtocol.hpp, src/adapters/QtNativeDownload*.?pp, src/adapters/WindowsNative*.?pp, src/app/native_host_main.cpp, src/app/NativeHostInputStep.hpp, tests/qt/NativeHost*Tests.cpp, tests/qt/NativeHostWatchdogHelper.cpp, tests/qt/NativeDownloadOwner*.cpp, tests/qt/QtNativeDownloadTransportTests.cpp, tests/qt/WindowsNativeMessagingIoTests.cpp, tests/qt/WindowsNativeDownloadLeaseTests.cpp | ForEach-Object { & $rcFormat --dry-run --Werror $_.FullName; if ($LASTEXITCODE -ne 0) { throw 'Formato do host nativo falhou' } }
cmake -S . -B out/product/native-messaging-tests -G Ninja -DCMAKE_BUILD_TYPE=Release -DRC_MUSIC_BUILD_NATIVE_MESSAGING=ON -DRC_MUSIC_BUILD_DESKTOP=OFF -DBUILD_TESTING=ON -DRC_MUSIC_NATIVE_MAIN_ORIGIN= -DRC_MUSIC_NATIVE_ENDPOINT= "-DCMAKE_PREFIX_PATH=$rcQt" "-DRC_MUSIC_CATCH2_SOURCE_DIR=$rcCatch2" "-DCMAKE_MAKE_PROGRAM=$rcNinja"
if ($LASTEXITCODE -ne 0) { throw 'Configure do host nativo falhou' }
cmake --build out/product/native-messaging-tests --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Build Release do host nativo falhou' }
ctest --test-dir out/product/native-messaging-tests -R '^(rc_music.native.|rc_music.browser.NativeDownloadCodec$)' --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Testes do host nativo falharam' }
pwsh -NoProfile -File scripts/validate-docs.ps1
if ($LASTEXITCODE -ne 0) { throw 'Documentação inválida' }
pwsh -NoProfile -File scripts/validate-traceability.ps1
if ($LASTEXITCODE -ne 0) { throw 'Rastreabilidade inválida' }
git diff --check
if ($LASTEXITCODE -ne 0) { throw 'Diff inválido' }
```

O filtro acima é do host/codec, não a suíte inteira nem a composição gráfica. Para build sem testes, usar cache novo com `-DBUILD_TESTING=OFF` e compilar `rc_music_native_host`; configuração vazia continua desabilitada e não exige Qt Test. Essa receita não instala/lança setup, registra Native Messaging, carrega extensão ou acessa navegador/rede/conversor.

### Contrato de recepção

- Uma mensagem por sessão, prefixo de 32 bits little-endian do Windows, corpo de 1 byte a 256 KiB validado antes da reserva. Recepção fragmentada tem deadline total de 5 s; EOF, excesso, segunda mensagem ou relógio regressivo fecham sem nova admissão.
- Origem deve coincidir exatamente com uma extensão configurada (`chrome-extension://` + 32 letras `a`–`p` + `/`). A configuração é entrada confiável do futuro host, não do JSON nem derivada do próprio chamador. Isso não autentica processos maliciosos do mesmo usuário.
- JSON UTF-8 estrito, profundidade limitada, chaves duplicadas inclusive escapadas recusadas antes do parser Qt, schema/campos/tipos exatos e lote atômico. URLs já normalizadas pelo cliente preservam escapes/query; não reserializar via QUrl e alterar assinaturas. Títulos usam até 200 unidades UTF-16, sem controles/bidi ou surrogates isolados; domínios têm rótulos até 63 e nome até 253 caracteres sem ponto final.
- A sessão fecha a admissão antes de chamar a porta, inclusive em reentrância. Exceção/receipt contraditório não geram ACK falso. O deadline de montagem não preempta uma implementação bloqueante de `submit`; a porta exige enfileiramento rápido e não faz downloads.
- O overload assíncrono observa `AsyncDownloadSubmission` por UUID, sem dirigir IO/tick/execução. Pending não tem ACK/IDs nem mensagem própria no wire; completion válida responde v1, com um ID distinto por item. Há prazo total próprio de 5 s inclusivos depois do frame completo, sem renovação por poll; clock regressivo falha fechado. EOF/entrada extra/timeout só fecham o observador, sem cancelar/repetir trabalho do dono.
- O serviço comum mantém idempotência entre conexões e coordena progresso/cancelamento via portas. A [admissão durável](download-jobs.md#admissão-durável-assíncrona) conecta controller/worker/store e só torna aceitação observável após Saved matching; novo startup restaura histórico inerte. O overload síncrono não oferece essa garantia de durabilidade e não serve ao host durável. O transporte binário/IPC opcional acima já existe; identidade instalada e composição protegida de produção continuam pendentes. Model/binding/painel comuns existem no shell instrumentado, sem engine de navegação. Nenhuma URL é aberta pela biblioteca de recepção.

## Evidência do incremento

TDD em 2026-09-08: policy começou com 9 falhas de normalização/seleção/recusa ausentes; cliente nativo com 7 falhas de envio/ACK/timeout ausentes; detector falhou por ausência do listener (`0 !== 1`); controller iniciou com 17 cenários sem comportamento. As implementações foram adicionadas depois dos Reds. A revisão acrescentou regressões para arrays esparsos, mutações contínuas e seleção durante verificação assíncrona. Resultado consolidado: **71/71 testes verdes** (26 detector, 25 popup/integração, 10 policy, 7 canal nativo, 3 manifesto), sem skips. Sintaxe dos scripts de produção/testes e `git diff --check` passaram. Documentação: 65 Markdown, 83 requisitos, 17 checks e 80 etapas; rastreabilidade: 15 planejados, 66 em andamento, 2 comprovados. Nenhum requisito foi promovido a comprovado por este incremento.

Os testes executam detector real em VM com DOM/Chrome/timers falsos, controller/DOM do popup, política real, canal nativo falso e manifesto/CSP estáticos. Não dependem de internet, contas, mídia pessoal, instalação ou Registry. Isso não comprova carregamento MV3/CSP no Brave real nem visualização nativa.

### Incremento nativo de 2026-09-08

Depois do baseline do cliente acima, os stubs C++ confirmaram Red comportamental em pedidos válidos/respostas ausentes, montagem de frames (3 passaram/32 falharam) e sessão (2 passaram/18 falharam). A saída do Qt Test precisou das mesmas flags de logging dos runners existentes; isso não exigiu instalar/copiar DLLs. Regressões adicionais expuseram tolerâncias do Qt para escapes, números JSON e BOM, além de divergências de normalização de URL. O cliente teve Red para percent inválido, surrogate isolado/corte de emoji e nomes DNS excessivos, seguido das correções.

- Build Release MSVC 19.44/toolset 14.44, Qt Core/Test 6.11.2, `/W4 /WX`: **3/3 alvos CTest**, **193/193 casos Qt** (137 codec, 35 framing, 21 sessão), sem skips. Diretório local `out/product/browser-integration-tests`; não foi alegada execução dos testes do core desse diretório.
- Extensão: **110/110 testes**, incluindo **36 vetores compartilhados** em [native-protocol-vectors.json](../tests/extensions/native-protocol-vectors.json). BOM inicial é escapado no arquivo da fixture para o loader de teste não o remover antes da validação; testes byte-level também cobrem o BOM UTF-8 literal.
- Regressão do núcleo sem Qt/ponte: rebuild de `out/product/m1-core-release-20260905`, **227/227 CTest**, opção da ponte OFF e ausência de Qt/ponte no grafo.
- Componente Release com `BUILD_TESTING=OFF`: biblioteca compilada em `out/product/browser-integration-release`, zero testes e nenhuma dependência Qt Test/Catch2/WebEngine no grafo. Não é runtime nem instalador novo.
- Formato dos dez C++ novos, sintaxe JS, validação documental (65 Markdown/83 requisitos/17 checks/80 etapas), rastreabilidade e `git diff --check` aprovados. Revisão independente sem achados materiais restantes no recorte.

Nenhum requisito foi promovido a comprovado: 15 planejados, 66 em andamento, 2 comprovados. Não houve benchmark, execução instalada/nativa, mídia pessoal, rede de download ou alteração do player. Executável de host, admissão idempotente real e workflow completo continuam próximos trabalhos, não resultados deste incremento.

### Incremento de host e IPC de 2026-10-02

TDD do transporte começou com stubs: **2 casos passaram/15 falharam** pelos contratos ausentes. Regressões posteriores expuseram reentrância de polling/shutdown e clock, preservação do observador em callbacks e reconhecimento prematuro de parada; a revisão independente acrescentou três Reds antes das correções. O alvo `rc_music_QtNativeDownloadTransport_tests` terminou focado em **27/27 casos Qt**, sem skips, **1/1 CTest**, com socket real e owner assíncrono falso. Isso comprova o contrato do transporte, não persistência ou execução real de downloads.

O adapter Win32 teve **13/13 casos Qt** focados. O build Release MSVC `/W4 /WX` do host/watchdog passou e `rc_music.native.host-process` ficou verde em **1/1 CTest** (8,81 s), usando o main/stdio/IPC reais contra peer de teste independente e helper próprio do watchdog; a contagem Qt confirmada no log final foi **32/32**, sem skips. Não transferir esses checks para Brave instalado. O cliente JS foi reexecutado: **110/110**, sem skips. O alvo de recepção Qt Core isolado recompilou em Release/testes OFF, com zero testes e sem Qt Network no grafo; o transporte mantém sua dependência separada. Formato dos C++ alterados, QML inalterado com qmllint e validação documental/rastreabilidade passaram nos gates proporcionais.

O ensaio host → IPC → Main/controller instrumentados, com sessão/checkpoint reais e **somente publicação sintética**, ficou verde: a seleção host-process + downloads-composition + native-downloads-composition passou **3/3 CTest** em 11,69 s. Exerceu admissão/replay do mesmo UUID, UI/importação durável e novo startup, sem duplicar jobs/fila. A composição de teste usa identidade sintética compilada e um sentinel de saída fixo dentro do TEMP próprio, acionado pelo driver somente depois dos ACKs/replays; não há comando de controle adicional no IPC ou no host de produção. O driver observa o host já encerrado depois da resposta; isso não comprova o instante exato da parada física do leitor no momento do ACK. Um alvo separado com configuração vazia comprova recusa antes de I/O; não transfere identidade instalada ao host de produção.

A consolidação Release ON/build + `all_qmllint` passou, seguida de **470/470 registros CTest**, zero falhas, em 138,98 s. Incluiu `rc_music.process.native-downloads-composition` **1/1** em 1,95 s e host-process **1/1** em 9,11 s. Esses tempos são duração de checks com trabalho concorrente, não benchmark ou aprovação de desempenho. Não habilitam rede/conversor, instalação/Registry, extensão/browser, engine interna ou identidade de produção e não aprovam release. Os probes finais do desktop OFF/host de produção com defaults vazios têm registro separado; não inferir sua conclusão pelo fixture desabilitado no ON. O [checkpoint central](development-plan.md#17-próxima-sequência-segura) registra o fechamento e seus riscos residuais.

### Incremento de reserva cooperativa de 2026-10-02

As regressões novas começaram Red: transporte **27 passagens/3 falhas**, depois **27/4** com failed-listen contra stub; lease **14/9** com aquisição ausente; processo **3/4** contra servidor anterior sem reserva. A seleção focada passou **3/3 CTest em 4,82 s**, sem skips: transporte **31/31 QtTest**, `WindowsNativeDownloadLeaseTests` **23/23** e `NativeDownloadOwnerProcessTests` **7/7**. Há cobertura de mesma thread/processos, endpoint case-insensitive, campos excluídos da chave, DACL, marker livre preexistente, colisão de tipo, retenção pelo último handle, failed-listen e lifecycle até destruição/morte reconhecida do filho próprio. Não herança foi conferida no `SECURITY_ATTRIBUTES` do código, sem ensaio de filho herdando handles. Isso não comprova outra sessão/usuário, pipe hostil ou identidade/ownership instalados. Os **470/470** acima pertencem ao incremento anterior; gates globais e de produção desta mudança são registrados separadamente no [checkpoint central](development-plan.md#17-próxima-sequência-segura).

## Pendências e próxima integração

1. Gate real isolado do Brave/Chromium, incluindo popup/teclado, permissão temporária, navegação, iframe e ativação/remoção; não executar em perfil pessoal sem autorização.
2. Compor o host/IPC e reservas cooperativa/nativa com identidade instalada estável, propriedade entre edições e lifecycle protegido de produção. Primeira criação/retenção do nome tem prova própria; autenticação do pipe e disponibilidade entre usuários continuam pendentes. Persistência/admissão, importação durável e UI comum já têm componentes/ensaios próprios; recuperação física/acionável e registro/ownership/upgrade permanecem gates, incluindo R1.10.
3. Fechar a composição protegida de executor/rede/TLS/filesystem/conversão/importador sobre a [coordenação já implementada](download-jobs.md), com ferramentas corrigidas/revisadas e isolamento efetivo. Componentes e ensaios locais existentes não aprovam conteúdo da internet nem ativam downloads reais.
4. Evoluir descoberta para casos não cobertos: iframes, Shadow DOM aberto/fechado, mídia em tráfego anterior ao clique ou sem URL direta. Blob/MSE, manifestos HLS/DASH, live e DRM não são tratados como MP4 baixável nem contornados.
5. Navegação interna da Completa, opcionais no instalador e extensão YouTube têm implementação/gates separados. Este cliente não conclui R1.2/R1.7/R1.8, FR-051–053 ou release.
