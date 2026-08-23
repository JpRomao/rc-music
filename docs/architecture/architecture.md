# Arquitetura do sistema

**Estado:** proposta de referência; mapeamento Tauri condicionado ao ADR-0001
**Stack de exemplo:** Rust + Tauri 2 + Svelte  
**Última revisão:** 2026-08-23

As invariantes de domínio, portas, concorrência, privacidade local e integração por capabilities são independentes da stack. Nomes, diretórios e controles de WebView/Tauri neste documento descrevem somente o candidato do M0 e deixam de valer se o ADR-0001 for rejeitado.

## 1. Objetivos arquiteturais

A arquitetura deve:

- manter a thread de UI livre de trabalho bloqueante e impedir trabalho adicional do código próprio no callback de saída;
- permitir TDD do comportamento sem WebView, dispositivo ou arquivo real;
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

O aplicativo é um processo desktop de instância única. **No candidato Tauri**, o WebView é somente apresentação local; não hospeda o domínio nem abre páginas remotas.

## 3. Componentes no candidato Tauri

```text
┌─────────────────────────────────────────────────────────────────┐
│ UI Svelte                                                       │
│ Views · componentes · acessibilidade · interpolação visual      │
└──────────────────────────────┬──────────────────────────────────┘
                               │ comandos tipados / snapshots
┌──────────────────────────────▼──────────────────────────────────┐
│ Boundary IPC / Presenter                                          │
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

`ui → ipc/presenter → application → domain/ports ← adapters`

- domínio não importa Tauri, Svelte, Rodio, Lofty, Win32 ou D-Bus;
- adapters implementam contratos definidos no núcleo;
- DTOs de IPC não são entidades do domínio;
- UI nunca acessa arquivos ou APIs privilegiadas diretamente;
- nenhum adapter chama componente visual.

## 4. Estrutura inicial do repositório

Se o ADR-0001 for aceito, o projeto deve começar como uma aplicação Tauri com módulos internos, não um workspace de muitos crates:

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

Extrair um crate somente quando pelo menos um destes casos existir:

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

## 8. Comandos, eventos e IPC

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

Regras:

- validar tamanho, tipo e intervalo de todo payload na borda;
- não expor command genérico para executar path/URL;
- agrupar ticks de posição a 5–10 Hz;
- incluir `track_id`/geração em respostas assíncronas;
- manter DTOs pequenos; capa é servida por cache/protocolo controlado, não repetida em base64 a cada evento;
- aplicar backpressure/coalescing a posição, nunca perder eventos ended/error.

## 9. Modelo de concorrência

O desenho de workers é invariante; os nomes WebView/Tauri no diagrama são o mapeamento do candidato atual.

```text
WebView/UI thread
    │ commands                     snapshots/eventos
    ▼                                      ▲
Tauri command boundary / application coordinator
    │                                      │
    ├──► Audio worker (dono do stream, sink e decoder)
    ├──► Metadata/file worker pool
    └──► Atomic persistence worker
```

- exatamente um worker é dono do dispositivo/engine de áudio;
- código próprio chamado no callback de saída não loga, não chama UI, não toma lock longo e não adiciona I/O. Rodio pode puxar o decoder e o arquivo no callback CPAL; M0 mede underruns e latência. Se a biblioteca bloquear de modo relevante, o adapter ganha prefetch/ring buffer limitado ou o backend é rejeitado;
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

Cobrir paths Unicode, aspas, múltiplos arquivos, chamadas simultâneas e diretório de trabalho diferente. O IPC deve aceitar somente mensagens locais esperadas e impor limites.

## 14. Integração com plataforma

### Windows

- tray e menu;
- SMTC/metadata/teclas de mídia;
- eventos de sessão/power;
- página oficial de default apps;
- associações por template/hook NSIS próprio, com ProgID, `Capabilities` e `RegisteredApplications` por usuário; não confiar apenas em `bundle.fileAssociations`;
- high contrast, reduced motion e UI Automation via WebView2;
- assinatura futura do executável/instalador.

### Linux

- MPRIS por D-Bus;
- StatusNotifier/tray quando ambiente suportar;
- XDG `.desktop`, ícones, MIME e diretórios de dados;
- teste Wayland e X11 na distribuição de referência;
- fallback visível quando tray estiver ausente.

Cada adapter publica capabilities. A aplicação não finge que uma integração indisponível funcionou.

## 15. Segurança e privacidade

Os princípios de input não confiável, privilégio mínimo e ausência de rede são invariantes. CSP, capabilities e protocolo local abaixo são controles condicionais ao candidato Tauri.

- WebView carrega assets locais, com CSP restrita e sem navegação remota;
- capabilities Tauri concedem somente comandos/janelas necessários;
- paths/URLs nunca são executados como comandos;
- capas e tags são entrada não confiável, com limites de bytes/dimensões/texto;
- protocolos locais validam IDs opacos, não paths arbitrários vindos da UI;
- logs redigem paths, query strings e conteúdo por padrão;
- nenhuma porta de rede, telemetria, analytics ou auto-update no MVP;
- dependencies passam por auditoria de licença e advisories;
- `unsafe` e FFI ficam em módulos de plataforma, com invariantes documentadas.

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

Erros carregam código estável, contexto seguro, possibilidade de retry e ação sugerida. A UI recebe mensagem localizada; log local recebe cadeia de causas redigida. Panics são bugs e não mecanismo de erro.

Métricas de performance são produzidas por benchmarks locais/CI e comandos diagnósticos, não telemetria do usuário.

## 17. Distribuição

### Windows

- release x64 primeiro;
- instalador NSIS por usuário;
- file associations somente para matriz aprovada, por template/hook que cumpra o modelo de apps padrão do Windows;
- runtime WebView2 por bootstrapper quando necessário;
- versão portátil é artefato separado e declara limitações;
- assinatura é requisito antes de distribuição pública, não para builds locais.

### Linux

- `.deb` para distribuição de referência;
- AppImage depois de validar codecs, WebKitGTK, tray e MIME;
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

O resolver não entra no domínio local nem, no candidato Tauri, no WebView. URLs expiradas podem ser resolvidas novamente de modo limitado. Cache possui cota e política de limpeza. Lives e streams sem duração publicam capabilities correspondentes.

YouTube só recebe implementação após ADR específico; o caminho oficialmente permitido pode exigir player visível e, portanto, ser incompatível com a proposta audio-only/tray.

## 19. Pontos de teste por fronteira

| Fronteira | Teste principal |
|---|---|
| Domínio | unitários e propriedades sobre estado/fila/tempo |
| Application | casos de uso com fakes e relógio determinístico |
| Playback adapter | contrato comum + corpus por formato |
| Metadata adapter | fixtures válidas, ausentes, hostis e limites |
| Persistence | round-trip, migração, corrupção e falha durante replace |
| IPC | schema, limites, input inválido e coalescing |
| UI | componentes/estados/acessibilidade sem engine real |
| Windows/Linux | smoke de tray, associações, media controls e lifecycle |
| Pacote | instalação, upgrade, abertura, desinstalação e arquivos preservados |

## 20. Decisões e gates

Permanecem propostos:

- ADR-0001: Rust + Tauri + Svelte;
- ADR-0002: Rodio/Symphonia + Lofty;
- requisito confirmado: player local-first e offline no MVP;
- ADR-0003: mecanismo proposto para isolar um resolver remoto futuro.

O M0 deve provar UI, codecs, acessibilidade, integração e orçamentos antes de aceitá-los. Até lá, esta arquitetura descreve a direção recomendada, não uma alegação de implementação.
