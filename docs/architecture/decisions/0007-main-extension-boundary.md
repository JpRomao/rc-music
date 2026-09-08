# ADR-0007: fronteira da extensão principal Chromium

**Estado:** Proposto

**Data:** 2026-09-08

**Escopo:** implementação incremental de R1.7/FR-051–053; não escolhe downloader, conversor, engine embarcada ou distribuição.

## Contexto

O [ADR-0006](0006-browser-editions-and-extensions.md) aceita a extensão principal independente de YouTube e compartilhada pelas edições Lite/Completa. Precisamos implementar a seleção de mídia sem transformar conteúdo de páginas em comandos privilegiados nem adicionar acesso permanente ao histórico. A revisão abaixo precede código; aceitação técnica depende de adoção e testes no incremento correspondente.

## Decisão proposta

Usar Manifest V3, JavaScript/HTML/CSS próprios sem dependências de runtime ou código remoto, e APIs Chromium 106+ (`documentId`). Brave é o alvo primário; a compatibilidade real continua exigindo smoke isolado autorizado. Node.js já disponível serve somente ao runner `node:test` local; não é empacotado, instalado nem exigido pelo player.

A extensão usa `activeTab`, `scripting` e `nativeMessaging`. O clique abre o popup e autoriza inspeção temporária da página principal; não há `<all_urls>`, cookies, storage, histórico, webRequest, content scripts permanentes ou acesso a arquivos locais. A API oficial permite injeção em mundo isolado e identificação do documento, mesmo após navegações na mesma origem. [activeTab](https://developer.chrome.com/docs/extensions/develop/concepts/activeTab), [scripting](https://developer.chrome.com/docs/extensions/reference/api/scripting).

O detector somente produz candidatos. A página privilegiada do popup valida snapshots, apresenta seleção explícita e chama Native Messaging diretamente. Não há listener genérico de comandos de páginas/service worker; o detector não tem caminho para iniciar downloads. O canal detector aceita apenas a identidade e URL exatas do popup próprio e para ao desconectar. O popup fixa aba/documento/URL, invalida mudanças de navegação/SPA e revalida antes do envio. O [modelo de ameaças](../../security/browser-extension-threat-model.md) define limites e responsabilidades que o host futuro também deverá impor.

Detecção inicial cobre `audio`, `video`, seus `source` e links diretos MP3/MP4/M4A na página principal, com atualização enquanto o popup estiver aberto. Não confundir candidatos com arquivos decodificados ou autorização do titular. Iframes sem autorização, shadow roots fechados, tráfego anterior ao clique, blob/MSE, playlists adaptativas e conteúdo protegido continuam lacunas explícitas, não suporte universal. Evoluir cobertura depois, sem reduzir o requisito a este primeiro recorte.

O pedido `download.request` v1 contém ID e de 1 a 20 itens com URL HTTP(S), título e tipo de candidato. Não aceita comando, destino no disco, headers, cookies ou opções de executável. Só seleção explícita e confirmação de uso autorizado o produzem. Resposta correlacionada significa aceitação do lote pelo host, nunca download/conversão concluídos. Não retransmitir automaticamente após timeout/desconexão; resultado incerto exige conferir a fila no app antes de repetir.

O nome de protocolo local de desenvolvimento será `com.rcmusic.media`, sem registrar host no Windows neste incremento. O receptor nativo ainda não existe: a extensão precisa mostrar ausência/erro da ponte em vez de fingir sucesso. O host final deverá autenticar origens de extensão explícitas, tratar o framing nativo de 32 bits em ordem do host e manter logs fora de stdout; esse protocolo não é `OpenSources`. [Native Messaging](https://developer.chrome.com/docs/extensions/develop/concepts/native-messaging).

## Segurança, licenças e distribuição

Somente código próprio, sem copiar exemplos ou empacotar bibliotecas novas; licença pública do projeto e publicação continuam pendentes. Não instalar extensão, registrar host, mudar políticas do navegador ou baixar ferramentas neste incremento. Atualizações de código serão explícitas, sem mecanismo próprio de auto-update ou execução remota. O app permanece offline e sem nova dependência.

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

Revisar por lacunas de detecção que exijam permissões maiores, risco de origem/SSRF, mudanças de API, distribuição, identidade final das extensões/host ou necessidade de fila persistente no navegador. Não aceitar ferramentas de download/conversão ou a engine da Completa por associação.
