# Brave/Letras → RC Music → MP3

**Estado:** desenho aprovado pelo mantenedor: “Implementar desta forma”. A configuração local da ponte e o teste no Brave já foram autorizados; não é necessário pedir novamente essa mesma autorização.

## Objetivo e aceite

Navegar no Brave, selecionar a mídia pela extensão e acompanhar no RC Music a aquisição de um MP3 final válido. MP4 é somente uma possível entrada: extrair e converter seu áudio, sem entregar vídeo MP4, renomear extensão ou interromper a faixa atual.

O exemplo de aceite é `https://www.letras.mus.br/bleach/1222774/`. A página foi aberta no Brave real em janela dedicada. Seu HTML público declara `YoutubeID: "wW9TwZdWpjw"`; não foi encontrado um arquivo MP4 direto nesse HTML. A cópia de inspeção está em `C:/Users/Joao/AppData/Local/hermes/cache/scratch/letras-1222774-source-a96b14.html`, SHA-256 `fa33f9e8a3604fbf56c587d08a4298bb6b0650db3521df824f6b5387eb68e562`. Essa observação identifica a origem declarada, não comprova aquisição ou um contrato oficial do site.

Critério final: selecionar o vídeo dessa página, enviar uma vez à ponte apropriada, observar o pedido na aplicação desktop e obter MP3 decodificável/publicado com confirmação durável, sem autoplay ou duplicação. Um ACK, uma fixture local ou o resultado isolado do resolver não satisfazem esse aceite.

## Abordagem

Reaproveitar MV3, Native Messaging, hosts separados e o pipeline existente, conforme os ADRs [0006](../../architecture/decisions/0006-browser-editions-and-extensions.md), [0007](../../architecture/decisions/0007-main-extension-boundary.md) e [0017](../../architecture/decisions/0017-youtube-audio-and-lyrics-acquisition.md). O app continua dono do download, conversão, cancelamento, publicação e importação.

Alternativas descartadas: baixar no navegador e só abrir o arquivo depois não entrega a coordenação no app; um novo servidor HTTP local ou captura genérica de tráfego/cookies amplia permissões e contradiz as fronteiras aceitas. Não reimplementar serviços que já existem nem transformar a principal em um encaminhador YouTube.

## Extensões

- **Principal:** preservar mídia direta MP3/MP4/M4A e a recusa de YouTube. Exercitar sua ponte existente com fixture própria MP4 → MP3.
- **YouTube:** permitir selecionar o vídeo declarado na página Letras, além das URLs YouTube já reconhecidas. Manter pacote, ID, host e pedido `youtube.audio.request` próprios.
- Acrescentar somente `scripting` à extensão YouTube, junto das permissões existentes `activeTab` e `nativeMessaging`. Sem `<all_urls>`, cookies, `webRequest`, histórico, downloads próprios ou código remoto.
- Ler o documento principal em mundo isolado somente após abrir/atualizar o popup. Para Letras, reconhecer o descriptor público do player por parsing de dados, nunca `eval` ou execução do JavaScript da página. Um iframe YouTube reconhecido pode fornecer a mesma identidade; fontes ambíguas ou divergentes devem ser recusadas.
- Restringir a leitura específica aos hosts Letras exatos e à estrutura de página reconhecida. Limitar elementos/scripts/bytes examinados e os metadados retornados. Uma mudança na estrutura do site produz indisponibilidade acionável, não descoberta arbitrária ou fallback para outros provedores.
- Mostrar título editável, seleção explícita e a página Letras como contexto da letra. Não presumir que o nome do catálogo ou canal seja o artista da gravação.
- Antes do envio e do reenvio explícito, reconferir janela, aba, URL, documento e identidade do vídeo. Navegação, troca de documento, fonte ou foco revogam a seleção. Respostas antigas não rearmam o popup.
- Manter os schemas e limites atuais da ponte. A página não fornece executável, argumentos, destino, headers ou autoridade de execução.

## Ponte e aplicativo

Reutilizar os perfis compilados, manifesto Native, componente opcional e owner existentes. Conferir identidade da extensão, origem permitida, executável e endpoint antes de registrar ou executar. Um perfil vazio ou componente ausente deve preservar o player local e expor o motivo, sem fingir download.

O registro autorizado é somente por usuário para os hosts RC Music. Preservar valores preexistentes; conflitos de propriedade exigem resolução, não overwrite. Não alterar associações/padrões do Windows, políticas do Brave, outras extensões, UAC ou WFP. Não fechar o Brave ou a instância pessoal do player. Usar janela dedicada e dados de teste isolados; se a instância atual impedir isolamento, registrar o impedimento antes de propor sua substituição.

Utilizar as ferramentas autenticadas e o pipeline existente de rede → conversão real → segunda validação MP3 → publicação sem overwrite → importação durável. Cancelamento exige parada/drenagem reconhecidas. Não relaxar TLS/DNS, quotas, identidade, NoChild, ACKs ou gates de isolamento para obter um resultado aparente.

O gate físico AppContainer e a distribuição pública permanecem separados. Se a jornada pública depender de um gate de segurança não atendido, parar nesse gate e relatar o bloqueio; a autorização da ponte não autoriza novos perfis, elevação ou mudança de política.

## Validação proporcional

1. Red/Green do coletor e do popup: página Letras reconhecida, dados malformados/ambíguos, limites, documento/vídeo substituídos na mesma URL, perda de foco, replay e zero envio sem clique.
2. Regressão das duas extensões para preservar a separação e os schemas; atualizar e testar o inventário fechado do empacotamento se houver novo asset.
3. Checks focados da ponte/composição somente quando seus inputs forem alterados; reutilizar recibos e ferramentas preservados, sem remontagens ou suíte C++ global por hábito.
4. Configuração real: ler de volta os hosts exatos registrados e conferir extensão/ID/erros no Brave; confirmar o componente e o painel no app.
5. Jornada real autorizada: mídia direta própria MP4 → MP3 e página Letras → canal YouTube → MP3. Verificar bytes/decodificação, recibos e ausência de duplicação/autoplay. Se uma delas falhar, não declarar a integração completa.
6. Registrar checks executados, evidências anteriores, falhas e gates pendentes separadamente. Não usar testes sintéticos como prova no site real.

## Fora do escopo

Saída de vídeo, captura de áudio do sistema, scraping automático de letras, tradução/LLM, DRM ou autenticação contornados, exportação de cookies, Linux, benchmarks, novo player, outro protocolo de download ou publicação de extensões/instaladores.
