# Aurora — contrato visual e migração nativa incremental

## Estado e origem do aceite

**Visual aprovado pelo mantenedor em 2026-11-15.** Após escolher Aurora e avaliar o refinamento claro/escuro, o usuário respondeu: **“Ficou bom o visual com essas cores, pode prosseguir com ele”**.

Referência apresentada: `out/product/uiux-planning-4efd8ba/visual-v2/Aurora-Claro-Escuro.html`, SHA-256 `cb4f6dce459a9539955b5ae2273aaf45094e511205f154b888a9be2c328aaa6e`. O HTML é uma referência visual descartável, não uma nova stack ou backend. Os 37 checks daquele artefato são evidência de protótipo, não de Qt/QML.

Este aceite permite prosseguir incrementalmente com a UI Aurora no produto existente. Não aprova APIs de catálogo/pesquisa ainda não desenhadas, um provedor remoto, migração pessoal, instalação, publicação ou retirada de recursos além do Lidarr já solicitado.

### Retomada após a primeira fundação nativa

O mantenedor avaliou o checkpoint: **“Ainda está longe do projeto apresentado no protótipo (C:\Users\Joao\Documents\ChatGPT\music-player\out\product\brave-letras\out\product\uiux-planning-4efd8ba\visual-v1\content), mas já deixou o visual melhor”**. Isso não é aceite de fidelidade visual nem conclusão do redesenho. A composição musical do Aurora de `visual-v1/content/aurora-v3.html`, com o refinamento de cores já aprovado em `visual-v2`, continua sendo o alvo; trocar apenas cores não satisfaz essa direção.

Autorização e condição posteriores: **“Pode continuar com a refatoração. Gere o instalador apenas quando tiver terminado completamente o redesenho da aplicação”**. Prosseguir com o redesenho nativo, preservando paridade e dados; builds, testes e capturas isoladas são permitidos, mas não gerar instaladores intermediários. O setup anterior permanece histórico, sem requalificação. Provider remoto, persistência nova, instalação pessoal e publicação conservam seus gates específicos.

## Intenção

Transformar o player em um aplicativo musical completo, com navegação lateral e reprodução persistente, mantendo as capacidades e os dados existentes. Busca interna deve se tornar a jornada principal sem depender da extensão. A biblioteca futura será permanente e independente da fila, playlists e histórico.

### Abordagens consideradas e escolha

- **Aurora:** coleção por capas, hierarquia editorial e cores quentes; **base escolhida pelo usuário**, incluindo o dark refinado.
- **Pulso:** tabela/filtros/inspetor mais densos; organização apreciada, mas cores rejeitadas. A opção de lista no refinamento Aurora é complementar, não substituição da identidade nem autorização de copiar integralmente o B.
- **Reescrever a stack ou incorporar o HTML:** descartado pelo coordenador. Não resolve os contratos de reprodução/persistência e acrescentaria risco sem necessidade. Manter C++20, Qt Quick/QML, Qt Multimedia/FFmpeg e TagLib.

## Tokens da referência aceita

| Papel | Claro | Escuro |
|---|---|---|
| Canvas | `#F5F1E9` | `#1B1917` |
| Navegação | `#EEE8DD` | `#1E1C19` |
| Superfície | `#FFFCF6` | `#24211D` |
| Superfície elevada | `#EEE8DD` | `#2D2923` |
| Hover | `#EAE1D5` | `#353029` |
| Divisória decorativa | `#D7CAB9` | `#4C4236` |
| Contorno de controle | `#A58C73` | `#89715A` |
| Texto primário | `#302D28` | `#F2EADD` |
| Texto secundário | `#726557` | `#BFB09D` |
| Destaque | `#934B32` | `#D6A27E` |
| Texto sobre destaque | `#FFFAF4` | `#2D1C13` |
| Seleção | `#EEE0D2` | `#3A2C23` |
| Aviso | `#795A21` | `#E2C78B` |
| Erro | `#9B3C32` | `#F2ADA0` |

Mapear papéis para o contrato nativo, não colar CSS no QML. O contorno funcional não usa a divisória decorativa se isso violar o contraste de controles. Para papéis sem token próprio na referência, preservar semântica e contraste, registrando a escolha técnica sem atribuí-la ao usuário.

**Mapeamento nativo (fatia 1, 2026-11-16).** `ShellUiContract` expõe `selectionColor`, `hoverColor` e `dividerColor` com os valores da tabela. Em alto contraste, viram respectivamente Highlight, Window e WindowText do sistema. No construtor do fixture M0 congelado, Seleção e Hover reutilizam a superfície elevada e Divisória reutiliza a borda, de modo que os renders históricos não mudam. O contrato também fixa a escala tipográfica em pixels (`typeEyebrow` 10, `typeCaption` 12, `typeBody` 14, `typeSubtitle` 18, `typeTitle` 20, `typeSection` 24, `typeHeading` 30, `typeDisplay` 36) e os breakpoints `compactWidth` 900 e `wideWidth` 1200 do protótipo. Na navegação Aurora, o item atual usa fundo de Seleção, texto em Destaque e peso semibold, sem depender só da barra de 3 px. O hover usa o token Hover, e as divisórias da lateral, do cabeçalho e do player usam Divisória opaca.

**Ícones e marca (fatia 2, 2026-11-16).** Os ícones de linha do protótipo, mais aleatório, mudo, sol, lua e navegador desenhados na mesma grade, ficam em `assets/icons/` (SVG 24×24, traço 1,5, pontas arredondadas, cor preta) e são servidos em `:/rc_music/icons/`, fora do inventário congelado `:/m0`. `ShellIcon.qml` usa `IconImage` de `QtQuick.Controls.impl`, que já faz parte do runtime do estilo Basic, para tingi-los com a cor do texto do controle; o ícone é decorativo e o nome acessível continua no texto traduzido. No modo Aurora, a lateral, o botão de tema (sol ou lua conforme o tema oferecido) e o transporte (aleatório, anterior, reproduzir/pausar, próxima, repetir, mudo) usam esses ícones; o fixture congelado mantém os glifos. A marca são três barras de destaque (12/24/18 px) ao lado do nome; na lateral compacta de 68 px aparece só a marca.

Georgia/serif de sistema nos títulos editoriais e Segoe UI/fonte do sistema nos controles são a referência, sem baixar ou embutir fontes. Layout deve funcionar com fallback. Preservar alvos mínimos 40 px, preferidos 44 px, e janela mínima 720×520; escala/DPI e texto longo não podem tornar ações essenciais inalcançáveis.

## Contratos da migração

1. **Uma reprodução autoritativa.** Presenter, backend, sessão, metadados e owners permanecem acima das superfícies. Navegação, tema e troca de visualização não emitem load/play/pause/seek nem recriam o engine.
2. **Tema nativo.** Preservar Sistema/Claro/Escuro, preferência existente, salvamento reconhecido e precedência do alto contraste. O dark inicial do HTML era apresentação da proposta, não autorização de sobrescrever preferências ou forçar Dark em toda instalação. Troca rápida no cabeçalho deve usar o comando de preferências existente.
3. **Acessibilidade e atalhos.** Manter foco visível, nomes/roles, ordem de Tab, Enter, leitor de tela e política de movimento reduzido. No aplicativo, Espaço continua com a regra já aprovada de reprodução; não copiar do HTML uma regra incompatível para botões.
4. **M0 preservado.** Não modificar fixtures autenticadas, hashes, corpus, protocolo ou cores históricas para fazer o novo visual passar. Separar expectativas da aparência de produto das expectativas congeladas; manter os mesmos contratos funcionais, traduções, HC e notificações.
5. **Remoção Lidarr primeiro.** Retirar UI, binding, adapter, bootstrap/lifetime, biblioteca/recurso e testes exclusivos antes da casca definitiva. Não apenas ocultar o botão. Não tocar no servidor externo, revogar chaves, cancelar pedidos remotos ou apagar áudios. Preservar Qt Network e dependências compartilhadas de letras/downloads.
6. **Navegação honesta.** Migrar superfícies em recortes utilizáveis, com os bindings existentes. Não chamar uma fila de biblioteca permanente, nem apresentar resultados online fictícios como recurso pronto. Atalhos transitórios para painéis existentes não equivalem a páginas já migradas; registrar explicitamente a fronteira entregue.
7. **Player persistente.** Transporte, faixa/posição/volume e letras continuam disponíveis e não são filhos descartáveis das páginas. Preservar skip, aleatório/repetição, seek confirmado, estados de erro e recuperação, não somente play/pause do desenho.
8. **Sem perda de recursos/dados.** Fila, playlists, letras/versões/romaji, retry/cópia sanitizada, extensão opcional, navegação experimental e preferências não relacionadas ao Lidarr permanecem. Não criar schemas novos apenas para mudar o visual.

## Ordem e fronteiras

- Primeira frente: retirada integral Lidarr e fundação Aurora nativa, com testes dirigidos e composição real compilável.
- Em seguida: casca/lateral/host e transporte persistente, migrando superfícies com foco/shortcuts preservados. O plano de execução deve definir o menor recorte funcional e seus nomes/assinaturas a partir das fontes reais; não tornar as antigas janelas modais páginas apenas por renomeá-las.
- Biblioteca permanente e seu ingresso precisam de plano técnico de persistência/recuperação próprio; pesquisa online precisa de escolha/viabilidade do provider. Esses gates não bloqueiam a aparência e a navegação locais.
- Não alterar a arquitetura de sandbox, DNS/TLS, allowlists, pins, ACKs, NoChild, AppContainer ou WFP nesta frente. Não habilitar downloads por causa do redesign.

## Aceite da primeira frente

- Contrato nativo oferece as cores Aurora nos dois temas e mantém comportamento Sistema/HC/reduced motion.
- Inputs/fixtures históricos permanecem intactos; diferenças visuais de produto têm asserts próprios, não relaxamento dos oráculos antigos.
- Tema/navegação preservam identidade da faixa, posição, fila, volume e contagem de comandos de áudio.
- Ações mantidas continuam acessíveis; Lidarr não possui capacidade ativa ou dependência exclusiva no produto.
- Build e testes Qt/QML selecionados pelo risco passam sobre a mesma fonte; regressão ampla somente no marco que a justificar.
- Resultado visual nativo é comparado com a referência aceita em ambiente isolado. HTML verde não é esse aceite nativo.

## Validação, preservação e fora do escopo

Aplicar [validação proporcional](../../../agents.md) e [comandos reais](../../development-commands.md). Registrar Red/Green, filtros, contagens, falhas e pendências. Não exigir toda a suíte a cada subtarefa; não declarar release global a partir do recorte.

Trabalhar no checkout isolado já existente, preservando WIP do checkout principal. Ensaios usam dados sintéticos e diretórios próprios. Sem instalação, Registro, UAC, push, publicação, substituição/reinício do aplicativo pessoal, clipboard pessoal ou leitura de músicas/credenciais.

Este documento fixa o aceite visual e as fronteiras. O plano por frente registra escolhas técnicas do executor; não promove o plano diretor inteiro ou todos os futuros subsistemas a implementação aprovada.
