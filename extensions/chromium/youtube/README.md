# RC Music — extensão YouTube opcional

Este pacote MV3 é separado de `../main`. A extensão principal continua sem suporte direto ou indireto a YouTube. O host deste pacote é **`com.rcmusic.youtube`**; não usa `com.rcmusic.media` e não registra nenhum deles no Windows.

## Fluxo do cliente

1. Abra uma página de música no Letras ou uma página HTTPS de vídeo no YouTube (`watch`, `shorts`, `embed` ou `youtu.be`) e abra esta extensão.
2. Selecione explicitamente o vídeo atual. Confira título e artista. Para escolher outro nome para o MP3, preencha **Nome do arquivo (opcional)**. O padrão é Música - Artista.mp3. Informe a página do Letras se a conhecer. A seleção usa somente o ID do vídeo e metadados editáveis, sem capturar a URL do stream, cookies ou o player.
3. **Baixar MP3 + letra** envia um pedido de áudio ao componente YouTube. Desmarcar a letra muda a ação para **Baixar MP3**. O servidor precisa resolver o áudio, converter/validar o MP3 e tratar Letras/LRCLIB; o cliente não baixa ou converte mídia.
4. **Pedido aceito** é somente confirmação de admissão. Progresso e conclusão pertencem ao aplicativo. Falha/desconexão/timeout não provoca retry automático: confira a fila no player antes de reenviar.

Abrir/atualizar a lista não faz pedido nativo. Mudança de vídeo/aba revoga a seleção. O popup usa texto simples, possui controles de teclado e mantém estado somente enquanto está aberto. Permissões: `activeTab` e `nativeMessaging`, sem permissão global de sites, script injetado, gravação, coleta de histórico ou tráfego HTTP da extensão.

## Estado e limites

O cliente e seu contrato têm testes offline; este pacote **não instala a ponte nem habilita downloads no player normal**. O host separado, resolver, execução protegida, aquisição de letras e integração do instalador têm seus próprios gates. Uma página do Letras informada não comprova que o serviço esteja acessível ou que a importação esteja autorizada. Não há promessa de aprovação na Chrome Web Store ou funcionamento físico no Brave por esses testes.

O protocolo v1 é `youtube.audio.request`/`youtube.audio.response`, com UUIDv4, até **20 vídeos** e **64 KiB** por mensagem. Este popup seleciona um vídeo por pedido. Só admite IDs YouTube de 11 caracteres, metadados de até 512 bytes UTF-8 e página opcional HTTPS do Letras de até 2048 bytes. Não aceita shell, executável, caminho local ou flags de ferramentas em mensagens.

Os testes são os quatro arquivos `tests/extensions/youtube-*.test.mjs`; `youtube-protocol-vectors.json` é compartilhado com o codec Qt. São usados Chrome/Native Messaging falsos e dados sintéticos. Não executam navegador, host nativo, download, instalação ou Registro.

O [ADR-0006](../../../docs/architecture/decisions/0006-browser-editions-and-extensions.md) conserva a integração YouTube independente e opcional, para Lite e Completa. Este recorte não altera os opcionais do instalador nem os perfis existentes do navegador.
