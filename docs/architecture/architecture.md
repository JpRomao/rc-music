# Arquitetura do sistema

**Estado:** proposta de referência; candidatos condicionados ao M0 e a ADR explícito
**Stacks de experimento:** C++20 + Qt 6 Quick/QML (hipótese principal) e Rust + Tauri 2 + Svelte (desafiante A/B)
**Última revisão:** 2026-08-23

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

- carregar última versão válida ou default;
- salvar preferências e sessão atomicamente;
- migrar formatos conhecidos;
- manter backup limitado da última gravação válida.

### `PlatformIntegration`

- tray e visibilidade da janela;
- controles/metadata SMTC ou MPRIS;
- abrir página de apps padrão;
- capacidades disponíveis;
- eventos de suspensão, retomada e shutdown.

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

Os mesmos comandos são usados por UI, tray, atalhos, SMTC/MPRIS e segunda instância. Isso impede regras divergentes.

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
  "skipBackwardSeconds": 5,
  "skipForwardSeconds": 5,
  "closeBehavior": "tray"
}
```

Gravação:

1. serializar para arquivo temporário no mesmo diretório;
2. flush e, quando apropriado, sincronizar;
3. substituir atomicamente o destino;
4. manter no máximo um backup válido;
5. nunca apagar o arquivo bom antes de validar o novo.

Salvar por debounce após mudanças importantes e no shutdown. A fila preserva paths ausentes como itens indisponíveis para que o usuário possa localizar ou remover. SQLite só entra com biblioteca indexada e busca que comprovem necessidade.

## 13. Instância única e abertura pelo shell

Sequência de startup:

1. capturar argumentos como valores nativos, sem reparse de string;
2. adquirir instância única cedo;
3. se secundária, encaminhar lista tipada e ativar a principal, então sair;
4. principal carrega preferências e sessão pausada;
5. inicializa engine e integrações;
6. processa argumentos novos conforme política de fila;
7. mostra/restaura a janela.

Cobrir paths Unicode, aspas, múltiplos arquivos, chamadas simultâneas e diretório de trabalho diferente. O canal local entre instâncias deve aceitar somente mensagens esperadas e impor limites; IPC adicional do shell, quando existir, segue as mesmas regras.

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
