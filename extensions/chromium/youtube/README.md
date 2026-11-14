# RC Music — extensão YouTube opcional

Este pacote MV3 é separado de `../main`. A extensão principal continua sem suporte direto ou indireto a YouTube. O host deste pacote é **`com.rcmusic.youtube`**; não usa `com.rcmusic.media` e não registra nenhum deles no Windows.

## Fluxo do cliente

1. Abra uma página de música no Letras ou uma página HTTPS de vídeo no YouTube (`watch`, `shorts`, `embed` ou `youtu.be`) e abra esta extensão.
2. Selecione o vídeo atual e confira sua URL. Confirme o título e o artista da música. O popup aproveita créditos musicais estruturados quando disponíveis e permite preencher os campos manualmente. Informe a **URL do Letras (opcional)** se a conhecer. Deixe **Nome do arquivo (opcional)** vazio para usar **Música - Artista.mp3**.
3. **Baixar MP3 + letra** envia um pedido de áudio ao componente YouTube. Desmarcar a letra muda a ação para **Baixar MP3**. O servidor precisa resolver o áudio, converter/validar o MP3 e tratar Letras/LRCLIB; o cliente não baixa ou converte mídia.
4. **Pedido aceito** é somente confirmação de admissão. Progresso e conclusão pertencem ao aplicativo. Falha/desconexão/timeout não provoca retry automático: confira a fila no player antes de reenviar.

Abrir ou atualizar a lista não faz pedido nativo. Mudança de vídeo ou aba revoga a seleção. O popup usa texto simples, possui controles de teclado e mantém estado somente enquanto está aberto. Usa `activeTab` e `scripting` para ler dados da página ativa e `nativeMessaging` para enviar o pedido. A leitura fica restrita à página selecionada.

## Estado e limites

O cliente e seu contrato têm testes offline; este pacote **não instala a ponte nem habilita downloads no player normal**. O host separado, resolver, execução protegida, aquisição de letras e integração do instalador têm seus próprios gates. Uma página do Letras informada não comprova que o serviço esteja acessível ou que a importação esteja autorizada. Não há promessa de aprovação na Chrome Web Store ou funcionamento físico no Brave por esses testes.

O protocolo v1 é `youtube.audio.request`/`youtube.audio.response`, com UUIDv4, até **20 vídeos** e **64 KiB** por mensagem. Este popup seleciona um vídeo por pedido e envia seu ID e a URL canônica correspondente. O host valida a concordância entre os dois. Título e artista têm até 512 bytes UTF-8. A página opcional do Letras tem até 2048 bytes e usa HTTPS. O nome de arquivo só é enviado quando foi preenchido. Atualize a extensão e o host do RC Music juntos para usar a versão 0.1.5.

Os testes em `tests/extensions/youtube-*.test.mjs` usam Chrome/Native Messaging falsos e dados sintéticos. O corpus `youtube-protocol-vectors.json` é compartilhado com o codec Qt. A disponibilidade dos créditos em uma página específica depende dos dados fornecidos pelo site.

O [ADR-0006](../../../docs/architecture/decisions/0006-browser-editions-and-extensions.md) conserva a integração YouTube independente e opcional, para Lite e Completa. Este recorte não altera os opcionais do instalador nem os perfis existentes do navegador.
