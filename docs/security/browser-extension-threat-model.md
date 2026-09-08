# Fronteira de segurança da extensão principal

**Data:** 2026-09-08. **Estado:** desenho inicial R1.2/R1.7; controles não comprovados até implementação/testes. Download/conversão e receptor nativo ainda exigem revisão própria.

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
- Detector debounced e efêmero durante popup aberto; navegação/desconexão desfaz timers/observadores. Reabrir não acumula observadores.
- Timeout da resposta nativa de 5 s, cancelamento de observação e resultado incerto sem reenvio.

Esses são limites de segurança/UX, não benchmarks nem garantia de consumo da engine. Ajustes precisam de testes de fronteira, sem usar páginas pessoais.

## Validação e lacunas

Cobrir conteúdo dinâmico, duplicatas, lotes parcialmente inválidos sem ampliar seleção, campos malformados/extra, payload excessivo, navegação com mesma origem, documentos removidos, remetente falso, ACK de outro pedido, dupla ação e respostas tardias. Erros apresentados usam códigos próprios, nunca mensagens livres de navegador/host contendo paths ou URLs.

Pendente: teste real isolado de Brave, receptor nativo/registro por usuário, destinos DNS/redirects, fila de downloads/progresso/cancelamento, conversão e importação, perfil interno da Completa, instaladores e sua propriedade entre edições. Não declarar R1.2 ou FR-051–053 completos com testes somente do cliente.
