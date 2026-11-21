# Sala de escuta — ouvir junto com amigos

## Estado

**Aprovado pelo mantenedor em 2026-11-17** ("Aprovar"). Desenho escrito a partir da conversa com o mantenedor e revisado por um leitor independente. As escolhas do mantenedor aparecem como decisões; as minhas, como "proposta" ou "minha decisão", e a aprovação cobre as duas. O plano de execução está em [2026-11-17-listening-room.md](../plans/2026-11-17-listening-room.md).

## Intenção

O mantenedor quer ouvir música com amigos que moram em outras cidades, cada um no próprio computador. Uma pessoa é o host, e a sala funciona como um "streaming" pessoal da biblioteca dela:

> Teremos 1 host que envia um link e com esse link as pessoas se conectam e escutam juntos (cada um no seu volume, porém as mesmas músicas).

O host decide quem pode controlar a sala e o que cada pessoa pode fazer. Os convidados também podem trazer músicas próprias, se tiverem permissão. Mais tarde, as salas devem aceitar itens do YouTube tocados sem baixar.

## Quem usa

- Amigos em cidades diferentes, todos com o RC Music instalado (decisão do mantenedor).
- Grupos pequenos, normalmente conversando ao mesmo tempo em outro app de voz. É por isso que a sincronia importa.

## Objetivos

1. Todos ouvem a mesma música no mesmo ponto, com **diferença abaixo de 100 ms** entre participantes (meta do mantenedor; até 150 ms ainda é aceitável).
2. Cada pessoa controla só o próprio volume e o próprio mudo.
3. O host cria um convite em forma de link, com **número máximo de pessoas** e uma escolha: o convite **termina ao fechar o app** ou **fica ativo até ser revogado**. Ele pode revogar a qualquer momento.
4. O host define **permissões por pessoa** e **cargos** (conjuntos de permissões com nome) que ele mesmo cria.
5. Cada playlist é **privada por padrão** e pode ficar **visível para a sala**.
6. Com permissão, o convidado coloca na fila **músicas que ele tem** e o host não tem.
7. Na primeira abertura, o app pede um **apelido** e sorteia um **avatar** de uma lista do app. A pessoa pode trocar o avatar por outro da lista ou por uma imagem própria.

## Fora do escopo agora

- Chat, voz ou vídeo dentro da sala. O grupo usa o app de voz que já tem.
- Entrar pelo navegador sem o RC Music.
- Itens do YouTube tocados sem baixar. O modelo da fila já reserva o tipo, mas a implementação depende da reprodução direta do YouTube, que é outra frente.
- Linux. Windows primeiro, como o resto do produto.
- Servidor de retransmissão (TURN). Quando a conexão direta falhar, a sala avisa com clareza (ver Riscos).
- Contas, login, nuvem de perfis ou sincronização entre computadores.

## Anti-objetivos

Situações que tornam a função um fracasso mesmo que ela funcione tecnicamente:

- a música passar por um servidor de terceiros ou ficar guardada nele;
- o app passar a depender de internet para tocar música local fora da sala;
- caminhos de arquivo, a biblioteca completa ou playlists privadas do host chegarem aos convidados;
- uma pessoa sem permissão conseguir mudar a fila ou a reprodução da sala;
- o convidado ficar com cópias permanentes das músicas de outros;
- custo financeiro para o mantenedor.

## Restrições

- **Exceção explícita ao local-first.** `agents.md` §10 e NFR-PRIV-004 limitam a rede a ações explícitas e proíbem outros serviços remotos. A sala só usa rede enquanto a pessoa está hospedando ou participando, e só para isso. O ADR-0022 registra a exceção e atualiza o contrato.
- O app continua inteiro sem rede. Abrir o app não conecta a nada; só criar ou entrar numa sala conecta.
- Stack aceita (ADR-0005): C++20, Qt Quick/QML, Qt Multimedia/FFmpeg. As dependências novas precisam de licença compatível e manutenção ativa.
- **Custo zero.** O mantenedor aceitou o servidor de apresentação "se não for custar nada". O plano gratuito do Cloudflare Workers tem limites fixos: ao passar deles, as chamadas falham em vez de gerar cobrança (Durable Objects com SQLite estão no plano gratuito desde 2025-04-07, com 100 mil requisições por dia). A conta fica no plano gratuito, sem cartão.
- Arquitetura em camadas: as regras da sala ficam no domínio, sem Qt e sem rede; rede, WebRTC e Cloudflare ficam em adaptadores atrás de portas.

## Abordagens

### Como o áudio chega aos convidados

**A) O arquivo chega antes e cada um toca no horário combinado — escolhida pelo mantenedor.**
O host é o centro, e cada convidado se conecta só a ele. A música vai inteira para um cache temporário de cada convidado, e a próxima é enviada durante a atual. Os apps acertam o relógio entre si, e cada um toca o arquivo local no instante marcado pelo host. As músicas de convidados sobem até o host, que as repassa aos outros.

- A favor: a meta de 100 ms é alcançável, porque nada depende da rede na hora de tocar; pular e avançar são instantâneos depois que o arquivo chega; uma oscilação da internet não corta o som; o player local continua o mesmo.
- Contra: ao entrar, o convidado espera a música atual baixar (cerca de 3 s para 8 MB a 20 Mbps); o convidado guarda uma cópia temporária, apagada ao sair.

**B) Transmissão ao vivo, como uma rádio — descartada.**
O host decodifica, codifica em Opus e transmite pela trilha de áudio do WebRTC. Cada convidado ouviria com 150 a 300 ms de atraso, diferente para cada um, o que estoura a meta. Também exigiria um codificador novo, codificaria duas vezes as músicas de convidados e faria pular e avançar sentirem a latência.

**C) Só sincronizar a posição, e cada um toca a própria cópia — descartada.**
Só funciona quando todos têm o mesmo arquivo, e os amigos não compartilham bibliotecas. A ideia volta como caso particular para itens do YouTube: cada app busca o próprio áudio a partir do ID do vídeo.

### Como os amigos se encontram

A conexão é P2P com WebRTC, mas os dois lados precisam trocar "endereços de conexão" antes de se conectar.

1. **Servidor mínimo próprio no Cloudflare Workers (plano gratuito) — escolhido pelo mantenedor.** Ele só repassa essas mensagens, nunca a música, e não guarda nada.
2. Serviço público de mensagens (ntfy.sh) com mensagens cifradas — descartado: depende de um terceiro e dos limites de uso dele.
3. Troca manual de código — descartada: cada amigo teria de devolver um código ao host.

## Decisões do mantenedor

- Amigos em outras cidades, todos com o RC Music instalado.
- Conexão P2P; caminho A; apresentação pelo Cloudflare Workers gratuito.
- Sincronia "o menos possível de delay": abaixo de 100 ms, com 150 ms ainda bom.
- Apelido escolhido na primeira abertura; avatar automático de uma lista do app, trocável por outro da lista ou por uma imagem própria.
- Convidados podem adicionar músicas próprias, **desde que tenham a permissão**.
- Pessoas novas esperam a aprovação do host; quem ele já aceitou antes entra direto.
- Convite "até revogar" não reabre a sala sozinho: o host clica em "Abrir sala".
- Só as playlists do host podem ficar visíveis para a sala.
- Identidade sem conta, cargos que valem só no app de quem hospeda, reconexão pelo mesmo link e itens fora do escopo (chat, voz, navegador): propostas confirmadas pelo mantenedor ("as propostas são exatamente como pensei").

## Desenho

### 1. Perfil local

- **Primeira abertura:** um diálogo pede o apelido (1 a 32 caracteres, Unicode, sem caracteres de controle) e mostra o avatar sorteado. A pessoa pode trocar o avatar ali ou depois. O app só pede de novo se o perfil sumir.
- **Avatares do app:** um conjunto de 16 a 24 ilustrações SVG no estilo dos ícones Aurora, em `assets/avatars/` (proposta: formas geométricas em cores Aurora, sem rostos, para evitar questões de licença).
- **Avatar próprio:** PNG ou JPEG de até 2 MiB, recortado em quadrado e reduzido para 256 × 256 px no momento da escolha. Só a versão reduzida é guardada e enviada às salas.
- **Identidade:** na primeira abertura, o app gera um certificado próprio (chave privada mais certificado autoassinado), usado no DTLS do WebRTC. A **impressão digital** desse certificado é o ID da pessoa: o host reconhece um amigo por ela, e não pelo apelido, que pode repetir ou mudar. A chave privada fica no perfil, protegida pela DPAPI do Windows.
- **Persistência:** `profile.json` no diretório de configuração atual, com backup e gravação confirmada, como as playlists (ADR-0015).

### 2. Convite e link

- **Criar convite** (só no app do host): máximo de pessoas (1 a 10; o padrão é 5); **duração**, "termina quando eu fechar o app" (padrão) ou "fica ativo até eu revogar"; **cargo inicial** de quem entrar (padrão "Ouvinte").
- O teto de 10 é uma proposta, por causa do envio de arquivos pelo host: 10 convidados recebendo um MP3 de 8 MB somam 80 MB de envio por música.
- **Conteúdo do link:** ID da sala (128 bits aleatórios), segredo do convite (128 bits) e impressão digital do certificado do host. Formato:

  ```text
  https://<worker>.workers.dev/j/<id-da-sala>#<segredo>.<impressão-do-host>
  ```

  A parte depois do `#` nunca sai do navegador nem chega ao servidor. A página `/j/` mostra "Abra no RC Music" e um botão que copia o link. No app, **Entrar com link** aceita o link colado. A abertura automática por `rcmusic://` depende de registrar o protocolo no Windows, que é tarefa do instalador e fica fora desta frente.
- **Revogar:** encerra a sala, desconecta todos e invalida o segredo. Um convite novo gera outro link. (Proposta: não há "revogar só o link e manter quem já entrou", para manter uma única ação simples.)
- **Hospedar é um modo explícito:** a sala só existe entre "Abrir sala" e "Encerrar sala" (ou o fechamento do app). Fora dele, o host ouve sozinho, sem transmitir. Minimizar para a bandeja não fecha o app, então a sala continua; "fechar o app" é sair de fato.
- **Convite "até revogar"** (decisão do mantenedor): o link fica salvo no perfil do host e continua valendo, mas ao reabrir o app a sala **não** abre sozinha. Ela espera o host clicar em "Abrir sala" na página Ouvir junto. Com a sala fechada, quem tentar entrar vê "O host não está com a sala aberta".
- **Entrada com aprovação dos novos** (decisão do mantenedor): quem o host já aceitou antes, em qualquer sala dele, entra direto. Uma pessoa nova fica em "Aguardando o host", e o host vê o pedido com avatar e apelido para **Aceitar** ou **Recusar**. Pessoas bloqueadas são recusadas sem pedido. Assim, um link vazado não basta para ouvir as músicas do host.
- **Lotação:** o host recusa quem chegar com a sala cheia ("A sala está cheia"). O host e as pessoas aguardando aprovação não contam no limite.
- **Versões diferentes:** a primeira mensagem traz a versão do protocolo. Versões incompatíveis são recusadas com "Atualize o RC Music para entrar nesta sala" (minha decisão).

### 3. Servidor de apresentação (Cloudflare Workers)

- Código em `services/room-signaling/` (TypeScript), com um Worker e um Durable Object por sala, usando a API de WebSocket com hibernação.
- **O host abre a sala** conectando um WebSocket com o ID e uma prova de posse do segredo.
- **O convidado entra** conectando com o mesmo ID. O servidor só repassa mensagens entre o host e cada convidado da sala.
- **O conteúdo das mensagens é cifrado** de ponta a ponta com uma chave derivada do segredo do convite (AES-GCM via HKDF). O servidor vê apenas blocos opacos e não consegue trocar as impressões digitais dos certificados, o que impede um ataque de intermediário pelo servidor.
- **O servidor não guarda nada:** nem banco, nem IP, nem logs com conteúdo. O estado é só a lista de sockets abertos.
- **Limites:** mensagem de até 16 KiB, até 20 mensagens por segundo por socket, até 11 sockets por sala (host mais 10) e 10 minutos sem conexão do host fecham a sala.
- **Hospedagem:** a conta da Cloudflare e o `wrangler deploy` são passos do mantenedor. O app recebe o endereço do Worker por configuração de build, como o perfil de download.

### 4. Conexão P2P

- **Biblioteca:** [libdatachannel](https://github.com/paullouisageneau/libdatachannel) (MPL-2.0, C++17, ativa), só com data channels e sem trilhas de mídia. A licença e a forma de obter a dependência entram no ADR-0022.
- **Descoberta de rota:** STUN gratuito da Cloudflare (`stun.cloudflare.com:3478`). Sem TURN nesta frente.
- **Canais por conexão:**
  - `control`: confiável e ordenado; mensagens de estado, intenções e relógio.
  - `media`: confiável e ordenado; blocos binários de arquivos, com controle de fluxo pelo `bufferedAmount`.
- **Mensagens de controle:** JSON com versão do protocolo, tipo e limites de tamanho. O decodificador rejeita mensagens desconhecidas ou grandes demais, como os protocolos de download já fazem.
- **Topologia em estrela:** cada convidado fala só com o host. Os convidados não se conectam entre si, então não há malha nem um furo de NAT a mais por par.

### 5. Modelo da sala (domínio)

- **`RoomState`, autoritativo no host:**
  - membros (ID, apelido, avatar, cargo, ajustes individuais);
  - fila de `RoomItem`s;
  - item atual;
  - estado de reprodução: tocando ou pausado, mais uma âncora (relógio do host, posição);
  - versão crescente.
- **`RoomItem`:**
  - ID do item;
  - origem tipada: `HostTrack`, `GuestTrack(dono)` ou, no futuro, `YoutubeVideo(id)`;
  - metadados limitados: título, artista, álbum, duração e capa reduzida;
  - a versão de letra escolhida pelo host, se houver, com até 64 KiB, para os convidados verem a letra sincronizada (minha decisão);
  - hash SHA-256 e tamanho do conteúdo;
  - quem adicionou.
- **Convidados enviam intenções** (pular, pausar, adicionar, remover e outras). O host confere a permissão, aplica e transmite o novo estado com a versão seguinte. Uma intenção recusada volta com o motivo, e o app do convidado mostra "Sem permissão".
- **Membros aguardando:** um estado `Pending` antes de `Admitted`. Pessoas aguardando não recebem estado da sala, fila nem arquivos.
- **Relação com o player local:**
  - **No host,** ao abrir a sala, a fila atual dele vira a fila da sala, e o `PlayerController` continua mandando. As mudanças feitas pelo host no próprio app viram estado da sala. Os itens de convidados vivem no cache temporário e ficam fora do salvamento de sessão. Ao encerrar a sala, o host continua com a fila, sem os itens de convidados. (Minha decisão.)
  - **Aleatório, repetir e o fim da fila** valem para a sala inteira e só o host os controla. A ordem que o host define é a que todos ouvem e a que a antecipação de arquivos segue. (Minha decisão.)
  - **No convidado,** a fila local é guardada ao entrar e restaurada ao sair, e o player passa a seguir a sala. Volume e mudo continuam locais. (Proposta.)
- **Saída do dono de uma música:** as músicas que um convidado enviou continuam na fila depois que ele sai, porque o host já tem a cópia. Elas saem quando tocam, quando alguém as remove ou quando a sala acaba.
- **Saída do host:** a sala acaba para todos ("O host encerrou a sala").
- **Queda de conexão de um convidado:** ele tenta voltar sozinho por 30 s e depois oferece "Entrar de novo".

### 6. Transferência de arquivos

- **Endereçamento por conteúdo:** cada arquivo é identificado pelo SHA-256. Um convidado que já recebeu o arquivo não o recebe de novo.
- **Ordem:** primeiro o item atual, depois o próximo, depois o resto da fila, até 3 itens à frente.
- **Cache do convidado:** `room-cache/` no diretório de configuração, com limite de 1 GiB. É apagado ao sair da sala e, na abertura do app, quando sobrar de uma queda. Os arquivos não entram na biblioteca nem na fila local.
- **Músicas de convidados (com permissão):**
  - o arquivo sobe primeiro até o host;
  - o host recusa o que passar de 50 MiB ou não for áudio válido, conferindo com as mesmas regras e o mesmo probe de metadados da importação local;
  - só depois o host o põe na fila e o distribui.
- **Arquivo atrasado:** se a hora de tocar chegar e o arquivo ainda não tiver chegado, o convidado mostra "Carregando…" e entra no ponto certo da música quando o arquivo terminar.
- **O que nunca é enviado:** caminhos e nomes de arquivo locais.

### 7. Sincronia

- **Relógio:** o convidado mede a diferença para o relógio do host com trocas no estilo NTP pelo `control`. São 8 amostras na entrada e uma nova a cada 5 s, e vale a de menor ida e volta.
- **Comando de tocar:** o host diz "tocar o item X a partir da posição P no instante T do meu relógio". T fica cerca de 300 ms no futuro, para dar tempo a todos.
- **Correção contínua:** a cada segundo o convidado compara a posição real com a esperada. Diferenças pequenas são corrigidas sem salto audível, com ajuste leve de velocidade se o backend permitir, ou com um seek num ponto de baixo volume. Diferenças grandes são corrigidas com seek.
- **Atraso do dispositivo de saída:** fones Bluetooth acrescentam 150 a 250 ms que o app não enxerga. Cada pessoa tem um ajuste manual de atraso (−500 a +500 ms) nas configurações da sala.
- **Medição:** a interface de desenvolvedor registra a diferença estimada de cada convidado. A meta é abaixo de 100 ms com saída com fio, e 150 ms é o limite aceitável.
- **Risco principal:** o `QMediaPlayer` não promete início nem posição com precisão de milissegundos. A **fatia S1** é um protótipo descartável que mede:
  - o atraso entre `play()` e o som;
  - a granularidade de `position()`;
  - a precisão do seek;
  - se `setPlaybackRate` funciona sem artefatos.

  Se o protótipo mostrar que 150 ms não é alcançável com o backend atual, a alternativa é um caminho próprio de saída: decodificar com o FFmpeg e escrever num `QAudioSink`, com agendamento por amostra. Isso é bem maior, e a decisão volta ao mantenedor antes de seguir.

### 8. Permissões e cargos

**Permissões**, cada uma ligável por cargo e por pessoa:

| Permissão | O que libera |
|---|---|
| Pular | próxima, anterior e avançar ou voltar na música |
| Pausar | pausar e retomar a sala |
| Adicionar da sala | pôr na fila faixas de playlists visíveis |
| Adicionar playlist | pôr na fila uma playlist visível inteira |
| Editar fila | remover e reordenar itens |
| Enviar músicas próprias | pôr na fila arquivos do próprio computador (sobem para o host) |
| Adicionar do YouTube | reservada para a frente do YouTube |

- **Só do host, sempre:** criar e editar cargos, mudar o cargo de alguém, expulsar, bloquear, mudar o limite de pessoas e revogar.
- **Cargos:** o host cria, renomeia e exclui cargos, cada um com um conjunto de permissões. O app traz dois cargos iniciais, editáveis: "Ouvinte" (nenhuma permissão) e "DJ" (todas). Quem tinha um cargo excluído volta ao cargo inicial do convite.
- **Ajuste individual:** além do cargo, o host pode liberar ou negar uma permissão para uma pessoa. O ajuste vence o cargo.
- **Efeito imediato:** quando algo muda, o host recalcula e envia as permissões efetivas, e o app do convidado esconde ou desabilita os controles na hora. O host sempre confere de novo cada intenção, então a interface é só conforto e nunca a proteção.
- **Persistência:** cargos, pessoas já aceitas, cargo de cada pessoa (pelo ID, com o último apelido visto) e bloqueios ficam no perfil do host, em `room-roles.json`, e valem para as próximas salas.

### 9. Visibilidade de playlists

- **Só as playlists do host** podem ficar visíveis (decisão do mantenedor). Convidados trazem músicas próprias uma a uma, com a permissão Enviar músicas próprias.
- **Novo campo** `visibility` em cada playlist: `private` (padrão) ou `room`. O documento de playlists ganha uma nova versão de esquema. Arquivos antigos carregam como privadas.
- **O que o convidado vê:** só o nome e as faixas (metadados limitados) das playlists `room`, enquanto estiver na sala. As privadas nem aparecem.
- **Mudar a visibilidade** durante a sala atualiza na hora o que os convidados veem.

### 10. Interface

- **Nova página "Ouvir junto"** na navegação lateral.
  - **Fora de uma sala:** "Criar sala" e "Entrar com link".
  - **Host:**
    - "Abrir sala" e "Encerrar sala";
    - link com botão Copiar;
    - pedidos para entrar, com Aceitar e Recusar (também como notificação do Windows quando o app estiver em segundo plano);
    - lista de pessoas com avatar, apelido, cargo e menu (mudar cargo, ajustar permissões, expulsar, bloquear);
    - aba Cargos;
    - "Revogar e encerrar".
  - **Convidado:** pessoas na sala, "Sair da sala" e o próprio ajuste de atraso.
- **Barra do player durante a sala:** um selo "Na sala de <apelido> · N pessoas". Controles sem permissão ficam desabilitados, com a dica "O host não liberou".
- **Fila:** mostra o avatar de quem adicionou cada item e o estado de carregamento no convidado.
- **Playlists:** chave "Visível para a sala" no detalhe da playlist.
- **Configurações:** seção Perfil, com apelido e avatar.
- **Aviso de privacidade:** ao criar ou entrar na primeira sala, o app avisa uma vez que, como em chamadas de voz, os participantes ficam sabendo o endereço IP uns dos outros.
- Tudo funciona por teclado e leitor de tela, como o resto da Aurora.

## Requisitos e documentos afetados

- `docs/product/requirements.md`:
  - novos requisitos FR para perfil, sala, convite, permissões e cargos, visibilidade e músicas de convidados;
  - NFR-PRIV-004 ganha a exceção da sala;
  - novos limites NFR-LIMIT para tamanhos de mensagem, arquivo, cache e pessoas.
- `agents.md` §10: a exceção limitada ao servidor de apresentação da sala.
- **ADR-0022:** sala P2P, libdatachannel, Cloudflare Workers e o modelo de confiança.
- **ADR-0015:** revisão com o campo de visibilidade.
- Guia do usuário, estado do projeto e arquitetura.

## Fatias propostas

| Fatia | Entrega |
|---|---|
| S0 | ADR-0022, requisitos e contrato atualizados (só documentação) |
| S1 | Protótipo descartável de sincronia com dois `QMediaPlayer` no mesmo computador, com relatório; **ponto de decisão** |
| S2 | Perfil: domínio, store, diálogo de primeira abertura, avatares, Configurações |
| S3 | Domínio da sala: estado, intenções, permissões, cargos e versões, com testes puros |
| S4 | Protocolo de controle e de arquivos: codecs com limites, testes de mensagens inválidas |
| S5 | Servidor de apresentação: Worker e Durable Object com testes locais (Miniflare) e cifragem ponta a ponta |
| S6 | Transporte: libdatachannel no build, porta `RoomTransport`, teste com dois pares em loopback e um servidor de apresentação falso |
| S7 | Host e convidado de ponta a ponta: criar, entrar, lotação, revogar, convite persistente e reconexão |
| S8 | Transferência e cache: endereçamento por conteúdo, antecipação, limpeza e quota |
| S9 | Sincronia real: relógio, agendamento, correção, ajuste de atraso e medição |
| S10 | Permissões na UI, cargos, ajustes individuais e expulsar/bloquear |
| S11 | Visibilidade de playlists e "adicionar playlist" |
| S12 | Músicas de convidados: envio, validação no host e distribuição |
| S13 | Teste real entre dois computadores, por internet, e documentação final |

S1 vem logo depois da documentação porque pode mudar o desenho da sincronia. S3 e S4 não dependem de rede e podem andar em paralelo com S5.

## Testes

- **Unitários, no domínio:** permissões efetivas (cargo mais ajuste), aplicação e recusa de intenções, versões de estado, conta de relógio e agendamento com relógio falso, lotação e revogação.
- **Codecs:** mensagens válidas, inválidas, grandes demais e de versão desconhecida.
- **Transporte:** dois pares libdatachannel no mesmo processo, só com candidatos locais e um servidor de apresentação falso. Nenhum teste depende de internet.
- **Worker:** testes com Miniflare ou Vitest no pool de Workers: repasse, limites, sala cheia e cifragem opaca.
- **QML:** página Ouvir junto, controles desabilitados sem permissão e diálogo de perfil.
- **Teste real:**
  - dois apps no mesmo computador, com perfis separados;
  - depois, dois computadores em redes diferentes, com medição da diferença de sincronia.

  A forma de rodar um segundo perfil num build de produto fica para o construtor, porque o `--m0-test-config-root` não existe em builds de produto.

## Riscos

- **Precisão do `QMediaPlayer`:** pode impedir os 100 ms. A fatia S1 decide antes do resto.
- **Conexão direta falhar:** acontece com CGNAT simétrico, redes móveis e corporativas. Sem TURN, a sala mostra "Não foi possível conectar ao anfitrião. Verifique a conexão e tente de novo.". O TURN da Cloudflare tem 1.000 GB gratuitos por mês e pode entrar depois, com credenciais temporárias emitidas pelo Worker. Fica registrado como evolução, não nesta frente.
- **Envio do host:** com 10 pessoas e músicas grandes, a internet de envio do host pode atrasar a primeira música. A antecipação reduz, mas não elimina.
- **Direitos autorais:** o host envia cópias temporárias das próprias músicas a amigos. O app apaga as cópias e não as importa, mas a responsabilidade pelo conteúdo compartilhado é de quem hospeda. O guia do usuário vai dizer isso.
- **Distribuição para os amigos:** os amigos só conseguem instalar o RC Music depois que existir o instalador, que o mantenedor pediu só quando o redesenho terminar. Até lá, os testes usam dois apps neste computador e, se houver, outro computador do mantenedor. O teste com amigos (S13) depende do instalador.
- **Limites do plano gratuito:** se forem ultrapassados, as salas novas falham até o dia seguinte (UTC), sem cobrança. O uso previsto fica muito abaixo de 100 mil requisições por dia.

## Fica para o construtor

- Nomes de classes, de arquivos e a divisão interna dos adaptadores, seguindo as camadas atuais.
- Como obter e compilar a libdatachannel e a pilha TLS dela, na mesma linha das outras dependências.
- Detalhes do formato JSON do protocolo e dos documentos `profile.json` e `room-roles.json`.
- O desenho exato das ilustrações de avatar, dentro do estilo Aurora.
- Os valores finos de sincronia (limiares de correção, antecedência do comando de tocar), a partir da medição da S1.
