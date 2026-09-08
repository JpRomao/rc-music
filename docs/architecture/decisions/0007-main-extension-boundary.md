# ADR-0007: fronteira da extensão principal Chromium

**Estado:** Aceito

**Data:** 2026-09-08

**Escopo:** implementação incremental de R1.7/FR-051–053, incluindo cliente MV3 e biblioteca opcional de admissão C++; não escolhe downloader, conversor, engine embarcada ou distribuição.

## Contexto

O [ADR-0006](0006-browser-editions-and-extensions.md) aceita a extensão principal independente de YouTube e compartilhada pelas edições Lite/Completa. Precisamos implementar a seleção de mídia sem transformar conteúdo de páginas em comandos privilegiados nem adicionar acesso permanente ao histórico. A revisão foi registrada como proposta no commit `a42a470`, antes do código; a aceitação técnica ocorre pela adoção no [cliente testado](../../browser-extension.md), não por aprovação de host, ferramentas ou distribuição.

## Decisão

Usar Manifest V3, JavaScript/HTML/CSS próprios sem dependências de runtime ou código remoto, e APIs Chromium 106+ (`documentId`). Brave é o alvo primário; a compatibilidade real continua exigindo smoke isolado autorizado. Node.js já disponível serve somente ao runner `node:test` local; não é empacotado, instalado nem exigido pelo player.

A extensão usa `activeTab`, `scripting` e `nativeMessaging`. O clique abre o popup e autoriza inspeção temporária da página principal; não há `<all_urls>`, cookies, storage, histórico, webRequest, content scripts permanentes ou acesso a arquivos locais. A API oficial permite injeção em mundo isolado e identificação do documento, mesmo após navegações na mesma origem. [activeTab](https://developer.chrome.com/docs/extensions/develop/concepts/activeTab), [scripting](https://developer.chrome.com/docs/extensions/reference/api/scripting).

O detector somente produz candidatos. A página privilegiada do popup valida snapshots, apresenta seleção explícita e chama Native Messaging diretamente. Não há listener genérico de comandos de páginas/service worker; o detector não tem caminho para iniciar downloads. O canal detector aceita apenas a identidade e URL exatas do popup próprio e para ao desconectar. O popup fixa aba/documento/URL, invalida mudanças de navegação/SPA e revalida antes do envio. O [modelo de ameaças](../../security/browser-extension-threat-model.md) define limites e responsabilidades que o host futuro também deverá impor.

Detecção inicial cobre `audio`, `video`, seus `source` e links diretos MP3/MP4/M4A na página principal, com atualização enquanto o popup estiver aberto. Não confundir candidatos com arquivos decodificados ou autorização do titular. Iframes, Shadow DOM aberto/fechado, tráfego anterior ao clique, blob/MSE, playlists adaptativas e conteúdo protegido continuam lacunas explícitas, não suporte universal. Evoluir cobertura depois, sem reduzir o requisito a este primeiro recorte.

O pedido `download.request` v1 contém ID e de 1 a 20 itens com URL HTTP(S), título e tipo de candidato. Não aceita comando, destino no disco, headers, cookies ou opções de executável. Só seleção explícita e confirmação de uso autorizado o produzem. Resposta correlacionada significa aceitação do lote pelo host, nunca download/conversão concluídos. Não retransmitir automaticamente após timeout/desconexão; resultado incerto exige conferir a fila no app antes de repetir.

O nome de protocolo local de desenvolvimento será `com.rcmusic.media`, sem registrar host no Windows neste incremento. Ainda não há executável de host nem adapter de stdin/stdout: a extensão precisa mostrar ausência/erro da ponte em vez de fingir sucesso. O protocolo oficial usa JSON UTF-8, prefixo de comprimento de 32 bits em ordem nativa e origem do chamador no primeiro argumento; o adapter Windows futuro deve usar I/O binário e manter logs fora de stdout. Isso não é `OpenSources`, cujo contrato continua estritamente local. [Native Messaging](https://developer.chrome.com/docs/extensions/develop/concepts/native-messaging).

### Biblioteca nativa opcional de admissão

A [composição CMake própria](../../../cmake/RcMusicBrowserIntegration.cmake), ativada explicitamente por `RC_MUSIC_BUILD_BROWSER_INTEGRATION` e desligada por padrão, separa codec, framing e sessão em uma biblioteca C++/Qt Core. Não conecta essa biblioteca ao player, não acrescenta navegador à Lite e não cria transporte ou download. O [registro do componente](../../browser-extension.md) distingue evidência automatizada de integração ainda pendente.

- `NativeMessageFrame` monta uma única mensagem por sessão com prefixo little-endian do Windows e corpo de 1 byte a 256 KiB. Tamanho e bytes excedentes são verificados antes de copiar/reservar o corpo; prefixos fragmentados, overflow e duas mensagens coalescidas não podem contornar o limite. Um resultado completo entrega o corpo uma só vez.
- O prazo de montagem é total, de 5 s, inclusive na fronteira, medido pelo relógio monotônico fornecido pelo chamador. Novos fragmentos não renovam o prazo; relógio regressivo, entrada posterior ao estado terminal ou EOF incompleto encerram a sessão. Esse prazo não mede download/conversão nem substitui um watchdog do futuro processo de I/O.
- `NativeDownloadCodec` exige UTF-8 e strings JSON bem formados, escapes válidos, pares de surrogates válidos, chaves únicas após decodificação e profundidade limitada, antes de aplicar schema exato. URLs/títulos e respostas conservam os limites do cliente; validação sintática de hostname não comprova segurança de DNS/redirects.
- `NativeDownloadSession` aceita somente a origem configurada exata no formato `chrome-extension://` + 32 letras `a`–`p` + `/`, sem wildcard, path adicional ou query. A origem permitida vem da composição confiável, nunca do JSON da página; identidade final e instalação ainda não foram definidas. Essa comparação não autentica um processo local capaz de invocar um futuro executável com argumentos próprios.
- A porta pura [DownloadSubmission](../../../include/m0/ports/DownloadSubmission.hpp) recebe apenas o lote tipado e faz admissão, não download/conversão síncronos. Lote inválido não chega à porta; ACK exige recibo válido com um ID de job distinto por item. Falha/incerteza não gera ACK fictício nem retry. O futuro serviço precisa assumir o lote independentemente da conexão e deduplicar request IDs entre sessões; essa garantia ainda não está implementada por esta biblioteca.

O incremento posterior do [ADR-0008](0008-download-job-service.md) implementa essa porta com retenção/deduplicação na mesma execução e testa conexões independentes contra o serviço real. Ele não acrescenta transporte de host, persistência entre processos, execução de download ou integração instalada às garantias desta biblioteca.

## Segurança, licenças e distribuição

Somente código próprio, sem copiar exemplos ou empacotar bibliotecas novas; licença pública do projeto e publicação continuam pendentes. A biblioteca de admissão reutiliza Qt Core da mesma toolchain aceita pelo ADR-0005, mantendo seus gates de licença/notices/distribuição; não aprova outra biblioteca, downloader ou conversor. Não instalar extensão, registrar host, mudar políticas do navegador ou baixar ferramentas neste incremento. Atualizações de código serão explícitas, sem mecanismo próprio de auto-update ou execução remota. O app permanece offline e sem nova dependência obrigatória.

Dados de página ficam efêmeros no popup/detector; apenas mídia selecionada vai ao host local. Sem logs de URLs, query strings visíveis, persistência de navegação ou cookies. A política de uso limitado exige uso necessário à função declarada; submissão futura exige divulgação/privacidade próprias e revisão, não aprovação implícita desta decisão. [Limited Use](https://developer.chrome.com/docs/webstore/program-policies/limited-use).

Domínios identificados como YouTube são recusados tanto na página quanto na mídia candidata. Não oferecer encaminhamento à extensão separada. A Chrome Web Store lista facilitar download de YouTube como causa de rejeição; filtros não garantem aprovação nem identificam todo proxy arbitrário. O host deve revalidar destinos/redirects e não pode confiar no filtro do cliente. [Troubleshooting oficial](https://developer.chrome.com/docs/webstore/troubleshooting/).

## Alternativas e consequências

- Acesso permanente a todos os sites: desnecessário para começar a seleção autorizada, aumenta exposição; futura detecção de rede exige decisão própria.
- Servidor HTTP localhost: rejeitado; amplia a superfície e conflita com ADR-0006.
- Service worker roteando comandos genéricos: desnecessário para o primeiro fluxo; o popup pode chamar o canal nativo diretamente, reduzindo fronteiras de mensagens.
- Framework web/build npm: desnecessário para uma lista e ações; manter módulos pequenos e testes nativos de Node.

Fechar o popup encerra detecção e pode impedir observar uma resposta nativa; não cancela trabalho que um host já aceitou. O host futuro terá fila/status/cancelamento independentes. A integração completa só estará pronta com host, download/conversão, importação e instaladores validados nas duas edições.

## Critérios de aceitação e revisão

TDD cobre descoberta/deduplicação/limites, limpeza de listeners, remetentes falsos, navegação, múltipla seleção e envio exclusivo de selecionados, contratos estritos/ACK, timeout/desconexão sem retry e texto hostil sem HTML. Checks devem executar código de produção com DOM/Chrome/relógio falsos, sem rede ou perfis pessoais. Testes fake não provam APIs carregadas pelo Brave, CSP real ou usabilidade visual nativa; esses smokes permanecem separados.

A biblioteca nativa exige Qt Test sem UI/rede com bytes hostis, todas as divisões do prefixo/corpo, prazo/clock determinísticos, origem configurada, schema/UTF-8/escapes/chaves duplicadas e porta de admissão falsa. Testar que mensagens inválidas não despacham e que exceções/recibos incorretos não fabricam aceitação. Checks verdes de biblioteca não provam executável, I/O binário, registro, identidade instalada, idempotência entre conexões, download ou release.

Revisar por lacunas de detecção que exijam permissões maiores, risco de origem/SSRF, mudanças de API, distribuição, identidade final das extensões/host ou necessidade de fila persistente no navegador. Não aceitar ferramentas de download/conversão ou a engine da Completa por associação.
