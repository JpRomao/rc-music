# Sala de escuta — plano de implementação

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Amigos com o RC Music instalado ouvem juntos a fila do host, com menos de 100 ms de diferença, permissões por cargo e por pessoa, playlists visíveis e músicas enviadas por convidados.

**Architecture:**
- **Domínio puro:** membros, cargos, permissões, convite, relógio e estado versionado da sala. O host é a autoridade: convidados enviam intenções, o host confere e transmite o estado.
- **Transporte:** WebRTC com data channels (libdatachannel) em estrela a partir do host, com o encontro dos pares feito por um Worker gratuito da Cloudflare que só repassa mensagens cifradas.
- **Áudio:** o arquivo chega inteiro antes e cada app toca a própria cópia temporária no instante combinado.

**Tech Stack:** C++20, Qt 6.11 Quick/QML, Qt Multimedia/FFmpeg, Catch2 3.15.3, Qt Test, libdatachannel (MPL-2.0), Windows CNG (BCrypt/NCrypt) e DPAPI, TypeScript com Cloudflare Workers e Durable Objects, Vitest.

**Spec:** [docs/superpowers/specs/2026-11-17-listening-room-design.md](../specs/2026-11-17-listening-room-design.md), aprovado em 2026-11-17. Leia o desenho antes de qualquer tarefa: ele traz o porquê de cada decisão.

## Global Constraints

- Abrir o app não conecta a nada. A rede existe só entre "Abrir sala" e "Encerrar sala", ou entre "Entrar com link" e "Sair da sala".
- O app continua inteiro sem rede, e nenhum teste depende de internet.
- Nunca enviar caminhos ou nomes de arquivo locais, a biblioteca completa ou playlists privadas.
- **Limites fixos**, como constantes em `include/m0/domain/ListeningRoomLimits.hpp`:

  | Constante | Valor |
  |---|---|
  | `room_protocol_version` | `1` |
  | `minimum_room_listeners` | `1` |
  | `maximum_room_listeners` | `10` |
  | `default_room_listeners` | `5` |
  | `maximum_room_queue_items` | `500` |
  | `maximum_room_control_message_bytes` | `256 * 1024` |
  | `room_file_chunk_bytes` | `64 * 1024` |
  | `maximum_guest_upload_bytes` | `50 * 1024 * 1024` |
  | `room_cache_quota_bytes` | `1024ull * 1024 * 1024` |
  | `maximum_room_lyrics_bytes` | `64 * 1024` |
  | `room_prefetch_items` | `3` |
  | `maximum_nickname_code_points` | `32` |
  | `maximum_custom_avatar_input_bytes` | `2 * 1024 * 1024` |
  | `avatar_pixels` | `256` |
  | `room_play_lead_ms` | `300` |
  | `room_clock_samples` | `8` |
  | `room_clock_resample_ms` | `5000` |
  | `room_reconnect_window_ms` | `30000` |
  | `minimum_output_offset_ms` | `-500` |
  | `maximum_output_offset_ms` | `500` |

- **Limites do Worker:** mensagem de até 16 KiB, até 20 mensagens/s por socket, até 11 sockets por sala, e a sala fecha após 10 minutos sem o host.
- **Sincronia:** meta abaixo de 100 ms entre participantes com saída com fio; limite aceitável de 150 ms.
- **Textos da UI** em português, nos arquivos `*Text.qml`. O contrato de UI recusa literais de string fora deles, inclusive em comentários.
- **Persistência** em JSON, com backup e confirmação de gravação, como `QtJsonPlaylistStore`:
  - `profile.json`;
  - `room-roles.json`;
  - `avatars/custom.png`;
  - `room-cache/`;
  - `room-identity.pem`, guardado com DPAPI.
- **Dependências novas:**
  - libdatachannel com a pilha TLS dela;
  - pacotes npm do Worker.

  Baixar fontes ou pacotes exige a autorização explícita do mantenedor no momento da tarefa, com nome, origem e tamanho. A licença vai para o ADR-0022.
- **A conta da Cloudflare e o `wrangler deploy` são passos do mantenedor.** O agente nunca cria conta nem publica.
- **Validação proporcional** (`agents.md`):
  - testes focados por tarefa;
  - `scripts/validate-docs.ps1` em toda mudança de documentação;
  - formatadores com `clang-format` e `qmlformat --newline unix -i`;
  - a suíte completa só no fim de cada fatia.
- **Commits** pequenos, em Conventional Commits, em português e terminando com a linha `Co-Authored-By`. Depois de cada commit verde, o merge fast-forward em `main` e o push seguem a regra do repositório.
- O instalador continua bloqueado até o fim do redesenho. A S13 com amigos depende dele.

## Review Focus

1. **Perda de conexão no meio de uma transferência ou de uma música.** O convidado reconecta dentro de 30 s, retoma do ponto certo e não duplica nem corrompe o arquivo. O cache parcial é descartado ou retomado pelo hash, nunca tocado incompleto. Os testes ficam nas tarefas S8.2 e S7.4.
2. **Intenções simultâneas ou fora de ordem:** dois convidados removem o mesmo item, ou um pula enquanto o host reordena. O host aplica na ordem de chegada; a segunda intenção, que já não tem efeito, é recusada com `UnknownItem`, e todos convergem para a mesma versão. O teste fica na S3.3.
3. **Permissão revogada com uma intenção já em trânsito.** O host confere no momento de aplicar, então a intenção é recusada mesmo enviada antes da mudança, e a UI do convidado se atualiza. O teste fica na S3.3.
4. **Arquivo de convidado malicioso ou inválido:** extensão de áudio com outro conteúdo, tamanho declarado diferente do real, hash que não confere, ou mais de 50 MiB. O host recusa sem travar a sala nem gravar nada fora do cache. Os testes ficam nas tarefas S8.1 e S12.1.
5. **Host que fecha o app ou para de responder sem dizer adeus.** Os convidados percebem em até 10 s pela queda do canal, mostram "O host encerrou a sala", restauram a fila local e limpam o cache. O teste fica na S7.4.

---

## Fatia S0 — Contrato e ADR

### Task S0.1: ADR-0022, requisitos e exceção de rede

**Files:**
- Create: `docs/architecture/decisions/0022-listening-room.md`
- Modify:
  - `docs/architecture/decisions/README.md` (índice);
  - `docs/architecture/decisions/0015-named-playlists.md` (seção "Revisão de 2026-11-17: visibilidade para a sala");
  - `docs/product/requirements.md`;
  - `agents.md` §10;
  - `docs/project-status.md`.

- [ ] **Step 1:** Escrever o ADR-0022 com estado **Aceito**, porque o mantenedor aprovou o desenho em 2026-11-17. Seções:
  - contexto, com a citação do pedido;
  - decisão: caminho A, estrela no host, libdatachannel, Worker gratuito, cifragem ponta a ponta, entrada com aprovação dos novos e "Abrir sala" explícito;
  - alternativas: B, C, ntfy.sh, troca manual e TURN;
  - consequências: IP visível entre pares, envio do host e cópias temporárias;
  - critérios de revisão: S1 reprovada, plano gratuito insuficiente ou TURN necessário;
  - licenças: libdatachannel MPL-2.0 e dependências (libjuice MPL-2.0, usrsctp BSD-3, plog MIT e a pilha TLS escolhida).
- [ ] **Step 2:** Em `requirements.md`, criar a seção `### 2.10 Sala de escuta` com os requisitos abaixo, todos P1 e Confirmado, com critérios tirados do desenho:

  | Ordem | Requisito |
  |---|---|
  | 1 | Perfil: apelido e avatar |
  | 2 | Criar, abrir, encerrar e revogar a sala; convite com lotação e duração |
  | 3 | Entrada com aprovação dos novos, expulsar e bloquear |
  | 4 | Reprodução sincronizada e ajuste de atraso |
  | 5 | Permissões, cargos e ajustes individuais |
  | 6 | Visibilidade das playlists do host |
  | 7 | Músicas enviadas por convidados |

  Os IDs seguem em sequência depois do FR-056 (o validador recusa citar um ID antes de ele existir). Acrescentar o próximo NFR-LIMIT, com os limites da sala, e mudar NFR-PRIV-004 para incluir a exceção: a rede da sala só existe enquanto a pessoa hospeda ou participa. Atualizar a matriz de rastreabilidade (seção 8).
- [ ] **Step 3:** Em `agents.md` §10, trocar "não integrar outros serviços remotos" por esse texto mais a exceção: "exceto o servidor de apresentação da sala de escuta do ADR-0022, que só repassa mensagens cifradas enquanto uma sala está aberta".
- [ ] **Step 4:** Rodar `pwsh -NoProfile -File scripts/validate-docs.ps1` e `git diff --check`.
  Esperado: `OK`, com 1 Markdown e 8 requisitos a mais do que antes, e diff limpo.
- [ ] **Step 5:** Commit `docs: aceita a sala de escuta (ADR-0022) e seus requisitos`.

---

## Fatia S1 — Protótipo de sincronia (descartável, ponto de decisão)

### Task S1.1: Medir a precisão do QMediaPlayer

**Files:**
- Create:
  - `experiments/listening-room-sync/CMakeLists.txt`;
  - `experiments/listening-room-sync/main.cpp`;
  - `experiments/listening-room-sync/LoopbackRecorder.cpp`;
  - `experiments/listening-room-sync/make_clicks.py`;
  - `docs/listening-room-sync-spike.md`.
- Fora do build do produto e dos testes; não entra em `CMakeLists.txt` raiz.

- [ ] **Step 1:** `make_clicks.py` gera, sem dependências externas (só `wave` e `struct`), dois WAV de 3 min a 48 kHz com um clique de 1 ms por segundo:
  - um só no canal esquerdo;
  - outro só no direito.

  Converter para MP3 com o `ffmpeg` do toolchain atual.
- [ ] **Step 2:** `main.cpp` cria dois `QMediaPlayer`, cada um com o seu `QAudioOutput` no dispositivo padrão. Executa os cenários abaixo 20 vezes cada:

  | Cenário | O que testa |
  |---|---|
  | (a) | `play()` dos dois no mesmo tick de um `QTimer` agendado para T |
  | (b) | carregar, `setPosition(P)` e `play()` em T |
  | (c) | um player começa 2 s depois e entra com `setPosition` calculado |
  | (d) | 3 min corridos sem correção |
  | (e) | correção com `setPlaybackRate(1.01)` e `0.99` por 2 s |

- [ ] **Step 3:** `LoopbackRecorder.cpp` grava a saída do sistema por WASAPI loopback (`AUDCLNT_STREAMFLAGS_LOOPBACK`). A diferença entre os inícios dos cliques esquerdo e direito é a dessincronia real em ms. Também registrar `position()` dos dois a cada 10 ms, para saber a granularidade.
- [ ] **Step 4:** Escrever `docs/listening-room-sync-spike.md`:
  - p50, p95 e máximo de cada cenário;
  - granularidade de `position()`;
  - se a mudança de velocidade gera artefato audível (ouvido pelo mantenedor, se ele quiser) e se `setPlaybackRate` altera o tom;
  - valores escolhidos para `room_drift_nudge_ms` e `room_drift_seek_ms`;
  - **veredito.**
- [ ] **Step 5 — Gate:**
  - **p95 de (b) e (c) ≤ 100 ms com correção:** seguir com `QMediaPlayer` e registrar no relatório.
  - **Entre 100 e 150 ms:** seguir e anotar o risco.
  - **Acima de 150 ms:** **parar e levar ao mantenedor** a alternativa `FFmpeg` + `QAudioSink` antes da S9.
- [ ] **Step 6:** Commit `docs: mede a sincronia possível com o QMediaPlayer` (relatório e experimento).

---

## Fatia S2 — Perfil (apelido e avatar)

### Task S2.1: Domínio do perfil

**Files:**
- Create:
  - `include/m0/domain/ListenerProfile.hpp`;
  - `src/domain/ListenerProfile.cpp`;
  - `include/m0/domain/ListeningRoomLimits.hpp`, com as constantes de Global Constraints;
  - `tests/core/ListenerProfileTests.cpp`.
- Modify: `cmake/RcMusicCore.cmake` (fontes do `rc_music_core`) e `cmake/RcMusicTests.cmake` (teste).

**Interfaces:**
- Produces:

  ```cpp
  inline constexpr std::uint32_t bundled_avatar_count = 20;
  enum class AvatarKind : std::uint8_t { Bundled, Custom };
  struct AvatarChoice final { AvatarKind kind{AvatarKind::Bundled}; std::uint32_t bundled_index{}; std::string custom_sha256{}; bool operator==(const AvatarChoice&) const = default; };
  struct ListenerProfile final { std::string nickname{}; AvatarChoice avatar{}; bool operator==(const ListenerProfile&) const = default; };
  [[nodiscard]] std::optional<std::string> canonicalNickname(std::string_view raw);
  [[nodiscard]] AvatarChoice initialAvatar(std::uint64_t random_value) noexcept;
  [[nodiscard]] bool isValidListenerProfile(const ListenerProfile& profile);
  ```

- [ ] **Step 1: Testes que falham**:
  - `canonicalNickname("  Ana  ") == "Ana"`;
  - `canonicalNickname("")`, só espaços, `"a\nb"` e `"a‎b"` → `nullopt`;
  - 32 code points (`"日"` repetido 32 vezes) → aceito; 33 → `nullopt`;
  - UTF-8 inválido (`"\xC3"`) → `nullopt`;
  - `initialAvatar(41).bundled_index == 41 % bundled_avatar_count`;
  - `isValidListenerProfile` recusa `Custom` sem um hash hexadecimal de 64 caracteres e `Bundled` com índice ≥ 20.
- [ ] **Step 2:** `ctest --test-dir <build> -R "rc_music.core.*ListenerProfile"` → FAIL, porque o arquivo não existe.
- [ ] **Step 3:** Implementar reaproveitando a política de linha única usada por `isValidPlaylistLibrarySnapshot`.
- [ ] **Step 4:** Mesmo comando → PASS.
- [ ] **Step 5:** Commit `feat: domínio do perfil de quem ouve junto`.

### Task S2.2: Store do perfil e imagem do avatar

**Files:**
- Create:
  - `include/m0/ports/ProfileStore.hpp`;
  - `src/adapters/QtJsonProfileStore.{hpp,cpp}`;
  - `src/adapters/QtAvatarImage.{hpp,cpp}`;
  - `tests/qt/QtJsonProfileStoreTests.cpp`;
  - `tests/qt/QtAvatarImageTests.cpp`.
- Modify: `cmake/RcMusicUi.cmake` e `tests/ui/CMakeLists.txt`.

**Interfaces:**
- Produces:
  - `ProfileStore`, com `load() -> ProfileLoadResult{std::optional<ListenerProfile>, ProfileLoadStatus}` e `save(const ListenerProfile&) -> ProfileSaveStatus`, nos mesmos estados de `PlaylistStore`;
  - `QtJsonProfileStore(QString config_directory)`;
  - `std::optional<QByteArray> normalizeAvatarImage(const QString& path)`: PNG de 256×256;
  - `QString saveCustomAvatar(const QString& config_directory, const QByteArray& png)`: devolve o SHA-256 em hexadecimal e grava `avatars/custom.png`.

- [ ] **Step 1: Testes que falham**:
  - ida e volta de `profile.json`;
  - backup usado quando o principal está corrompido;
  - `FutureVersion` bloqueia a gravação;
  - `normalizeAvatarImage`:
    - aceita PNG e JPEG de 100×300 → 256×256, recortado no centro;
    - recusa GIF, arquivo de 2 MiB + 1 byte e PNG truncado.
- [ ] **Step 2:** Rodar os dois testes → FAIL.
- [ ] **Step 3:** Implementar copiando a estrutura de gravação confirmada de `QtJsonPlaylistStore`. Para a imagem, usar `QImageReader` com `setAllocationLimit`.
- [ ] **Step 4:** Os dois testes → PASS.
- [ ] **Step 5:** Commit `feat: guarda o perfil e o avatar próprio`.

### Task S2.3: Primeira abertura e Configurações

**Files:**
- Create:
  - `assets/avatars/avatar-01.svg` … `avatar-20.svg`: formas geométricas nas cores Aurora, sem rostos, 24×24 escalável;
  - `src/presentation/ProfileBinding.{hpp,cpp}`;
  - `src/ui/qml/ProfileSetupDialog.qml`;
  - `src/ui/qml/AvatarPicker.qml`;
  - `src/ui/qml/ListenerAvatar.qml`;
  - `src/ui/qml/ProfileText.qml`;
  - `tests/qt/ProfileBindingTests.cpp`;
  - `tests/ui/quick/tst_profile.qml`.
- Modify:
  - `src/ui/qml/Main.qml`: abre o diálogo quando `profile.needsSetup`;
  - `src/ui/qml/PreferencesPanel.qml`: seção Perfil;
  - recursos Qt dos ícones;
  - `src/app/main.cpp`: composição.

**Interfaces:**
- Produces: `ProfileBinding` (QObject), com:
  - `Q_PROPERTY(bool needsSetup)`, `QString nickname`, `QUrl avatarSource`, `int avatarIndex`;
  - `Q_INVOKABLE bool setNickname(QString)`, `chooseBundledAvatar(int)` e `bool chooseCustomAvatar(QUrl)`.

- [ ] **Step 1: Testes que falham**:
  - `ProfileBindingTests`: sem perfil, `needsSetup == true`; `setNickname("Ana")` grava e muda `needsSetup` para `false`; `chooseCustomAvatar` com GIF retorna `false` e mantém o avatar.
  - `tst_profile.qml`:
    - o diálogo aparece sem perfil e o botão Continuar fica desabilitado com o campo vazio;
    - Tab percorre o campo, a grade de avatares e Continuar;
    - escolher um avatar pela grade muda `avatarIndex`.
- [ ] **Step 2:** `quick.sh tst_profile.qml` e o teste Qt → FAIL.
- [ ] **Step 3:** Implementar, com o avatar inicial vindo de `QRandomGenerator::system()`.
- [ ] **Step 4:** Os testes → PASS. Depois, rodar o contrato de UI e `tst_accessible_interface_tree.qml`.
- [ ] **Step 5:** Commit `feat: apelido e avatar na primeira abertura`. Fim da fatia: rodar `fullcheck.sh` completo e atualizar `docs/user-guide.md` (Perfil).

---

## Fatia S3 — Domínio da sala

### Task S3.1: Cargos, permissões e conhecidos

**Files:**
- Create:
  - `include/m0/domain/RoomRoster.hpp`;
  - `src/domain/RoomRoster.cpp`;
  - `tests/core/RoomRosterTests.cpp`.

**Interfaces:**
- Produces:

  ```cpp
  enum class RoomPermission : std::uint8_t { Skip, Pause, AddFromRoom, AddPlaylist, EditQueue, UploadOwnTracks, AddYoutube };
  inline constexpr std::size_t room_permission_count = 7;
  using RoomPermissionSet = std::bitset<room_permission_count>;
  struct RoomRole final { std::uint64_t id{}; std::string name{}; RoomPermissionSet permissions{}; };
  struct PermissionOverride final { RoomPermission permission{}; bool allowed{}; };
  struct KnownListener final { std::string fingerprint{}; std::string last_nickname{}; std::uint64_t role_id{}; std::vector<PermissionOverride> overrides{}; bool blocked{}; };
  struct PersistedInvite final { std::array<std::uint8_t,16> room_id{}; std::array<std::uint8_t,16> secret{}; std::size_t max_listeners{}; std::uint64_t default_role_id{}; };
  struct RoomRosterSnapshot final { std::vector<RoomRole> roles{}; std::vector<KnownListener> listeners{}; std::optional<PersistedInvite> invite{}; };
  [[nodiscard]] RoomRosterSnapshot defaultRoomRoster(); // roles: {1,"Ouvinte",{}}, {2,"DJ",all}
  class RoomRoster final {
    public:
      [[nodiscard]] bool restore(RoomRosterSnapshot);
      [[nodiscard]] RoomRosterSnapshot snapshot() const;
      [[nodiscard]] std::optional<std::uint64_t> createRole(std::string name, RoomPermissionSet permissions);
      [[nodiscard]] bool renameRole(std::uint64_t role_id, std::string name);
      [[nodiscard]] bool setRolePermissions(std::uint64_t role_id, RoomPermissionSet permissions);
      [[nodiscard]] bool removeRole(std::uint64_t role_id, std::uint64_t fallback_role_id);
      void remember(std::string_view fingerprint, std::string_view nickname, std::uint64_t role_id);
      [[nodiscard]] bool assignRole(std::string_view fingerprint, std::uint64_t role_id);
      [[nodiscard]] bool setOverride(std::string_view fingerprint, RoomPermission, std::optional<bool> allowed);
      [[nodiscard]] bool setBlocked(std::string_view fingerprint, bool blocked);
      [[nodiscard]] const KnownListener* find(std::string_view fingerprint) const noexcept;
      [[nodiscard]] RoomPermissionSet effectivePermissions(std::string_view fingerprint) const;
  };
  ```

- [ ] **Step 1: Testes que falham**:
  - `defaultRoomRoster()` tem "Ouvinte" sem permissões e "DJ" com todas;
  - uma pessoa com DJ e o ajuste `Pause=false` não tem Pause e tem Skip;
  - uma pessoa com Ouvinte e o ajuste `UploadOwnTracks=true` tem só esse;
  - `setOverride(..., nullopt)` remove o ajuste;
  - `removeRole(2, 1)` move os DJs para Ouvinte e mantém os ajustes;
  - remover o último cargo, usar como fallback o próprio cargo removido, nome vazio ou nome repetido → `false`;
  - `effectivePermissions` de uma pessoa desconhecida → vazio;
  - uma pessoa bloqueada → vazio;
  - `restore` de um snapshot com `role_id` inexistente → `false` e o estado anterior intacto.
- [ ] **Step 2:** `-R "rc_music.core.*RoomRoster"` → FAIL.
- [ ] **Step 3:** Implementar.
- [ ] **Step 4:** PASS.
- [ ] **Step 5:** Commit `feat: cargos e permissões da sala`.

### Task S3.2: Link do convite

**Files:**
- Create:
  - `include/m0/domain/RoomInviteLink.hpp`;
  - `src/domain/RoomInviteLink.cpp`;
  - `tests/core/RoomInviteLinkTests.cpp`.

**Interfaces:**
- Produces:

  ```cpp
  struct RoomInviteLink final { std::array<std::uint8_t,16> room_id{}; std::array<std::uint8_t,16> secret{}; std::array<std::uint8_t,32> host_fingerprint{}; bool operator==(const RoomInviteLink&) const = default; };
  [[nodiscard]] std::string formatInviteLink(const RoomInviteLink&, std::string_view worker_origin); // "<origin>/j/<b64url room>#<b64url secret>.<b64url fp>"
  [[nodiscard]] std::optional<RoomInviteLink> parseInviteLink(std::string_view text, std::string_view worker_origin);
  ```

- [ ] **Step 1: Testes que falham**:
  - ida e volta com valores fixos;
  - aceita espaços em volta e uma quebra de linha final;
  - recusa outra origem, `http://` quando a origem é `https://`, base64url com padding ou com caracteres inválidos, tamanhos errados, falta do `#` e parte extra depois da impressão digital.
- [ ] **Step 2:** FAIL.
- [ ] **Step 3:** Implementar.
- [ ] **Step 4:** PASS.
- [ ] **Step 5:** Commit `feat: formato do link de convite da sala`.

### Task S3.3: Estado da sala e intenções

**Files:**
- Create:
  - `include/m0/domain/ListeningRoom.hpp`;
  - `src/domain/ListeningRoom.cpp`;
  - `tests/core/ListeningRoomTests.cpp`.

**Interfaces:**
- Consumes: `RoomRoster` e `RoomPermission` (S3.1); `AvatarChoice` (S2.1).
- Produces:

  ```cpp
  enum class MemberState : std::uint8_t { Pending, Admitted };
  struct RoomMember final { std::string fingerprint{}; std::string nickname{}; std::string avatar_sha256{}; MemberState state{}; };
  enum class RoomItemOrigin : std::uint8_t { HostTrack, GuestTrack, YoutubeVideo };
  struct RoomTrackSummary final { std::string title{}, artist{}, album{}; std::int64_t duration_ms{}; };
  struct RoomItem final { std::uint64_t id{}; RoomItemOrigin origin{}; std::string owner_fingerprint{}; std::string content_sha256{}; std::uint64_t size_bytes{}; RoomTrackSummary summary{}; std::string youtube_id{}; std::string added_by{}; };
  struct RoomPlayback final { std::optional<std::uint64_t> current_item{}; bool playing{}; std::int64_t anchor_host_ms{}; std::int64_t anchor_position_ms{}; };
  struct RoomSnapshot final { std::uint64_t version{}; std::vector<RoomMember> members{}; std::vector<RoomItem> queue{}; RoomPlayback playback{}; };
  enum class JoinDecision : std::uint8_t { Admitted, Pending, RejectedFull, RejectedBlocked, RejectedClosed };
  enum class RoomIntentKind : std::uint8_t { Next, Previous, Seek, Pause, Resume, AddItems, AddPlaylist, RemoveItem, MoveItem };
  struct RoomIntent final { RoomIntentKind kind{}; std::int64_t position_ms{}; std::uint64_t item_id{}; std::size_t destination{}; std::uint64_t playlist_id{}; std::vector<std::uint64_t> catalog_entry_ids{}; std::optional<RoomItem> uploaded{}; };
  enum class IntentRejection : std::uint8_t { NotAdmitted, MissingPermission, UnknownItem, InvalidArgument, QueueFull };
  [[nodiscard]] RoomPermission requiredPermission(const RoomIntent&) noexcept; // Next/Previous/Seek→Skip, Pause/Resume→Pause, AddItems→AddFromRoom (or UploadOwnTracks if uploaded), AddPlaylist→AddPlaylist, Remove/Move→EditQueue
  class ListeningRoom final {
    public:
      ListeningRoom(std::size_t max_listeners, std::uint64_t default_role_id, RoomRoster& roster);
      [[nodiscard]] JoinDecision requestJoin(const RoomMember& candidate);
      [[nodiscard]] bool approve(std::string_view fingerprint);   // Pending→Admitted, remembers in roster
      [[nodiscard]] bool reject(std::string_view fingerprint);
      void leave(std::string_view fingerprint);
      [[nodiscard]] bool kick(std::string_view fingerprint, bool block);
      [[nodiscard]] std::optional<IntentRejection> authorize(std::string_view fingerprint, const RoomIntent&, const RoomSnapshot& current) const;
      [[nodiscard]] std::size_t admittedCount() const noexcept;
      [[nodiscard]] std::vector<RoomMember> members() const;
  };
  ```

  O host monta o `RoomSnapshot` na camada de aplicação (S7) e incrementa `version` a cada mudança. O domínio só autoriza e cuida dos membros.

- [ ] **Step 1: Testes que falham**:
  - **Entrada:**
    - pessoa conhecida e não bloqueada → `Admitted`;
    - pessoa nova → `Pending`, e o `approve` muda para `Admitted` e registra no roster com `default_role_id`;
    - bloqueada → `RejectedBlocked`.
  - **Lotação:** com `max_listeners=2` e dois admitidos, um terceiro conhecido → `RejectedFull`. Pendentes não contam: dois pendentes mais dois admitidos ainda deixam aprovar um quando um admitido sai.
  - **Autorização:**
    - `authorize` de um pendente → `NotAdmitted`;
    - Ouvinte pedindo `Next` → `MissingPermission`;
    - DJ pedindo `RemoveItem` de um ID ausente no snapshot → `UnknownItem`.
  - **(Review Focus 2)** Dois `RemoveItem` iguais em sequência: o primeiro passa, o segundo, conferido contra o snapshot já sem o item, → `UnknownItem`.
  - **(Review Focus 3)** Uma intenção autorizada antes de `setOverride(Skip=false)` e conferida depois → `MissingPermission`.
  - **Fila:** `AddItems` com a fila em 500 → `QueueFull`.
  - **Expulsão:** `kick(fp, true)` remove a pessoa e a deixa bloqueada no roster.
- [ ] **Step 2:** FAIL.
- [ ] **Step 3:** Implementar.
- [ ] **Step 4:** PASS.
- [ ] **Step 5:** Commit `feat: membros e autorização de intenções da sala`.

### Task S3.4: Relógio e posição esperada

**Files:**
- Create:
  - `include/m0/domain/RoomClock.hpp`;
  - `src/domain/RoomClock.cpp`;
  - `tests/core/RoomClockTests.cpp`.

**Interfaces:**
- Produces:

  ```cpp
  struct ClockSample final { std::int64_t sent_local_ms{}, host_ms{}, received_local_ms{}; };
  class RoomClockEstimator final {
    public:
      void add(const ClockSample&);                       // keeps the last room_clock_samples
      [[nodiscard]] std::optional<std::int64_t> offsetMs() const;    // host − local, from the minimum-RTT sample: host_ms − (sent+received)/2
      [[nodiscard]] std::optional<std::int64_t> roundTripMs() const;
  };
  [[nodiscard]] std::int64_t expectedPositionMs(const RoomPlayback&, std::int64_t host_now_ms) noexcept; // paused: anchor_position; playing: anchor_position + (host_now − anchor_host), never negative
  enum class DriftCorrection : std::uint8_t { None, Nudge, Seek };
  [[nodiscard]] DriftCorrection classifyDrift(std::int64_t drift_ms, std::int64_t nudge_ms, std::int64_t seek_ms) noexcept;
  ```

- [ ] **Step 1: Testes que falham**:
  - **Offset pela amostra de menor ida e volta:** com as amostras `{0,1050,100}` (RTT 100) e `{200,1220,240}` (RTT 40), o offset é `1220 − 220 = 1000` e o RTT é 40.
  - **Janela:** mantém só as últimas 8 amostras.
  - **Posição esperada:** `expectedPositionMs({1, true, 5000, 30000}, 5250) == 30250`; pausado devolve 30000; com `host_now < anchor`, nunca dá negativo.
  - **Correção**, com os limiares (40, 120):
    - drift 30 → `None`;
    - drift −60 → `Nudge`;
    - drift 200 → `Seek`.
- [ ] **Step 2:** FAIL.
- [ ] **Step 3:** Implementar.
- [ ] **Step 4:** PASS.
- [ ] **Step 5:** Commit `feat: relógio da sala`.

### Task S3.5: Store do roster

**Files:**
- Create:
  - `include/m0/ports/RoomRosterStore.hpp`;
  - `src/adapters/QtJsonRoomRosterStore.{hpp,cpp}`;
  - `tests/qt/QtJsonRoomRosterStoreTests.cpp`.

- [ ] **Step 1: Testes que falham**:
  - ida e volta de cargos, conhecidos com ajustes, bloqueio e convite persistente (`room_id` e `secret` em base64url);
  - arquivo ausente → `defaultRoomRoster()`;
  - backup em caso de corrupção;
  - `FutureVersion` bloqueia a gravação;
  - o segredo nunca aparece em log nem em `qDebug`, verificado com um handler de mensagens capturando a saída.
- [ ] **Step 2:** FAIL.
- [ ] **Step 3:** Implementar seguindo o `QtJsonPlaylistStore`.
- [ ] **Step 4:** PASS.
- [ ] **Step 5:** Commit `feat: guarda cargos, conhecidos e convite da sala`. Fim da fatia: rodar `fullcheck.sh`.

---

## Fatia S4 — Protocolo

### Task S4.1: Codec das mensagens de controle

**Files:**
- Create:
  - `include/m0/domain/RoomMessages.hpp`: os tipos das mensagens, puros, sem Qt, para a camada de aplicação usar;
  - `src/adapters/RoomProtocolCodec.{hpp,cpp}`;
  - `tests/qt/RoomProtocolCodecTests.cpp`.

**Interfaces:**
- Consumes: os tipos da S3.
- Produces: os tipos abaixo, em `RoomMessages.hpp`. Só as funções `encode*` e `decode*` ficam no adaptador Qt.

  ```cpp
  struct RoomHello { std::uint32_t protocol_version; std::string nickname; std::string avatar_sha256; };
  struct RoomJoinResult { JoinDecision decision; };
  struct RoomStateMessage { RoomSnapshot snapshot; RoomPermissionSet my_permissions; };
  struct RoomIntentMessage { std::uint64_t request_id; RoomIntent intent; };
  struct RoomIntentRejected { std::uint64_t request_id; IntentRejection reason; };
  struct RoomClockPing { std::int64_t sent_local_ms; };
  struct RoomClockPong { std::int64_t sent_local_ms; std::int64_t host_ms; };
  struct RoomCatalogEntry { std::uint64_t id; RoomTrackSummary summary; };
  struct RoomCatalogPlaylist { std::uint64_t id; std::string name; std::vector<RoomCatalogEntry> entries; };
  struct RoomCatalog { std::vector<RoomCatalogPlaylist> playlists; };
  struct RoomFileRequest { std::string content_sha256; std::uint64_t offset; };
  struct RoomFileOffer { std::string content_sha256; std::uint64_t size_bytes; std::uint32_t transfer_id; };
  struct RoomLyrics { std::uint64_t item_id; std::string lrc_text; };
  enum class RoomByeReason : std::uint8_t { Closed, Revoked, Kicked, VersionMismatch, Full };
  struct RoomBye { RoomByeReason reason; };
  struct RoomDriftReport { std::int64_t drift_ms; };   // used by S9.2
  using RoomControlMessage = std::variant<RoomHello, RoomJoinResult, RoomStateMessage, RoomIntentMessage, RoomIntentRejected, RoomClockPing, RoomClockPong, RoomCatalog, RoomFileRequest, RoomFileOffer, RoomLyrics, RoomBye, RoomDriftReport>;
  struct RoomFileChunk { std::uint32_t transfer_id; std::uint64_t offset; std::vector<std::uint8_t> payload; };
  // adapter (RoomProtocolCodec.hpp):
  [[nodiscard]] QByteArray encodeRoomControl(const RoomControlMessage&);
  [[nodiscard]] std::optional<RoomControlMessage> decodeRoomControl(QByteArrayView bytes);
  // binary media frame: u32 transfer_id (LE) | u64 offset (LE) | payload ≤ room_file_chunk_bytes
  [[nodiscard]] QByteArray encodeFileChunk(const RoomFileChunk&);
  [[nodiscard]] std::optional<RoomFileChunk> decodeFileChunk(QByteArrayView bytes);
  ```

  Formato JSON `{"v":1,"t":"<tipo>",...}`, no estilo de `BrowserMediaSnapshotCodec`.
- [ ] **Step 1: Testes que falham**:
  - ida e volta de cada variante;
  - recusa:
    - mensagem acima de 256 KiB;
    - `"v":2`;
    - tipo desconhecido;
    - campo faltando ou de tipo errado;
    - fila com 501 itens;
    - letra acima de 64 KiB;
    - apelido inválido (usa `canonicalNickname`);
    - hash fora do padrão hexadecimal de 64 caracteres;
    - permissão fora do intervalo;
    - `RoomDriftReport` acima de ±60.000 ms;
  - chunk com payload acima de 64 KiB ou cabeçalho truncado → `nullopt`.
- [ ] **Step 2:** FAIL.
- [ ] **Step 3:** Implementar.
- [ ] **Step 4:** PASS.
- [ ] **Step 5:** Commit `feat: protocolo de mensagens da sala`.

---

## Fatia S5 — Servidor de apresentação

### Task S5.1: Worker e Durable Object

**Files:**
- Create:
  - `services/room-signaling/package.json`;
  - `services/room-signaling/wrangler.toml`: Durable Object `RoomRelay` com `new_sqlite_classes`, para o plano gratuito;
  - `services/room-signaling/tsconfig.json`;
  - `services/room-signaling/vitest.config.ts`;
  - `services/room-signaling/src/index.ts`;
  - `services/room-signaling/src/RoomRelay.ts`;
  - `services/room-signaling/src/joinPage.ts`;
  - `services/room-signaling/test/relay.test.ts`;
  - `services/room-signaling/README.md`.
- Modify: `docs/browser-extension.md`, ou um novo `docs/room-signaling.md`, com os comandos.

**Interfaces:**
- Produces:
  - **Host:** `GET /rooms/<room_id>/host` (upgrade WebSocket), com o header `X-Room-Host-Token: <b64url>`. A primeira conexão guarda `SHA-256(token)` no armazenamento do objeto. Outro host com um token diferente recebe 409.
  - **Convidado:** `GET /rooms/<room_id>/guest` (upgrade). O objeto atribui um `peer` (u32).
  - **Envelopes JSON:**
    - `{to: peer|"host", data: <b64 ciphertext>}`;
    - chegam como `{from: peer|"host", data}`;
    - convidados só falam com o host.
  - **Eventos do servidor:**
    - `{event:"peer-left", peer}` para o host;
    - `{event:"host-offline"}` para convidados, também na conexão de um convidado sem host.
  - **Página:** `GET /j/<room_id>` devolve a página "Abra no RC Music", com o botão Copiar o link. O fragmento fica no navegador.
  - **Limites:** 16 KiB, 20 mensagens/s e 11 sockets. Após 10 min sem host, um alarme fecha a sala e apaga o armazenamento.
  - **Privacidade:** sem logs de conteúdo nem de IP (`console.log` proibido fora dos testes).

- [ ] **Step 0:** Pedir ao mantenedor autorização para o `npm install`:
  - pacotes: `wrangler`, `@cloudflare/vitest-pool-workers`, `vitest` e `typescript`;
  - versões fixadas e `package-lock.json` commitado.
- [ ] **Step 1: Testes que falham** (Vitest no pool de Workers):
  - o host conecta, o convidado conecta, um envelope do convidado chega ao host com `from` e um envelope do host chega só ao convidado certo;
  - um convidado tentando falar com outro convidado → descartado;
  - mensagem de 16 KiB + 1 → socket fechado com 1009;
  - a 21ª mensagem no mesmo segundo → descartada;
  - o 12º socket → recusado;
  - segundo host com outro token → 409;
  - host cai → convidados recebem `host-offline`;
  - o alarme de 10 min apaga o estado;
  - a página `/j/` não contém o fragmento nem scripts externos.
- [ ] **Step 2:** `npx vitest run` dentro de `services/room-signaling` → FAIL.
- [ ] **Step 3:** Implementar com a API de hibernação (`state.acceptWebSocket`) e o papel e o `peer` em `serializeAttachment`.
- [ ] **Step 4:** PASS, e `npx tsc --noEmit` limpo.
- [ ] **Step 5:** Commit `feat: servidor de apresentação da sala no Cloudflare Workers`.
- [ ] **Step 6:** O README traz os passos do mantenedor:
  - criar a conta gratuita, sem cartão;
  - `npx wrangler login`;
  - `npx wrangler deploy`;
  - copiar a URL `*.workers.dev` para a configuração de build `RC_MUSIC_ROOM_SIGNALING_ORIGIN`.

  O agente **não** faz o deploy.

---

## Fatia S6 — Transporte P2P

### Task S6.1: libdatachannel no build

**Files:**
- Modify:
  - o runner de configuração (`.psd1`) e `docs/development-commands.md`: campo `DataChannelPrefix`;
  - `cmake/RcMusicDesktop.cmake`: `find_package(LibDataChannel CONFIG REQUIRED)` só quando `RC_MUSIC_LISTENING_ROOM=ON`, padrão ON no produto.
- Create: `cmake/RcMusicListeningRoom.cmake` e `tests/cmake/ListeningRoomBuildBoundaryTests.cmake`.

- [ ] **Step 0:** Pedir ao mantenedor autorização para baixar a libdatachannel. Usar a tag estável mais recente, com SHA-256 registrado no ADR-0022, mais os submódulos (libjuice, usrsctp e plog). Compilar com `-DNO_MEDIA=ON -DNO_EXAMPLES=ON -DNO_TESTS=ON` e a pilha TLS recomendada pelo projeto para Windows (OpenSSL 3.x), instalando em `out/product/libdatachannel-<versão>`.
- [ ] **Step 1:** Teste de fronteira de build que falha:
  - `rc_music_core` não linka `LibDataChannel::LibDataChannel`;
  - só `rc_music_listening_room` linka.
- [ ] **Step 2:** FAIL.
- [ ] **Step 3:** Configurar.
- [ ] **Step 4:** PASS. O build Release copia as DLLs ao lado do executável no runtime local.
- [ ] **Step 5:** Commit `build: libdatachannel para a sala de escuta`.

### Task S6.2: Identidade, cifra dos envelopes e porta de transporte

**Files:**
- Create:
  - `include/m0/ports/RoomTransport.hpp`;
  - `src/adapters/WindowsRoomIdentity.{hpp,cpp}`;
  - `src/adapters/WindowsRoomEnvelopeCipher.{hpp,cpp}`;
  - `tests/qt/WindowsRoomIdentityTests.cpp`;
  - `tests/qt/WindowsRoomEnvelopeCipherTests.cpp`;
  - `tests/support/FakeRoomTransport.hpp`.

**Interfaces:**
- Produces:

  ```cpp
  // ports/RoomTransport.hpp
  enum class RoomLinkFailure : std::uint8_t { SignalingUnavailable, HostOffline, Unreachable, Refused, VersionMismatch, Closed };
  class RoomLink {   // one admitted or pending peer connection; Qt-free, the adapter runs the S4 codec and drops invalid input
    public:
      virtual ~RoomLink() = default;
      [[nodiscard]] virtual std::string peerFingerprint() const = 0;   // hex SHA-256 of the remote DTLS certificate
      virtual bool sendControl(const RoomControlMessage&) = 0;
      virtual bool sendMedia(const RoomFileChunk&) = 0;
      [[nodiscard]] virtual std::size_t mediaBufferedBytes() const = 0;
      virtual void close() = 0;
      std::function<void(RoomControlMessage)> onControl; std::function<void(RoomFileChunk)> onMedia;
      std::function<void(RoomLinkFailure)> onClosed;
  };
  class RoomHostEndpoint {
    public:
      virtual ~RoomHostEndpoint() = default;
      virtual void open(const RoomInviteLink&, std::function<void(std::shared_ptr<RoomLink>)> on_incoming,
                        std::function<void(RoomLinkFailure)> on_failure) = 0;
      virtual void closeAll() = 0;
  };
  class RoomGuestConnector {
    public:
      virtual ~RoomGuestConnector() = default;
      virtual void connect(const RoomInviteLink&, std::function<void(std::shared_ptr<RoomLink>)> on_connected,
                           std::function<void(RoomLinkFailure)> on_failure) = 0;
      virtual void cancel() = 0;
  };
  // adapters
  struct RoomIdentity { std::string certificate_pem; std::string key_pem; std::array<std::uint8_t,32> fingerprint; };
  [[nodiscard]] std::optional<RoomIdentity> loadOrCreateRoomIdentity(const QString& config_directory); // room-identity.pem, key protected by DPAPI
  class WindowsRoomEnvelopeCipher { public: explicit WindowsRoomEnvelopeCipher(std::span<const std::uint8_t,16> secret, std::span<const std::uint8_t,16> room_id);
      [[nodiscard]] QByteArray seal(QByteArrayView plain) const;              // 12-byte random nonce | AES-256-GCM ciphertext+tag, AAD = room_id, key = HKDF-SHA256(secret, "rc-music room signaling v1")
      [[nodiscard]] std::optional<QByteArray> open(QByteArrayView sealed) const;
      [[nodiscard]] QByteArray hostToken() const; };                          // HKDF(secret, "rc-music room host v1")
  ```

- [ ] **Step 1: Testes que falham**:
  - **Identidade:**
    - `loadOrCreateRoomIdentity` cria, e a segunda chamada devolve a mesma impressão digital;
    - o arquivo em disco não contém `PRIVATE KEY` em texto claro;
    - um arquivo corrompido → `nullopt`, sem sobrescrever.
  - **Cifra:**
    - `seal` e `open` fazem ida e volta;
    - dois `seal` do mesmo texto diferem;
    - um byte trocado → `nullopt`;
    - outro segredo ou outro `room_id` → `nullopt`;
    - vetor de teste HKDF da RFC 5869 (caso 1) contra a função interna.
- [ ] **Step 2:** FAIL.
- [ ] **Step 3:** Implementar:
  - BCrypt para HKDF e AES-GCM;
  - NCrypt/`CertCreateSelfSignCertificate` para gerar o certificado ECDSA P-256;
  - exportação em PEM, com a chave cifrada por `CryptProtectData`.

  Conferir em `rtc::Configuration` que `certificatePemFile`, `keyPemFile` e `keyPemPass` existem na versão escolhida. Se não existirem, parar e registrar no ADR-0022 antes de seguir.
- [ ] **Step 4:** PASS.
- [ ] **Step 5:** Commit `feat: identidade e cifra da sala`.

### Task S6.3: Transporte libdatachannel

**Files:**
- Create:
  - `src/adapters/DataChannelRoomTransport.{hpp,cpp}`;
  - `tests/qt/DataChannelRoomTransportTests.cpp`;
  - `tests/support/LoopbackSignaling.hpp`: servidor WebSocket local em `127.0.0.1` com o mesmo protocolo da S5, em C++, usando o `rtc::WebSocketServer` da libdatachannel.

**Interfaces:**
- Consumes: as portas e a cifra da S6.2, e `room_protocol_version` (S2.1).
- Produces:
  - `DataChannelRoomHostEndpoint(QString signaling_origin, RoomIdentity, std::vector<std::string> ice_servers)`;
  - `DataChannelRoomGuestConnector(...)`, com os mesmos parâmetros.
  - ICE padrão: `{"stun:stun.cloudflare.com:3478"}`. Nos testes, a lista é vazia (só candidatos locais).
  - Canais `control` e `media`, ambos confiáveis e ordenados.
  - **Verificação da impressão digital:** o convidado confere a impressão digital DTLS do host contra o `host_fingerprint` do link e fecha com `Refused` se divergir.

- [ ] **Step 1: Testes que falham**, com dois pares no mesmo processo, `LoopbackSignaling` e ICE só local:
  - host e convidado conectam, e `control` e `media` trafegam nos dois sentidos;
  - o `peerFingerprint()` de cada lado bate com a identidade do outro;
  - um link com outra impressão digital do host → `Refused`;
  - host ausente → `HostOffline`;
  - servidor desligado → `SignalingUnavailable`;
  - host fecha → `onClosed(Closed)` no convidado em até 10 s (Review Focus 5);
  - o servidor de loopback registra que só vê envelopes cifrados: nenhum `a=fingerprint` em claro.
- [ ] **Step 2:** FAIL.
- [ ] **Step 3:** Implementar. Os callbacks da libdatachannel chegam em threads próprias: repassar para a thread do Qt com `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`.
- [ ] **Step 4:** PASS, 10 vezes seguidas sem falha intermitente (`ctest --repeat until-fail:10`).
- [ ] **Step 5:** Commit `feat: conexão P2P da sala`. Fim da fatia: rodar `fullcheck.sh`.

---

## Fatia S7 — Host e convidado de ponta a ponta

### Task S7.1: Caso de uso do host

**Files:**
- Create:
  - `include/m0/application/ListeningRoomHost.hpp`;
  - `src/application/ListeningRoomHost.cpp`;
  - `tests/core/ListeningRoomHostTests.cpp`.

**Interfaces:**
- Consumes:
  - `ListeningRoom`, `RoomRoster` e `RoomRosterStore` (S3);
  - `PlayerController` (existente);
  - `RoomHostEndpoint` (S6.2);
  - um relógio injetado `std::function<std::int64_t()> host_clock_ms`;
  - as mensagens puras de `RoomMessages.hpp` (S4), enviadas pelo `RoomLink`;
  - uma porta nova `include/m0/ports/RoomTrackCatalog.hpp`: `class RoomTrackCatalog { public: virtual ~RoomTrackCatalog() = default; virtual void describe(std::filesystem::path, std::function<void(std::optional<RoomItem>)> done) = 0; };`. O adaptador `src/adapters/QtRoomTrackCatalog.{hpp,cpp}` calcula o SHA-256 e lê os metadados no worker existente, fora da thread da UI, e também anexa a letra escolhida pelo host (até `maximum_room_lyrics_bytes`), lida do `LyricsVariantsStore`.
- Produces:

  ```cpp
  struct RoomOpenOptions { std::size_t max_listeners{default_room_listeners}; bool until_revoked{false}; std::uint64_t default_role_id{1}; };
  class ListeningRoomHost {
    public:
      [[nodiscard]] std::optional<RoomInviteLink> open(const RoomOpenOptions&);  // reuses the persisted invite when until_revoked and present
      void close();                         // "Encerrar sala": keeps the persisted invite
      void revoke();                        // closes, forgets the persisted invite, drops everyone
      [[nodiscard]] bool approve(std::string_view fingerprint); [[nodiscard]] bool reject(std::string_view fingerprint);
      [[nodiscard]] bool kick(std::string_view fingerprint, bool block);
      [[nodiscard]] RoomSnapshot snapshot() const;
      std::function<void(const RoomSnapshot&)> onChanged; std::function<void(const RoomMember&)> onJoinRequest;
  };
  ```

  - Mapeia a fila do `PlayerController` para `RoomItem`s: `HostTrack` com hash e resumo vindos do leitor de metadados existente, por um `RoomTrackCatalog` injetado.
  - Incrementa `version` a cada mudança da fila, da reprodução ou dos membros, e transmite `RoomStateMessage` com as permissões efetivas de cada membro.
  - Aplica as intenções autorizadas chamando `PlayerController` (next, previous, seek, pause, play, enqueue, remove, move).
  - Os itens `GuestTrack` ficam fora do `SessionSnapshot`.
- [ ] **Step 1: Testes que falham**, com `FakeRoomTransport`, `DeterministicPlaybackBackend` e um relógio falso:
  - **Abrir:** `open` sem convite persistido gera um link novo; com `until_revoked`, salva o convite, e o próximo `open` reaproveita o mesmo link.
  - **Revogar:** `revoke` apaga o convite, e o próximo `open` gera outro.
  - **Entrada de conhecido:** o `Hello` de um conhecido recebe `JoinResult{Admitted}` e o estado.
  - **Entrada de desconhecido:** recebe `Pending` e dispara `onJoinRequest`; depois do `approve` recebe o estado.
  - **Intenções:**
    - `Next` de um DJ avança o player e a versão;
    - `Next` de um Ouvinte → `IntentRejected{MissingPermission}`, sem mudança.
  - **Ações do próprio host:** pausar no app muda o estado e transmite o snapshot com a âncora `(host_clock, position)`.
  - **Encerrar:** `close()` manda `Bye` e mantém o convite persistido.
  - **Itens de convidados:** o `SessionSnapshot` salvo durante a sala não contém `GuestTrack`.
  - **Abrir sala explícito:** um host construído com convite persistido não chama `RoomHostEndpoint::open` até `open()` ser chamado.
  - **Versão:** um `Hello` com `protocol_version = 2` recebe `Bye{VersionMismatch}` e não entra.
  - **Controles exclusivos do host:** aleatório, repetir e fim da fila mudados pelo host aparecem na ordem do snapshot; não há intenção de convidado para eles.
- [ ] **Step 2:** FAIL.
- [ ] **Step 3:** Implementar.
- [ ] **Step 4:** PASS.
- [ ] **Step 5:** Commit `feat: host da sala de escuta`.

### Task S7.2: Caso de uso do convidado

**Files:**
- Create:
  - `include/m0/application/ListeningRoomGuest.hpp`;
  - `src/application/ListeningRoomGuest.cpp`;
  - `tests/core/ListeningRoomGuestTests.cpp`.

**Interfaces:**
- Produces:

  ```cpp
  enum class GuestRoomState : std::uint8_t { Idle, Connecting, WaitingApproval, Joined, Reconnecting, Ended };
  class ListeningRoomGuest {
    public:
      void join(const RoomInviteLink&); void leave();
      [[nodiscard]] std::uint64_t sendIntent(const RoomIntent&);  // returns request_id
      [[nodiscard]] GuestRoomState state() const; [[nodiscard]] const RoomSnapshot& snapshot() const;
      [[nodiscard]] RoomPermissionSet permissions() const;
      std::function<void()> onChanged; std::function<void(std::uint64_t, IntentRejection)> onRejected;
      std::function<void(RoomLinkFailure)> onEnded;
  };
  ```

  - Ao entrar em `Joined`, guarda a fila local (`SessionSnapshot`) e a restaura ao sair ou ao acabar a sala.
  - Ignora snapshots com versão menor ou igual à atual.
- [ ] **Step 1: Testes que falham**:
  - sequência `Connecting → WaitingApproval → Joined`;
  - um snapshot antigo (versão 3 depois da 5) é ignorado;
  - `leave()` restaura exatamente a fila e a posição anteriores;
  - um `RoomIntentRejected` dispara `onRejected` com o mesmo `request_id`;
  - queda do link → `Reconnecting`: reconecta em até 30 s com o mesmo link e volta a `Joined`; depois dos 30 s, `Ended` e a fila restaurada;
  - `Bye` do host → `Ended` com `onEnded(Closed)`.
- [ ] **Step 2:** FAIL.
- [ ] **Step 3:** Implementar.
- [ ] **Step 4:** PASS.
- [ ] **Step 5:** Commit `feat: convidado da sala de escuta`.

### Task S7.3: Página Ouvir junto (básica)

**Files:**
- Create:
  - `src/presentation/ListeningRoomBinding.{hpp,cpp}`;
  - `src/presentation/RoomMembersModel.{hpp,cpp}`;
  - `src/ui/qml/ListeningRoomPanel.qml`;
  - `src/ui/qml/RoomText.qml`;
  - `tests/qt/ListeningRoomBindingTests.cpp`;
  - `tests/ui/quick/tst_listening_room.qml`.
- Modify:
  - `SideNavigation.qml` e `NavigationText.qml`: item "Ouvir junto" com o ícone `people`, a desenhar na grade dos ícones;
  - `Main.qml`;
  - `TransportBar.qml`: selo "Na sala de <apelido> · N pessoas";
  - `src/app/main.cpp`: composição, com a origem vinda de `RC_MUSIC_ROOM_SIGNALING_ORIGIN`. Sem origem configurada, a página mostra "Sala indisponível neste build".

**Interfaces:**
- Produces: `ListeningRoomBinding` com:
  - `role` (0 nenhum, 1 host, 2 convidado), `state`, `inviteLink`, `members` e `pendingRequests`;
  - `Q_INVOKABLE`: `openRoom(int maxListeners, bool untilRevoked)`, `closeRoom()`, `revoke()`, `copyLink()`, `approve(QString)`, `reject(QString)`, `join(QString link)`, `leave()`.

- [ ] **Step 1: Testes que falham**:
  - **Binding:**
    - `join` com texto inválido → `false` e o erro "Link de convite inválido";
    - `openRoom(11, false)` → recusado;
    - `openRoom(10, false)` → aceito.
  - **QML:**
    - **Fora da sala:** mostra "Criar sala" e "Entrar com link".
    - **Formulário do host:**
      - o seletor de lotação vai de 1 a 10, com padrão 5;
      - a duração tem duas opções, com padrão "termina quando eu fechar o app".
    - **Pedidos:** um pedido pendente mostra Aceitar e Recusar, e os dois respondem ao teclado.
    - **Aviso de privacidade:** o aviso sobre o IP aparece uma vez, no primeiro uso.
    - **Selo:** o selo da barra aparece só durante a sala.
- [ ] **Step 2:** FAIL.
- [ ] **Step 3:** Implementar. O pedido de entrada com o app em segundo plano usa a notificação existente do sistema (`PlatformIntegration`).
- [ ] **Step 4:** PASS, mais o contrato de UI.
- [ ] **Step 5:** Commit `feat: página Ouvir junto`.

### Task S7.4: Ponta a ponta com transporte real

**Files:**
- Create:
  - `tests/qt/ListeningRoomEndToEndTests.cpp`: dois `ListeningRoomHost`/`Guest` sobre `DataChannelRoomTransport` e `LoopbackSignaling`, com o playback determinístico.

- [ ] **Step 1: Testes que falham**:
  - criar, entrar como desconhecido, aprovar, receber o estado e pular com DJ;
  - sala cheia → "A sala está cheia";
  - **revogar:**
    - o convidado sai;
    - o link antigo passa a dar `HostOffline`, porque o host não abre aquela sala;
    - um link novo funciona;
  - **(Review Focus 5)** destruir o host sem `Bye` → o convidado chega a `Ended` em até 10 s, com a fila local restaurada;
  - **(Review Focus 1)** derrubar o link do convidado durante a música → ele volta a `Joined` com a mesma versão.
- [ ] **Step 2:** FAIL.
- [ ] **Step 3:** Corrigir o que faltar.
- [ ] **Step 4:** PASS, com `--repeat until-fail:10`.
- [ ] **Step 5:** Commit `test: sala de ponta a ponta em loopback`. Fim da fatia: `fullcheck.sh` e o guia do usuário (Ouvir junto, básico).

---

## Fatia S8 — Transferência e cache

### Task S8.1: Envio e recepção de arquivos por hash

**Files:**
- Create:
  - `src/adapters/RoomFileTransfer.{hpp,cpp}`;
  - `src/adapters/RoomCache.{hpp,cpp}`;
  - `tests/qt/RoomFileTransferTests.cpp`;
  - `tests/qt/RoomCacheTests.cpp`.

**Interfaces:**
- Produces:

  ```cpp
  class RoomCache { public: explicit RoomCache(QString config_directory); // <config>/room-cache
      [[nodiscard]] std::optional<QString> pathFor(std::string_view sha256) const;   // only complete, verified files
      [[nodiscard]] bool reserve(std::uint64_t bytes);                                // false above room_cache_quota_bytes after evicting played items
      void purgeAll(); };                                                             // on leave and at startup
  class RoomFileSender { public: void offer(RoomLink&, const QString& local_path, std::string_view sha256, std::uint32_t transfer_id);
      void pump(RoomLink&); };   // sends chunks while mediaBufferedBytes() < 1 MiB
  class RoomFileReceiver { public: explicit RoomFileReceiver(RoomCache&);
      [[nodiscard]] bool accept(const RoomFileOffer&); [[nodiscard]] bool consume(const RoomFileChunk&);
      std::function<void(std::string /*sha256*/, QString /*path*/)> onComplete;
      std::function<void(std::string, RoomTransferError)> onFailed; };
  enum class RoomTransferError : std::uint8_t { TooLarge, HashMismatch, OutOfOrder, QuotaExceeded, Io };
  ```

  O arquivo parcial fica em `room-cache/<sha>.part` e é renomeado só depois de conferir o SHA-256.
- [ ] **Step 1: Testes que falham**, com o fixture de áudio pequeno do corpus de testes:
  - envio completo → `onComplete` com um hash igual ao do original;
  - **(Review Focus 4)**:
    - tamanho declarado menor que o real → `TooLarge` e o `.part` apagado;
    - chunk adulterado → `HashMismatch`;
  - chunk fora de ordem → `OutOfOrder`;
  - quota cheia → `QuotaExceeded`;
  - `purgeAll` remove `.part` e arquivos completos;
  - **(Review Focus 1)** retomada: com um `.part` de 3 chunks, `RoomFileRequest{offset}` continua dali e termina com o hash certo;
  - um caminho com `..` ou um separador no hash → recusado (o nome vem do hash validado).
- [ ] **Step 2:** FAIL.
- [ ] **Step 3:** Implementar com `QCryptographicHash::Sha256`.
- [ ] **Step 4:** PASS.
- [ ] **Step 5:** Commit `feat: transferência de músicas da sala`.

### Task S8.2: Antecipação e "Carregando…"

**Files:**
- Modify:
  - `ListeningRoomHost` e `ListeningRoomGuest` (S7);
  - `ListeningRoomEndToEndTests.cpp`;
  - `QueuePanel.qml` e `QueueText.qml`: avatar de quem adicionou e o estado "Carregando…".

- [ ] **Step 1: Testes que falham**:
  - o host envia o item atual primeiro, depois os próximos `room_prefetch_items` (3) na ordem da fila;
  - um convidado que já tem o hash não recebe de novo;
  - um convidado que entra no meio da música recebe primeiro o item atual;
  - mudar a fila durante o envio cancela as transferências de itens que saíram;
  - ao sair, o cache fica vazio;
  - quando o item atual tem letra escolhida pelo host, o convidado recebe `RoomLyrics` e o painel de letras mostra a letra sincronizada; letra acima de 64 KiB não é enviada;
  - no QML, um item sem arquivo mostra "Carregando…" e o avatar do autor;
  - no convidado, os botões de aleatório e repetir ficam desabilitados com a dica "O host controla".
- [ ] **Step 2:** FAIL.
- [ ] **Step 3:** Implementar.
- [ ] **Step 4:** PASS.
- [ ] **Step 5:** Commit `feat: antecipa as próximas músicas da sala`. Fim da fatia: `fullcheck.sh`.

---

## Fatia S9 — Sincronia real

### Task S9.1: Seguir o host no tempo

**Files:**
- Create:
  - `include/m0/application/RoomPlaybackFollower.hpp`;
  - `src/application/RoomPlaybackFollower.cpp`;
  - `tests/core/RoomPlaybackFollowerTests.cpp`.
- Modify:
  - `ListeningRoomGuest`, que passa a usar o follower;
  - `PreferencesSnapshot` e o codec: `room_output_offset_ms`, de −500 a 500, padrão 0;
  - `PreferencesPanel.qml` e `ListeningRoomPanel.qml`: o ajuste de atraso;
  - `PlaybackBackend`: `setPlaybackRate(double)` com padrão "não suportado", só se a S1 aprovou a correção por velocidade.

**Interfaces:**
- Consumes: `RoomClockEstimator`, `expectedPositionMs` e `classifyDrift` (S3.4); os limiares definidos no relatório da S1.
- Produces:

  ```cpp
  class RoomPlaybackFollower {
    public:
      RoomPlaybackFollower(PlaybackBackend&, std::function<std::int64_t()> local_clock_ms, std::int64_t nudge_ms, std::int64_t seek_ms);
      void setOutputOffsetMs(std::int64_t);
      void apply(const RoomPlayback&, std::optional<QString> /*local file of current item*/);
      void tick();   // every 1000 ms: compares backend position with expectedPositionMs(host_now + offset)
      void addClockSample(const ClockSample&);
  };
  ```

- [ ] **Step 1: Testes que falham**, com relógio falso e backend determinístico:
  - **Comando de tocar:**
    - `apply(playing, anchor_host = now_host + 300)` não toca antes do instante;
    - no instante, carrega e toca da posição certa;
    - o offset de saída de +100 ms faz tocar 100 ms antes.
  - **Correção:**
    - drift de 200 ms → um seek para a posição esperada;
    - drift de 60 ms → velocidade 1,0x ±δ por um tempo limitado, ou nenhuma ação se a velocidade não for suportada;
    - drift de 20 ms → nada.
  - **Pausa:** pausar segura na posição da âncora.
  - **Arquivo ausente:** sem arquivo local, o follower espera e entra na posição esperada quando o arquivo chegar.
- [ ] **Step 2:** FAIL.
- [ ] **Step 3:** Implementar.
- [ ] **Step 4:** PASS.
- [ ] **Step 5:** Commit `feat: convidado segue o tempo do host`.

### Task S9.2: Medição da diferença

**Files:**
- Modify:
  - `ListeningRoomGuest` e `ListeningRoomHost`: o convidado envia a cada 5 s `RoomClockPing` e um `RoomDriftReport` (definido na S4) com a diferença estimada mais recente;
  - painel de diagnóstico existente (`ShellDiagnosticBinding`): lista a diferença por pessoa.

- [ ] **Step 1: Testes que falham**:
  - o host guarda a última diferença por membro;
  - o diagnóstico mostra o apelido e a diferença em ms, sem a impressão digital.
- [ ] **Step 2:** FAIL.
- [ ] **Step 3:** Implementar.
- [ ] **Step 4:** PASS.
- [ ] **Step 5:** Commit `feat: mostra a diferença de sincronia por pessoa`. Fim da fatia: `fullcheck.sh`.

---

## Fatia S10 — Permissões, cargos e moderação na UI

### Task S10.1: Cargos e ajustes

**Files:**
- Create:
  - `src/presentation/RoomRolesModel.{hpp,cpp}`;
  - `src/ui/qml/RoomRolesEditor.qml`;
  - `src/ui/qml/RoomMemberMenu.qml`;
  - `tests/qt/RoomRolesModelTests.cpp`;
  - `tests/ui/quick/tst_listening_room_roles.qml`.
- Modify:
  - `ListeningRoomBinding`: `createRole`, `renameRole`, `setRolePermission`, `removeRole`, `assignRole`, `setOverride`, `kick` e `block`, que gravam no `RoomRosterStore`;
  - `TransportBar.qml` e `QueuePanel.qml`: controles desabilitados sem permissão, com a dica "O host não liberou".

- [ ] **Step 1: Testes que falham**:
  - **Cargos:**
    - criar o cargo "Amigos" com Pular e Pausar;
    - atribuir a uma pessoa → o convidado recebe as novas permissões no próximo estado;
    - ajuste individual negando Pausar → o botão de pausa do convidado fica desabilitado.
  - **Moderação:**
    - expulsar com bloqueio → a pessoa sai e, ao tentar voltar, recebe `RejectedBlocked`;
    - "Desbloquear" na lista de conhecidos libera.
  - **Teclado:** o editor de cargos funciona só pelo teclado, e cada caixa de permissão tem nome acessível.
  - **Proteção do cargo:** excluir o cargo inicial do convite → recusado, com a mensagem "Escolha outro cargo inicial antes".
- [ ] **Step 2:** FAIL.
- [ ] **Step 3:** Implementar.
- [ ] **Step 4:** PASS.
- [ ] **Step 5:** Commit `feat: cargos e permissões na sala`. Fim da fatia: `fullcheck.sh`.

---

## Fatia S11 — Visibilidade das playlists

### Task S11.1: Campo de visibilidade

**Files:**
- Modify:
  - `include/m0/domain/PlaylistLibrary.hpp`: `enum class PlaylistVisibility : std::uint8_t { Private, Room };` em `NamedPlaylist::visibility{Private}` e `[[nodiscard]] bool setVisibility(std::uint64_t, PlaylistVisibility);`;
  - `src/domain/PlaylistLibrary.cpp`;
  - `src/adapters/QtJsonPlaylistStore.cpp`: nova versão do esquema; arquivos antigos carregam como `Private`;
  - `PlaylistsBinding` e `PlaylistsPanel.qml`: a chave "Visível para a sala".
- Test:
  - `tests/core/PlaylistLibraryTests.cpp`;
  - `tests/qt/QtJsonPlaylistStoreTests.cpp`;
  - `tests/qt/PlaylistsBindingTests.cpp`;
  - `tests/ui/quick/tst_playlists.qml`.

- [ ] **Step 1: Testes que falham**:
  - uma playlist nova é `Private`;
  - `setVisibility` com ID inexistente → `false`;
  - o arquivo da versão anterior carrega com tudo `Private`;
  - ida e volta preserva `Room`;
  - a chave da UI tem nome acessível e muda o estado.
- [ ] **Step 2:** FAIL.
- [ ] **Step 3:** Implementar.
- [ ] **Step 4:** PASS.
- [ ] **Step 5:** Commit `feat: playlists visíveis para a sala`.

### Task S11.2: Catálogo e "adicionar playlist"

**Files:**
- Modify:
  - `ListeningRoomHost`: monta o `RoomCatalog` só com as playlists `Room` e os resumos das faixas, sem caminhos; retransmite quando a visibilidade muda; resolve `AddItems` (IDs do catálogo) e `AddPlaylist` em entradas da fila;
  - `ListeningRoomGuest` e `ListeningRoomPanel.qml`: lista "Playlists do host", com "Adicionar" e "Adicionar todas".
- Test: `ListeningRoomHostTests` e `tst_listening_room.qml`.

- [ ] **Step 1: Testes que falham**:
  - o catálogo não contém playlists privadas nem caminhos (procurar `":\\"` e `"/"` no JSON codificado);
  - tornar uma playlist privada durante a sala a remove do catálogo dos convidados;
  - `AddPlaylist` com permissão enfileira todas as faixas na ordem;
  - `AddPlaylist` sem a permissão → `MissingPermission`;
  - `AddItems` com um ID de catálogo de playlist privada → `UnknownItem`;
  - passar de 500 itens → `QueueFull`.
- [ ] **Step 2:** FAIL.
- [ ] **Step 3:** Implementar.
- [ ] **Step 4:** PASS.
- [ ] **Step 5:** Commit `feat: convidados adicionam das playlists visíveis`. Fim da fatia: `fullcheck.sh`.

---

## Fatia S12 — Músicas de convidados

### Task S12.1: Enviar música própria

**Files:**
- Modify:
  - `ListeningRoomGuest`: `uploadTrack(QString local_path)`;
  - `ListeningRoomHost`: recebe em `RoomFileReceiver`, valida e enfileira como `GuestTrack`;
  - `ListeningRoomPanel.qml` e `QueuePanel.qml`: "Adicionar música minha", que usa `QtAudioFilesPicker` e só aparece com a permissão.
- Test: `ListeningRoomHostTests`, `ListeningRoomEndToEndTests` e `tst_listening_room.qml`.

- [ ] **Step 1: Testes que falham**:
  - **Sem permissão:** o host recusa a oferta antes de receber bytes (`MissingPermission`), e o botão não aparece.
  - **Com permissão:** o arquivo sobe, o host confere com `LocalAudioFilePolicy` e o probe de metadados, e o item entra na fila com `origin=GuestTrack` e `added_by`.
  - **(Review Focus 4)** Um arquivo de texto renomeado para `.mp3` → recusado com "Arquivo de áudio inválido", sem gravar nada.
  - **(Review Focus 4)** Um arquivo de 50 MiB + 1 → recusado antes de transferir.
  - **Dono saindo:** o dono sai, e o item continua e toca para os outros.
  - **Fim da sala:** ao encerrar, os arquivos de convidados somem do cache do host e da fila.
- [ ] **Step 2:** FAIL.
- [ ] **Step 3:** Implementar.
- [ ] **Step 4:** PASS.
- [ ] **Step 5:** Commit `feat: convidados enviam músicas próprias`. Fim da fatia: `fullcheck.sh`.

---

## Fatia S13 — Teste real e documentação final

### Task S13.1: Dois apps neste computador

- [ ] **Step 1:** Adicionar ao build de testes um argumento `--profile-root <dir>`, aceito só quando `RC_MUSIC_TEST_PROFILE_ROOT=ON` e desligado no produto, para um segundo perfil isolado. Incluir o teste de fronteira de build que garante a opção desligada no produto.
- [ ] **Step 2:** Com o Worker publicado pelo mantenedor, montar o runtime com `New-RcMusicLocalRuntime.ps1` e abrir dois apps com perfis separados. Roteiro:
  1. criar a sala;
  2. entrar com o link;
  3. aprovar;
  4. tocar, pular, pausar e adicionar de playlist visível;
  5. enviar música própria;
  6. revogar.

  Medir a diferença com o loopback da S1.
- [ ] **Step 3:** Registrar o resultado em `docs/listening-room-sync-spike.md` (seção "Teste real").

### Task S13.2: Entre redes diferentes

- [ ] **Step 1:** Quando houver o instalador ou outro computador do mantenedor, repetir o roteiro entre redes diferentes (por exemplo, um dos lados no 4G). Registrar:
  - se a conexão direta funcionou;
  - o tempo para entrar;
  - a diferença medida pela S9.2.
- [ ] **Step 2:** Atualizar:
  - `docs/user-guide.md`: Ouvir junto completo, com o aviso de IP e o de direitos autorais;
  - `docs/project-status.md`;
  - ADR-0022 (consequências reais);
  - os sete requisitos da seção 2.10, com evidências.
- [ ] **Step 3:** `validate-docs.ps1`.
- [ ] **Step 4:** Commit `docs: teste real da sala de escuta`.

---

## Ordem e paralelismo

1. S0 primeiro.
2. Depois, S1, cujo veredito pode mudar a S9.
3. Em seguida, três trilhas que podem correr ao mesmo tempo, porque não tocam os mesmos arquivos:
   - S2 → S3 → S4;
   - S5;
   - S6.1.
4. S6.2 e S6.3 dependem de S4 e S6.1.
5. Depois, S7 → S8 → S9.
6. S10, S11 e S12 podem seguir em qualquer ordem depois da S8.
7. S13 por último.
