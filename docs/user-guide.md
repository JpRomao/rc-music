# RC Music — guia de uso

Este guia descreve o aplicativo Windows atual para uso local privado e experimental. Não é anúncio de release nem comprovação de instalação, segurança, desempenho ou acessibilidade nativa finais. A consulta opcional de letras é descrita abaixo; reprodução/download de fontes remotas e Linux mantêm seus gates próprios. Para a composição dos arquivos e as limitações dos artefatos, consulte os guias de [runtime Windows](windows-runtime.md) e [instalador Windows](windows-installer.md).

## Playlists

Abra **Playlists** para criar uma lista vazia com **Nova playlist**, ou copie a fila atual com **Salvar fila como…**. Selecione uma lista e use **Adicionar arquivos…** para escolher músicas locais ou **Adicionar fila atual** para copiar a fila. Selecione uma faixa da lista para mover, remover ou usar **Localizar arquivo…** quando ela mudar de lugar. Excluir uma lista conserva os arquivos de áudio. A alteração aparece depois da confirmação de salvamento. Em caso de falha, escolha **Tentar salvar** ou **Recarregar do disco** para conferir o que foi gravado.

No painel, **↑**, **↓**, **Home** e **End** movem o foco nas listas. **Enter** ou **Espaço** seleciona a linha em foco e **Tab** leva aos controles seguintes.

**Adicionar à fila** acrescenta a playlist ao fim da fila, preservando a música atual e as duplicatas. Escolher a lista não inicia áudio; para tocar, use a fila do player. As playlists ficam salvas independentemente da restauração da sessão e funcionam offline. O [guia de playlists](playlists.md) explica backup, limites e recuperação.

## Letras das músicas

Use **Versões** para guardar várias formas da letra da música atual. O RC Music abre a primeira versão salva ou a letra do arquivo local quando disponível. Selecione outra versão, **Arquivo / busca atual** ou **Sem letra** para lembrar essa escolha por música. Em **Adicionar versão**, cole texto ou LRC e escolha **Original**, **Romanizada** ou **Tradução**. **Salvar letra atual** guarda a letra do arquivo ou da busca. Selecione **Arquivo / busca atual** para usar essa ação.

Na lista de versões, use **↑/↓** para mover o cursor e **Home/End** para ir ao início ou ao fim. **Enter** ou **Espaço** aplicam a versão em foco à letra exibida, preservando a reprodução.

Para buscar, adicionar ou editar a letra de outra música, abra o menu da faixa na **Fila** e selecione **Gerenciar letras**. Use **Buscar no LRCLIB**, escolha a gravação e o formato, confira a prévia e clique em **Salvar versão**. A letra é guardada para a faixa escolhida enquanto a música atual segue. A mesma ação está em **Versões** para a música atual.

Escolha uma versão para exibi-la acima do progresso. LRC mantém seus tempos; texto comum fica sem sincronização. Excluir pede confirmação e remove somente aquela versão, preservando o áudio e as outras letras. As versões sobrevivem ao reinício e são associadas ao caminho exato do arquivo; mover/renomear a música não transfere a associação automaticamente. Em falha, tente salvar novamente ou recarregue do disco mediante confirmação; se a fila já não tiver faixa atual, a ação **Abandonar alteração** descarta apenas o candidato em memória, sem desfazer uma gravação anterior. Veja [limites e recuperação](lyrics-variants.md).

A versão selecionada aparece **acima da barra de progresso**. O player carrega automaticamente um arquivo `.lrc` UTF-8 na mesma pasta e com o mesmo nome base do áudio: `música.mp3` e `música.lrc`. Os versos acompanham a posição confirmada, incluindo pausa e saltos; arrastar o progresso ainda mostra uma prévia até o ajuste ser confirmado. Texto sem tempos aparece em uma área rolável. Nesta primeira matriz, arquivos UNC, links e offset diferente de zero não são aceitos.

Para buscar no player, clique em **Buscar letra**, confira as tags de título, artista e álbum ou preencha os campos em branco e confirme **Buscar**. A busca por gravações envia os campos preenchidos ao LRCLIB. A duração serve para comparar os resultados no app. Escolha a gravação e, se houver mais de um formato, **Sincronizada** ou **Texto simples**. Confira a prévia antes de usar ou guardar. Se a reprodução mudar, a busca da música atual é fechada. A busca aberta pela fila lê as tags da faixa escolhida e permanece vinculada a ela.

Ao detectar japonês, **Gerar romaji** aparece junto à letra. Na fila, abra **Gerenciar letras** e selecione uma versão japonesa salva. Revise o rascunho editável e salve como uma nova versão. Os tempos da letra são preservados. Com chaves de parceiro, o **LyricFind** pode complementar a busca do LRCLIB. [Preparar o romaji](lyrics-variants.md#gerar-romaji) · [Configurar o LyricFind](lyrics-variants.md#fonte-opcional-lyricfind)

Na barra de volume, a porcentagem atual de **0 a 100%** aparece ao passar o mouse ou durante o arraste, inclusive ao levar o ponteiro para fora do slider enquanto o botão estiver pressionado. Mexer no volume desativa o mudo; levá-lo a 0% ativa o mudo. O ícone mostra o estado do som: o alto-falante com "x" aparece quando o som está mudo ou em 0%, e **Ativar som** volta ao último volume audível.

## Pesquisar e solicitar álbuns no Lidarr

**Recurso retirado do aplicativo por decisão do mantenedor em 2026-11-15.** O botão, o cliente e o painel Lidarr deixam de fazer parte do fluxo vigente; não há etapa de informar chave, conectar ou solicitar álbuns no RC Music. FR-055 permanece fora de escopo conforme o [ADR-0018](architecture/decisions/0018-aurora-and-lidarr-removal.md).

A retirada se limita à integração do app: não desinstala ou altera o servidor, seus pedidos ou arquivos de áudio. Letras, playlists e downloads opcionais continuam com seus fluxos próprios. Esta seção mantém a âncora de referências antigas; o [guia histórico Lidarr](lidarr-integration.md) conserva o comportamento e as evidências anteriores, sem recomendar sua retomada. Artefatos antigos podem ainda conter aquela interface e não são atualizados por este aviso.

## Abrir música e organizar a fila

- **Abrir áudio** (`Ctrl+O`): selecione um ou mais arquivos. Eles entram no fim da fila e a primeira faixa nova começa a tocar quando estiver pronta; a fila anterior não é apagada.
- **Fila → Adicionar arquivos** (botão `+`): acrescenta os arquivos sem trocar a faixa atual. Se não houver faixa ativa, inicia a primeira nova.
- Enquanto o seletor está aberto, a reprodução em andamento continua. Cancelar não adiciona arquivos. A escolha de um arquivo ainda não confirma que ele possa ser decodificado.
- Arraste arquivos ou pastas locais para a janela. Sobre a fila, o indicador mostra a posição de inserção; fora dela, os itens são acrescentados ao fim. A busca em pastas mostra progresso e permite **Cancelar**; itens já adicionados permanecem. Confira o resumo de itens ignorados.

O seletor inclui MP3, M4A/MP4, FLAC, WAV e OGG/OGA, além de todos os arquivos para diagnóstico. A extensão sozinha não garante suporte: por exemplo, M4A com AAC não equivale a M4A com ALAC. Consulte a declaração específica do artefato no guia do instalador; não há suporte irrestrito prometido a qualquer arquivo desses contêineres. URLs de sites não são fontes de reprodução nesta versão.

Abra ou oculte **Fila** com `Ctrl+L`. Um clique ou `Ctrl+↑`/`Ctrl+↓` seleciona uma linha. Com a lista em foco, `Home` e `End` selecionam e revelam a primeira e a última faixa. Duplo clique, `Enter` ou **Tocar agora** reproduzem a ocorrência selecionada. Arquivos repetidos são permitidos e cada linha é independente. `Espaço` pausa ou continua a faixa atual, mesmo com foco na fila.

Use **Mover para cima/baixo** ou `Alt+↑`/`Alt+↓`. **Remover** (`Delete` com foco na fila) retira somente a ocorrência escolhida, mesmo quando a reprodução avança enquanto você navega pela lista. **Limpar fila** pede confirmação. Nenhuma dessas ações apaga arquivos do disco. Remover a faixa atual pode selecionar a próxima disponível. Sem sucessora, a reprodução é descarregada e as linhas anteriores são preservadas.

O menu de uma linha abre com botão direito, tecla Menu ou `Shift+F10`. Ele oferece também recuperação de ocorrências indisponíveis. Se a mesma música aparece duas vezes, remover ou localizar uma delas não altera automaticamente a outra.

**Aleatório** (`F1`) liga/desliga o sorteio das próximas faixas, com estado visível e salvo localmente. Não troca a música atual; sem repetição, as faixas já percorridas não voltam ao sorteio. Com repetição, o app percorre as ocorrências disponíveis de uma rodada antes de começar outra. Ao desligar, segue a ordem exibida na fila, sem restaurar uma ordem antiga. Itens novos entre as próximas faixas participam dos próximos sorteios; com repetição, a rodada considera toda a fila disponível.

**Repetir** (`F2`) liga/desliga a repetição da fila, com estado visível e salvo localmente. Ligada: ao terminar, a ocorrência vai para o fim e a próxima disponível toca; uma única música pode repetir sozinha. Ocorrências removidas não reaparecem e arquivos indisponíveis são ignorados sem ficar tentando indefinidamente. Desligada: mantém o avanço normal e a preferência de fim da fila. Desligar não desfaz o embaralhamento nem apaga o histórico da fila.

## Reprodução e teclado

Use **Reproduzir/Pausar**, **Faixa anterior**, **Próxima faixa**, **Volume** e **Silenciar/Ativar som**. Faixa anterior reinicia a atual depois de mais de 3 segundos, quando ela permite seek; caso contrário, procura a anterior disponível. Anterior segue a ordem exibida e não volta do início ao fim, mesmo com repetição. Ao terminar uma faixa, o player segue para a próxima reproduzível. Com repetição ligada, Próxima também pode voltar ao início. Falhas de arquivo podem fazer uma ocorrência ser pulada e marcada na fila; confira o aviso e os detalhes.

Clique ou arraste **Posição da faixa** para avançar ou voltar. Durante o arraste, o tempo mostra o destino pretendido; soltar confirma o ajuste. Ajustar a posição em pausa mantém a pausa. Se o arquivo não permitir seek, estiver carregando ou houver ajuste pendente, o controle explica a indisponibilidade. Uma recusa devolve o controle à posição confirmada.

Os botões de salto começam em 5 segundos para cada direção. Altere-os separadamente em **Preferências → Reprodução**, usando inteiros de 1 a 300. Os valores válidos atualizam os botões e os atalhos e são salvos localmente.

Em **Preferências → Reprodução → Saída de áudio**, escolha uma saída conectada ou **Padrão do sistema**. A escolha vale até fechar o RC Music. Ao trocar a saída durante uma faixa, a fila é preservada e a faixa volta pausada na posição anterior quando o arquivo permite seek.

| Ação na janela | Atalho |
|---|---|
| Abrir arquivos | `Ctrl+O` |
| Reproduzir/pausar | `Espaço` |
| Saltar pelos valores configurados | `←` / `→` |
| Faixa anterior/próxima | `Shift+←` / `Shift+→` (também `Ctrl+←` / `Ctrl+→`) |
| Diminuir/aumentar volume | `↓` / `↑` |
| Alternar mudo | `M` |
| Ligar/desligar modo aleatório | `F1` |
| Ligar/desligar repetição da fila | `F2` |
| Mostrar/ocultar fila | `Ctrl+L` |
| Preferências | `Ctrl+,` |
| Encerrar o aplicativo | `Ctrl+Q` |

Na janela principal, esses atalhos funcionam também com foco em botões, sliders e fila: `Espaço` nunca ativa o último botão clicado; use `Enter` para ativar o controle focado. `↑`/`↓` ajustam o volume em um ponto percentual. Use `Tab`/`Shift+Tab` para percorrer controles e `Ctrl+↑`/`Ctrl+↓` para selecionar linhas da fila. Na barra de posição, `Page Down`/`Page Up` ajustam 10 segundos e `Home`/`End` vão ao início/fim. Editores de texto, menus e diálogos conservam suas teclas próprias; feche ou cancele o painel ativo para voltar aos comandos da janela. Não são atalhos globais do Windows.

## Metadados e detalhes

Título, artista, álbum, número da faixa, ano e capa aparecem quando disponíveis. Sem título válido, o player usa o nome do arquivo; campos desconhecidos mostram **Não informado**. Tags ou capas ausentes, inválidas ou grandes demais não significam necessariamente falha de reprodução.

Abra **Detalhes da faixa** para ver nome, caminho, tamanho, modificação em UTC e propriedades conhecidas de áudio, como contêiner, codec, bitrate, taxa de amostragem, canais e profundidade de bits. **Mostrar diagnóstico** explica problemas de leitura e apresentação. **Copiar caminho** e **Copiar diagnóstico** copiam apenas após sua ação; revise o conteúdo antes de compartilhá-lo. O app não oferece edição de tags ou capa.

## Perfil

Na primeira abertura, o app pede um **apelido** e mostra um avatar sorteado entre os 20 do app. O apelido tem de 1 a 32 caracteres, numa linha só, sem espaços repetidos nem caracteres especiais; se ele não servir, o diálogo explica o motivo e **Continuar** fica desabilitado. Antes de continuar, você pode trocar o avatar na grade: **Tab** passa do campo para a grade e dela para **Continuar**, as setas percorrem os avatares e **Espaço** ou **Enter** escolhe o que está em foco. O app só volta a pedir se o perfil sumir.

Depois, mude apelido e avatar em **Configurações → Perfil**. **Usar imagem própria…** aceita PNG ou JPEG de até 2 MiB; o app recorta a imagem em quadrado, reduz para 256 × 256 px e guarda só essa cópia. O perfil fica em `profile.json`, no diretório de configuração, e é como você aparece na página **Ouvir junto**. Se o arquivo foi criado por uma versão mais recente ou não pôde ser lido, o app não pede um perfil novo nem altera o arquivo, e a seção Perfil avisa isso.

## Ouvir junto

A página **Ouvir junto** abre uma sala para ouvir a mesma fila com outras pessoas. Quem entra vê o anfitrião e quem mais está na sala e ouve, no próprio app, a música atual do anfitrião no mesmo ponto que ele. Abrir o app não conecta a nada: a rede só é usada enquanto você está numa sala. Um build sem o servidor de apresentação da sala mostra "Sala indisponível neste build".

Na primeira sala, criando ou entrando, o app avisa uma vez que, como em chamadas de voz, os participantes ficam sabendo o endereço IP uns dos outros. Só convide quem você conhece.

- **Abrir a sala:** **Criar sala** pede quantas pessoas podem entrar (de 1 a 10; padrão 5) e quanto o convite dura: **Termina quando eu fechar o app** (padrão) ou **Vale até eu revogar**. **Abrir sala** abre a sala com a sua fila atual. Você e quem ainda espera aprovação não contam na lotação.
- **Convidar:** **Copiar link** copia o convite; mande o link para quem você quiser. Um convite **Vale até eu revogar** continua o mesmo quando você abre a sala de novo com essa opção, mas a sala nunca reabre sozinha.
- **Aprovar:** cada pessoa nova aparece em **Pedidos para entrar**, com **Aceitar** e **Recusar**; com o app em segundo plano, o pedido também vira uma notificação do sistema. Quem espera não recebe nada da sala. Quem você aceitou uma vez entra direto nas próximas salas.
- **Entrar:** **Entrar com link** pede o convite que alguém mandou para você. Enquanto o anfitrião não aceita, a página mostra "Esperando o anfitrião aceitar seu pedido…". Ao entrar, o app guarda a sua fila e a posição, e as devolve quando você sai ou a sala acaba; se o player ainda estiver ocupado (importando arquivos ou recebendo um download), a fila volta assim que ele terminar, e nada se perde se o app fechar antes. Durante a sala, a barra do player mostra quem é o anfitrião e quantas pessoas estão na sala.
- **Fila da sala:** quem entrou vê, no painel da fila, a fila do anfitrião, com o avatar de quem adicionou cada música. O app recebe do anfitrião a música atual e as três seguintes; até o arquivo chegar, o item mostra "Carregando…". A letra que o anfitrião escolheu aparece no painel de letras. Aleatório e repetir ficam com o anfitrião ("O host controla"). As músicas recebidas ficam num cache temporário, apagado quando você sai ou a sala acaba, e quando o app abre.
- **Tocar junto:** quem entrou toca a música atual do anfitrião no mesmo ponto que ele: começa quando ele começa, pausa onde ele pausa e acompanha os saltos. Uma música que ainda está chegando entra no ponto em que a sala está quando o arquivo termina de chegar, sem voltar ao começo. Pequenas diferenças são corrigidas aos poucos, sem salto audível; diferenças maiores, com um salto. Durante a sala, a sua fila fica guardada e o player toca só a sala; o painel de letras acompanha a posição da sala. **Ajuste de sincronia**, na página **Ouvir junto** ou em **Preferências**, adianta (valor positivo) ou atrasa o seu som, para compensar uma saída que atrasa, como fones Bluetooth.
- **Sala cheia:** quem já foi aceito e chega com a sala lotada vê "A sala está cheia". Uma pessoa nova fica esperando, e o anfitrião só consegue aceitá-la quando houver lugar.
- **Sair e encerrar:** **Sair da sala** tira você na hora e libera o seu lugar. **Fechar sala** encerra a sala para todos, que veem "O anfitrião encerrou a sala.". **Revogar convite**, que aparece nas salas **Vale até eu revogar**, encerra a sala e invalida o link: quem tentar usá-lo depois vê "O anfitrião não está com a sala aberta agora.", e a próxima sala terá um link novo.
- **Conexão:** se a sua conexão com a sala cair, o app tenta voltar sozinho por 30 s ("A conexão caiu. Tentando voltar à sala…") e, se não conseguir, mostra o motivo; use **Entrar com link** para tentar de novo. Se o anfitrião fechar o app, a sala acaba para todos, que veem "O anfitrião encerrou a sala.".

## Preferências, bandeja e restauração

As alterações válidas são salvas automaticamente; não há botão Aplicar.

- **Geral:** em **Ao fechar**, escolha **Minimizar para bandeja** ou **Encerrar**. Fechar para a bandeja mantém o áudio e pode mostrar **Continuar na bandeja**. Se a bandeja não estiver disponível ao tentar ocultar, o app informa a limitação e minimiza normalmente. **Iniciar minimizado** exige bandeja disponível para ser ativado; se essa preferência já estiver salva mas a bandeja faltar na próxima abertura, a janela aparece visível, não minimizada, com aviso.
- **Restaurar última sessão:** ao abrir o app sem novos arquivos, recupera fila, seleção e posição salvas, sem iniciar áudio automaticamente. Use Reproduzir para retomar; a posição depende de o arquivo ainda estar disponível e permitir seek. Abrir explicitamente novos arquivos continua sendo uma ação de reprodução.
- **Reprodução:** além dos saltos, **Ao terminar a fila** permite **Parar e manter seleção** ou **Parar e limpar seleção**, quando a repetição está desligada. Limpar a seleção não é apagar a fila nem repetir sua reprodução.
- **Ajuste de sincronia na sala:** de −500 a +500 ms, em passos de 10 ms (setas para cima e para baixo); o padrão é 0. Vale quando você está na sala de outra pessoa: um valor positivo faz o seu som tocar antes, para compensar uma saída que atrasa, como fones Bluetooth. O mesmo ajuste aparece na página **Ouvir junto** enquanto você está numa sala.
- **Aparência:** escolha tema **Sistema**, **Claro** ou **Escuro**, e movimento **Sistema** ou **Reduzido**. A troca não exige reinício. O alto contraste ativo do sistema prevalece sobre o tema escolhido.

O menu da bandeja oferece **Mostrar/Ocultar**, transporte e **Sair**. Para encerrar de verdade, use **Sair**, `Ctrl+Q` ou feche a janela com **Ao fechar → Encerrar** selecionado; mudar essa preferência não encerra o app naquele instante. Ocultar a janela não encerra o processo. A saída aguarda as gravações necessárias e pode ser cancelada se houver falha ao salvar.

**Abrir Aplicativos Padrão**, quando disponível em Geral, abre a tela oficial do Windows, onde você escolhe o aplicativo padrão. A presença do RC Music entre as opções depende do artefato e de seu registro. Consulte as opções e os limites dos [instaladores experimentais](windows-installer.md).

## Navegação experimental

A edição Completa experimental inclui o botão **Navegar** e requer o WebView2 Evergreen x64 já instalado para abrir a janela de navegação. Informe um endereço HTTPS e use **Voltar**, **Avançar**, **Recarregar** ou **Parar**. Fechar essa janela mantém o player aberto. Em caso de falha ao abrir, o app orienta como tentar novamente e preserva a reprodução. Veja o [navegador integrado](embedded-browser.md).

Na janela de navegação, use **Mídia → Detectar mídia** para listar áudio e vídeo da página principal e de componentes acessíveis. Marque até 20 itens e use **Conferir seleção** para verificar se as fontes escolhidas continuam iguais. Mudança de página ou fonte exige nova escolha. **Baixar selecionados** permanece indisponível no navegador integrado. A janela usa perfil próprio e bloqueia permissões, popups e downloads nativos.

## Extensões para Brave e Chromium

Os candidatos preparados para Lite e Completa experimental oferecem **Preparar extensão principal** para detectar mídia em páginas e **Preparar extensão YouTube** para o fluxo separado do YouTube. As duas opções começam desmarcadas. Ao escolher qualquer uma, o instalador seleciona as ferramentas compartilhadas.

Após instalar o candidato com a opção escolhida, abra `brave://extensions`, `chrome://extensions` ou `edge://extensions`. Ative **Modo desenvolvedor**, escolha **Carregar sem compactação** e selecione a pasta indicada na tela final do instalador. Use `browser-extensions\main` para a principal e `browser-extensions\youtube` para a de YouTube. Após atualizar o RC Music, carregue a pasta da nova versão. A instalação, a conexão com o Brave e o download público ainda aguardam validação. Veja [opções, caminhos e estado do instalador](windows-installer.md#extensões-principal-e-youtube-como-opções-separadas).

## Downloads

Quando a lista de tarefas tem foco, o contorno identifica o cartão sob o cursor. Use as **setas** para trocar de tarefa e **Home/End** para ir à primeira ou à última. **Tab** abre a sequência de botões do cartão em foco. **Shift+Tab** volta do primeiro botão ao cartão.

Quando o componente de downloads está ativo, abra **Downloads** no player para acompanhar pedidos, progresso e histórico. Use **Cancelar** em um pedido que ainda permite cancelamento e **Tentar importar novamente** se o MP3 já estiver pronto e a importação para a fila falhar. As opções de repetir o carregamento ou o salvamento aparecem conforme o erro.

Ao sair pelo menu ou com **Ctrl+Q**, o RC Music salva a fila de downloads. Na próxima abertura, retoma os pendentes na mesma ordem. O download que estava em andamento reinicia a transferência do começo. Os itens cancelados manualmente e os arquivos já concluídos ficam no histórico. Se o processo for encerrado à força, o aplicativo preserva as evidências para revisão em **Downloads**.

Na página **Buscar**, pesquise pelo nome ou use **Colar links** para enviar vários vídeos de uma vez, um link do YouTube por linha. Um link do Letras na mesma linha ou na linha seguinte leva a letra junto. **Consultar links** mostra o título de cada vídeo e avisa sobre linhas sem link. **Abrir no YouTube** abre o vídeo no navegador para conferir antes de baixar. **Baixar todos** envia de uma vez os vídeos que ainda não foram pedidos.

Até três downloads rodam ao mesmo tempo; os demais esperam na fila e começam assim que uma vaga libera.

Na página **Atividade**, os filtros **Todos**, **Com erro**, **Concluídos**, **Na fila** e **Baixando** mostram só os pedidos daquele estado. Com o teclado, o grupo de filtros é uma parada do Tab, e as setas para a esquerda e a direita trocam de filtro. Pedidos cancelados aparecem só em **Todos**.

**Abrir pasta de downloads** abre o destino atual no Explorador. Em **Preferências → Downloads → Alterar pasta**, escolha uma pasta local existente no mesmo volume da pasta de downloads do aplicativo. A mudança vale para novas transferências. Os arquivos que já foram baixados ficam onde estão.

Em **Revisar letras**, use **Abrir no Letras.mus.br** para acessar a página associada. Copie cada versão que quiser guardar. No player, escolha **Colar letra**, marque **Usar página do Letras como origem** e use **Colar outra versão** para reunir original, romanização e traduções. Clique em **Guardar versões** para salvar o conjunto. [Fluxo de revisão](download-lyrics.md)

Se uma gravação for recusada, confirme **Descartar tentativa** para continuar a revisão. Use **Fechar** para voltar ao diálogo na mesma sessão ou **Concluir revisão** para liberar o pedido para arquivamento. Para acrescentar outra letra depois, abra o mesmo MP3 e use **Versões**.

Em um item interrompido, use **Verificar arquivos**. Se aparecer **Encerrar recuperação…**, o painel informa se há temporários reconhecidos ou se os arquivos esperados estão ausentes. Confirme para encerrar o item. Se o app fechou depois de registrar a limpeza, faça a mesma verificação para concluir o encerramento ao reabrir. A ação preserva a fila e qualquer MP3 final. Quando todos os itens do pedido estiverem encerrados, use **Arquivar pedidos encerrados** para liberar espaço no histórico ativo.

**Recuperar histórico** aparece quando o app pode restaurar registros preservados e reservar outra identificação para futuros pedidos. Se houver divergência entre o histórico e as confirmações da fila, os dados ficam preservados para revisão. Repetir a leitura não resolve essa divergência. Veja os [estados e ações de recuperação](download-jobs.md#painel-compartilhado-e-preparação-de-saída).

## Resolver problemas

**Arquivo não encontrado, movido ou acesso negado.** Verifique se o arquivo ou drive está disponível. **Tentar reproduzir novamente** reabre o mesmo caminho. Se a faixa fazia parte de uma restauração interrompida, ela volta pausada na posição salva, quando o áudio permite seek. As demais retentativas podem iniciar a reprodução. Se o arquivo mudou de lugar, use **Localizar arquivo…** na ocorrência afetada. A localização preserva a ordem da fila. Para a faixa atual em erro, tenta recuperar a posição com a reprodução pausada. Para outra linha, a faixa atual continua tocando e uma nova tentativa verifica o arquivo localizado. **Abrir outro áudio** também está disponível.

**A unidade da faixa foi desconectada:** reconecte a unidade e use **Tentar reproduzir novamente**. O app mantém a faixa na fila e a recupera pausada, na posição anterior quando o arquivo permite. Se o Windows atribuir outra letra à unidade, use **Localizar arquivo…** para indicar o novo caminho.

**Arquivo inválido ou não suportado:** consulte Detalhes da faixa e tente outro arquivo conhecido. O erro pode vir do conteúdo, não apenas da extensão; renomear a extensão não converte o áudio. Não altere o original para tentar contornar o erro.

Se a saída de áudio escolhida for desconectada, reconecte-a e use **Tentar novamente** no aviso ou escolha outra em **Preferências → Reprodução**. A recuperação preserva a fila e volta pausada na posição anterior quando possível. Se estiver usando **Padrão do sistema**, confira a saída padrão nas configurações de som do Windows. Se não houver som sem aviso de erro, confira o volume e o mudo no player e no Windows.

**Preferências ou sessão recuperadas:** revise o aviso, confira preferências/fila e retome quando desejar. Se os dados anteriores eram inválidos e não havia backup válido, o app usa valores seguros ou uma sessão vazia. **Dispensar aviso** apenas fecha o aviso.

**Não foi possível salvar:** as alterações ainda existem nesta execução, mas podem não sobreviver ao encerramento. Verifique espaço e acesso ao armazenamento e use **Tentar novamente**. Se Sair não concluir, resolva o aviso antes de tentar sair outra vez; não é necessário apagar configurações para repetir a gravação.

**Dados de uma versão mais recente:** leia se a ação é **Substituir preferências**, **Substituir sessão** ou **Substituir dados locais**. Só confirme se quiser realmente substituir os dados indicados; a confirmação avisa que isso não pode ser desfeito. Cancelar os preserva. Se o aviso mudar, uma confirmação anterior deixa de valer.

**O app não inicia ou o Windows bloqueia o artefato:** registre a mensagem exata e consulte os [pré-requisitos e limites do runtime](windows-runtime.md) e do [instalador](windows-installer.md). Não desative antivírus/SmartScreen, não baixe DLLs avulsas e não apague configurações ou entradas de Registry para contornar o problema. Encaminhe a mensagem ao mantenedor.

## Diagnóstico local opcional

Primeiro use **Detalhes da faixa → Mostrar diagnóstico**. Para investigar uma falha com o mantenedor, o executável Windows real aceita `--diagnostics`. Encerre a instância existente pelo próprio app; depois, em PowerShell aberto na pasta do runtime que contém `RCMusic.exe`, use:

```powershell
& .\RCMusic.exe --diagnostics
```

Essa opção vale para a execução iniciada, não é uma preferência permanente nem ativa diagnóstico numa instância já aberta. Na próxima abertura normal, omita-a. Não use opções `--m0-*`: são reservadas a executáveis de teste e recusadas pelo produto.

Os registros ficam na subpasta `diagnostics` do diretório local de configuração, cujo identificador interno continua **MusicPlayer**, não ao lado das músicas. São eventos/códigos e contagens, sem conteúdo de áudio, tags ou caminhos livres. O limite atual é de três arquivos de até 256 KiB; rotação e expiração de sete dias são verificadas ao gravar, não por um serviço de limpeza em segundo plano. O diagnóstico é de melhor esforço e pode não registrar todos os eventos. Ao pedir ajuda, informe a versão/artefato, a ação realizada e a mensagem observada; compartilhe apenas informações que tenha revisado.
