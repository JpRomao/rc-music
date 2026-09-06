# RC Music — guia de uso

Este guia descreve o aplicativo Windows atual para uso local privado e experimental. Não é anúncio de release nem comprovação de instalação, segurança, desempenho ou acessibilidade nativa finais. Linux e fontes remotas ainda não fazem parte desta entrega. Para a composição dos arquivos e as limitações dos artefatos, consulte os guias de [runtime Windows](windows-runtime.md) e [instalador Windows](windows-installer.md).

## Abrir música e organizar a fila

- **Abrir áudio** (`Ctrl+O`): selecione um ou mais arquivos. Eles entram no fim da fila e a primeira faixa nova começa a tocar quando estiver pronta; a fila anterior não é apagada.
- **Fila → Adicionar arquivos** (botão `+`): acrescenta os arquivos sem trocar a faixa atual. Se não houver faixa ativa, inicia a primeira nova.
- Enquanto o seletor está aberto, a reprodução em andamento continua. Cancelar não adiciona arquivos. A escolha de um arquivo ainda não confirma que ele possa ser decodificado.
- Arraste arquivos ou pastas locais para a janela. Sobre a fila, o indicador mostra a posição de inserção; fora dela, os itens são acrescentados ao fim. A busca em pastas mostra progresso e permite **Cancelar**; itens já adicionados permanecem. Confira o resumo de itens ignorados.

O seletor inclui MP3, M4A/MP4, FLAC, WAV e OGG/OGA, além de todos os arquivos para diagnóstico. A extensão sozinha não garante suporte: por exemplo, M4A com AAC não equivale a M4A com ALAC. Consulte a declaração específica do artefato no guia do instalador; não há suporte irrestrito prometido a qualquer arquivo desses contêineres. URLs de sites não são fontes de reprodução nesta versão.

Abra ou oculte **Fila** com `Ctrl+L`. Um clique ou `Ctrl+↑`/`Ctrl+↓` seleciona uma linha; duplo clique, `Enter` ou **Tocar agora** reproduzem essa ocorrência. Arquivos repetidos são permitidos e cada linha é independente. `Espaço` pausa/continua a faixa atual, mesmo com foco na fila.

Use **Mover para cima/baixo** ou `Alt+↑`/`Alt+↓`. **Remover** (`Delete` com foco na fila) retira somente a ocorrência escolhida; **Limpar fila** pede confirmação. Nenhuma dessas ações apaga arquivos do disco. Remover a faixa atual pode selecionar a próxima disponível; sem sucessora, a reprodução é descarregada, preservando as linhas anteriores.

O menu de uma linha abre com botão direito, tecla Menu ou `Shift+F10`. Ele oferece também recuperação de ocorrências indisponíveis. Se a mesma música aparece duas vezes, remover ou localizar uma delas não altera automaticamente a outra.

**Aleatório** (`F1`) liga/desliga o sorteio das próximas faixas, com estado visível e salvo localmente. Não troca a música atual; sem repetição, as faixas já percorridas não voltam ao sorteio. Com repetição, o app percorre as ocorrências disponíveis de uma rodada antes de começar outra. Ao desligar, segue a ordem exibida na fila, sem restaurar uma ordem antiga. Itens novos entre as próximas faixas participam dos próximos sorteios; com repetição, a rodada considera toda a fila disponível.

**Repetir** (`F2`) liga/desliga a repetição da fila, com estado visível e salvo localmente. Ligada: ao terminar, a ocorrência vai para o fim e a próxima disponível toca; uma única música pode repetir sozinha. Ocorrências removidas não reaparecem e arquivos indisponíveis são ignorados sem ficar tentando indefinidamente. Desligada: mantém o avanço normal e a preferência de fim da fila. Desligar não desfaz o embaralhamento nem apaga o histórico da fila.

## Reprodução e teclado

Use **Reproduzir/Pausar**, **Faixa anterior**, **Próxima faixa**, **Volume** e **Silenciar/Ativar som**. Faixa anterior reinicia a atual depois de mais de 3 segundos, quando ela permite seek; caso contrário, procura a anterior disponível. Anterior segue a ordem exibida e não volta do início ao fim, mesmo com repetição. Ao terminar uma faixa, o player segue para a próxima reproduzível. Com repetição ligada, Próxima também pode voltar ao início. Falhas de arquivo podem fazer uma ocorrência ser pulada e marcada na fila; confira o aviso e os detalhes.

Clique ou arraste **Posição da faixa** para avançar ou voltar. Durante o arraste, o tempo mostra o destino pretendido; soltar confirma o ajuste. Ajustar a posição em pausa mantém a pausa. Se o arquivo não permitir seek, estiver carregando ou houver ajuste pendente, o controle explica a indisponibilidade. Uma recusa devolve o controle à posição confirmada.

Os botões de salto começam em 5 segundos para cada direção. Altere-os separadamente em **Preferências → Reprodução**, usando inteiros de 1 a 300. Os valores válidos atualizam os botões e os atalhos e são salvos localmente.

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

## Preferências, bandeja e restauração

As alterações válidas são salvas automaticamente; não há botão Aplicar.

- **Geral:** em **Ao fechar**, escolha **Minimizar para bandeja** ou **Encerrar**. Fechar para a bandeja mantém o áudio e pode mostrar **Continuar na bandeja**. Se a bandeja não estiver disponível ao tentar ocultar, o app informa a limitação e minimiza normalmente. **Iniciar minimizado** exige bandeja disponível para ser ativado; se essa preferência já estiver salva mas a bandeja faltar na próxima abertura, a janela aparece visível, não minimizada, com aviso.
- **Restaurar última sessão:** ao abrir o app sem novos arquivos, recupera fila, seleção e posição salvas, sem iniciar áudio automaticamente. Use Reproduzir para retomar; a posição depende de o arquivo ainda estar disponível e permitir seek. Abrir explicitamente novos arquivos continua sendo uma ação de reprodução.
- **Reprodução:** além dos saltos, **Ao terminar a fila** permite **Parar e manter seleção** ou **Parar e limpar seleção**, quando a repetição está desligada. Limpar a seleção não é apagar a fila nem repetir sua reprodução.
- **Aparência:** escolha tema **Sistema**, **Claro** ou **Escuro**, e movimento **Sistema** ou **Reduzido**. A troca não exige reinício. O alto contraste ativo do sistema prevalece sobre o tema escolhido.

O menu da bandeja oferece **Mostrar/Ocultar**, transporte e **Sair**. Para encerrar de verdade, use **Sair**, `Ctrl+Q` ou feche a janela com **Ao fechar → Encerrar** selecionado; mudar essa preferência não encerra o app naquele instante. Ocultar a janela não encerra o processo. A saída aguarda as gravações necessárias e pode ser cancelada se houver falha ao salvar.

**Abrir Aplicativos Padrão**, quando disponível em Geral, abre a tela oficial do Windows; não muda o aplicativo padrão sozinho. A presença do RC Music entre as opções depende do artefato e de seu registro. Este manual não instrui instalar ou registrar o app; veja as limitações do [instalador](windows-installer.md).

## Resolver problemas

**Arquivo não encontrado, movido ou acesso negado:** verifique se o arquivo ou drive está disponível. Use **Tentar reproduzir novamente** para reabrir o mesmo caminho; essa ação explícita pode iniciar a reprodução. Se o arquivo mudou de lugar, use **Localizar arquivo…** na ocorrência afetada. A localização preserva sua posição na fila: se for a faixa atual em erro, tenta recuperá-la pausada e restaurar a posição; se for outra linha, não interrompe a faixa atual e pede uma tentativa de reprodução para verificar a nova origem. Cancelar a localização não troca o caminho. **Abrir outro áudio** continua sendo uma alternativa.

**Arquivo inválido ou não suportado:** consulte Detalhes da faixa e tente outro arquivo conhecido. O erro pode vir do conteúdo, não apenas da extensão; renomear a extensão não converte o áudio. Não altere o original para tentar contornar o erro.

**Saída de áudio indisponível ou alterada:** conecte uma saída ou escolha a saída padrão nas configurações de som do Windows. Depois use **Tentar novamente** no aviso. A recuperação é pausada e conserva a fila; a posição é restaurada quando possível. Reconectar o dispositivo não reinicia o áudio automaticamente. Ainda não há seletor de dispositivo dentro do RC Music. Se não houver som sem aviso de erro, confira o volume/mudo do player e a saída e o volume do Windows.

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
