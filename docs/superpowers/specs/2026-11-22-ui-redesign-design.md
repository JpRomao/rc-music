# Redesenho da interface — continuação do Aurora

## Estado e origem

**Pedido do mantenedor (2026-11-21):** “Precisamos de uma mudança no visual completa. Fazer uma refatoração de UI. Faça o design completo pensando no UX das funcionalidades de hoje e colocando uma boa UI, acessível, bem separada, com botões melhores e um player mais agradável (tem que ter tema claro e escuro, as cores de hoje não são ruins)”.

Perguntado se o trabalho substitui ou continua o Aurora ([2026-11-15-aurora-shell-design.md](2026-11-15-aurora-shell-design.md)), respondeu **“Continuar o Aurora”**. A paleta, os tokens, os contratos de migração 1–8 e as fatias 1–8 daquele documento continuam valendo. Este documento define a etapa seguinte: a linguagem visual definitiva, as peças comuns, cada tela e as poucas mudanças de comportamento aprovadas.

Sobre o que incomoda hoje, o mantenedor disse: **“As bordas são feias e poderíamos colocar ícones melhores”**.

**Mockups aprovados**, tela a tela, entre 2026-11-21 e 2026-11-22, no companheiro visual do brainstorming. Cópias de referência ficam em `out/product/ui-redesign-mockups-20261122/` (fora do Git):
- `telas/` traz as telas aprovadas. Abrem no navegador com rede, porque os ícones vêm do unpkg.
- `gerador/` traz o código que gerou as telas e `decisoes.md`, o registro das escolhas.

| Tela | Arquivo | SHA-256 |
|---|---|---|
| Botões e ícones | `botoes-e-icones-v2.html` | `d3f2276d30ec623942ebdb0605b4c0c1201f9e3e3b488d30c002749b3d145216` |
| Player | `player-v2.html` | `19e101ea0f661b3051fd9bcec4e8f08c32b1cf3cbc0966e9e1641abc90635e09` |
| Casca e Início | `inicio-v3.html` | `c91b7145035b71d4b0ad410b2d919de3b3bffa619aed8c6ffceefa025bced8fd` |
| Biblioteca e Buscar | `biblioteca-buscar-v2.html` | `4a085fbfe93eb53c122224d6c170c96c264cbfdefba7980072e1f964cb8b9258` |
| Letras e gavetas | `letras-e-gavetas.html` | `f7e9e0883b314c2d7678cc5d2679373ddd19d3892e35b17b4c86c28e889a7af5` |
| Menu da faixa | `menu-da-faixa.html` | `ace9d03249893efadc9fb45907823d4ee86d5ad5a82ecfc38e6bc8b8be369873` |
| Playlists e Atividade | `playlists-atividade-v2.html` | `6a013c9dab52fc9430d720a9139f877cbc66013e7dac4e3090e6193443eaf7ae` |
| Ouvir junto | `ouvir-junto.html` | `437c85c9e003194e40bc8f5080ab17b690ec466cb91b81cc9b5cf4703a9f1133` |
| Configurações, perfil e detalhes | `configuracoes.html` | `68abddf1ea2192a6d90f85a2d655c95fe0e7671524a47848073566a72bd5534a` |
| Diálogos de letra e avisos | `dialogos-e-avisos.html` | `fde11fef4b4513dd286f4ad32fa7ca17964b273cfca595d1a19911952079460c` |
| Janela estreita, alto contraste e bandeja | `estreita-contraste-bandeja.html` | `731bc01e4993e139b0306eee0e67c615166d4fd341f8bf7973cbd8aea28e31fb` |
| Marcas de estado | `marcas-de-estado.html` | `7581d176e000b1f02cde02bd5dafe33d93d18fa2402d81715bd7b827307f66cc` |

Os mockups são HTML descartável. Eles mostram composição, hierarquia e comportamento, não pixels exatos nem uma stack nova. Os textos que aparecem neles são os de hoje ou rascunhos. Os textos definitivos seguem a seção “Textos”.

**O que este aceite não cobre:** instalador intermediário, que só sai quando o redesenho terminar por completo; persistência nova; provedor remoto novo; Linux; mudanças de protocolo da sala; e qualquer comportamento além da lista “Comportamento novo”.

## Intenção

O app hoje funciona, mas cada tela foi crescendo com o próprio jeito de fazer as coisas. Há bordas de 1 px por toda parte, dois botões de destaque diferentes, campos e diálogos no estilo Basic ao lado de campos estilizados, glifos de texto (−5, ↑, ×, A−) misturados com ícones, páginas com até 15 botões à vista e um × com três significados diferentes. O objetivo é que o RC Music pareça um só aplicativo, agradável de usar todo dia:
- uma linguagem visual única, sem bordas, separando as coisas por preenchimento;
- ícones bons e consistentes;
- uma ação principal à vista por lugar e o resto a um clique (⋯ ou botão direito);
- um player mais agradável;
- tema claro e escuro com as cores atuais;
- acessibilidade igual ou melhor que a de hoje.

### Objetivos

- Todas as superfícies do modo Aurora usam as mesmas peças (seção “Peças”). Nenhuma tela reimplementa botão, campo, diálogo ou menu.
- Cada tela fica como no mockup aprovado e com todas as ações de hoje alcançáveis.
- Os textos ficam revisados com um glossário único.
- O resultado funciona nos dois temas, em alto contraste, com movimento reduzido e na janela mínima de 720×520, por mouse e por teclado, e com leitor de tela.

### Fora do objetivo

- Funções novas além das listadas em “Comportamento novo”.
- Paleta nova. As cores de hoje ficam, por pedido do mantenedor.
- Mudar o protocolo, a sincronia ou os papéis da sala.
- Tematizar a janela nativa do Navegar (`BrowserMediaPanel`, QWidget com WebView2). Ela continua como está, e só o item da lateral muda.
- Tematizar o menu da bandeja além da organização. Ele continua nativo do Windows.

### Anti-objetivos (falha mesmo que “funcione”)

- Uma ação que existe hoje ficar inalcançável por mouse, teclado ou leitor de tela.
- O foco visível piorar, a ordem de Tab virar caos, ou um controle cair abaixo de 40 px de alvo.
- Contraste abaixo de WCAG AA em qualquer tema: 4,5:1 para texto e 3:1 para elementos não textuais que carregam informação.
- O fixture M0 congelado mudar de aparência, ou asserts antigos serem relaxados para o novo visual passar.
- Textos prometerem algo que o app não faz, ou literais de UI aparecerem fora dos catálogos `*Text.qml`.
- Duas implementações da mesma peça convivendo no modo Aurora no fim. O caminho do fixture M0 congelado, que só existe nos builds de teste, continua usando os componentes antigos, porque o contrato 4 do Aurora manda preservar a aparência histórica.

## Restrições

- Stack aceita no ADR-0005: C++20, Qt 6.11 Quick/QML com estilo Basic forçado (`main.cpp`), Qt Multimedia/FFmpeg e TagLib. Nenhuma dependência de código nova.
- **Ícones Phosphor** (`@phosphor-icons/core`, licença MIT) entram como assets SVG, só os que forem usados. A licença vai para `docs/delivery/licenses/`, junto das outras.
- Tokens vêm do `ShellUiContract` (paleta, escala tipográfica, alvos 40/44, breakpoints 900/1200, movimento 160/120 ms ou 0 com movimento reduzido). O alto contraste continua mapeado para as cores do sistema e tem precedência sobre o tema.
- Literais de UI só nos `*Text.qml`. O contrato de UI recusa literais fora deles, inclusive em comentários. Strings do C++ continuam nos lugares de hoje (`LifecycleText.hpp`, `ShellUiContract.cpp` e os bindings).
- Testes Quick rodam com `QT_FATAL_WARNINGS`. Os comandos estão em [development-commands.md](../../development-commands.md).
- Fixture M0 congelado preservado (contrato 4 do Aurora).
- Validação proporcional e TDD conforme [agents.md](../../../agents.md).
- Testes que tocam som exigem aviso e permissão do mantenedor. O app de teste roda sempre do mesmo caminho de executável. O RC Music instalado do mantenedor não é tocado.

## Abordagens

- **A. Peças primeiro, depois as telas, sobre o Aurora atual — escolhida pelo mantenedor (“A, peças primeiro”).** A primeira fatia cria as peças comuns, cada uma com seus testes. Depois vem uma fatia por tela, cada uma trocando o visual, aplicando os textos revisados da área e ajustando os testes no mesmo commit. As mudanças de comportamento são fatias próprias, com teste antes. No meio do caminho o app mistura telas novas e antigas, o que é aceitável porque não há instalador intermediário.
- **B. Tela por tela, sem peças.** Entrega as primeiras telas mais cedo, mas repete o problema de hoje: botões e campos refeitos em cada tela, que voltam a divergir. Não escolhida.
- **C. Casca nova em paralelo, trocada no fim.** Descartada. Duas interfaces convivendo, testes reescritos de uma vez e conflito com a regra de promover a implementação existente sem duplicar lógica.

## Decisões do mantenedor

Na ordem em que foram tomadas:
1. Continuar o Aurora.
2. **Botões no estilo A** (preenchimento tonal, sem borda) e **ícones Phosphor**: “A + Phosphor”.
3. **Player C**: “C, progresso no topo”.
4. **Início B**: abaixo do destaque, as estantes “Na sua fila” e “Da sua biblioteca”.
5. **Biblioteca toca na hora**: “Sim, tocar agora”, e ainda: “Temos que ter a opção de apenas adicionar a fila também”.
6. **Linha do verso no lugar da faixa de letras**: “B, linha do verso”.
7. **Playlist ganha “Tocar agora”**, com a regra geral: “Comportamento de clique duplo na obra deve coloca-la na fila e tocar ela e ter uma opção (botão direito do mouse ou botão "..." deve abrir as opções disponíveis da trilha) e haver a opção de adicionar a fila nessas opções”.
8. **Menus da faixa por lugar**: “Estão certos”.
9. **Confirmação antes de Expulsar, Expulsar e bloquear e Fechar sala**: “Sim, nos três”.
10. Configurações, Perfil, primeiro uso e Detalhes da faixa: “Estão bons”.
11. Diálogos de letra e avisos: “Estão bons”.
12. Janela estreita, alto contraste e bandeja: “Estão bons”.
13. **Revisão de textos**: “Se puder melhorar os diálogos e escritas do app, faça”.
14. **Marcas de estado**: entre manter só o preenchimento dos mockups e acrescentar uma marca pequena em Destaque nos estados ligado, escolhido e nos campos, escolheu **“Com marca”** (tela `marcas-de-estado.html`).
15. **Tocar agora uma playlist com Aleatório ligado**: escolheu **“1ª, depois sorteio”**. Toca a primeira da playlist, e as próximas saem do sorteio de sempre, entre tudo o que vem depois na fila.

Estas são decisões minhas, sem objeção do mantenedor, e valem até ele dizer o contrário:
- a Fila sai da lateral;
- o topo não tem busca;
- o perfil fica no rodapé da lateral;
- o destaque mostra “Detalhes da faixa” e “Letras”;
- “Abrir áudio” é a ação principal do cabeçalho da Início;
- o caminho do fixture M0 mantém os componentes antigos (contrato 4 do Aurora);
- a Busca fica sem miniatura, porque o app não recebe miniaturas do YouTube e buscá-las seria rede nova.

## Linguagem visual

**Cores.** A paleta e o mapeamento de alto contraste do Aurora ficam como estão. O token `success` (`#167344` / `#6DDA9B`), que já existe e hoje não tem uso, passa a marcar confirmações positivas (“Pedido”, “Caminho copiado.”, “Conectado”).

**Sem bordas.** Controles, cartões e itens de lista não têm contorno. A separação vem do preenchimento:
- canvas para o fundo;
- superfície para cartões, player e linha do verso;
- superfície elevada para menus, diálogos e cartões flutuantes;
- Seleção para os controles tonais e o item escolhido;
- Hover ao passar o mouse.

Divisórias de 1 px só aparecem entre linhas de configuração, dentro de menus e acima do rodapé de diálogos. **Em alto contraste**, os controles tonais e os cartões ganham contorno de 2 px na cor do texto do sistema, porque o preenchimento deixa de distinguir. O item escolhido e o botão principal usam a cor de seleção do sistema, e não há sombras.

**Marcas de estado** (decisão 14). Como o preenchimento tonal tem só cerca de 1,2:1 contra o fundo, cada estado que carrega informação ganha uma marca pequena em Destaque, que tem 4,9:1 ou mais contra todos os fundos nos dois temas:
- botão de ícone ligado: ponto de 4 px sob o ícone;
- item escolhido da lateral e das abas de Configurações: barra de 3 px à esquerda;
- opção escolhida da chave segmentada: traço de 3 px sob o texto;
- campo: linha de 1 px embaixo, que vira 2 px em Destaque no foco.

A linha do campo precisa de 3:1 contra o fundo em que o campo está. O contorno sobre o canvas no tema claro dá 2,83:1, então ali vale outro token, como o texto secundário. Em alto contraste, as marcas usam a cor de seleção do sistema.

**Raios.**
- Controles: cerca de 10 px.
- Cartões, gavetas e diálogos: cerca de 16 px.
- Menus: cerca de 12 px.
- Selos e o play: totalmente redondos.

Os valores exatos e se viram tokens novos ficam com o construtor (ver “Deixado ao construtor”).

**Sombras.** Só menus, diálogos, gavetas e o cartão flutuante de importação têm sombra suave. Nada de sombra em alto contraste.

**Ícones.** Phosphor regular nos controles. Play, pausa, anterior e próxima usam a variante fill. O tingimento segue o `ShellIcon.qml` atual (cor do texto do controle). No modo Aurora, os 29 ícones desenhados à mão em `assets/icons/` são substituídos (ficam só os que o fixture M0 ainda usar); os glifos de texto (−5, +5, ↑, ↓, ×, ✓, A−, A+, ✕, +) somem do modo Aurora. Mapeamento usado nos mockups (`gerador/kit.mjs`):

| Uso | Ícone |
|---|---|
| Início | house |
| Biblioteca | books |
| Buscar | magnifying-glass |
| Playlists | playlist |
| Ouvir junto | users-three |
| Atividade | download-simple |
| Navegar | globe-simple, mais arrow-square-out |
| Configurações | sliders-horizontal |
| Letra | microphone-stage |
| Fila | queue |
| Volume | speaker-high / speaker-slash |
| Mais ações | dots-three |
| Fechar | x |
| Excluir ou limpar | trash |
| Faixa tocando | waveform |

**Tipografia.** A escala do contrato continua. Títulos de página, destaque e diálogos usam Georgia/serifa; controles usam Segoe UI.

**Foco.** Anel de 3 px em Destaque, com afastamento, em todo controle focável, inclusive linhas de lista e células da grade de avatares.

**Movimento.** Os mesmos 160/120 ms, ou 0 com movimento reduzido. As gavetas deslizam, e diálogos e menus aparecem com fade.

## Peças

Estes são os papéis e o comportamento de cada peça. Nomes de arquivo e API ficam com o construtor. Toda peça tem nome e papel acessíveis, foco visível, estado desabilitado visível (hoje o `AccessibleActionButton` não tem) e versão de alto contraste.

1. **Botão** com cinco tipos:

   | Tipo | Aparência |
   |---|---|
   | principal | Destaque, com texto sobre destaque |
   | tonal | Seleção, com texto |
   | discreto | transparente; Hover ao passar o mouse |
   | perigo | tonal com texto em Erro |
   | perigo preenchido | fundo Erro, texto claro no tema claro e escuro no tema escuro; só no botão que confirma uma ação destrutiva |

   Ícone opcional à esquerda. Alvo de no mínimo 40 px. A versão “pequena” dos mockups pode ser visualmente menor, desde que a área clicável fique em 40 px. Substitui `AccessibleActionButton`, o `primary` de `AccessibleTransportButton` e os botões soltos com estilo próprio.
2. **Botão de ícone**: quadrado de 40 px, discreto ou tonal, com estado “ligado” (Seleção, ícone em Destaque e o ponto da marca de estado). O nome acessível é sempre o texto da ação, por exemplo “Mais ações”, “Fechar fila” ou “Adicionar à fila”.
3. **Campo de texto** e **campo de busca** (com lupa): preenchidos, sem contorno, com a linha de baixo da marca de estado e erro abaixo do campo em Erro e marca de erro no próprio campo. Substitui os quatro campos locais duplicados e os TextField Basic (filtro da Biblioteca, Buscar, link da sala, nomes de cargo, nome de playlist).
4. **Caixa de seleção** (ComboBox) com o mesmo desenho do campo e popup no estilo do menu.
5. **Chave segmentada**: duas a cinco opções, um único ponto de Tab e setas para mover. A opção escolhida tem o traço da marca de estado. Serve para Tema, Movimento, Ao fechar, Ao terminar a fila, Pesquisar/Colar links, filtros da Atividade, Lista/Capas, duração do convite e Como o cargo/Liberar/Negar.
6. **Interruptor** para sim/não: Restaurar última sessão, Iniciar minimizado, Visível para a sala e permissões de um cargo. A trilha desligada precisa de 3:1 contra o fundo, então usa o token de contorno, não a divisória.
7. **− valor +** para números: segundos de avanço e retrocesso, sincronia e quantidade de pessoas na sala. Mantém a edição por teclado de hoje.
8. **Caixinha de marcar** só dentro de diálogos (por exemplo “Avisar ao fechar para a bandeja nesta sessão”): arredondada e em Destaque quando marcada.
9. **Selo** (chip): Tocando, Com letra, Sincronizada, Instrumental, cargo, “cargo inicial”, “duração diferente” (em Aviso) e destinos do download.
10. **Avatar redondo** com recorte circular (hoje é quadrado), nos tamanhos 32, 36, 56, 64 e 72.
11. **Menu** no estilo elevado, com ícone, texto, atalho à direita e submenu (›). Itens destrutivos ficam em Erro, depois de um separador. Substitui os Menus Basic (menu de contexto da fila, menu de playlists da Biblioteca).
12. **Diálogo**: fundo da janela escurecido (o véu cobre a janela inteira, inclusive lateral e player), superfície elevada, título com serifa, × no canto para diálogos que não são de confirmação e Esc para fechar.
    - **Confirmação:** título em pergunta, texto com a consequência, “Cancelar” discreto e o botão que confirma repetindo o verbo; perigo preenchido quando destrói algo.
    - Substitui os Dialogs Basic (playlists, `browserError`), `DownloadConfirmation`, `QueueClearConfirmation`, a confirmação de substituir preferências e o `WindowNotice`.
13. **Gaveta**: 400 px, encostada à direita, do topo até o player, com o mesmo cabeçalho nas duas (rótulo, título com serifa, subtítulo e ×). A Fila escurece o fundo e fecha ao clicar fora. A Letra não escurece o fundo e continua não modal.
14. **Aviso**:
    - faixa no topo da página, com ícone, título, texto e até duas ações, em Seleção e sem borda amarela;
    - cartão flutuante, ancorado acima da linha do verso no canto direito, que não empurra a página;
    - linha de confirmação curta em `success`, com ícone.
15. **Estado vazio**: ícone num círculo, frase com serifa, uma linha de ajuda e até duas ações.
16. **Linha de faixa** e **cartão de faixa**, compartilhados por Biblioteca, playlist, Fila e estantes da Início, com o menu da faixa (seção “Comportamento novo”).

## Casca

**Lateral**, nas larguras atuais de 68, 196 e 224 px:
- Marca no topo.
- Itens: Início; Biblioteca, com contagem de faixas; Buscar; Playlists, com contagem; Ouvir junto; Atividade, com contagem do que precisa de atenção; Navegar, com o ícone de janela externa, porque abre a janela nativa e não uma página.
- Embaixo, Configurações e o chip do perfil (avatar, apelido e “Seu perfil”), que abre Configurações › Perfil.
- **A Fila sai da lateral.** Ela abre pelo botão Fila do player e por Ctrl+L, como hoje.
- Item escolhido: Seleção, texto em Destaque, semibold e a barra da marca de estado.
- Lateral compacta (68 px): só ícones, com nome no tooltip e no leitor de tela.
- As contagens só aparecem se o binding já expuser o número. Não se cria estado novo para isso.

**Topo**: o caminho “Seu espaço / página” à esquerda e o botão de tema tonal (sol ou lua) à direita. Sem busca.

**Páginas**: o cabeçalho editorial de hoje (rótulo, título com serifa, descrição e ações à direita), **sem botão de fechar**, inclusive em Atividade, Configurações e Playlists. Esc continua com o comportamento atual de voltar ou fechar onde ele existir. Rolagem única por página: nada de lista com rolagem dentro de página com rolagem (hoje a Biblioteca limita a lista a 640 px).

**Player C**, sempre visível:
- O progresso ocupa a borda de cima do player inteiro (4 px em Destaque sobre a divisória, com marcador redondo), sem linha separadora.
- À esquerda: capa de 64 px (48 px na janela compacta), título com serifa, “artista · álbum” e “1:19 / 3:37”.
- No centro: aleatório (36), anterior, play/pausa redondo de 52 px em Destaque, próxima e repetir (36).
- À direita: Letras, Fila com selo de contagem, volume com barra e porcentagem.
- Os botões −5 s/+5 s saem sempre. A função continua em ←/→ e na barra de progresso.
- Na janela compacta, a barra de volume some e o botão de volume continua.

**Linha do verso** (substitui a faixa de letras de 96 px com quatro botões):
- 48 px acima do player, em todas as páginas.
- Mostra o verso atual com serifa, o próximo em texto secundário e “Abrir letra”, que abre a gaveta da Letra.
- Aparece só com letra sincronizada. Sem letra, ou com letra simples ou protegida, ela some.
- As quatro ações da faixa antiga vão para a gaveta da Letra: Gerar romaji, Versões, Abrir letra e Buscar letra.
- Na janela compacta: 40 px, só o verso atual e o botão só com ícone.

## Telas

Cada item descreve o mockup aprovado. O que não está escrito segue o mockup.

**Início**:
- Cabeçalho com “Abrir áudio” (principal).
- Destaque em cartão sem borda: capa de 168 px, “Agora tocando”, título com serifa de 36 px, artista · álbum, “Detalhes da faixa” e “Letras” (tonais).
- Estante “Na sua fila”: capas reais, nome inteiro e selo “Tocando” na faixa atual, com “Ver fila”, que abre a gaveta.
- Estante “Da sua biblioteca”, com “Ver toda a biblioteca”.
- Estados vazio e de erro com a peça de estado vazio.

**Biblioteca**:
- Barra com campo de filtro, contagem “N faixas” e chave Lista/Capas.
- Avisos de ação (por exemplo “… foi para o fim da fila.”) na linha de confirmação, com “Ver fila”.
- **Lista**:
  - colunas #, Faixa (capa de 40, título e artista), Álbum e Tempo;
  - linhas de 56 px sem divisória;
  - a faixa tocando mostra a onda no lugar do número;
  - a linha sob o mouse ou com foco mostra ▶ (Tocar agora) no lugar do número e, à direita, + (Adicionar à fila) e ⋯;
  - “Localizar” só aparece em faixa indisponível, que fica esmaecida com “Indisponível” em Aviso.
- **Capas**: o + de contorno sobre a capa sai. Ao passar o mouse ou com foco aparecem ▶, + e ⋯. A faixa tocando mostra o selo “Tocando”, e a indisponível, “Indisponível”.
- Janela compacta: a coluna Álbum some.

**Buscar**:
- Chave “Pesquisar / Colar links” (o mesmo estado `linksMode` de hoje).
- Campo de busca grande com “Buscar” (principal); durante a busca, o botão vira “Cancelar busca”.
- Resultado: número, título, “canal · duração”, ícone “Abrir no YouTube” e “Baixar” (tonal); quando já pedido, mostra “Pedido” em `success`.
- Selo “Com letra” quando `hasLyricsPage` (já existe no binding e não aparecia).
- Sem miniatura. O mockup mostra uma só como ilustração, mas `OnlineSearchResult` não traz imagem e buscá-la seria rede nova, fora do escopo.
- Colar links: área de links preenchida, “Consultar links”, status, “Baixar todos” (principal) e problemas por linha em Aviso, com ícone.

**Playlists**:
- Lista de playlists à esquerda (cerca de 250 px): mosaico 2×2, nome, “N faixas” e “na sala” quando visível para a sala.
- A playlist aberta fica num cartão à direita:
  - mosaico, rótulo, nome com serifa, contagem e interruptor “Visível para a sala”;
  - botões “Tocar agora” (principal), “Adicionar à fila”, “Adicionar arquivos” e ⋯ (Adicionar a fila atual, Renomear, separador, Excluir playlist).
- Cabeçalho: “Salvar fila como playlist” (tonal) e “Nova playlist” (principal).
- Faixa sob o mouse ou com foco: ↑, ↓ e ⋯. Faixa com arquivo não encontrado: texto em Aviso e “Localizar”.
- A linha de recuperação (Tentar salvar, Recarregar do disco, Descartar edição…, Tentar abrir novamente) vira um aviso em faixa no cartão. O botão principal é a ação de recuperar, e as outras ficam no ⋯ do aviso.
- Diálogos de nome, excluir e descartar usam as peças de diálogo e campo.

**Gaveta da Fila**:
- Cabeçalho “Reprodução / Fila / N faixas · duração” e ×.
- Barra de ícones: Adicionar arquivos, Mover para cima, Mover para baixo, Remover e, à direita, “Limpar fila” como discreto com lixeira em Erro (o × some), que continua pedindo confirmação.
- **Aleatório e Repetir saem da gaveta**, porque já estão no player.
- Linhas de 56 px com capa de 40 e estado (Localizando…, Arquivo não disponível.) em Aviso.
- Estado vazio novo, com a ação de adicionar arquivos.
- O convidado de sala vê a lista da sala (“Adicionada por …”) como hoje.

**Gaveta da Letra**:
- Cabeçalho “Letra sincronizada / título / Versão: … · artista” e ×.
- Barra: “Versões”, “Buscar letra”, ⋯ (Gerar romaji) e tamanho do texto (−, Aa, +).
- O verso atual fica em Destaque, semibold e maior, sem caixa de borda; os versos passados em texto secundário.
- “Voltar ao verso atual” quando a pessoa rola para longe.
- Clicar num verso continua levando a ele.
- Atribuição e LyricFind como hoje.
- Estado vazio novo: “Buscar letra” (principal) e “Adicionar versão”.

**Atividade**:
- Cabeçalho com “Abrir pasta de downloads” (tonal).
- A pilha de botões de largura total vira **um aviso só**, quando há algo a fazer: Revisar letras, recuperar ou salvar o histórico, sair sem salvar. A ação principal vai no aviso e as demais no ⋯ dele.
- Filtros numa chave segmentada, com “Arquivar encerrados” (discreto) ao lado.
- Cada download vira um cartão:
  - ícone de estado num círculo (baixando, convertendo, erro, concluído, na fila);
  - título com serifa e linha de estado (erro em Erro);
  - selos de destino (MP3 pronto, Na biblioteca, Com letra) e barra de 4 px quando ativo;
  - **uma ação principal conforme o estado** (Cancelar, Tentar novamente, Adicionar à fila…) e ⋯ com o resto (Informações do download, Verificar arquivos, Validar MP3, Encerrar recuperação…, Liberar validação…);
  - foco de 3 px.
- “Informações do download” usa o diálogo padrão, com “Informações copiadas.” visível.

**Ouvir junto**: os mesmos estados e funções de hoje, em cartões.
- **Sem sala**: cartão de início (ícone, “Criar sala” principal, “Entrar com link” tonal). O formulário de abrir sala usa − N + para a quantidade e uma chave para a duração; o de entrar usa a peça de campo. O aviso de privacidade antes da primeira sala vira diálogo.
- **Anfitrião**:
  - À esquerda: o pedido para entrar em destaque, com Aceitar e Recusar; abaixo, o cartão “Na sala” com avatar redondo, nome, selo do cargo, “você”, sincronia e ⋯ por pessoa.
  - À direita: “Convite”, com o link encurtado num campo de leitura, botão copiar, limite e validade, e “Revogar convite”; “Cargos” resumido, com “Editar”; e “Pessoas conhecidas”, com “Desbloquear”.
  - No cabeçalho: “Copiar convite” e ⋯ (Revogar convite, Editar cargos, Fechar sala).
- **Convidado**:
  - No cabeçalho: “Sair da sala” no tipo perigo, com o ícone de saída no lugar do ×.
  - À esquerda: “Na sala” e “Playlists do anfitrião”, que abrem e fecham, com “Adicionar todas” e + por faixa, ou o aviso de permissão.
  - À direita: “Sua conexão” (estado e sincronia − +) e “Sua música” (“Adicionar música minha”).
- **Menu de uma pessoa** (diálogo):
  - avatar, nome e sincronia;
  - “Cargo” numa caixa de seleção;
  - “Permissões de …” com as 7 permissões, cada uma numa chave Como o cargo / Liberar / Negar, que substituem as 8 ComboBoxes;
  - “Expulsar” e “Expulsar e bloquear” no tipo perigo, separados.
- **Cargos** sai do meio da página e vira diálogo próprio:
  - “Cargo inicial do convite”;
  - lista de cargos à esquerda, com contagem e “Novo cargo”;
  - à direita, o cargo escolhido: campo de nome, 7 interruptores e “Excluir cargo”.
  - Erros de salvar ou de nome repetido continuam como hoje, no lugar certo.
- Avisos de erro da sala usam a faixa de aviso.

**Configurações**:
- As abas viram uma coluna à esquerda (Geral, Reprodução, Aparência, Downloads, Perfil), com ícone, e o título repetido da seção sai.
- Cada opção é uma linha: nome e explicação de hoje à esquerda, controle à direita.
- Uma seção por vez, como hoje, para não alongar o ciclo de Tab.
- Controles:
  - Ao fechar: chave.
  - Restaurar última sessão e Iniciar minimizado: interruptores, com o motivo quando desabilitado.
  - Abrir Aplicativos Padrão: tonal.
  - Segundos para retroceder e avançar, e sincronia: − +.
  - Ao terminar a fila: chave.
  - Saída de áudio: caixa de seleção.
  - Tema e Movimento: chaves.
  - Pasta de downloads: caminho e “Alterar pasta…”.
- Sem botão de fechar.

**Perfil e primeiro uso**:
- Avatar grande, apelido com “Salvar apelido” e “Apelido salvo.” visível em `success`.
- Grade de avatares redondos: o escolhido tem anel em Destaque e o foco tem anel próprio.
- “Usar imagem própria…”.
- O diálogo de primeiro uso continua obrigatório: título com serifa, mensagem de hoje, campo com erro no campo, grade e “Continuar” visivelmente desabilitado enquanto inválido.

**Detalhes da faixa**:
- Diálogo com capa de 96 px, rótulo, título com serifa, artista · álbum e ×.
- Seções Faixa e Áudio lado a lado; Arquivo e Diagnóstico abaixo, em cartões.
- Caminho completo num campo de leitura com “Copiar caminho”.
- “Caminho copiado.” e “Diagnóstico copiado.” visíveis (hoje só o leitor de tela anuncia).
- Mostrar e ocultar diagnóstico continua.

**Diálogos de letra**:
- **Versões**:
  - lista de escolha única, em que a marcada é a versão em uso, com selos de forma e tipo;
  - “Buscar no LRCLIB” e “Adicionar versão” no topo, e ⋯ com Salvar letra atual e Gerar romaji;
  - Editar e Excluir saem do rodapé para o ⋯ de cada versão (e o botão direito);
  - a recuperação (Tentar salvar, Recarregar do disco, Abandonar alteração) vira aviso em faixa;
  - o formulário de versão usa as peças de campo, caixa de seleção e caixinha de marcar.
- **Buscar no LRCLIB**:
  - campos em grade e a nota de privacidade com cadeado;
  - gravações em lista de escolha única com selos (Sincronizada, Instrumental, “duração diferente”), no lugar da ComboBox;
  - formato numa chave e prévia em superfície;
  - um botão principal por vez: Buscar, depois Usar nesta sessão ou Marcar como instrumental;
  - “Salvar versão” tonal e “Consulta por metadados exatos” discreto.
- **Letra do áudio baixado** (aberta pela Atividade): o mesmo desenho, com “Colar letra” e “Abrir no Letras.mus.br” onde existem hoje.

**Avisos**:
- Preferências e sessão (recuperadas, padrão, versão mais recente, falha ao salvar): faixa no topo, com as mesmas ações. A confirmação de substituir usa perigo preenchido.
- Importação de arquivos: cartão flutuante com barra, contagens e “Cancelar”, e depois “Fechar” e “Escolher outros arquivos” (os glifos × e ✓ saem).
- Bandeja indisponível e continuar na bandeja: diálogo padrão.
- Arrastar e soltar: a sobreposição de hoje, com raio de 16 px.

**Bandeja** (menu nativo): o título da faixa atual não é clicável; depois vêm Reproduzir/Pausar, Faixa anterior e Próxima faixa; separador; Mostrar/Ocultar; separador; Sair.

**Janela mínima (720×520)**:
- lateral de 68 px;
- Biblioteca sem Álbum;
- linha do verso compacta e player compacto;
- destaque da Início encolhido, com os botões “Detalhes” e “Letras”;
- toda ação essencial alcançável.

## Comportamento novo

São as únicas mudanças de comportamento aprovadas. Cada uma é uma fatia própria em TDD, com teste no nível certo: C++ para a regra e Quick para o gesto.

1. **Tocar agora a partir da Biblioteca.** Clique duplo, Enter, o ▶ da linha ou do cartão e o item do menu põem a faixa logo depois da faixa atual e começam a tocá-la. O resto da fila continua depois dela.
   - Sem faixa atual, ou com a fila vazia, ela vira a atual e toca.
   - Faixa indisponível: ação desabilitada, com o motivo.
   - Durante a sala, segue as mesmas permissões e restrições que “Adicionar à fila” já segue hoje.
   - Recomendação: um caso de uso único no presenter, que enfileira, posiciona e toca de forma atômica e testável, em vez de compor três chamadas no QML.
2. **“Adicionar à fila” continua como ação própria** em todo lugar (fim da fila, sem interromper o que toca).
3. **Tocar agora uma playlist**: insere a playlist inteira logo depois da faixa atual e toca a primeira. Com Aleatório ligado, as próximas saem do sorteio de sempre, entre tudo o que vem depois na fila (decisão 15); o sorteio não muda. “Adicionar à fila” da playlist continua indo para o fim.
4. **Faixa de playlist individual**: “Tocar agora” (clique duplo e Enter também) e “Adicionar à fila” para uma faixa da playlist. Hoje só a playlist inteira vai para a fila.
5. **Menu da faixa em todo lugar**: botão direito, ⋯, tecla Menu e Shift+F10 abrem o menu. Os itens condicionais só aparecem quando valem.

   | Lugar | Clique duplo / Enter | Itens do menu |
   |---|---|---|
   | Biblioteca (lista, capas, estante da Início) | Tocar agora | Tocar agora · Adicionar à fila · Adicionar à playlist › · Localizar arquivo… (se indisponível) · ─ · Remover da biblioteca |
   | Faixa de playlist | Tocar agora | Tocar agora · Adicionar à fila · ─ · Mover para cima · Mover para baixo · Localizar arquivo… · ─ · Remover da playlist |
   | Fila (gaveta, estante da Início) | toca a partir dela (já está na fila) | Tocar agora · Gerenciar letras · Tentar reproduzir novamente (se falhou) · Localizar arquivo… (se indisponível) · ─ · Mover para cima · Mover para baixo · ─ · Remover da fila |
   | Resultado do YouTube | nada (evita baixar sem querer) | Baixar · Abrir no YouTube |

   Os atalhos de hoje continuam: Alt+↑/↓ e Delete na fila. “Adicionar à playlist” a partir da Fila ficou de fora, porque seria uma função nova.
6. **Confirmações da sala**: Expulsar, Expulsar e bloquear e Fechar sala pedem confirmação, com o diálogo de confirmação e o botão de perigo preenchido.
7. **Retornos visíveis**: “Caminho copiado.”, “Diagnóstico copiado.”, “Apelido salvo.” e “Informações copiadas.” aparecem na tela, além de serem anunciados.
8. **Ações movidas, não removidas**:
   - ±5 s saem do player (continuam em ←/→ e na barra);
   - Aleatório e Repetir saem da gaveta da Fila (continuam no player e em F1/F2, que hoje chamam a gaveta e passam a chamar o mesmo estado sem depender dela);
   - a Fila sai da lateral (continua no player e em Ctrl+L);
   - as ações da faixa de letras vão para a gaveta da Letra;
   - Editar e Excluir versão vão para o menu de cada versão;
   - os botões de cada download vão para o ⋯ do cartão.

   O teste de cada fatia prova que a ação continua alcançável por mouse, teclado e leitor de tela.

## Textos

A revisão cobre os catálogos `*Text.qml` (cerca de 680 frases) e os textos do C++ que chegam à tela.

**Princípios:**
- tratar por “você”, com tom calmo e direto, sem jargão (LRCLIB, LRC, MP3 e YouTube só onde a função é sobre eles);
- botões e itens de menu com verbo e objeto, sem ponto final; títulos sem ponto final;
- erros que dizem o que aconteceu e o que fazer, sem culpar a pessoa e sem detalhe interno;
- confirmações destrutivas com o título em pergunta nomeando a coisa, o corpo dizendo o que se perde e o que se preserva, e o botão repetindo o verbo;
- um termo por conceito, com o glossário;
- placeholders e plurais preservados;
- nenhum texto prometendo o que o app não faz.

O glossário, as inconsistências e as trocas propostas, catálogo por catálogo, entram num anexo deste documento (`2026-11-22-ui-redesign-copy.md`, nesta pasta) quando a revisão terminar. **As trocas só entram depois de o mantenedor aprovar o tom pelas de maior impacto.** Cada fatia de tela aplica as trocas da sua área e ajusta os testes que fixam aquelas frases. A troca de texto nunca relaxa o que o teste verifica.

## Acessibilidade

- **Contraste**: todos os pares de texto/fundo e de elemento não textual/fundo usados pelas peças são verificados nos dois temas (4,5:1 para texto; 3:1 para ícones, marcas de estado, trilha do interruptor desligado, anel de foco e marcador do progresso). O preenchimento tonal é decorativo: o que identifica o controle é o texto ou o ícone, e o que identifica o estado é a marca. Os pares que falharem trocam de token, nunca o contrário. Dois já falham com os tokens de hoje: texto secundário sobre Seleção no tema claro (4,37:1) e contorno sobre o canvas no tema claro (2,83:1). Em alto contraste valem as cores do sistema.
- **Teclado**:
  - Tab percorre lateral → topo → página → linha do verso → player, com as gavetas e diálogos prendendo o foco enquanto abertos (a Letra não é modal e não prende).
  - Enter toca; tecla Menu e Shift+F10 abrem o menu da faixa.
  - Setas movem dentro da chave segmentada, da grade de avatares e das listas.
  - Esc fecha gaveta, menu e diálogo e devolve o foco a quem abriu.
  - Os atalhos globais de hoje (Ctrl+O, Espaço, M, ←/→, ↑/↓, Shift/Ctrl+←/→, F1, F2, Ctrl+L, Ctrl+,, Ctrl+Q) não mudam.
- **Leitor de tela**: todo botão de ícone tem nome (“Mais ações”, “Fechar fila”, “Adicionar à fila”…). Estados (ligado, selecionado, desabilitado com motivo, tocando) e retornos (“foi para o fim da fila”, “Caminho copiado.”) são anunciados.
- **Movimento reduzido**: sem animações. A barra indeterminada da Atividade continua desligada.
- **Janela mínima, escala e DPI**: nada essencial sai da tela, e textos longos são elididos com o nome completo acessível.

## Testes e validação

- **Peças**: testes Quick por peça, escritos antes (Red), cobrindo tipos, estados, foco, teclado, nome acessível e alto contraste.
- **Telas**: os testes Quick existentes acompanham cada fatia. Quando um id, a ordem de Tab ou um texto mudam, o teste muda no mesmo commit, para o novo comportamento aprovado, sem enfraquecer o que verifica. Testes de composição ganham casos para os estados novos (vazios, aviso único da Atividade, linha do verso).
- **Comportamento novo**: testes C++ (Catch2/Qt Test) para Tocar agora (faixa, playlist e faixa de playlist, com e sem faixa atual, fila vazia, indisponível, sala, Aleatório ligado) e testes Quick para os gestos (clique duplo, Enter, Menu/Shift+F10, ⋯) e para as confirmações da sala.
- **Capturas**: o harness de capturas (`RC_MUSIC_UI_ARTIFACT_DIR`) gera, por fatia, as telas afetadas em claro, escuro e 720×520. O alto contraste é capturado nas fatias de peças e no fim. As capturas são evidência para o aceite visual do mantenedor, não substituto dele.
- **Contraste**: um teste que calcula o contraste dos pares de tokens usados pelas peças nos dois temas.
- Testes que tocam som exigem aviso e permissão antes. Não há instalador até o fim do redesenho. A suíte completa roda só nos marcos (fim das peças, metade das telas, fim).

## Ordem das fatias

1. Peças, ícones Phosphor e licença, com testes e capturas. A licença Phosphor vai para `docs/delivery/licenses/`.
2. Casca: lateral (sem Fila, com Navegar e perfil), topo e player C.
3. Linha do verso e gavetas da Fila e da Letra.
4. Início.
5. Biblioteca, mais a fatia de comportamento Tocar agora.
6. Menu da faixa em todos os lugares.
7. Buscar.
8. Playlists, mais a fatia de comportamento Tocar agora (playlist e faixa).
9. Atividade.
10. Ouvir junto, mais a fatia de comportamento das confirmações.
11. Configurações, Perfil, primeiro uso e Detalhes da faixa.
12. Diálogos de letra (Versões, LRCLIB, Letra do áudio baixado).
13. Avisos e bandeja.
14. Passada final de casos de borda (720×520, alto contraste, escala), remoção do que ficou morto (componentes e ícones que nem o Aurora nem o fixture M0 usam), guia do usuário e status.

Cada fatia aplica os textos revisados da sua área. Ao fechar as fatias, o mantenedor faz o aceite visual. Só depois sai o instalador.

## Deixado ao construtor

- Nomes, arquivos e API das peças, e se substituem componentes existentes no lugar ou entram como novos, com remoção dos antigos ao fim da migração (sem duas implementações no fim).
- Valores exatos de raio, sombra, espaçamento e altura, e se viram tokens novos no `ShellUiContract` ou ficam nas peças. Os mockups usam raios de 10, 12 e 16 e o espaçamento base de 4.
- Como desenhar a sombra no Qt Quick (por exemplo `MultiEffect` ou camadas), desde que sem custo perceptível e desligada em alto contraste.
- Como expor as contagens da lateral, usando só o que os bindings já sabem.
- A organização dos testes e quais capturas viram referência.

## Riscos

- **Testes presos a ids, Tab e textos**: muitos dos 48 arquivos Quick vão mudar. Cada mudança precisa ser revisada como mudança de contrato, não como ajuste para passar.
- **`QT_FATAL_WARNINGS`**: peças novas não podem gerar avisos de binding, anchors ou propriedades inexistentes. Tratar como falha.
- **Ícones**: o Phosphor usa grade de 256 e preenchimento, enquanto os ícones atuais usam traço. É preciso confirmar o tingimento pelo `ShellIcon` e a nitidez em 16–24 px e em DPI alto na fatia de peças.
- **Sala**: o redesenho toca a UI recém-entregue do ADR-0022. Não muda binding, protocolo nem sincronia, e os testes da sala continuam verdes a cada fatia.
- **M0**: as peças só valem no modo Aurora. O fixture congelado não muda. Ele só existe nos builds de teste (`RC_MUSIC_ENABLE_FIXTURE_SCENARIOS`), mas renderiza os mesmos arquivos QML pelos ramos `!inlinePages` e `usesFrozenFixture`, e cerca de 13 arquivos Quick conferem a aparência antiga. Cada fatia mantém esses ramos e esses testes intactos.

## Fora do escopo

Navegar nativo (`BrowserMediaPanel`), Linux, persistência nova, funções além de “Comportamento novo”, instalador intermediário, publicação e qualquer instalação no host.
