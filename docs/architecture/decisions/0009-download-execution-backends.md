# ADR-0009: adapters de rede e conversão para downloads

**Estado:** Proposto

**Data:** 2026-09-08

**Escopo:** avaliação prévia FR-052/R1.2/R1.8; nenhuma ferramenta nova é instalada, distribuída ou aceita por este documento.

## Contexto

O serviço do [ADR-0008](0008-download-job-service.md) precisa de execução concreta sem entregar URLs ou mídia não validada ao playback. A revisão local encontrou Qt Network 6.11.2 já provisionado e um FFmpeg CLI separado do conjunto usado pelo Qt Multimedia. A disponibilidade local não comprova segurança, licença de distribuição ou adequação ao pacote Lite.

## Proposta e evidência local

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

**Gate concreto do Schannel:** a revisão do código Qt 6.11.2 identificou construção de cadeia via `CertGetCertificateChain` sem flags cache-only de recuperação/revogação. AIA, raízes/CTL e CRL/OCSP podem provocar consultas auxiliares fora do socket pinado; a chamada nativa síncrona também não é interrompida imediatamente pelo deadline da sessão. CA por instância não remove esse caminho, e `VerifyNone` não o evita nem seria substituto seguro da autenticação. Não foi encontrado toggle público cache-only no backend Schannel. Antes de internet, escolher/revisar backend ou alteração que preserve verificação completa e restrinja egress, com teste próprio; não habilitar mídia remota só porque TLS em loopback passou. Os testes sintéticos não capturaram consultas auxiliares do validador nativo. [Construção da cadeia Qt](https://github.com/qt/qtbase/blob/v6.11.2/src/plugins/tls/schannel/qtls_schannel.cpp#L2447-L2470), [contrato Win32 e flags](https://learn.microsoft.com/en-us/windows/win32/api/wincrypt/nf-wincrypt-certgetcertificatechain).

## Conversão, isolamento e publicação

Executável fixo por caminho confiável, vetor de argumentos, janela oculta e nenhuma shell. Entrada local privada e finalizada, demuxers/protocolos limitados, referências externas desabilitadas, seleção de áudio fixa e descarte de metadata/chapters não necessários. Progresso e stderr devem ser consumidos com limites, sem mensagens livres ou URLs nos logs. Processo separado não equivale a sandbox; decidir Job Object/restrições e provar término do filho em cancelamento/timeout.

Validar o MP3 final por conteúdo/áudio, publicar atomicamente sem sobrescrever arquivos existentes e somente então chamar a importação local. Cancelamento/falha remove apenas parciais pertencentes ao job, nunca arquivos pessoais ou diretórios amplos.

## Alternativas, consequências e critérios de revisão

- Reutilizar DLLs do playback diretamente: exigiria dependências de desenvolvimento/ABI e amplia impacto de decoder hostil no processo principal.
- Downloader genérico com cookies de navegador: fora do escopo privado da principal e desnecessário para mídia HTTP(S) direta.
- FFmpeg acessando rede: perde a fronteira única de DNS/redirects/SSRF; rejeitado como desenho da principal.

Antes de aceitar: testes de DNS misto/rebinding, redirects/downgrade/provedor, SNI/Host/certificado, request-target, conteúdo chunked/excessivo/stall, cancelamento e publicação; conversor falso com crash/hang/flood, MP4 sintético com/sem áudio, referências externas, paths Unicode e colisão sem sobrescrita. Ferramentas corrigidas/proveniência/licenças e isolamento precisam de evidência própria. Download real, execução em perfil pessoal e instalação não são autorizados por esta proposta.
