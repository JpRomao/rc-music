# Spike S1.1: sincronia possível com o QMediaPlayer

**Escopo:** protótipo descartável da fatia S1 da sala de escuta. Mede, num único PC, início, seek, deriva e ajuste de velocidade de dois `QMediaPlayer`. Não mede a sincronia entre PCs. [Plano](superpowers/plans/2026-11-17-listening-room.md) · [Desenho, seção 7](superpowers/specs/2026-11-17-listening-room-design.md)
**Data:** 2026-11-17.

## Veredito

**p95 com correção: (b) 11,9 ms e (c) 12,0 ms.** Os dois ficam abaixo de 100 ms. Pelo gate da S1, a sala **segue com o `QMediaPlayer`**.

Esses números são uma diferença no mesmo PC. `eA − eB` cancela tudo o que atinge os dois players ao mesmo tempo: a mixagem, a thread principal e os inícios atrasados em conjunto, como em b-3, s-16, e2-15 e e2-19.

Para estimar players independentes, comparei todos os pares de players das janelas com correção, inclusive de execuções diferentes: `|e_i − e_j|`. Esse indicador é pessimista:

| Cenário | Pares | p50 | p95 | máx. |
|---|---|---|---|---|
| (b) | 780 | 7,9 | 40,6 | 68,1 |
| (c) | 780 | 8,7 | 41,1 | 57,1 |

O gate continua aprovado por esse indicador.

O resultado vale para uma única saída. Entre PCs, somam-se o erro do relógio e a diferença de latência de cada saída, que este spike não mede. A S9.2 mede isso no teste real.

Riscos para a S9:

1. **Velocidade com compensação de tom (padrão do Qt):**
   - cada troca para uma velocidade diferente de 1,0 gera cerca de **115 ms de silêncio**;
   - enquanto a velocidade fica diferente de 1,0, o som atrasa mais **104 a 116 ms**.

   O ajuste fino por velocidade só serve com `setPitchCompensation(false)`.
2. **Inícios atrasados:** em 9 de 216 inícios (4,2%), o som chegou 127 a 391 ms depois de T, contra cerca de 70 ms no caso normal. Só a correção com seek resolve.
3. **Seeks falsos:** pausas de 127 a 641 ms na thread principal atrasaram os `positionChanged`. O corretor tratou valores velhos como atraso real e fez seeks sem necessidade.
4. **Saltos pequenos e invisíveis:** em cerca de 1.150 s de áudio sem comandos houve 6 saltos de +8 a +533 ms no som, sempre atrasando.
   - O salto de 533 ms aparece em `position()` (−499 ms) e a correção o resolve com seek.
   - O de 46 ms aparece pela metade (−27 ms).
   - Os de 8 a 33 ms ficam invisíveis: `position()` mudou no máximo 5 ms.

   Como nunca adiantam, os saltos invisíveis podem se somar ao longo de uma faixa. Na medição, eles somaram cerca de 106 ms em 1.151 s, perto de 5,5 ms/min, todos com outro aplicativo tocando alto. A recomendação para a S9 está em [Limiares](#limiares-escolhidos).

## Ambiente

- Windows 11 Pro 10.0.26300.
- Saída padrão "Alto-falantes (3- USB Audio Device)". Mixagem de 48 kHz, 2 canais, float de 32 bits. O ID do endpoint do loopback é igual ao do `QMediaDevices::defaultAudioOutput()`.
- Qt 6.11.2 e Qt Multimedia 6.11.2 com `QT_MEDIA_BACKEND=ffmpeg`. FFmpeg `7.1.5-rcmusic2`, com as DLLs do SDK preparado. [Comandos](development-commands.md#qt-multimedia-com-ffmpeg-corrigido)
- MSVC 14.44, build Release com Ninja.
- Cada `QAudioOutput` com volume 0,5 e compensação de tom no padrão (ligada), exceto em (e2) e na sonda.
- Outros aplicativos tocavam áudio durante parte das medições, com picos de até 0,72 no loopback. Para tratar isso, o detector exige cliques isolados e alinhados à grade de 1 s.

## Método

- [`make_clicks.py`](../experiments/listening-room-sync/make_clicks.py) gera dois WAV de 3 min, a 48 kHz, com um clique de 1 ms por segundo, em 0,5 s + k. Um tem o clique só no canal esquerdo; o outro, só no direito. Também gera um seno de 1 kHz para a sonda de tom.
  - O `ffmpeg` 8.1.1 converte os WAV para MP3 de 320 kbit/s.
  - No MP3 decodificado, o primeiro clique cai em 500,0 ms. O atraso do codificador é compensado (gapless).
- [`main.cpp`](../experiments/listening-room-sync/main.cpp) usa dois players:
  - o player A toca `clicks-left.mp3` e o B toca `clicks-right.mp3`, cada um com o seu `QAudioOutput` no dispositivo padrão;
  - o comando em T usa `QTimer` preciso disparado 3 ms antes, mais espera ativa no QPC até T.
- [`LoopbackRecorder.cpp`](../experiments/listening-room-sync/LoopbackRecorder.cpp) grava a mixagem do sistema por WASAPI loopback (`AUDCLNT_STREAMFLAGS_LOOPBACK`) com o QPC de cada pacote.
- **Medida:** para cada clique, `e = conteúdo do clique − (P + t − T)`.
  - A dessincronia é `eA − eB`; o valor positivo indica A adiantado.
  - O índice do clique vem de `position()`, amostrado a cada 10 ms.
  - Cada janela usa a mediana dos cliques dentro dela.
  - O loopback mede a chegada à mixagem; a latência do dispositivo de saída fica de fora.
- **Detector:**
  - um filtro casado para o pulso de 1 ms (segunda diferença na borda de subida menos a de 1 ms depois);
  - o limiar é metade da pontuação mediana dos cliques na calibração, cerca de 0,17;
  - o candidato precisa cair na grade de 1 s (±12 ms) junto com outro candidato e estar isolado, sem outro candidato a menos de 30 ms no mesmo canal;
  - se dois candidatos caem no mesmo clique, vale o de maior pontuação.
- **Correção**, usada em (b) e (c) a partir de 3,5 s:
  - a cada 100 ms, o erro de cada player é a mediana de `posição − ideal` nos `positionChanged` dos últimos 500 ms, com pelo menos 3 leituras;
  - acima do limiar de seek: `setPosition(ideal)` e 1 s de espera;
  - acima do limiar de ajuste: velocidade 1 ± 0,01 até o erro cair à metade do limiar ou cruzar o zero;
  - a medição usou ajuste a partir de 30 ms e seek a partir de 120 ms. Os valores recomendados estão em [Limiares](#limiares-escolhidos).
- **Janelas:**
  - em (b), sem correção é de T a T + 3 s; com correção é de T + 10,5 s a T + 13,5 s;
  - em (c), A começa em T. B carrega o arquivo em T + 2 s, faz `setPosition(P + agora − T)` e `play()`. As janelas são as mesmas de (b), contadas a partir do `play()` de B.
- **Percentis:** posto mais próximo; o p95 de 20 valores é o 19º.

## Resultados

Todos os valores estão em ms e são valores absolutos de `eA − eB`.

| Cenário | n | p50 | p95 | máx. |
|---|---|---|---|---|
| (a) `play()` dos dois no mesmo tick em T | 20 | 0,0 | 10,1 | 203,0 |
| (b) sem correção (P pré-carregado e `play()` em T) | 19 | 0,0 | 132,6 | 132,6 |
| **(b) com correção** | 20 | 0,0 | **11,9** | 24,0 |
| (c) sem correção (B entra 2 s depois) | 20 | 6,0 | 16,0 | 16,0 |
| **(c) com correção** | 20 | 6,0 | **12,0** | 15,9 |
| (d) 3 min sem correção: início / fim / máximo | 1 | 4,0 / 4,0 / 4,0 | | |
| (s) seek com a música tocando, depois do seek | 19 | 6,0 | 174,0 | 174,0 |

- **Atraso de `play()` até a mixagem:** a mediana de `e` no início é de cerca de −70 ms, com faixa normal de −60 a −105 ms.
  - Os 9 inícios atrasados (risco 2) causam os máximos de (a) e (b) sem correção.
  - Em (s), o máximo de 174 ms vem de um início atrasado de A, que não recebeu o seek.
- **(b) com correção:** os seeks corrigiram o início atrasado de b-3. O máximo de 24 ms ficou abaixo do limiar de ajuste.
- **(c):** o carregamento local levou de 5,4 a 7,6 ms. Com `setPosition` calculado, o atrasado entrou a até 16 ms do outro.
- **(d) deriva:**
  - entre os dois players, 0,001 ms/min, ou seja, nenhuma;
  - de cada player contra o relógio QPC, +0,56 ms/min no som;
  - de `position()` contra o QPC, −0,02 e +0,12 ms/min.

  O relógio do dispositivo anda cerca de 9 ppm à frente do QPC, e `position()` não vê essa diferença. Numa faixa de 4 min, isso dá cerca de 2 ms.
- **(s) precisão do seek:** depois de `setPosition(ideal)` com a música tocando, o `e` de B ficou entre −69,8 e −51,6 ms (p50 −62,5).
  - Em (b), b-3 ficou em −101 ms nos dois players depois dos seeks.
  - No som, o seek cai, portanto, entre 18 ms adiantado e 31 ms atrasado em relação ao regime normal, de cerca de −70 ms.
  - Para `position()`, o seek cai a até 6 ms do alvo (b-3: −6 e −4 ms).
  - O seek causa de 20 a 25 ms de silêncio (sonda de tom dos pilotos).
- **(e) velocidade 1,01 e depois 0,99 por 2 s em B**, com compensação de tom no padrão:
  - o deslocamento líquido de B, já de volta a 1,0, foi de +20,04 e −19,93 ms (p50), contra ±20 esperados;
  - o erro absoluto ficou em p50 0,07, p95 25,3 e máximo 28,0 ms (n = 38). Os desvios grandes vêm dos saltos espontâneos de e-15 e e-17, conferidos nos cliques brutos.
- **(e2) igual a (e), com `setPitchCompensation(false)`:**
  - o erro absoluto ficou em p50 0,40, p95 45,6 e máximo 527 ms (n = 40);
  - os desvios grandes vêm de saltos espontâneos em e2-0, e2-10 e e2-11. Em e2-10, um salto de 533 ms atingiu os dois players ao mesmo tempo.
- **Saltos espontâneos:** em 1.151 s de canais sem comando (A em a, d, e, e2 e s; B em a e d), houve saltos de +8, +14, +32, +33, +46 e +533 ms, sempre com o som atrasando.
  - Todos aconteceram na segunda execução, com outro aplicativo tocando alto. A causa não foi isolada.
  - Para medir o efeito em `position()`, comparei a média de `posição − ideal` nas amostras de 10 ms antes do salto (−1.000 a −100 ms) e depois do clique seguinte (+1.300 a +1.650 ms). Usei só o player A, em velocidade 1,0.
  - Variação de `position()` em cada salto:

    | Execução | Salto | Variação de `position()` |
    |---|---|---|
    | e2-10 | +533 | −499 |
    | e2-0 | +46 | −27 |
    | e-15 | +32 | +3 |
    | e2-11 | +33 | +1 |
    | e-17 | +14 | −5 |
    | s-15 | +8 | −4 |
  - Só os saltos grandes chegam a `position()`.

### Granularidade de `position()`

- `position()` muda em degraus de 48 ms (às vezes 72 ms), cerca de uma vez a cada 50 ms. Nas 63.170 mudanças das duas execuções, o intervalo teve p50 de 50,0 ms e p95 de 50,9 ms.
- O sinal `positionChanged` segue o mesmo ritmo: p50 de 50,0 ms e p95 de 50,8 ms (piloto 3, n = 2.521).
- Os dois players mostram os mesmos valores.
- `position()` fica de 67 a 104 ms à frente do que chega à mixagem (p50 77 ms, p95 96 ms, n = 80). Essa folga muda a cada início, e `position()` não a enxerga.
  - Entre dois players, essa parte invisível chegou a 37 ms, com p95 de 25 a 27 ms nos pares de (b) e (c).
  - Nas janelas com correção, o `e` de cada player foi de −128,5 a −53,6 ms. Exemplos: b-4 B −128,5; c-14 −110,7 e −109,1.
- No indicador de players independentes, a parte que `position()` vê, `|est_i − est_j|`, teve p95 de 21,4 ms em (b) e 30,4 ms em (c), com máximo de 41,5. Ela fica limitada pela faixa morta de ajuste.
- O estimador de erro, a mediana de 500 ms dos `positionChanged`, tem ruído p95 de 3,8 ms (piloto 3, n = 180).
- A thread principal parou de 127 a 641 ms em 6 de 138 execuções. Durante essas pausas, os valores chegaram atrasados. Isso causou 6 seeks falsos em b-1 e 2 em b-2.
- Duas dessas pausas coincidiram com falhas no som: a de 579 ms com o salto de 533 ms de e2-10, e a de 272 ms com o início atrasado de e2-15. Isso indica travamentos do sistema inteiro, não só da thread de interface.

### Velocidade e tom

A sonda tocou um seno de 1 kHz em um player. A frequência medida é a mediana dos períodos entre cruzamentos de zero.

| Compensação de tom | 1,01 | 0,99 | Silêncio na troca para ≠ 1,0 |
|---|---|---|---|
| Padrão (ligada) | 999,9 a 1.000,1 Hz | 999,9 a 1.000,1 Hz | 115 ms |
| Desligada | 1.009,9 a 1.010,1 Hz | 990,0 a 990,2 Hz | 0 ms |

- **Com a compensação (padrão):**
  - o tom fica igual;
  - cada troca para uma velocidade diferente de 1,0 gera 115 ms de silêncio;
  - enquanto a velocidade fica diferente de 1,0, o som fica 104 a 116 ms atrasado (medido em 18 execuções de (e)), e esse atraso some na volta para 1,0;
  - por isso, a correção por velocidade piora a sincronia durante o ajuste e cria duas falhas audíveis.
- **Sem a compensação:**
  - não há silêncio;
  - o tom sobe ou desce 1%, cerca de 17 cents, enquanto dura o ajuste.
- **Origem dos dados:** os números da sonda vêm dos três pilotos. A sonda da execução principal foi descartada porque outro áudio do sistema a contaminou: um dos segmentos mediu 2.220 Hz.
- **Escuta:** a sonda não avalia de ouvido se 17 cents incomodam. O mantenedor pode ouvir as gravações em `out/product/listening-room-sync/pilot*/raw/pitch-*.wav`.

## Limiares escolhidos

- **`room_drift_nudge_ms` = 20**, revisado. A medição usou 30.
  - O erro entre dois players independentes tem duas partes.
  - A parte invisível vem da folga entre `position()` e o som. Ela chegou a 37 ms (p95 de 25 a 27 ms), e nenhum limiar a corrige.
  - A parte visível fica dentro da faixa morta. Com 30 ms, dois players nos lados opostos podem ficar a até 60 ms; nos dados, o p95 foi de 21 a 30 ms e o máximo de 42 ms.
  - Com 20 ms, esse limite cai para 40 ms. Ainda fica 5 vezes acima do ruído do estimador (p95 de 3,8 ms) e dos degraus de 10 ms do início.
  - Com o ajuste parando na metade do limiar (10 ms), o corretor não persegue ruído.
  - O valor de 20 não foi exercitado nesta medição. A S9.2 precisa confirmar que ele não gera ajustes demais.
- **`room_drift_seek_ms` = 100**, abaixo dos 120 ms usados na medição:
  - acima de 100 ms, o erro já passou da meta da sala, e o seek corrige em menos de 1 s;
  - para `position()`, o seek cai a até 6 ms do alvo, dentro da faixa morta, então não oscila;
  - no som, o seek cai de 18 ms adiantado a 31 ms atrasado em relação ao regime normal; essa parte é invisível e não dispara ajustes;
  - entre 20 e 100 ms, o ajuste de 1% (10 ms/s) leva no máximo cerca de 9 s;
  - na medição, nenhum erro ficou entre 100 e 120 ms; os seeks ocorreram com erros de 133 a 496 ms.
- **Condições para a S9:**
  - ajustar a velocidade só com `setPitchCompensation(false)`; se a compensação precisar ficar ligada, corrigir só por seek;
  - descartar estimativas quando o último `positionChanged` tiver mais de 150 ms;
  - exigir duas avaliações seguidas acima do limiar antes de um seek;
  - aceitar os saltos pequenos e invisíveis dentro de uma faixa e zerar o acúmulo na troca de faixa, que começa com um novo início agendado.
    - Não fazer seek periódico por padrão: cada um custa 20 a 25 ms de silêncio, e `position()` não indica quando ele é necessário.
    - Com cerca de 5,5 ms/min medidos sob carga, uma faixa de 4 min acumula por volta de 20 ms.
    - Se o teste real da S9 mostrar faixas longas fora da meta, refazer o seek no ponto esperado num trecho de baixo volume.

## Desvios do plano

- **(d) com uma única execução completa**, em vez de 20. A decisão prévia era rodar 3 vezes, porque cada execução dura 3 min. O mantenedor cancelou a segunda durante a gravação e pediu que não fosse repetida.
- **Cenários extras:**
  - (s) seek com a música tocando, para escolher `room_drift_seek_ms`;
  - (e2) igual a (e), com a compensação de tom desligada;
  - sonda de tom.
- **Arquivo extra:** `LoopbackRecorder.h`, a interface do gravador.
- **Duas execuções:**
  - a primeira mediu a, b e c e foi interrompida em (s);
  - os dados de (s) da primeira foram descartados, porque o detector ainda não exigia cliques isolados e outro áudio criou cliques falsos;
  - a regra de isolamento entrou antes da segunda execução, que mediu s, e, e2, sonda e d;
  - nos dados brutos de a, b e c, nenhum candidato válido estava num aglomerado, então a regra não muda esses números.
- **Três pilotos curtos,** usados para ajustar o detector e escolher os limiares provisórios. Só a sonda de tom foi aproveitada deles.
- **Janelas excluídas por falta de cliques:**
  - b-7 sem correção: um tropeço de 14 ms no início de A tirou o primeiro clique da grade;
  - s-13 antes do seek;
  - e-1 na janela final.

## Teste real

**S13.1, 2026-11-19: dois apps no mesmo PC.** O build de testes abre um segundo app isolado com `--profile-root` ([comandos](development-commands.md#dois-apps-isolados-no-mesmo-computador)). Em vez do Worker publicado, o mantenedor aprovou um servidor local: `wrangler dev --local` em `127.0.0.1:8787`, a partir de `services/room-signaling`. A sincronia entre PCs e o servidor publicado ficam para a S13.2.

**Mídia:** três MP3 gerados para o teste, sem arquivos pessoais. São os cliques da S1 com etiquetas ("Cliques Esquerda" e "Cliques Direita") e um tom de 1 kHz de 20 s ("Tom 1 kHz").

**Medida:** o `LoopbackRecorder` da S1 gravou a saída do sistema. Os dois apps tocam a mesma faixa de cliques, então cada clique aparece duas vezes, e a distância entre os dois é a diferença entre os apps.

### Primeira execução

Criar a sala, entrar pelo link e aprovar funcionaram. O convidado como Ouvinte via só a playlist visível, sem botões de adicionar. Três falhas apareceram:

- **A barra do convidado ficava vazia** enquanto ele tocava. A gravação mostrou os cliques em dobro. A diferença começou em 36 ms e caiu para 12,6 ms em cerca de 4 s, com p95 de 31,7 ms. O convidado tocava, mas a barra não mostrava a música da sala. Corrigido em `a680257`.
- **A segunda música parava em 2 MiB.** Os pedaços que chegam antes da oferta da transferência ficam guardados até 2 MiB. Os que passavam disso eram descartados sem aviso, e o arquivo nunca terminava. Agora o convidado grava o que guardou e pede o resto a partir do arquivo parcial (`0039112`). O envio de músicas ao host tinha o mesmo defeito (`1401c37`). A revisão achou um laço sem limite nesse novo pedido. Ele passou a respeitar `room_file_retries` (`75b0f81`).
- **Play no host com fila, mas sem item atual, não faz nada.** O comportamento é anterior à sala e ficou registrado para depois.

### Segunda execução

Feita com `75b0f81` e um perfil de convidado novo, com cache vazio. O convite deu ao convidado o cargo DJ.

| Passo | Resultado |
|---|---|
| Criar, aviso de IP, entrar, aprovar | ok |
| Barra do convidado | mostra a música, o tempo e o estado da sala |
| Tocar, pular e pausar pelo convidado | ok nos dois apps |
| Adicionar de playlist visível | ok |
| Enviar música própria | toca nos dois como "Tom 1 kHz". Antes de tocar, a fila do host mostra o hash do arquivo no lugar do título |
| Revogar "Adicionar da sala" e "Enviar músicas próprias" | os botões somem na hora no convidado |
| Fechar a sala | o convidado vê "O anfitrião encerrou a sala." A música enviada sai da fila do host, que mantém as próprias. Os envios do host são apagados |

**Sincronia:** 44 pares de cliques em 45 s. A diferença foi de 9,3 ms a partir de cerca de 4 s, com máximo de 9,8 ms, e se manteve depois de um pulo feito pelo convidado. Fica abaixo de 100 ms.

**Falha:** depois do fim da sala, o `room-cache/` do convidado ainda guardava a música que ele mesmo enviou. O desenho manda apagar o cache ao sair da sala. Era o item atual: o cache era limpo antes de o player soltar o arquivo, e o Windows não apaga arquivo aberto. Agora o convidado solta o player antes de limpar o cache. Arquivos que ainda não puderem ser apagados são tentados de novo por cerca de 10 s (`231f372`). Numa terceira rodada curta, o convidado enviou o tom e a sala foi fechada com ele tocando. O `room-cache/` do convidado e o `room-uploads/` do host sumiram. Na abertura do app, o arquivo que tinha sobrado da segunda execução também foi apagado.

## Reprodução

Rode a partir da raiz, com o ambiente MSVC carregado:

```powershell
python experiments/listening-room-sync/make_clicks.py out/product/listening-room-sync/media --mp3
cmake -S experiments/listening-room-sync -B out/product/listening-room-sync/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=<qt-network-sdk> -DQt6Multimedia_DIR=<qt-multimedia>/lib/cmake/Qt6Multimedia
cmake --build out/product/listening-room-sync/build
```

Rode `listening_room_sync.exe --media <pasta> --out <pasta> --scenarios a,b,c,s,e,e2,p,d --nudge 30 --seek 120`. Antes, ponha no `PATH` os `bin` do Qt e do Qt Multimedia e no `QT_PLUGIN_PATH` os `plugins` dos dois. O programa toca cliques nas caixas de som. Os resultados brutos ficam em `results.csv` e `raw/`, ignorados pelo Git.
