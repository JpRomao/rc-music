# Arquitetura do sistema

**Estado:** proposta de referência; candidatos condicionados ao M0 e a ADR explícito
**Stacks de experimento:** C++20 + Qt 6 Quick/QML (hipótese principal) e Rust + Tauri 2 + Svelte (desafiante A/B)
**Última revisão:** 2026-09-01

As invariantes de domínio, portas, concorrência, privacidade local e integração por capabilities são independentes da stack. Os mapeamentos Qt e Tauri deste documento são projeções experimentais dessas mesmas fronteiras para o M0, conforme NFR-STACK-001–006; nenhum deles representa uma stack aceita antes das medições e de ADR explícito.

## 1. Objetivos arquiteturais

A arquitetura deve:

- manter a thread de UI livre de trabalho bloqueante e impedir trabalho adicional do código próprio no callback de saída;
- permitir TDD do comportamento sem toolkit visual, dispositivo ou arquivo real;
- isolar Windows, Linux, codecs, metadados, armazenamento e fontes remotas;
- preservar um único estado autoritativo de reprodução;
- começar em um pacote simples e crescer somente quando necessário;
- tornar falhas locais e remotas recuperáveis;
- manter arquivos locais independentes de rede;
- permitir substituir UI ou backend de áudio sem reescrever o domínio.

Não é objetivo criar framework de plugins, microserviços, servidor local público, banco ou sincronização.

## 2. Contexto

```text
                    ┌──────────────────────────────┐
 usuário / teclado ─►                              ├──► dispositivo de áudio
 shell / tray ──────►       Desktop Player         ├──► arquivos locais
 SMTC / MPRIS ──────►                              ├──► preferências/sessão
                    └──────────────┬───────────────┘
                                   │ porta futura, desativada no MVP
                                   ▼
                           resolver de fonte remota
```

O aplicativo é uma instância desktop única do ponto de vista do usuário. A quantidade de processos internos do shell é detalhe medido, não assumido. A camada visual somente apresenta estado e envia intenções; não hospeda regras de domínio nem abre conteúdo remoto.

## 3. Componentes e mapeamentos dos candidatos

O fluxo invariante é:

`ui → boundary/presenter → application → domain/ports ← adapters`

### Hipótese principal: C++20 + Qt 6 Quick/QML

```text
QML / Qt Quick UI
        │ propriedades, sinais e comandos tipados
        ▼
Boundary / Presenter C++ com Qt
        │ tipos e DTOs C++ sem Qt
        ▼
Application C++ puro → Domain/Ports C++ puro ← Adapters
                                               ├─ Qt Multimedia/FFmpeg
                                               ├─ TagLib/filesystem
                                               ├─ persistência atômica
                                               └─ Qt/plataforma Windows/Linux
```

`QObject`, tipos QML e detalhes de sinais/slots terminam no boundary/presenter ou nos adapters. `domain`, `application` e `ports` não importam headers Qt e permanecem testáveis como C++ puro.

### Desafiante A/B: Rust + Tauri 2 + Svelte

```text
┌─────────────────────────────────────────────────────────────────┐
│ UI Svelte                                                       │
│ Views · componentes · acessibilidade · interpolação visual      │
└──────────────────────────────┬──────────────────────────────────┘
                               │ comandos tipados / snapshots
┌──────────────────────────────▼──────────────────────────────────┐
│ Boundary IPC / Presenter                                        │
│ valida entrada · mapeia erros · serializa DTOs · limita eventos │
└──────────────────────────────┬──────────────────────────────────┘
                               │
┌──────────────────────────────▼──────────────────────────────────┐
│ Application                                                     │
│ PlayerController · QueueService · SessionCoordinator            │
└──────────────────────────────┬──────────────────────────────────┘
                               │ usa
┌──────────────────────────────▼──────────────────────────────────┐
│ Domain                                                          │
│ PlaybackState · Queue · Track · Preferences · invariantes       │
└──────────────────────────────┬──────────────────────────────────┘
                               │ contratos
┌──────────────────────────────▼──────────────────────────────────┐
│ Ports                                                           │
│ Playback · Metadata · Session · Platform · SourceResolver       │
└─────────┬────────────┬───────────────┬───────────────┬──────────┘
          │            │               │               │
     Rodio/CPAL      Lofty/FS       JSON atômico    Win/Linux
                                                        │
                                                 resolver futuro
```

### Regra de dependência

- domínio não importa Qt, QML, Tauri, Svelte, Rodio, Lofty, TagLib, FFmpeg, Win32 ou D-Bus;
- adapters implementam contratos definidos no núcleo;
- DTOs da apresentação ou do IPC não são entidades do domínio;
- UI nunca acessa arquivos ou APIs privilegiadas diretamente;
- nenhum adapter chama componente visual.

## 4. Estrutura do produto após a decisão

Durante os spikes, ambos os candidatos ficam isolados pelo [layout M0-L1](../m0/layout.md) e não criam scaffold de produto na raiz. Após M0.10, a stack vencedora começa M1 como uma única unidade de build e distribuição organizada em módulos internos. Se o candidato Qt vencer, o mapeamento inicial é:

```text
/
├── agents.md
├── README.md
├── docs/
├── CMakeLists.txt
├── src/
│   ├── domain/                  # C++ puro; sem QObject/QML
│   ├── application/             # C++ puro; sem QObject/QML
│   ├── ports/                   # contratos C++ puros
│   ├── adapters/
│   │   ├── audio/
│   │   ├── metadata/
│   │   └── persistence/
│   ├── platform/
│   │   ├── windows/
│   │   └── linux/
│   ├── presentation/            # boundary/presenters C++ com Qt
│   ├── ui/                       # QML, componentes, temas e assets
│   └── main.cpp
├── tests/
│   ├── contract/
│   ├── integration/
│   ├── e2e/
│   └── fixtures/audio/
└── tools/
```

Se o desafiante Tauri vencer o M0, o mapeamento equivalente é:

```text
/
├── agents.md
├── README.md
├── docs/
├── src/                         # Svelte/TypeScript
│   ├── components/
│   ├── features/
│   ├── lib/
│   ├── state/
│   └── styles/
├── src-tauri/
│   ├── src/
│   │   ├── domain/
│   │   ├── application/
│   │   ├── ports/
│   │   ├── adapters/
│   │   │   ├── audio/
│   │   │   ├── metadata/
│   │   │   └── persistence/
│   │   ├── platform/
│   │   │   ├── windows/
│   │   │   └── linux/
│   │   ├── ipc/
│   │   ├── lib.rs
│   │   └── main.rs
│   └── tests/
├── tests/
│   ├── e2e/
│   └── fixtures/audio/
└── tools/
```

Extrair uma biblioteca, crate ou pacote separado somente quando pelo menos um destes casos existir:

- o núcleo precisa ser consumido por outro binário real;
- fronteira de compilação ou features por plataforma traz benefício mensurável;
- testes e dependências não conseguem permanecer isolados por módulos;
- o adapter possui ciclo de release próprio.

## 5. Modelo de domínio

### Tipos principais

| Tipo | Responsabilidade |
|---|---|
| `TrackId` | Identidade única de uma ocorrência na fila |
| `MediaSource` | Origem tipada; `LocalFile` no MVP |
| `Track` | Fonte, metadata conhecida e capacidades |
| `Queue` | Ordem, item atual e navegação por ID |
| `PlaybackState` | Estado autoritativo da reprodução |
| `PlaybackSnapshot` | Estado imutável entregue à apresentação |
| `Position` / `Duration` | Valores validados, nunca negativos |
| `PlaybackCapabilities` | Seek, duração, device switch e outras capacidades |
| `Preferences` | Tema, saltos, volume, tray e esquema versionado |
| `Session` | Fila, item, posição e estado restaurável |

### Invariantes

- fila referencia o atual por `TrackId`, nunca por índice persistido;
- cada ocorrência tem ID próprio, mesmo para o mesmo path;
- posição está entre zero e duração conhecida;
- volume lógico está entre 0 e 1;
- valores de salto estão entre 1 e 300 s;
- estado `Playing` exige uma fonte carregada e reproduzível;
- sessão restaurada inicia `Paused` ou `Empty`, nunca `Playing`;
- erro de um item não invalida a fila inteira;
- capabilities vêm do backend/fonte e controlam a UI.

## 6. Máquina de estados

```text
Empty ──load──► Loading ──ready──► Ready ──play──► Playing
  ▲                │                 ▲  │            │
  │                ├──error──► Failed  └──pause──────┘
  │                │                 │               │
  └────clear───────┴─────────────────┴──end/clear────┘
                                      │
                                      └──next──► Loading
```

`Seeking` não precisa ser estado global de playback. A aplicação mantém uma operação de seek pendente; a UI mantém `SeekPreview` local. Assim pausa/tocando continuam representados corretamente.

Transições inválidas retornam erro tipado e não alteram o snapshot. Eventos atrasados carregam geração/ID para serem descartados.

## 7. Portas

Assinaturas finais serão escritas por TDD, mas os contratos conceituais são:

### `PlaybackBackend`

- carregar uma fonte local e retornar capacidades;
- play, pause e unload;
- seek absoluto assíncrono com sucesso/erro;
- alterar volume/mudo e, futuramente, dispositivo;
- produzir eventos de ready, position, ended, device-lost e error;
- encerrar de forma idempotente.

### `MetadataReader`

- ler tags e propriedades com cancelamento;
- impor limites de arte e texto;
- diferenciar ausente, inválido e não suportado;
- não depender de playback.

### `SessionStore`

- carregar a última sessão válida ou o snapshot vazio default;
- salvar somente a sessão em arquivo próprio, atomicamente; preferências pertencem ao `PreferencesStore`;
- migrar formatos conhecidos;
- manter backup limitado da última gravação válida;
- preservar versões futuras contra downgrade ou sobrescrita sem confirmação explícita.

### `PlatformIntegration`

- tray e visibilidade da janela;
- distinguir suporte da plataforma e disponibilidade momentânea da página de apps padrão, e abri-la por operação tipada sem aceitar URI, path ou ProgID da UI;
- concluir cada solicitação uma única vez na thread que iniciou a operação, distinguindo `Dispatched`, `Unavailable` e `Failed`; `Dispatched` comprova somente o handoff ao handler registrado, nunca que a página ficou visível ou que um padrão mudou;
- publicar capacidades disponíveis.

### `SystemMediaControls`

- publicar título, artista, álbum, estado, capacidades, posição e duração;
- receber comandos distintos de play, pause, anterior, próxima e seek;
- entregar callbacks na thread de aplicação/UI que instalou o handler;
- expor disponibilidade e shutdown idempotente, falhando fechado quando a integração nativa desaparece.

### `SystemPowerEvents`

- publicar somente as transições tipadas `Suspending` e `Resumed`, sem transportar mensagens, handles ou códigos nativos para o núcleo;
- entregar notificações sincronamente na thread de aplicação/UI que instalou o handler;
- expor disponibilidade, substituição explícita do handler e shutdown idempotente;
- manter o receptor nativo privado e estável separado da janela visual, com ciclo de vida encerrado somente no shutdown;
- limitar retries de inscrição nativa por backoff finito e permitir reativação somente por ação explícita ou mudança observada do estado da aplicação.

### `MediaSourceResolver`

- converter origem solicitada em descriptor reproduzível;
- MVP aceita somente arquivo local canônico;
- implementação remota futura retorna URL, headers, validade e capabilities;
- resolver é cancelável, limitado por timeout e nunca executa string de shell.

## 8. Comandos, eventos e boundary de apresentação

Comandos externos entram por uma única fachada de aplicação:

```text
OpenSources, Play, Pause, TogglePlayback,
SeekTo, SkipBy, Previous, Next,
SetVolume, ToggleMute,
QueueAdd, QueueMove, QueueRemove, QueueClear,
UpdatePreferences, ShowWindow, Exit
```

Os mesmos comandos são usados por UI, tray, atalhos, SMTC/MPRIS e segunda instância. Isso impede regras divergentes. Play e Pause permanecem comandos distintos nas fronteiras externas: eventos declarativos do sistema operacional não são convertidos em toggle.

Eventos para a UI são snapshots ou notificações semânticas:

- `PlaybackChanged(snapshot)`;
- `QueueChanged(snapshot)`;
- `MetadataUpdated(track_id, metadata)`;
- `PreferenceChanged(snapshot)`;
- `RecoverableError(error_dto)`;
- `PlatformCapabilityChanged(capabilities)`.

No candidato Qt, o presenter C++ traduz propriedades/sinais QML para esses comandos e snapshots sem expor `QObject` ao núcleo. No desafiante Tauri, a mesma fachada atravessa IPC tipado. Regras comuns:

- validar tamanho, tipo e intervalo de todo payload na borda;
- não expor command genérico para executar path/URL;
- agrupar ticks de posição a 5–10 Hz;
- incluir `track_id`/geração em respostas assíncronas;
- manter DTOs pequenos; capa é servida por cache ou protocolo controlado pela apresentação, não copiada integralmente a cada evento;
- aplicar backpressure/coalescing a posição, nunca perder eventos ended/error.

## 9. Modelo de concorrência

O desenho de workers é invariante e independe de como cada toolkit implementa processos ou threads internos.

```text
UI thread
    │ commands                     snapshots/eventos
    ▼                                      ▲
Boundary/presenter / application coordinator
    │                                      │
    ├──► Audio worker (dono do stream, sink e decoder)
    ├──► Metadata/file worker pool
    └──► Atomic persistence worker
```

No candidato Qt, `QML/GUI thread → presenter C++ → application coordinator`. No desafiante Tauri, `WebView/UI thread → IPC Tauri → application coordinator`. A árvore de processos pode diferir, mas ownership, cancelamento, backpressure e estado autoritativo obedecem ao mesmo contrato.

- exatamente um worker é dono do dispositivo/engine de áudio;
- código próprio chamado no callback de saída não loga, não chama UI, não toma lock longo e não adiciona I/O. O M0 mede underruns, latência e comportamento real de Qt Multimedia/FFmpeg e Rodio/CPAL/Symphonia separadamente; se um backend bloquear de modo relevante, seu adapter ganha prefetch/ring buffer limitado ou é rejeitado;
- metadata, arte, diretórios e hash/cache nunca rodam na thread visual;
- comandos usam canal limitado; posição pode ser coalescida;
- operações carregam cancellation token/geração;
- shutdown para novas entradas, salva snapshot, encerra adapters e aguarda apenas dentro de timeout definido.

Evitar mutex global contendo todo o estado. O controller serializa transições; snapshots imutáveis são compartilhados para leitura.

## 10. Progresso e seek

O engine é autoridade para posição. A UI mantém:

- posição confirmada;
- instante monotônico do snapshot;
- estado play/pause;
- preview local enquanto o usuário arrasta.

Enquanto toca, a UI interpola entre snapshots, limitada pela duração. Durante drag, ticks não movem o thumb. Soltar envia `SeekTo`; confirmação reconcilia a posição. Falha restaura o confirmado e oferece feedback.

Saltos são `SeekTo(clamp(position ± preference))`, compartilhados por botão, teclado e controles do sistema. Não existem implementações paralelas na UI.

## 11. Metadata, arte e cache

Pipeline:

1. validar/canonizar path sem perder a forma adequada para exibição;
2. ler `size` e `mtime`;
3. extrair tags/propriedades em worker;
4. validar texto, dimensões e bytes máximos da arte;
5. gerar thumbnail em cache se necessário;
6. publicar resultado somente se `TrackId` e geração ainda forem válidos.

Chave de cache proposta: path canônico + tamanho + mtime. Cache é otimização descartável, com limite de tamanho e LRU simples. Não usar hash integral de arquivos grandes na jornada de abertura.

## 12. Fila e persistência

Um arquivo JSON de preferências e outro de sessão simplificam migração e recuperação. Formato conceitual:

```json
{
  "schemaVersion": 1,
  "theme": "system",
  "motion": "system",
  "volume": 0.8,
  "muted": false,
  "skipBackwardSeconds": 5,
  "skipForwardSeconds": 5,
  "closeBehavior": "minimize_to_tray",
  "restoreLastSession": true,
  "startMinimized": false,
  "queueEndBehavior": "stop_keep_selection"
}
```

O schema acima está materializado no adapter experimental Windows. A porta, o codec, o store Windows, a coordenação assíncrona e a restauração pausada no presenter/bootstrap dos recortes de M4.1 são descritos abaixo; M4.4 integra o efeito local de `startMinimized` depois da restauração do placement. `PreferencesStore` não substitui `SessionStore`.

Em M4.2, a evolução funcional preserva o schema v1 acima como formato conhecido de leitura e passa a emitir sempre o schema v2:

```json
{
  "schemaVersion": 2,
  "theme": "system",
  "motion": "system",
  "volume": 0.8,
  "muted": false,
  "skipBackwardSeconds": 5,
  "skipForwardSeconds": 5,
  "closeBehavior": "minimize_to_tray",
  "restoreLastSession": true,
  "startMinimized": false,
  "queueEndBehavior": "stop_keep_selection",
  "windowPlacement": {
    "x": -1920,
    "y": 40,
    "width": 960,
    "height": 640
  }
}
```

`Preferences` acrescenta `std::optional<WindowPlacement>` ao fim do snapshot, e `windowPlacement` pode ser o objeto mostrado ou `null`. O decoder reconhece v1 como `Valid` com placement ausente, sem exigir a chave nova nem promover ou reescrever o documento durante `load()`. Em v2, a chave é obrigatória e aceita somente `null` ou quatro inteiros representáveis por `int`, com dimensões positivas; v3 ou superior é classificado como versão futura antes da leitura dos demais campos. A tolerância a chaves desconhecidas, o limite de 64 KiB e a substituição atômica do store permanecem.

A decisão de restaurar é uma policy pura, sem dependência de Qt: recebe o placement salvo, as áreas de trabalho conectadas e o tamanho mínimo `720 × 520`. O adapter fornece cada `QScreen::availableGeometry()` em DIPs e na ordem observada; a policy normaliza largura e altura ao mínimo, aceita coordenadas negativas, faz os limites em `int64_t` com rejeição de overflow e só devolve placement e índice quando o retângulo inteiro cabe em uma área válida individual. Áreas adjacentes nunca são tratadas como uma união virtual. Dados inválidos, nenhuma tela ou ausência de contenção integral produzem fallback, inclusive quando uma tela foi removida ou a área disponível mudou com o DPI.

O root QML nasce oculto. No bootstrap, a aplicação resolve o placement, chama `setScreen()` e depois `setGeometry()` antes de liberar a revelação da janela. Durante o uso, o adapter captura somente geometria de janela visível em estado normal, coalesce rajadas de mudanças e entrega o último placement ao autosave de preferências. A amostra pendente é drenada antes de ocultar, minimizar ou sair pelos casos de uso. Como o backend Windows pode publicar a geometria expandida antes do novo estado, uma transição nativa para minimizado, maximizado ou tela cheia cancela qualquer candidato que ainda não estabilizou; o último placement normal já persistido permanece autoritativo, e o retorno a normal agenda nova captura. Assim, estados oculto ou não normais não substituem a última geometria normal válida.

Um restart instrumentado entre dois processos exercita a persistência no primeiro processo e a restauração antes da revelação no segundo. Essa prova continua local e instrumentada: pacote/nativo, `CHK-WIN`, `CHK-E2E`, `CHK-CLOSE` e cenários físicos de DPI/múltiplos monitores permanecem abertos. Portanto, este incremento não marca M4.2 globalmente concluída.

O fechamento automatizado do recorte passou 225/225 no CTest Release combinado e 183/183 num core fresco sem Qt com MSVC 19.44/toolset 14.44. O alvo Quick passou 1/1 com 100 casos, `all_qmllint` ficou verde e o formato cobriu 112 arquivos C++ e 46 QML. Como `qmlformat` 6.11.2 não oferece `--check`, a validação QML comparou sua saída padrão normalizada em LF. A composição `BUILD_TESTING=OFF` foi recompilada, listou zero testes e rejeitou o hook reservado de restart de preferências com exit 1 sem criar a raiz solicitada. A validação documental passou com 44 Markdown, 83 requisitos, 17 checks e 76 etapas; nenhum benchmark foi executado.

O schema v1 separado da sessão é:

```json
{
  "schemaVersion": 1,
  "queue": {
    "items": [
      {
        "occurrenceId": "9007199254740993",
        "sourcePath": "C:\\Música\\Abertura 日本語.mp3"
      }
    ],
    "currentOccurrenceId": "9007199254740993"
  },
  "positionMs": "123456"
}
```

`queue` é uma unidade porque itens e seleção compartilham a mesma invariável. Sem seleção, `currentOccurrenceId` é `null` e `positionMs` precisa ser `"0"`. IDs e posição usam strings ASCII decimais canônicas para preservar exatamente inteiros acima de `2^53`; sinais, espaços, zeros à esquerda, expoentes, overflow e dígitos não ASCII são inválidos. Paths Unicode são preservados literalmente, sem canonicalização nem consulta ao filesystem no codec. Chaves extras são toleradas, mas todas as chaves v1 são obrigatórias.

`session.json` tem limite inclusivo de 16 MiB. O codec rejeita entrada maior antes do parse e não produz bytes se o snapshot for inválido, contiver path com `U+0000` ou exceder o limite serializado. Versão futura dentro do limite é classificada antes da leitura dos campos atuais. Os três primeiros recortes de M4.1 materializaram snapshot/hidratação, porta/codec v1 e store Windows; o quarto acrescentou writer/autosave no core C++ puro; o quinto integrou startup e presenter; o sexto acrescentou os cenários destrutivos locais e a contenção multiprocesso. Depois das preferências, o bootstrap chama o store de sessão somente se `restore_last_session=true`; quando a opção está desligada, fornece snapshot vazio sem ler `session.json`.

Antes de expor o estado ao QML, o presenter hidrata a fila e abre a ocorrência selecionada com a intenção `RestorePaused`, que nunca chama play. O seek restaurado é vinculado à geração aberta, espera duração conhecida quando o destino é positivo e aceita confirmação do backend dentro de 50 ms; trocar de geração descarta a restauração pendente. Uma marca de EOF da mesma geração sobrevive ao snapshot pausado e impede que o `Ended` relacionado avance ou inicie outra faixa. Alterações estruturais alimentam o autosave coalescido, mas a posição persistida vem somente de snapshots confirmados pelo backend. Retry, substituição explícita de versão futura e recuperação de timeout ficam no boundary existente; alternar restauração de `false` para `true` força uma gravação estrutural mesmo quando o snapshot corrente é vazio, reparando ou substituindo estado antigo autorizado.

Na recuperação local integrada, um primário inválido com backup válido hidrata a sessão pausada e publica `RecoveredFromBackup`. Se o replace primário falhar de verdade por sharing Windows, o resultado continua falho e o aviso expõe a ação QML de retry; depois que a contenção externa desaparece, essa ação regrava um primário válido sem autoplay. Se primário e backup forem inválidos, o bootstrap usa snapshot vazio, publica `UsingDefaults` e permite dispensar o aviso sem reescrever silenciosamente os documentos corrompidos.

Gravação:

1. serializar para arquivo temporário no mesmo diretório;
2. flush e, quando apropriado, sincronizar;
3. substituir atomicamente o destino;
4. manter no máximo um backup válido;
5. nunca apagar o arquivo bom antes de validar o novo.

O adapter Windows usa `session.json` e um único `session.json.bak` na raiz absoluta de configuração. `QSaveFile` grava no mesmo diretório com fallback direto desabilitado, e um `QLockFile` cooperativo envolve inspeção, rotação e commits; contenção falha imediatamente. A leitura nunca repara nem regrava. Sua precedência é: primário futuro, primário válido, backup futuro, backup válido, ambos ausentes e, por fim, defaults para arquivos inválidos ou ilegíveis. No save, arquivo ilegível falha fechado antes de qualquer tentativa de escrita, e qualquer versão futura bloqueia a operação sem confirmação explícita. A contenção é exercitada entre PIDs em `BeforeBackupCommit` e `AfterPrimaryCommit`: `getLockInfo()` identifica o holder, um writer concorrente retorna `Failed` sem mudar os slots e, depois da liberação, o holder e uma nova gravação terminam com o lock removido.

Na rotação comum, os bytes exatos do primário válido anterior são commitados no backup antes do novo primário. Quando o backup é futuro e a substituição foi confirmada, ele permanece intacto até o novo primário ser commitado; só então recebe os bytes exatos do primário anterior ou, quando este não era válido, o novo documento. `Failed` pode representar falha antes ou depois de um commit e não garante disco inalterado; o writer deve reconciliar por `load()` e manter a autorização explícita nos retries correspondentes. Além dos fault points e handles Windows, o fluxo local agora encerra à força o processo real em `BeforePrimaryCommit`, depois de verificar o primário anterior, o backup byte a byte e o PID proprietário do lock. O próximo save recupera o snapshot e remove o lock obsoleto, e o relançamento restaura a sessão pausada. Isso prova terminação de processo nesse checkpoint, não perda física de energia, durabilidade sob falha de hardware ou a jornada empacotada/nativa.

A consolidação automatizada final desse sexto recorte passou 214/214 no CTest Release combinado, 172/172 num core fresco sem Qt com MSVC 19.44/toolset 14.44 e 1/1 no alvo Quick final, que cobriu 99 casos. `all_qmllint` ficou verde. O formato cobriu 109 arquivos C++ e 46 QML; para estes, `qmlformat` 6.11.2 foi conferido pela saída padrão normalizada em LF porque não oferece `--check`. A composição `BUILD_TESTING=OFF` foi recompilada, expôs zero testes e rejeitou o hook reservado de crash com exit 1 sem criar a raiz solicitada. A validação documental passou com 44 Markdown, 83 requisitos, 17 checks e 76 etapas. Nenhum benchmark foi executado. Essa evidência continua local/instrumentada: pacote/nativo, `CHK-E2E`, M4.1 e os demais gates contratuais permanecem abertos.

O `SingleWorkerSessionWriter` possui uma única thread e mantém `save()` e a eventual reconciliação fora do caller. Cada `SessionRevision` identifica de forma indivisível o par snapshot + autorização `replace_future_version`; a mesma revisão não pode representar outro conteúdo ou outra autorização. Há no máximo uma gravação ativa, e `flush()` pode deixar somente a revisão solicitada mais recente atrás dela. A completion devolvida por `flush()` é consumida nesse caminho e não pode reaparecer em `poll()`.

Se `save()` retornar `Failed` ou lançar, o mesmo worker executa `load()` sem promover a operação a sucesso. O readback tri-state registra `true` apenas para `Loaded`/`RecoveredFromBackup` exatamente igual ao snapshot solicitado, `false` para resultado observado divergente ou status não recuperável e ausência de valor quando o próprio `load()` lança. `Saved` e `BlockedByFutureVersion` não reconciliam. Mesmo com readback `true`, o status permanece `Failed`, a sessão continua suja e a autorização de substituição permanece nos retries até um `Saved`, pois um primário legível ainda pode ocultar backup ou limpeza incompletos.

Chamadas concorrentes de `shutdown()` são serializadas, drenam uma gravação já aceita e fazem join uma única vez de forma idempotente. Esse join continua sem deadline: se o store síncrono não retornar, o encerramento pode permanecer bloqueado e a futura composição visual não pode tratá-lo como operação limitada por tempo.

O `SessionAutosave` mantém deadlines independentes e escolhe sempre o mais próximo. Mudanças estruturais usam debounce trailing de 500 ms, limitado a 5 s desde a primeira mudança estrutural ainda não aceita. Posição confirmada altera somente o escalar `position_ms` em O(1); o primeiro tick arma um checkpoint fixo de 10 s e ticks posteriores não o adiam. Preview de seek, interpolação visual e ticks não confirmados pelo backend nunca alimentam o snapshot persistido.

`checkpointConfirmedPosition()` e `requestSaveNow()` tornam o estado sujo imediatamente elegível. Na integração atual, seek confirmado, pausa, parada/fim, erro terminal, saída e a transição `Suspending` usam esses gatilhos somente depois de obter a posição confirmada. O caminho de suspensão inicia a escrita assíncrona, sem `flush()` bloqueante e sem comandar pause, play ou autoplay; por isso ele reduz a janela de perda, mas não garante que o replace já esteja durável antes de o sistema congelar o processo. Após `Failed` ou bloqueio por versão futura da revisão atual, deadlines automáticos são removidos: alterações posteriores continuam coalescidas no snapshot/revisão mais recentes, mas apenas retry explícito, confirmação explícita de substituição, save-now ou flush rearmam a escrita. Completion obsoleta nunca limpa a revisão atual.

Salvar por debounce após mudanças importantes e no shutdown. A fila preserva paths ausentes como itens indisponíveis para que o usuário possa localizar ou remover. SQLite só entra com biblioteca indexada e busca que comprovem necessidade.

## 13. Instância única e abertura pelo shell

Sequência de startup:

1. capturar argumentos como valores nativos, sem reparse de string;
2. adquirir instância única cedo;
3. se secundária, encaminhar lista tipada e ativar a principal, então sair;
4. principal carrega preferências e, somente quando `restore_last_session=true`, a sessão pausada;
5. inicializa engine e integrações;
6. processa argumentos novos conforme política de fila;
7. resolve a disposição inicial depois do placement: fonte explícita mostra/restaura; sem fonte, a preferência só oculta quando a tray está disponível;
8. mostra, oculta ou restaura a janela conforme essa policy.

Cobrir paths Unicode, aspas, múltiplos arquivos, chamadas simultâneas e diretório de trabalho diferente. O canal local entre instâncias deve aceitar somente mensagens esperadas e impor limites; IPC adicional do shell, quando existir, segue as mesmas regras.

No recorte funcional local de M4.3 do candidato Qt/Windows, a eleição usa um `QLockFile` na raiz de configuração do usuário e um `QLocalServer` nomeado pelo PID que solicita `UserAccessOption`. A instância secundária serializa somente o comando fechado `openSources` v1 (`version`, `command`, `paths`), tenta transferir a permissão de foreground ao PID proprietário como operação best-effort, aguarda ACK/NACK dentro de um prazo limitado e nunca inicializa o backend de áudio. A principal restaura/ativa a janela e entrega os mesmos paths ao caso de uso tipado de importação. A ACL efetiva entre usuários continua um check nativo de segurança, não uma conclusão desta implementação local.

O limite inclusivo é de 1.000 paths e 1 MiB do JSON UTF-8 serializado; o prefixo big-endian de quatro bytes do frame não participa desse MiB. A política é rejeitar o lote inteiro, nunca dividi-lo silenciosamente. A fila de bootstrap aceita no máximo 16 comandos e 4 MiB, e o servidor limita simultaneamente conexões ativas e o buffer individual; frames incompletos expiram. Antes do IPC, a instância invocada resolve paths relativos e rooted-only contra seu próprio diretório/volume e transmite somente paths de filesystem plenamente qualificados. Paths `:/` de recursos Qt, relativos a drive, valores com NUL, namespaces `\\.\`/`\\??\` de dispositivo e prefixos extended não canônicos são recusados.

ACK significa que o comando passou pela validação e ficou sob responsabilidade da instância principal. Ao iniciar uma saída, a principal pausa novos ingressos e drena os comandos já confirmados, inclusive a importação ativa pertencente ao dispatcher, antes do checkpoint final; uma importação local da UI sem ownership do IPC mantém a semântica anterior de cancelamento na saída. Se o prazo de saída expirar, a intenção é cancelada e a aplicação volta a aceitar trabalho, em vez de descartar o lote. Depois do commit da saída, o NACK muda de indisponibilidade transitória para encerramento definitivo.

O fechamento automatizado local passou 229/229 no CTest Release combinado. O teste de processo exerceu dois ACKs, incluindo um path Windows comum acima de 260 caracteres, NACK transitório durante a drenagem, persistência dos dois itens e um único backend. `m0_qt_shell_ui_qmllint`, formato de 122 C++/46 QML e validação documental ficaram verdes. A composição `BUILD_TESTING=OFF` foi recompilada, expôs zero testes, rejeitou o hook reservado com exit 1 sob o runtime autenticado e preservou `longPathAware=true` no manifesto. Como `qmlformat` 6.11.2 não possui `--check`, a conferência QML comparou a saída padrão normalizada em LF. Nenhum benchmark foi executado.

## 14. Integração com plataforma

### Windows

- tray e menu;
- SMTC/metadata/teclas de mídia;
- eventos de sessão/power;
- página oficial de default apps;
- associações por configuração/template de instalador próprio, com ProgID, `Capabilities` e `RegisteredApplications` por usuário; não confiar somente no default do empacotador;
- high contrast, reduced motion e UI Automation pela integração de acessibilidade do toolkit escolhido, validados em build empacotado;
- assinatura futura do executável/instalador.

### Linux

- MPRIS por D-Bus;
- StatusNotifier/tray quando ambiente suportar;
- XDG `.desktop`, ícones, MIME e diretórios de dados;
- teste Wayland e X11 na distribuição de referência;
- fallback visível quando tray estiver ausente.

Cada adapter publica capabilities. A aplicação não finge que uma integração indisponível funcionou.

No recorte funcional local de M4.4, a policy de startup é explícita. Depois de aplicar tela/geometria, `startMinimized=false` mostra a janela. Com a opção ligada e tray disponível, o root continua oculto antes da entrada no event loop; sem tray, ele é revelado e um aviso explica o fallback. Uma fonte inicial explícita prevalece sobre a preferência, e um comando `openSources` por IPC restaura/ativa a principal que iniciou oculta. O menu da tray usa ações reais para mostrar/ocultar/mostrar. O toggle de início minimizado permanece utilizável para desligar a opção depois que a capacidade de tray desaparece.

`QtShellPlatform` não deduz intenção a partir de `QWindow::isVisible()`: um estado interno `hidden_to_tray` é armado apenas por ocultação explícita, limpo por restauração/rebind e é o único que ativa a vigilância da capacidade. Assim, a janela invisível durante o bootstrap não é minimizada por uma falsa perda de tray. Se a capacidade desaparecer enquanto `hidden_to_tray` está ativo, o adapter interrompe a vigilância e recupera a janela como visível/minimizada.

A saída do presenter permanece transacional também em relação ao áudio. Novos ingressos são pausados; trabalho de fonte já ACKado é drenado; preferências e sessão são persistidas; só então o controller executa `PlaybackBackend::shutdown()` uma vez e, depois, solicita a saída da plataforma. Repetir a intenção depois do commit é inerte. Falha ou timeout antes do commit cancela a saída, mantém o backend ativo e restaura a aplicação alcançável.

Os Reds do recorte exigiram a nova policy de disposição, ownership explícito da ocultação, toggle recuperável, precedência de fonte e a ordem shutdown → saída; os Greens focados antecederam a consolidação. O CTest Release combinado passou 229/229 e o core Qt OFF 183/183; `m0_qt_shell_ui_qmllint` ficou verde e o formato cobriu 122 C++/46 QML, com `qmlformat` validado por comparação da saída padrão normalizada em LF. A composição `BUILD_TESTING=OFF` foi recompilada, listou zero testes e rejeitou os dois hooks M4.4 com exit 1 sem criar a raiz solicitada. A validação documental passou com 44 Markdown, 83 requisitos, 17 checks e 76 etapas; nenhum benchmark foi executado.

Essa evidência não encerra M4.4. Pacote/desktop e tray nativos, primeiro frame perceptível sem flash, fallback Linux real, restart do Explorer, NVDA/acessibilidade nativa e `CHK-WIN`, `CHK-LINUX`, `CHK-E2E`, `CHK-A11Y` e `CHK-CLOSE` permanecem abertos.

No recorte funcional local de M4.5, `SystemMediaControlsBinding` projeta o estado do presenter na porta separada `SystemMediaControls`: título, artista, álbum, status, capacidades, posição confirmada e duração. Snapshots iguais não são republicados; seek permanece desabilitado durante preview ou enquanto aguarda confirmação. Play, Pause, Previous, Next e Seek retornam aos mesmos casos de uso do presenter usados por UI, atalhos locais e tray. Descarregar, entrar em erro ou sair publica `Closed`; no teardown, o binding remove o handler e encerra a porta.

O transporte distingue a intenção explícita desejada do comando ainda em voo. Repetir a mesma intenção é inerte; uma intenção oposta antes do ACK é preservada e emitida somente após a confirmação obsoleta, sem duplicação. Navegação usa esse estado efetivo, e troca de geração, estado terminal ou EOF confirmado encerram a intenção pendente. Assim, eventos Play/Pause declarativos do sistema não herdam a ambiguidade de um toggle nem perdem a solicitação mais recente por atraso do backend.

No Windows, `WindowsSmtcAdapter` obtém os controles da janela pelo HWND real com C++/WinRT e `ISystemMediaTransportControlsInterop::GetForWindow`. A boundary publica status, metadata, botões e timeline, desabilita explicitamente operações não suportadas, entrega callbacks na thread da UI e degrada para indisponível sem deixar exceções atravessarem a fronteira. Destruição da `QPlatformSurface` libera o binding nativo; recriação válida faz rebind e republica o snapshot mais recente, enquanto geração e presença da surface impedem um rebind enfileirado obsoleto. `Closed` limpa o `DisplayUpdater` e desabilita os controles; shutdown revoga handlers e torna callbacks tardios inertes.

Os três alvos focados `m0.qt.system-media-controls.binding`, `m0.qt.windows.smtc` e `m0.qt.windows.smtc-native` ficaram verdes. O smoke nativo, sob QPA Windows, consultou externamente os controles do HWND e comprovou estado inicialmente desabilitado, status Playing, flags de transporte, título/artista/álbum e limpeza em `Closed`. A consolidação Release passou 239/239 no CTest combinado e 190/190 no core Qt OFF. `m0_qt_shell_ui_qmllint` ficou verde; nenhum QML mudou. A composição `BUILD_TESTING=OFF` recompilou, listou zero testes e rejeitou o hook reservado com exit 1. Nenhum benchmark foi executado.

Essa evidência não sintetiza uma tecla de mídia física, não percorre a jornada empacotada e não implementa MPRIS no Linux. Portanto M4.5, `CHK-WIN`, `CHK-LINUX`, `CHK-E2E` e `CHK-CLOSE` permanecem abertos globalmente.

No primeiro recorte funcional local de M4.6, `PlatformIntegration` expõe separadamente `defaultAppsSettingsSupported`, que informa se a operação pertence à plataforma, e `defaultAppsSettingsAvailable`, que representa a disponibilidade momentânea do handler, além da operação fechada `openDefaultAppsSettings`. Seu resultado tipado é `Dispatched`, `Unavailable` ou `Failed`. O presenter projeta essa fronteira nos estados somente leitura `Idle`, `Opening`, `Dispatched`, `Unavailable` e `Failed`, notificados pelo sinal dedicado `defaultAppsSettingsChanged`: recusa uma segunda abertura enquanto a primeira está em voo, usa geração para invalidar conclusão tardia e torna callbacks inertes depois da destruição ou do commit efetivo da saída. Uma saída ainda reversível por drenagem/persistência não descarta prematuramente a conclusão; se a saída falhar e for cancelada, a ação volta a ficar utilizável. A conclusão é reenfileirada para a thread da UI e exceções da fronteira degradam para falha recuperável.

No Windows, `QtShellPlatform` mantém internamente a única URI permitida, `ms-settings:defaultapps`, e a entrega a `QDesktopServices::openUrl`; QML não fornece nem altera esse valor. O retorno verdadeiro vira `Dispatched` apenas para indicar que o handoff ao handler registrado foi aceito. Retorno falso, exceção, operação ausente ou saída já iniciada produzem estado fechado e explícito, sem inferir que as Configurações abriram ou que qualquer associação foi modificada.

O painel Geral oferece no Windows um botão com nome e descrição acessíveis, foco de teclado e motivo de indisponibilidade. Quando a plataforma não suporta a operação, botão e feedback não são renderizados. Falha e indisponibilidade aparecem como texto localizado; o alerta só é anunciado assertivamente enquanto o painel está aberto e visível, sem fala tardia quando fechado, e a falha inclui um caminho manual pelas Configurações do Windows.

Os Reds focados exigiram a porta/estado ainda ausentes e os contratos do adapter e da UI. Os três Greens focados passaram individualmente: `m0.qt.shell-platform` 1/1, `m0.qt.presenter.initial-load` 1/1 e `m0.qt.quick.shell-states` 1/1. A consolidação passou 239/239 no CTest Release combinado, 190/190 no core Qt OFF e 1/1 no Quick com 100 casos internos; `m0_qt_shell_ui_qmllint`, formato de 131 C++/46 QML, composição `BUILD_TESTING=OFF` com zero testes e hook reservado rejeitado, e validação documental de 44 Markdown/83 requisitos/17 checks/76 etapas ficaram verdes. Nenhum benchmark foi executado neste recorte.

Esse recorte não escreve `UserChoice`, não cria ProgID, `Capabilities` ou `RegisteredApplications`, não publica uma matriz de formatos, não instala associações, não comprova double-click/Explorer ou pacote e não implementa equivalente Linux. M4.6 e os checks nativos, de pacote, E2E, acessibilidade e Linux continuam abertos globalmente.

No primeiro recorte funcional local de M4.7, suspensão e retomada deixam `PlatformIntegration` e passam pela porta separada `SystemPowerEvents`. No Windows, `WindowsPowerEvents` cria na thread da UI um `QWindow` privado, invisível e exclusivo para receber notificações. Seu `HWND` real é independente da janela visual e permanece estável enquanto a integração está ativa. `RegisterSuspendResumeNotification(..., DEVICE_NOTIFY_WINDOW_HANDLE)` fica registrado nesse receptor até shutdown. O filtro aceita somente `WM_POWERBROADCAST` para esse mesmo handle: `PBT_APMSUSPEND` produz `Suspending` e `PBT_APMRESUMEAUTOMATIC` produz `Resumed`. A entrega é síncrona na thread da UI; cada mensagem reconhecida é consumida com resultado `TRUE` antes de devolver controle ao QPA, inclusive quando a transição consecutiva é deduplicada, o callback lança ou encerra a integração durante a entrega. Eventos não reconhecidos, outro `HWND` e qualquer mensagem recebida após shutdown falham fechados e retornam `false`. A porta entrega exclusivamente essas transições nativas. Transições consecutivas iguais são deduplicadas sobre o registro contínuo, mas um `Resumed` inicial continua válido para reconciliar um suspend que o processo não observou.

Uma falha inicial de `RegisterSuspendResumeNotification` mantém o receptor privado e aplica backoff coarse single-shot finito de 1 s, 5 s e 30 s. Se a tentativa posterior aos 30 s também falhar, o timer para; o orçamento só reinicia por `start()` explícito ou por um `applicationStateChanged` observado enquanto a integração continua indisponível. Shutdown cancela qualquer retry, remove o filtro, desfaz o registro quando presente e destrói o receptor, tornando timeout e callback tardios inertes. Criar, destruir ou recriar a surface da janela visual não afeta o `HWND` privado nem interrompe o registro. O binding remove o handler antes de encerrar a porta. A fronteira Win32 fica isolada no adapter e ligada a `User32`; o core recebe somente o enum tipado.

Ao receber `Suspending`, o presenter captura a última posição confirmada da geração corrente, solicita `requestSaveNow()` e deixa inerte o polling funcional até a retomada; polling ainda necessário à persistência assíncrona pode continuar. A integração de power não chama `flush()`, `pause()`, `play()` nem cria autoplay. Em `Resumed`, o presenter atualiza capabilities da plataforma e consulta o snapshot atual do backend; quando a geração ainda corresponde à sessão ativa, reconcilia estado, posição e erro pelos mesmos contratos de playback. O evento de retomada também rearma o polling aplicável. Transições duplicadas e eventos após o commit da saída são inertes.

Esse é apenas o recorte de suspend/resume de `FR-038`. Os testes registram uma janela real e exercitam a boundary sem suspender fisicamente a máquina; não cobrem Modern Standby, hibernação, ordem/delay reais do sistema nem garantem que o save assíncrono se tornou durável antes do congelamento. Perda de dispositivo, drive desconectado, arquivo removido, restart do Explorer, pacote, E2E nativo e Linux permanecem pendentes; M4.7 continua parcial. Nenhum benchmark pertence a este recorte.

O segundo recorte de M4.7 mantém a recuperação de fonte dentro das fronteiras existentes. A UI solicita `retryQueueOccurrence(ID)`; o presenter valida a ocorrência indisponível e resolve seu path na fila interna, sem aceitar outro path do QML. A aceitação do novo load precede a mudança de seleção. `LocalFileQueue::selectUnavailableForRetry` não altera a disponibilidade, e `markAvailable` somente confirma a transição após `Ready`, `Playing` ou `Paused` da geração correspondente. O `LocalQueueListModel` conserva metadata e identidade, limpa o diagnóstico com sinais restritos ao item recuperado e pode atualizar somente o diagnóstico de um item que segue indisponível.

O presenter mantém o ID da tentativa em voo separado da posição da linha. Assim, duplicatas e reordenação não confundem loading, e callbacks obsoletos não recuperam outra ocorrência. Rejeição síncrona do load preserva a sessão anterior; erro de tentativa aceita mantém diagnóstico recuperável e suprime a continuação automática específica dessa tentativa. Outro retry exige uma ação explícita. A tentativa aceita também assume a intenção de reprodução sobre a importação incremental: lotes que chegam depois não tomam a seleção de volta. O histórico de problemas dessa importação usa IDs de ocorrência, separado da disponibilidade mutável, para conservar a contagem após recuperação e deduplicar novas falhas do mesmo item.

Fila e erro apresentam “Tentar reproduzir novamente” por ação acessível, com loading associado ao ID e “Abrir outro áudio” preservado. A [evidência central do segundo recorte](../../experiments/m0/candidates/qt/README.md#retry-m47) inclui os contratos de teste e distingue a integração com uma WAV restaurada no mesmo path temporário dos cenários ainda pendentes de dispositivo, drive físico, arquivo relocalizado, Explorer, pacote e Linux. Esse recorte não encerra M4.7 nem `NFR-REL-005`.

O terceiro recorte de M4.7 mantém a enumeração e a seleção da saída padrão dentro do adapter Qt. `QtAudioOutputDevices` é uma fronteira de teste específica desse adapter, não uma nova porta de domínio: encapsula identidade, `QAudioDevice`, aplicação ao `QAudioOutput` e notificação de mudança. A implementação real observa `QMediaDevices::audioOutputsChanged` e consulta `defaultAudioOutput()`. Cada load fixa a saída corrente; ausência inicial ou mudança de identidade em uma geração ativa produz `AudioOutputUnavailable`/`AudioOutputChanged`, preservando posição/duração confirmadas e retirando o player antigo antes do evento. Mudanças que conservam a identidade padrão são inertes. Comandos e sinais de mídia também reconciliam essa identidade; reconexão não dispara load ou play.

Esses erros atravessam a porta `PlaybackBackend` como códigos puros e não viram indisponibilidade de ocorrência em `LocalTrackSession`. A apresentação usa a ação fechada `recover-audio-output` em `executeErrorAction()` para abrir novamente o path interno em `RestorePaused`. A posição usa o estado de restauração já vinculado à geração, com tolerância, tratamento de mídia sem seek e proteção de EOF existentes; outra seleção explícita invalida a restauração. Fila, duplicatas e identidade do modelo permanecem intactas, e a UI não recebe handles nem IDs de dispositivos. O [registro central do terceiro recorte](../../experiments/m0/candidates/qt/README.md#audio-output-m47) descreve testes e limites: topologia injetada não comprova troca física, áudio audível, driver, pacote ou Linux. A escolha manual de saída continua reservada a `FR-018`/M6.1; M4.7 permanece parcial.

O quarto recorte de M4.7 fica inteiramente no adapter de shell. No QPA Windows real, `QtShellPlatform` possui `WindowsTaskbarEvents`, que registra `TaskbarCreated` em um `QWindow` top-level privado e invisível. O filtro aceita somente a mensagem registrada para seu próprio `HWND` e entrega o sinal enfileirado com guardas de lifetime; shutdown remove filtro e receptor. A leitura de capacidade exige esse monitor pronto, ícone pretendido não vazio e presença de shell/taskbar por `GetShellWindow`/`FindWindowW`, falhando fechada quando o monitor não inicia; `refreshAvailability()` permite nova tentativa. Essa prova de presença não é confirmação de `NIM_ADD`, e geometria do ícone não é critério de disponibilidade por causa do overflow legítimo.

O Qt conserva a responsabilidade de registrar novamente o ícone. `handleTrayHostRestart()` apenas torna alcançável, minimizada e sem solicitar ativação, a janela cuja ocultação pertence ao adapter. A política não manipula o ícone privado, não introduz flag persistente de janela e não altera o domínio, áudio, fila ou preferências. O estado de interação vindo do binding adia recuperação durante saída pendente; retorno da interação revalida ownership, e rebind/saída confirmada invalidam a intenção antiga. Janela de bootstrap apenas oculta, janela já visível/minimizada e retorno do shell não reaplicam a disposição inicial. O [registro central de taskbar](../../experiments/m0/candidates/qt/README.md#taskbar-m47) separa os contratos do adapter e a caracterização do registro nativo do Qt da validação ainda pendente com Explorer realmente reiniciado, pacote e Linux.

O quinto recorte de M4.7 introduz a porta C++ pura `AudioFileLocator`, separando seleção de arquivo e reprodução. `locateQueueOccurrence(ID)` resolve o path anterior dentro do presenter; a UI não fornece nem modifica paths. `QtAudioFileLocator` mantém um diálogo assíncrono de seleção local vinculado à janela, com uma única solicitação e cancelamento, sem sondar ou navegar automaticamente ao path ausente. A conclusão tipada retorna pela thread da UI e o presenter a reenfileira, validando geração da solicitação, identidade da ocorrência e path original antes de aplicar. Cancelamento, remoção, substituição, recuperação concorrente e saída/destruição invalidam conclusões obsoletas.

A operação pura `replaceUnavailableSource` substitui apenas o path de uma ocorrência indisponível, sem I/O, alocação de outro ID ou mudança de seleção. O modelo reconstrói somente a linha afetada com fallback do novo nome, metadata limpa e diagnóstico fechado `source_relocated`, mantendo `available=false`. Em uma ocorrência não atual, não existe chamada de áudio ou metadata: o retry posterior verifica a nova origem. Na ocorrência atual em erro, o novo load é aceito antes da troca da fila e usa `RestorePaused`, renovando gerações, arte e metadata e restaurando a posição confirmada conforme o contrato de seek. Rejeição conserva a origem anterior. O snapshot estrutural já contém paths por ID, portanto a troca usa o autosave existente sem alterar schema v1; disponibilidade continua transitória.

O alvo de posição restaurada ainda pendente deve sobreviver aos erros de origem recuperáveis `source_unavailable`, `invalid_media` e `access_denied`, preservando o valor no snapshot até recuperação ou cancelamento previsto por seleção/transporte explícito e gestão de geração. O [registro central de localização](../../experiments/m0/candidates/qt/README.md#file-location-m47) documenta as ações acessíveis da UI, a regressão de persistência/decoder que motivou esse ajuste, os checks automatizados verdes e os riscos residuais. A seleção isolada não comprova decode; a integração com decoder real usa arquivo temporário renomeado e não comprova dispositivo ou drive físico, diálogo nativo, pacote ou Linux. M4.7 continua parcial.

## 15. Segurança e privacidade

Os princípios de input não confiável, privilégio mínimo e ausência de rede são invariantes. Controles específicos complementam, mas não substituem, as regras comuns.

- no desafiante Tauri, o WebView carrega somente assets locais, com CSP restrita, sem navegação remota e com capabilities limitadas aos comandos/janelas necessários;
- no candidato Qt, QML não recebe paths arbitrários nem acesso direto a filesystem/plataforma; somente tipos e operações deliberadamente expostos pelo presenter ficam visíveis;
- paths/URLs nunca são executados como comandos;
- capas e tags são entrada não confiável, com limites de bytes/dimensões/texto;
- protocolos locais validam IDs opacos, não paths arbitrários vindos da UI;
- logs redigem paths, query strings e conteúdo por padrão;
- nenhuma porta de rede, telemetria, analytics ou auto-update no MVP;
- dependências passam por auditoria de licença e advisories; Qt LGPL e a configuração/licença do FFmpeg possuem gate explícito;
- `unsafe`, FFI e APIs nativas inseguras ficam em adapters ou módulos de plataforma, com invariantes documentadas.

## 16. Erros e observabilidade

Taxonomia mínima:

- `UnsupportedFormat`;
- `CorruptMedia`;
- `FileUnavailable` / `PermissionDenied`;
- `OutputDeviceUnavailable`;
- `SeekUnsupported` / `SeekFailed`;
- `MetadataInvalid`;
- `PersistenceCorrupt`;
- `PlatformCapabilityUnavailable`;
- `RemoteResolutionFailed` futuro.

Erros carregam código estável, contexto seguro, possibilidade de retry e ação sugerida. A UI recebe mensagem localizada; log local recebe cadeia de causas redigida. Panic, abort e exceção não tratada são bugs, não mecanismos de erro.

Métricas de performance são produzidas por benchmarks locais/CI e comandos diagnósticos, não telemetria do usuário.

## 17. Distribuição

### Windows

- release x64 primeiro;
- instalador NSIS por usuário como baseline experimental até a decisão M0;
- file associations somente para matriz aprovada, por template/hook que cumpra o modelo de apps padrão do Windows;
- dependências do candidato são declaradas e medidas separadamente da RAM: WebView2/bootstrapper no Tauri ou DLLs, plugins QML e componentes Qt/FFmpeg no Qt;
- versão portátil é artefato separado e declara limitações;
- assinatura é requisito antes de distribuição pública, não para builds locais.

### Linux

- `.deb` para distribuição de referência;
- AppImage depois de validar codecs, toolkit/runtime escolhido, tray e MIME;
- build em ambiente compatível com a glibc mínima;
- pacotes informam dependências reais e não embutem runtimes sem revisão.

Cada release gera manifesto com versão, commit, hashes, plataforma, formatos testados e validações executadas.

## 18. Evolução para fontes remotas

```text
URL solicitada
    │
    ▼
RemoteSourceResolver
    │ policy + timeout + cancellation
    ▼
ResolvedStream { url, headers, expiry, seek_capability, metadata }
    │
    ▼
PlaybackBackend
```

O resolver não entra no domínio local nem na camada visual de qualquer candidato. URLs expiradas podem ser resolvidas novamente de modo limitado. Cache possui cota e política de limpeza. Lives e streams sem duração publicam capabilities correspondentes.

YouTube só recebe implementação após ADR específico; o caminho oficialmente permitido pode exigir player visível e, portanto, ser incompatível com a proposta audio-only/tray.

## 19. Pontos de teste por fronteira

| Fronteira | Teste principal |
|---|---|
| Domínio | unitários e propriedades sobre estado/fila/tempo |
| Application | casos de uso com fakes e relógio determinístico |
| Playback adapter | contrato comum + corpus por formato |
| Metadata adapter | fixtures válidas, ausentes, hostis e limites |
| Persistence | round-trip, migração, corrupção e falha durante replace |
| Boundary/IPC | schema, tradução de tipos, limites, input inválido e coalescing |
| UI | componentes/estados/acessibilidade sem engine real |
| Windows/Linux | smoke de tray, associações, media controls e lifecycle |
| Pacote | instalação, upgrade, abertura, desinstalação e arquivos preservados |

## 20. Decisões e gates

Permanecem propostos:

- NFR-STACK-001–006: C++20 + Qt 6 Quick/QML é a hipótese principal e Tauri 2 + Svelte é o desafiante A/B, sem aceitação antecipada;
- baseline Qt do M0: Qt Multimedia/FFmpeg + TagLib atrás das portas comuns;
- ADR-0001 e ADR-0002: registram a comparação dos candidatos de UI e áudio; ambos permanecem `Proposto`;
- requisito confirmado: player local-first e offline no MVP;
- ADR-0003: mecanismo proposto para isolar um resolver remoto futuro.

O M0 deve comparar o mesmo recorte de UX, acessibilidade e integração e medir shells e backends de áudio separadamente. Somente candidatos que atendam aos budgets — ou a desvios aprovados — podem chegar à decisão; empate dentro das tolerâncias pré-registradas é resolvido pelos critérios de NFR-STACK-006. Até ADR explícito com números e ambiente reproduzível, esta arquitetura descreve candidatos, não uma implementação nem uma stack aceita.
