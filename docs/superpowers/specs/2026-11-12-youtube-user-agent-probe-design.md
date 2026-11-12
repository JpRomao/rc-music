# Prova temporária de compatibilidade User-Agent

**Autorização:** o mantenedor escolheu “Autorizar teste temporário de User-Agent”, após o desenho curto de preservar apenas o User-Agent público validado do resolvedor em uma tentativa isolada, sem cookies/Authorization/cabeçalhos da página, com pins e políticas ativos e restauração. Execução direta na worktree existente; não é autorização para promover uma correção permanente ou trocar a instalação.

## Objetivo e evidência existente

O teste real anterior correlacionou Asterisk ao job `aa06c29fbf9e4f9188c413249fc5df30-1` e à razão fechada `http_403`, com zero bytes e limpeza completa. O mantenedor relata o mesmo aviso para outra mídia que considera sem restrição autoral; isso não identifica o status exato desse outro pedido nem prova DRM.

O resolvedor Python preserva `http_headers`. O decoder C++ valida quatro hints públicos, mas os descarta; o GET mínimo não contém User-Agent. Essa diferença é comprovada por código, mas sua causalidade para o 403 ainda não foi testada. Ambos os lados já desabilitam proxy, portanto não se deve atribuir o problema a proxy por hipótese.

## Abordagens

- **Escolhida e autorizada:** tentativa de compatibilidade com somente o User-Agent do resolvedor, habilitada exclusivamente pela composição C++ do candidato. É uma intervenção estreita e testável; sucesso em uma nova URL/tempo não demonstra causalidade exclusiva nem aceitação global.
- **Alternativa:** auditoria de assinatura/runtime/contexto sem alterar a requisição. Mantém o transporte original, mas não testa diretamente a diferença de User-Agent.
- Não encaminhar dicionário genérico de headers, cookies, Authorization, Referer ou headers fornecidos pela página; não atualizar yt-dlp/EJS/Node como parte deste teste.

## Desenho técnico — decisões do agente

1. Preservar somente `QByteArray YoutubeResolvedAudioData::user_agent_hint`, já submetido à validação integral existente: ASCII imprimível, máximo de 512 bytes, duplicatas sem distinção de caixa recusadas, outros nomes não autorizados recusados. Ausência/vazio continuam sem header. A capability continua exclusiva da sessão do resolvedor após saída/EOF e não é criada por URL/flag de página.
2. `DirectDownloadLimits::forward_youtube_user_agent` começa em `false`. Somente a composição confiável do candidato o habilita; mídia direta e todos os defaults continuam mínimos. Uma capability válida fornece o hint em cada hop YouTube aprovado, sem mudar Host, request-target assinado, DNS, TLS, allowlists, expiração, cotas ou ACKs.
3. `PinnedDownloadEndpoint::user_agent` carrega um único valor limitado. O stream e o protocolo privado aplicam a mesma validação antes de abrir conexão/lançar worker; CR/LF/NUL/DEL/não ASCII/tamanho excessivo são recusados. Não existe entrada de nomes arbitrários de headers.
4. Preservar exatamente o request binário `RCMD` versão 1 quando não há User-Agent. Somente Stream com hint não vazio usa versão 2: os campos existentes, um campo length-prefixed de User-Agent após request-target, depois port/TLS. Decoder aceita v1 estrito e v2 estrito, rejeitando v2 Resolve, hint vazio/malformado, truncamento e bytes extras. Frames de resposta permanecem iguais. O worker antigo deve recusar v2, nunca aceitar removendo silenciosamente o hint.
5. Observador opcional confiável `DirectDownloadLimits::on_request_user_agent(bool)` registra apenas se o endpoint tentou incluir User-Agent. Sem valor, URL, IP, corpo, token ou metadata remota; callback não reentra/destrói sessão e exceções são contidas. O observador Restricted anterior continua separando 403 de outras falhas. No teste real, exigir registro completo que comprove inclusão antes de interpretar o resultado.
6. Artefatos privados novos ficam em `out/dg/ua/`, sem reescrever os recibos/evidências anteriores. Recompilar app e payload/worker reais; derivar novos hashes de artefatos produzidos pelo build confiável em perfil/recibo novos. Não modificar pins, manifestos ou header Node24223 originais para promover bytes diferentes. Ferramentas YouTube, media worker e demais bibliotecas permanecem conferidos.
7. Antes da tentativa, conferir o candidato e o listener sem conectar/enviar job. Capturar valores/tipos/existência dos aliases próprios YouTube Brave e Google/Chrome nas views relevantes e preservar ambos os hosts principais. Fechar somente RC Music graciosamente e usar config root novo. Fazer um único novo envio manual explicitamente solicitado; não reutilizar ID nem reenviar automaticamente.
8. Após falha, cancelamento ou sucesso, restaurar exatamente todos os aliases tocados, encerrar candidato pelos gates normais e reabrir o app original. Capturar nova linha de base da fila/posição antes do reinício: não impor o snapshot antigo se o mantenedor mudou a fila desde então.

## Validação e limites

TDD focado para hint/capability, default mínimo e opt-in, redirects e expiração, callback/erro/ACK, codec v1/v2 e HTTP real sintético no fio, incluindo injeção e limites. Ampliar aos consumidores do endpoint/protocolo e execução preparada, não à suíte global por hábito. Testes não usam mídia pessoal nem internet.

Uma revisão independente única examina segurança/defaults/proveniência antes da composição operacional. A evidência real exige um pedido correlacionado, inclusão do hint comprovada sem expor seu valor e resultado/limpeza/restauração verificáveis. Se persistir 403, registrar hipótese não confirmada e parar esta intervenção; não encadear mudanças de headers/ferramentas por tentativa. Se funcionar, validar bytes/decodificação/importação MP3 no candidato, sem declarar promoção/instalação ou causalidade exclusiva.

**Não objetivos:** resolver autenticação/DRM, burlar recusa, qualificar AppContainer/distribuição, mudar outras extensões ou encerrar Brave. O produto normal não passa a encaminhar User-Agent por padrão e nenhum sucesso é presumido.
