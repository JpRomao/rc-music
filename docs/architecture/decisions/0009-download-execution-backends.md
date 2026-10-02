# ADR-0009: adapters de rede e conversão para downloads

**Estado:** Proposto

**Data:** 2026-09-08

**Escopo:** avaliação prévia FR-052/R1.2/R1.8; nenhuma ferramenta nova é instalada, distribuída ou aceita por este documento.

## Contexto

O serviço do [ADR-0008](0008-download-job-service.md) precisa de execução concreta sem entregar URLs ou mídia não validada ao playback. A revisão local encontrou Qt Network 6.11.2 já provisionado e um FFmpeg CLI separado do conjunto usado pelo Qt Multimedia. A disponibilidade local não comprova segurança, licença de distribuição ou adequação ao pacote Lite.

## Proposta e evidência local

Incremento worker-only de 2026-10-02: [journal de ownership](../../download-ownership.md) separado dos recibos registra intenção/provas/publicação/cleanup e classifica candidatos no restart sem executar/importar/remover. O store nativo é reutilizado com checkpoint, limite fechado e upsert sob lock; o workspace durável preserva final físico em falha pósrename sem ACK. Isso não fecha resíduos do runtime, reconciliação fila+recibo, ferramenta corrigida/proveniência/licenças/sandbox ou perfil de distribuição. A adoção desse componente não muda o estado Proposto desta decisão de backend.

Investigar downloader Qt Network opcional e conversor FFmpeg em processo separado, recebendo somente arquivo temporário local. Não encaminhar URLs remotas ao FFmpeg nem usar concatenação de shell.

- O SDK aceito em `q/f75eb9bea8280844c6d1a2b3f4/sdk` contém Qt6Network, pacote CMake e plugin TLS Schannel. O [transporte opcional](../../download-network.md) passa a utilizá-los em seu alvo próprio, ainda fora do player.
- O FFmpeg/ffprobe disponível via WinGet é Gyan 8.1.1 full-build, com `libmp3lame`, `--enable-gpl` e `--enable-version3`; é distinto das DLLs de playback referenciadas como FFmpeg 7.1.5. O gate existente de proveniência/notices dessas DLLs não aprova o CLI.
- **Bloqueio de mídia não confiável:** a lista oficial de segurança já registra correções em 8.1.2 para CVE-2026-8461 e CVE-2026-30999. O CLI 8.1.1 não fica aprovado para processar downloads; revisar atualização, proveniência e alcance dos avisos antes de habilitar esse uso. Testes futuros de processo podem usar apenas fixtures sintéticas confiáveis enquanto isso. [Segurança FFmpeg](https://ffmpeg.org/security.html).
- Opções GPL mudam as obrigações de redistribuição; definir artefato exato, fontes correspondentes, notices, hashes e atualização explícita antes de empacotar. Não copiar silenciosamente o full-build para a Lite. Não há aprovação jurídica/publicação implícita. [Licenciamento FFmpeg](https://ffmpeg.org/legal.html).

Qt Network reutiliza a versão já provisionada, sem nova instalação. O módulo oferece LGPLv3/GPLv2 ou licença comercial e possui atribuições de terceiros próprias; registrar o conjunto exato no gate de notices antes de redistribuir. A consulta aos avisos oficiais identificou CVE-2025-14575 limitado ao backend OpenSSL Unix em versões antigas e CVE-2025-6338 de servidor Schannel corrigido antes de 6.11.2; isso não é auditoria exaustiva nem aprovação global da toolchain. [Licenças Qt Network](https://doc.qt.io/qt-6/qtnetwork-index.html#licenses-and-attributions), [aviso OpenSSL](https://www.qt.io/blog/security-advisory-untrusted-search-path-vulnerability-in-openssl), [aviso Schannel](https://www.qt.io/blog/security-advisory-recently-reported-incomplete-cleanup-issue-in-qts-schannel-handling-can-impact-qt).

## Fronteira de rede a provar

Resolver DNS assincronamente, limitar respostas/tempo e recusar endereços privados/reservados, inclusive IPv4 mapeado em IPv6. Não basta resolver e depois pedir a URL original, que pode causar nova resolução. Não usar `QHostAddress::isGlobal()` como sinônimo de endereço público permitido.

A hipótese técnica é conectar ao IP aprovado, conservando Host lógico e nome de verificação TLS/SNI; `QNetworkRequest::setPeerVerifyName` oferece nome separado para a verificação. Isso é proposta que exige ensaio de conexão/TLS real isolado, não prova de pinning. Cada redirect retorna à política comum, com limites, recusa de downgrade HTTPS e de YouTube; sem proxy/cookies/autenticação persistentes. [QNetworkRequest](https://doc.qt.io/qt-6/qnetworkrequest.html).

Testar no fio preservação do request-target assinado, já que QUrl/QNAM podem reserializar escapes. Impor cotas absolutas de bytes/disco, buffer/headers limitados, deadlines total/idle e cancelamento; proteção proporcional de descompressão não substitui um teto absoluto. Não liberar localhost na policy de produção para conseguir testar.

### Ensaio incremental do transporte

O [incremento de transporte](../../download-network.md) avalia Qt Network com `QSslSocket`/`QTcpSocket` conectando ao endereço numérico aprovado, HTTP/1.1 direto e parser incremental limitado. A alternativa a QNAM serve para preservar exatamente o request-target assinado e limitar cabeçalhos antes de armazená-los. Isso acrescenta responsabilidade de framing HTTP: cobrir comprimentos ambíguos, chunked, EOF, trailers e codificações não aceitas, sem criar cliente HTTP genérico. Usar somente GET, `Accept-Encoding: identity`, sem proxy, cookies, autenticação ou atualização automática. A política lexical de URL permanece compartilhada com a admissão.

Os testes de conexão usam exclusivamente servidores efêmeros de loopback com bytes/certificados sintéticos. A conexão de baixo nível recebe endpoint de composição confiável; a política usada pelo downloader continua recusando loopback e redes privadas. Confiança TLS adicional existe somente na instância de teste, sem alterar certificados/Registry do Windows. Testes separados de policy e transporte não provam por si só a integração inteira; registrar o que foi efetivamente composto.

Este ensaio não aceita o backend completo nem libera mídia da internet, conversão ou distribuição. A decisão final permanece proposta enquanto os gates acima e abaixo estiverem incompletos.

**Gate concreto do Schannel original:** a revisão do código Qt 6.11.2 identificou construção de cadeia via `CertGetCertificateChain` sem flags cache-only de recuperação/revogação. AIA, raízes/CTL e CRL/OCSP podem provocar consultas auxiliares fora do socket pinado; a chamada nativa síncrona também não é interrompida imediatamente pelo deadline da sessão. CA por instância não remove esse caminho, e `VerifyNone` não o evita nem seria substituto seguro da autenticação. Não foi encontrado toggle público cache-only no backend Schannel. O ensaio restrito abaixo comprova um caminho AIA local e preserva a autenticação; revisão dos demais caminhos, isolamento e composição continuam gates antes de internet. [Construção da cadeia Qt](https://github.com/qt/qtbase/blob/v6.11.2/src/plugins/tls/schannel/qtls_schannel.cpp#L2447-L2470), [contrato Win32 e flags](https://learn.microsoft.com/en-us/windows/win32/api/wincrypt/nf-wincrypt-certgetcertificatechain).

### Backend Schannel restrito em processo de ensaio

Em 2026-10-01, o [builder opt-in](../../../tools/download-tls/README.md) produziu `rcmusic-schannel-offline` das fontes Qt 6.11.2 já provisionadas, verificando 24 hashes de entrada antes da cópia. Originais e SDK permanecem intactos. A DLL é separada, ligada à mesma compilação Qt por usar APIs privadas; não foi instalada, ligada ao player ou selecionada no processo do aplicativo. Build Release MSVC 19.44 e contrato de limites C++ passaram **1/1 CTest** (12 casos internos); contratos do builder tiveram Red com cinco falhas e Green **5/5**.

A construção de cadeia conserva checagem de revogação e acrescenta `CERT_CHAIN_CACHE_ONLY_URL_RETRIEVAL`, `CERT_CHAIN_REVOCATION_CHECK_CACHE_ONLY` e `CERT_CHAIN_DISABLE_AIA`. As credenciais Schannel recebem cache-only de recuperação/revogação/criação e `SCH_CRED_NO_DEFAULT_CREDS` fixo, sem permitir habilitação pelo ambiente. AIA é explicitamente desativada na construção de cadeia; o SDK não oferece um flag separado `SCH_CRED_DISABLE_AIA`. Verificações de confiança, assinatura, tempo, uso, nome e revogação conhecida continuam presentes. [Credenciais Schannel](https://learn.microsoft.com/en-us/windows/win32/api/schannel/ns-schannel-sch_credentials).

O acumulador Qt `intermediateBuffer` e o pedido `missingData` têm teto de **256 KiB**, com aritmética sem overflow e recusa antes de reserva/resize nas rotas de handshake e descriptografia. Isso limita essa retenção Qt, não todas as alocações do Schannel/Windows. A composição futura em subprocesso ainda deve impor memória e encerramento por Job Object, inclusive para cancelar validação nativa síncrona.

**Política de revogação explícita:** mantém-se o soft-fail original Qt para status desconhecido/offline no cache. Aceitação de certificado não comprova status de revogação conhecido; revogação explícita continua erro. Restringir consultas auxiliares não aprova nem demonstra uma política mais forte de revogação.

O teste Qt isolado carrega/seleciona o backend somente em seu próprio processo e usa a fonte real `PinnedDownloadStream`, confiança por instância e servidores de loopback. Uma cadeia incompleta com issuer/CA e chaves sintéticas novos por execução aponta AIA para uma armadilha Node local, independente da thread do validador. O mesmo contrato no Schannel original falhou precisamente por detectar **REQUEST** na armadilha (Red: duas passagens/uma falha); no backend restrito passou com zero consultas à armadilha. A suite completa passou **5/5**, cobrindo certificado confiável/GET assinado, nome incorreto e issuer ausente sem GET de mídia, além da preservação da configuração padrão Qt. Não houve download de mídia da internet, instalação de certificados/host, registro do plugin ou uso de perfil pessoal.

Essa evidência comprova o cenário AIA exercitado, sem captura exaustiva do tráfego Windows. Permanecem abertos CRL/OCSP/CTL com cenários próprios, handshake excessivo/stall, alocações internas, memória/deadline/cancelamento do subprocesso, carregamento do artefato exato no worker e composição DNS/transferência/conversão/publicação/importação. Fontes correspondentes, notices e compatibilidade da DLL modificada também precisam integrar o gate de distribuição. O ADR permanece **Proposto** e o incremento não habilita downloads no aplicativo.

## Conversão, isolamento e publicação

O [loader worker-only](../../download-conversion.md#runtime-tls-no-futuro-processo-de-rede) implementa pins de artefatos e seleção exclusiva do plugin em processo novo. O [worker diskless/proxy](../../download-conversion.md#rede-diskless-e-protocolo-privado) já o exige inclusive para DNS/HTTP, com ambiente explícito e ACK nativo de processo/pipes. O [runtime pré-launch](../../download-conversion.md#runtime-privado-autenticado-antes-do-launch) acrescenta capability de nove cópias privadas autenticadas e bootstrap `/MT` OS-only que restringe DLL search antes de payload Qt absoluto `/MD`. Proxy retém a capability e deriva executable/CWD/ambiente/pins; o EXE Qt direto fica seam histórico de teste, não entrada autenticada. A tarefa assíncrona prepara essa capability fora da UI, por job, e só inicia a sessão depois de observar cancelamento/shutdown e deadline; não prepara arquivos na factory inerte. OS/ICU/API sets/KnownDLL/SxS/AppCompat e ausência de host comprometido são precondições explícitas. Perfil revisado, sandbox/notices/distribuição e cenários TLS completos continuam gates; este ADR permanece **Proposto**, sem seleção TLS no player ou relaxamento de confiança.

Executável fixo por caminho confiável, vetor de argumentos, janela oculta e nenhuma shell. Entrada local privada e finalizada, demuxers/protocolos limitados, referências externas desabilitadas, seleção de áudio fixa e descarte de metadata/chapters não necessários. Progresso e stderr devem ser consumidos com limites, sem mensagens livres ou URLs nos logs. Processo separado não equivale a sandbox; decidir Job Object/restrições e provar término do filho em cancelamento/timeout.

Validar o MP3 final por conteúdo/áudio, publicar atomicamente sem sobrescrever arquivos existentes e somente então chamar a importação local. Cancelamento/falha remove apenas parciais pertencentes ao job, nunca arquivos pessoais ou diretórios amplos.

### Composição worker-only e ownership

A sessão [WindowsDownloadJobSession](../../../src/adapters/WindowsDownloadJobSession.hpp) compõe transporte, workspace e conversor no controlador. A [porta rápida Windows](../../download-conversion.md#execução-assíncrona-windows) mantém sessão/I/O/destruição em thread Qt privada, ainda fora do app. O ensaio já percorre serviço real com importador falso; proxy/supervisor e arquivos/processos/validação/publicação são reais, transferência do helper e fixtures são sintéticas/autenticadas. Worker real tem ensaios separados de DNS literal/HTTP loopback; a jornada de conversão não usa DNS público/socket TLS real. Isso não aceita TLS, CLI ou distribuição: gates anteriores continuam abertos e o ADR permanece proposto.

O controlador conserva ownership de disco mesmo ao encerrar a rede. DNS/socket possuem subprocesso diskless opcional com pedido privado/frames limitados; conversor é outro subprocesso de uma ferramenta por vez. Não colocar workspace inteiro em filho que pode ser morto e limpar por UUID/path recebido. Supervisor mantém Job de um processo; descendentes exigiriam handles/ACK próprios, não accounting vazio. Sessão exige `pollStop().stopped` antes de selagem/conversão/cleanup/terminal e preserva primeira falha tipada antes de cancelar; Qt in-process não reconhece parada DNS nativa. Owner deve fechar/drenar serviço e porta assíncrona e observar também teardown da thread antes da destruição na UI; RAII/join bloqueante não substituem esse protocolo. Não conectar a sessão lenta diretamente como `DownloadExecution`. Journal/ownership durável e composição no app ainda faltam.

## Alternativas, consequências e critérios de revisão

O [incremento local Windows de 2026-10-01](../../download-conversion.md#incremento-de-rede-diskless) implementa workspace privado/limitado, supervisor, worker/proxy diskless e conversão/inspeção MP3/decodificação integral; teve fechamento proporcional 55/55 CTest e Release OFF/zero testes. O [incremento assíncrono posterior](../../download-conversion.md#incremento-de-execução-assíncrona) acrescenta porta rápida/lifecycle e passou 41/41 CTest focados, fora do app. O incremento pré-launch passou launcher 21/21 e fechamento 44/44 com filho real DNS literal/HTTP loopback e ownership/pins até ACK. A [preparação na tarefa assíncrona](../../download-conversion.md#incremento-de-preparação-na-tarefa-assíncrona) passou 19/19 e 17/17 casos novos e fechamento 44/44; seus ensaios param antes de DNS e não comprovam a jornada real completa. Fixtures sintéticas e ferramentas existentes não aprovam CLI 8.1.1 para mídia remota. Perfil revisado, sandbox, importação/persistência no player, atualização/licenças e distribuição continuam abertos. O ADR não se torna aceito por esses componentes isolados.

- Reutilizar DLLs do playback diretamente: exigiria dependências de desenvolvimento/ABI e amplia impacto de decoder hostil no processo principal.
- Downloader genérico com cookies de navegador: fora do escopo privado da principal e desnecessário para mídia HTTP(S) direta.
- FFmpeg acessando rede: perde a fronteira única de DNS/redirects/SSRF; rejeitado como desenho da principal.

Antes de aceitar: testes de DNS misto/rebinding, redirects/downgrade/provedor, SNI/Host/certificado, request-target, conteúdo chunked/excessivo/stall, cancelamento e publicação; conversor falso com crash/hang/flood, MP4 sintético com/sem áudio, referências externas, paths Unicode e colisão sem sobrescrita. Ferramentas corrigidas/proveniência/licenças e isolamento precisam de evidência própria. Download real, execução em perfil pessoal e instalação não são autorizados por esta proposta.
