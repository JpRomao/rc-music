# Fronteira de segurança da extensão principal

**Data:** 2026-09-08. **Estado:** R1.2/R1.7 em andamento; controles do cliente e biblioteca opcional C++ de admissão, com [evidências e lacunas separadas](../browser-extension.md). Não há executável de host, stdin/stdout, registro ou prova no Brave real. Download/conversão ainda exigem revisão própria.

Contrato: [ADR-0007](../architecture/decisions/0007-main-extension-boundary.md), [escopo aceito](../architecture/decisions/0006-browser-editions-and-extensions.md), FR-050–053 e NFR-PRIV-001–004.

## Ativos, fronteiras e controles

| Ameaça | Controle no cliente | Gate obrigatório no host/download futuro |
|---|---|---|
| Página envia comando privilegiado | Detector só relata, popup próprio único produtor de pedido, sem mensagens externas ou executores genéricos | Origem instalada autorizada, schema estrito, capacidades por cliente, nunca confiar em flag da página |
| Navegação/SPA troca seleção por outra mídia | Fixar tab/documentId/pageUrl, invalidar sessão e revalidar antes de enviar | ID de pedido para idempotência; não reutilizar consentimento de outro lote |
| Título/URL hostil e excesso de dados | Texto via textContent, limites e seleção vazia por padrão | Nenhuma concatenação de shell; parâmetros tipados e nomes de arquivo próprios |
| Acesso a serviço local/credenciais | Somente HTTP(S), sem userinfo, IP literal, host local ou porta não padrão; rejeitar esquemas especiais | Resolver DNS e verificar todos os endereços, conexão e cada redirect; bloquear privados/reservados/rebinding e downgrade HTTPS; cota/timeout/redirects finitos |
| YouTube oferecido indiretamente | Recusar domínios conhecidos de página e mídia; sem encaminhamento | Revalidar provedor/redirects/capacidades; proxies arbitrários não são reconhecíveis somente pelo hostname |
| Coleta implícita | Permissão temporária, sem cookies/headers/histórico/storage/rede própria; detectar não envia ao host | Rede só após ação; logs redigidos; sem importar sessão do Brave |
| ACK ausente ou resposta tardia | ID correlacionado, resposta tipada, timeout, sem retry automático | Deduplicação de requestId e consulta de fila/status; cancelamento não confunde aceitação com conclusão |
| Download/decoder hostil | Candidato não é anúncio de suporte; mensagem de limite/erro | Limites de bytes/tempo/disco, TLS normal, sandbox/processo do conversor a definir, sem DRM bypass, arquivo parcial privado e publicação atômica sem sobrescrita |

O filtro de host no navegador é defesa adicional, não prova de endereço público: nomes DNS públicos podem resolver para redes locais. Nenhum download real pode ser ativado antes dos gates do host. Não adicionar exceção localhost ao código de produção para testes; usar fakes.

## Limites iniciais do cliente

- 100 candidatos por snapshot, 2.000 elementos inspecionados por varredura; indicar truncamento.
- URL de até 8.192 caracteres, título de até 200 caracteres, snapshot serializado até 1 MiB.
- Lote de 1–20 URLs únicas, até 256 KiB UTF-8; IDs UUID; nenhuma chave desconhecida.
- URLs absolutas normalizadas sem fragmento, query preservada para funcionamento mas não exibida/logada.
- Sem userinfo, NUL/controles, IP literal, host de um rótulo/local, porta não padrão ou esquema diferente de HTTP(S).
- Detector efêmero com coalescência de 200 ms durante popup aberto, sem adiar indefinidamente por mutações contínuas; navegação/desconexão desfaz timers/observadores. Reabrir não acumula observadores.
- Timeout da resposta nativa de 5 s, cancelamento de observação e resultado incerto sem reenvio.

Esses são limites de segurança/UX, não benchmarks nem garantia de consumo da engine. Ajustes precisam de testes de fronteira, sem usar páginas pessoais.

## Biblioteca C++ de admissão e riscos residuais

A [composição opcional](../../cmake/RcMusicBrowserIntegration.cmake) reutiliza Qt Core da toolchain aceita; fica desligada por padrão e não liga rede, conversor ou engine ao player. Codec, frame e sessão recebem dados/relógio por APIs de teste, sem lançar processo ou acessar o navegador. Esse recorte não substitui a matriz de gates do host/download acima.

- **Comprimento hostil:** prefixo uint32 little-endian no Windows, corpo entre 1 e 262.144 bytes e armazenamento limitado antes da cópia/reserva. Zero, excesso, prefixo com overflow ou bytes de outra mensagem na mesma entrega tornam a sessão terminal, sem publicar a primeira mensagem parcialmente aceita.
- **Fragmentação/stall:** prefixo/corpo podem chegar em fragmentos; o limite total de montagem é 5.000 ms inclusivos a partir do início, sem renovação por atividade. Clock regressivo e EOF incompleto falham fechados; não usar soma de deadline sujeita a overflow. Esse limite é independente do timeout de observação do popup e não torna uma futura leitura síncrona de stdin cancelável por si só.
- **JSON ambíguo:** exigir UTF-8 válido, escapes JSON fechados, strings sem controles crus ou surrogate isolado, chaves decodificadas não duplicadas por objeto e profundidade máxima de quatro contêineres. O preflight antecede o parse e o schema exato; não aceitar campos extras, destinos no disco, comandos, cookies ou headers. Títulos/URLs continuam dados não confiáveis, não instruções.
- **Origem falsa:** a sessão compara a origem do chamador com uma configuração confiável exata, ambas com a forma `chrome-extension://[a-p]{32}/`. Configuração vazia, wildcard, sufixo ou query não concedem acesso. Não derivar a origem permitida da mensagem. Falta definir identidade final, manifest de host e quem os registra; argumentos de um processo local não são prova de autenticação contra outro processo local.
- **ACK falso ou duplicação:** a [porta DownloadSubmission](../../include/m0/ports/DownloadSubmission.hpp) é somente admissão de lote tipado. A sessão despacha no máximo uma mensagem e só responde aceitação com recibo válido e um job distinto por item; não repete automaticamente em falha/incerteza. A [coordenação posterior](../download-jobs.md) retém lotes/recibos e deduplica entre sessões que usam a mesma instância; conflito de UUID/lote e capacidade recusam atomicamente, sem expulsão. Ainda faltam persistência entre processos, transporte para o dono no app e UI efetiva de fila/status/cancelamento.
- **SSRF/arquivo hostil:** repetir controles sintáticos no C++ não valida DNS, conexão, redirect, TLS ou bytes de mídia. Nenhuma operação real de rede ou conversão fica autorizada por aprovar essa biblioteca. Ferramentas, cotas/disco, publicação atômica e isolamento do conversor permanecem gates separados.

O futuro adapter deve implementar stdin/stdout em modo binário, comprimento em bytes UTF-8 e nenhum log em stdout, conforme o [protocolo Native Messaging oficial](https://developer.chrome.com/docs/extensions/develop/concepts/native-messaging). A biblioteca não implementa esse transporte nem instala manifesto/Registry; `OpenSources` permanece um protocolo local distinto. Não reivindicar origem instalada, segurança contra processos locais hostis ou integração ponta a ponta com base no teste de sessão.

## Validação e lacunas

Cobrir conteúdo dinâmico, duplicatas, lotes parcialmente inválidos sem ampliar seleção, campos malformados/extra, payload excessivo, navegação com mesma origem, documentos removidos, remetente falso, ACK de outro pedido, dupla ação e respostas tardias. Erros apresentados usam códigos próprios, nunca mensagens livres de navegador/host contendo paths ou URLs.

Para a biblioteca, cobrir framing fragmentado/coalescido e limites antes de alocação, prazo/clock/EOF, UTF-8/escapes/chaves duplicadas, origem exata, lote atômico e respostas/erros de uma porta falsa. Exigir o registro dos checks efetivamente executados; a existência de testes não equivale a resultado verde.

O serviço puro e sua projeção Qt possuem testes próprios de cotas de retenção, idempotência, progresso, cancelamento/timeout, resultados obsoletos e publicação antes do handoff de importação. Nenhum teste fake valida DNS, bytes de mídia, isolamento do conversor ou filesystem. A [avaliação ADR-0009](../architecture/decisions/0009-download-execution-backends.md) permanece proposta, com atualização/segurança do CLI e licenças como gates antes de mídia remota.

O [transporte incremental](../download-network.md) revalida todos os endereços DNS e cada redirect, conecta ao IP numérico, mantém Host/SNI e request-target, rejeita certificado inválido e limita framing/corpo. As provas atuais são policy/session com I/O falso e socket com loopback real. TLS sem EOF autenticado não pode concluir corpo delimitado somente por fechamento. O Schannel original constrói cadeia sem flags cache-only: recuperação AIA/CRL/OCSP pode sair da conexão pinada e não responde imediatamente ao deadline da sessão. Em 2026-10-01, uma armadilha AIA em loopback observou **REQUEST** com o backend original, mesmo com confiança por instância; o mesmo contrato com o [plugin restrito opt-in](../../tools/download-tls/README.md) teve zero consultas à armadilha e erro TLS para issuer ausente. A suite real passou **5/5**, incluindo certificado confiável/GET assinado e nome incorreto sem GET de mídia, sem alterar defaults Qt. A seleção do plugin ocorreu somente em um processo de teste próprio, fora do aplicativo.

O plugin acrescenta cache-only de URL/revogação e `CERT_CHAIN_DISABLE_AIA` na cadeia, cache-only nas credenciais Schannel e `SCH_CRED_NO_DEFAULT_CREDS` fixo. Limita `intermediateBuffer`/`missingData` a **256 KiB** antes de alocar, preservando autenticação de confiança, assinatura, tempo, uso e nome. Mantém o soft-fail original Qt para revogação desconhecida/offline; aceitar um certificado não prova revogação conhecida, enquanto um resultado explícito de revogado continua erro. O ensaio AIA não é captura exaustiva de egress: CRL/OCSP/CTL, handshake excessivo/stall, memória interna, deadline/cancelamento em subprocesso e composição completa continuam gates antes de internet no [ADR-0009 proposto](../architecture/decisions/0009-download-execution-backends.md#backend-schannel-restrito-em-processo-de-ensaio). Limites de bytes legíveis e do acumulador Qt não são teto de todas as alocações Qt/TLS/OS. Nenhum plugin foi instalado ou conectado ao player.

Os [adapters locais Windows](../download-conversion.md) acrescentam área privada com cota lógica agregada, processo sem terminal com Job Object/pipes limitados e inspeção/conversão/decodificação integral antes de publicação sem sobrescrita. A prova é de fixtures sintéticas autenticadas; não processa conteúdo remoto, não conecta o fluxo ao player e não é sandbox de rede/filesystem. O teste nativo de junction durante validação exige recusa sem saída externa; dois casos de symlink seguem sem execução pela política do host. O proprietário ainda deve compor lifecycle, validação do mesmo workspace e cleanup reconhecido antes da importação.

O [runtime privado pré-launch](../download-conversion.md#runtime-privado-autenticado-antes-do-launch) autentica nove fontes/cópias por handle e mantém capability no proxy até teardown depois de ACK. Bootstrap OS-only restringe DLL search antes de payload Qt absoluto; configuração Qt é recurso fixo, extras externos são recusados, ambiente é mínimo e fontes/SDK/player não são alterados. Hash não aprova PE arbitrário: revisão da closure do perfil conhecido é pré-condição. System32/ICU/API sets/KnownDLL/SxS/AppCompat e ausência de comprometimento do mesmo usuário/host continuam confiança explícita; ACL/pins/Job não são sandbox universal. Cleanup só por identidade/não recursivo preserva desconhecidos; residuais incertos precisam de journal/recuperação próprios. Testes reais usam apenas DNS literal/HTTP loopback, não egress público ou toda cadeia TLS.

Pendente: teste real isolado de Brave, executável/adapter de I/O e identidade/registro por usuário, perfil privado revisado/factory worker e persistência/recuperação, composição worker/rede/TLS/filesystem/conversão validada, sandbox efetivo e CLI corrigido/revisado, tela de downloads/progresso/cancelamento, importação real, perfil interno da Completa, instaladores e sua propriedade entre edições. Não declarar R1.2 ou FR-051–053 completos com componentes isolados ou testes de loopback.
