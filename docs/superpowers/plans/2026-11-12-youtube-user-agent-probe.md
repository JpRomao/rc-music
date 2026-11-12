# Prova temporária de User-Agent — plano

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans, preservando a execução direta na worktree já escolhida. Etapas exigem evidência, não intenção.

**Goal:** executar uma nova tentativa YouTube com somente o User-Agent público validado, comprovando se foi incluído e qual foi o resultado, sem promover alteração permanente.
**Architecture:** hint efêmero na capability, opt-in C++ confiável desligado por padrão, endpoint limitado e protocolo privado estrito v1/v2. Candidato privado com novos artefatos/pins, dados isolados e restauração de todos os aliases próprios.
**Tech Stack:** C++20/Qt 6.11.2, CMake 3.31/Ninja/MSVC 14.44, Qt Test/CTest; nenhuma nova dependência.
**Spec:** [desenho aprovado do teste](../specs/2026-11-12-youtube-user-agent-probe-design.md).

## Global Constraints

- Apenas User-Agent do resolvedor, ASCII imprimível até 512 bytes; sem Cookie, Authorization, Referer ou headers da página. Não corrigir/normalizar valor inválido.
- Default mínimo e mídia direta permanecem sem encaminhamento. O opt-in não vem de página, CLI, ambiente, preferências ou JSON de runtime.
- Preservar DNS/TLS/allowlists, URL assinada, expiração, cotas, isolamento/NoChild, cancelamento e ACKs. Sem UAC/WFP, outras extensões ou fechamento de Brave.
- Não alterar instalação/header Node24223/receipts/pins anteriores. Novos binários recebem novos pins em perfil/recibo novos do produtor confiável; pins continuam obrigatórios.
- Evidência real registra somente booleano de inclusão, enum de recusa, job/resultado/limpeza. Nenhum valor de User-Agent/URL/IP/header/cookie/corpo.
- Reutilizar validação com entradas relevantes idênticas, ampliar por consumidores; nenhuma suíte global por hábito. Exigir contagem positiva e registrar skips/falhas.

## Review Focus

- Hint de resolução antiga/expirada ou de mídia direta nunca habilita User-Agent por engano.
- CR/LF/NUL/DEL/não ASCII, comprimento acima de 512 e bytes extras não atravessam codec nem stream.
- Default v1 continua byte a byte mínimo; um worker antigo não pode aceitar v2 removendo silenciosamente o hint.
- Exceções/redirects/cancelamento não mudam a recusa/ACK nem provocam vazamento de header para destino fora da policy.
- Pins do novo payload e todos os aliases tocados são conferidos/restaurados; arquivo diagnóstico incompleto não é resultado válido.

### Task 1: Hint limitado, opt-in e transporte até o worker

**Files — código:** criar `src/adapters/DownloadUserAgent.hpp`; modificar `YoutubeAudioResolution.hpp/.cpp`, `DirectDownloadSession.hpp/.cpp`, `PinnedDownloadStream.hpp/.cpp`, `DownloadWorkerProtocol.hpp/.cpp` em `src/adapters/`. `WindowsProcessDownloadIo.cpp` já copia o endpoint inteiro: alterar somente se a regressão demonstrar lacuna, não por hábito.
**Files — testes:** `tests/qt/{YoutubeAudioResolution,YoutubeResolverSession,DirectDownloadSession,PinnedDownloadStream,DownloadWorkerProtocol,WindowsProcessDownloadIo,WindowsDownloadNetworkWorker}Tests.cpp`, helpers sintéticos existentes se necessários. Documentar contrato em `docs/download-network.md` e `docs/youtube-audio-and-lyrics.md`.

**Interfaces produzidas:**
- `inline constexpr qsizetype download_max_user_agent_bytes = 512` e `bool validDownloadUserAgent(const QByteArray&)` inline, Qt Core apenas, em `DownloadUserAgent.hpp`; vazio permitido como ausência, demais bytes somente 0x20–0x7e.
- `QByteArray YoutubeResolvedAudioData::user_agent_hint{}` acrescentado ao final; só após validação integral de `http_headers`. Capability mantém valor imutável/efêmero.
- `QByteArray PinnedDownloadEndpoint::user_agent{}` acrescentado ao final. Valor não vazio produz uma única linha `User-Agent: <valor>`; nenhum outro header muda.
- Ao final de `DirectDownloadLimits`, `bool forward_youtube_user_agent{false}` e `std::function<void(bool)> on_request_user_agent{}`. Flag/capability decidem o endpoint em cada hop aprovado. Callback apenas observa inclusão tentada, não reentra/destrói sessão; exceções contidas.
- Protocolo: sem hint mantém request v1 idêntico. Stream com hint usa `RCMD` versão 2: runtime, IP/host/target, campo length-prefixed User-Agent, port/TLS. v2 Resolve/vazio/inválido/truncado/trailing recusados. Respostas permanecem iguais.

**Build de execução:** `out/dg/ua/build` (`<build>` nos comandos abaixo). Configurar Release/Ninja/MSVC14.44 com o ambiente de desenvolvimento já verificado, Qt6.11.2/TagLib existentes, Catch2 existente, `RC_MUSIC_BUILD_DESKTOP`, `RC_MUSIC_BUILD_DOWNLOAD_NETWORK`, `RC_MUSIC_BUILD_DOWNLOADS`, `RC_MUSIC_BUILD_DOWNLOAD_PERSISTENCE`, `RC_MUSIC_BUILD_NATIVE_MESSAGING` e `BUILD_TESTING` ativos. Usar plugin TLS restrito e CRT da composição revisada para os recortes de worker, sem instalar dependências ou enfraquecer gates por skips.

- [x] Escrever regressões: `capturesOnlyValidatedPublicUserAgentHint` espera bytes exatos `RCMusic Synthetic Agent/1`; negativos existentes Cookie/Authorization/duplicatas/injeção continuam recusados. `capabilityRetainsOwnedUserAgentHintAfterResolverExit` cobre captura após saída/EOF, sem poder forjar capability pela página.
- [x] Escrever casos Direct: `youtubeUserAgentRequiresTrustedOptIn` espera endpoint vazio por padrão e bytes exatos somente com flag/capability; mídia direta continua vazia. `youtubeUserAgentSurvivesOnlyApprovedRedirects` preserva hint/target e revalida policy/expiry. `requestUserAgentObserverCannotChangeOutcome` cobre observador ausente/exceção e ACK/cancelamento preservados.
- [x] Escrever casos de fio: `sendsOneValidatedUserAgentWithoutChangingSignedTarget` compara request inteiro em servidor sintético; `invalidUserAgentFailsBeforeConnection` usa CR/LF/NUL/DEL/não ASCII/513 bytes e confirma zero conexão; 512 imprimíveis aceitos, vazio omite.
- [x] Escrever codec: `userAgentStreamUsesStrictVersionTwo`, `minimalRequestsKeepLegacyVersionOne`, `userAgentVersionTwoRejectsMalformedRequests`; testar round-trip exato, Resolve recusado, vazio em v2, comprimentos, truncamento/extra e sintaxe inválida. Adapters/processo reais de teste comprovaram que UA chega no fio e não é removido entre encode/decode/open.
- [x] Acrescentar somente campos/stubs para compilar e rodar os Reds focados; confirmar falhas comportamentais por hint/header/versionamento ausentes. Não aceitar erro de compilação ou zero testes como Red válido.
- [x] Implementar o mínimo das interfaces acima, mantendo todos os controles e defaults, obter Green dos recortes e consumidores. Revisar callback contra reentrância, API/ABI e ausência de logs de valores.
- [x] Fechamento agrupado: build de `rc_music_youtube_resolution_tests`, `rc_music_youtube_resolver_session_tests`, `rc_music_DirectDownloadSession_tests`, `rc_music_PinnedDownloadStream_tests`, `rc_music_DownloadWorkerProtocol_tests`, `rc_music_WindowsProcessDownloadIo_tests`, `rc_music_WindowsDownloadNetworkWorker_tests`, `rc_music_WindowsPreparedDownloadTask_tests`, `rc_music_WindowsConfiguredDownloadExecution_tests`; `ctest --test-dir <build> -C Release --output-on-failure -V -R '^rc_music\.(youtube\.(resolution|resolver-session)|download-network\.(DirectDownloadSession|PinnedDownloadStream|DownloadWorkerProtocol|WindowsProcessDownloadIo|WindowsDownloadNetworkWorker|WindowsPreparedDownloadTask|WindowsConfiguredDownloadExecution))$'`. Resultado: **9/9 CTest, 364 casos Qt**, sem falhas/skips/warnings; 29,10 s do runner. Formato de 17 C++ dry-run, docs/traceability/diff passaram; não é suíte global nem aquisição pública.

### Task 2: Candidato autenticado, tentativa explícita e restauração

**Files:** artefatos privados novos em `out/dg/ua/`; não sobrescrever os da investigação HTTP403. Consumir as receitas reais, as fontes MSVC/Qt revisadas e `out/product/download-tools-node24223-20261112/PlayerDownloadPackageProfile.hpp` original; sem mudar seu SHA/pins.
Criar no diretório privado: `UserAgentProbeProfile.hpp` (wrapper/opt-in/pins/sinks fechados), `ProbeBuildInit.cmake`/`ProbeTarget.cmake` (defer e target `rc_music_user_agent_probe_shell`), `prepare-probe-runtime.py` (cópia/autenticação sem dados pessoais), `probe-runtime-manifest.json` (proveniência/hashes novos) e backups/resultados operacionais. Runtime em `out/dg/ua/runtime`, dados novos em `out/dg/ua/data`.
**Interfaces consumidas:** as da Task1; composição `profile.execution.job.network.forward_youtube_user_agent = true`, observadores booleano/Restricted em arquivos novos completos, perfil de rede com hashes novos autenticados. Native mantém `chrome-extension://lhcdmfamadilnahlckpalijljfohjgkc/` e usa endpoint `rc-music-user-agent-probe-20261112`; app/host irmãos `RCMusic.exe`/`rc_music_youtube_native_host.exe`. O planejador real `New-RcMusicYoutubeNativeManifestPlan` confere o manifesto/recibo do novo host; isso não substitui a prova de IPC/recebimento.

- [ ] Preparar build Release privado com shell main/UI/presenter/owner reais e somente `--m0-test-config-root` absoluto; sem cenários/executor/worker fictícios. Buildar `rc_music_download_network_bootstrap`, `rc_music_download_network_payload`, host YouTube e shell diagnóstico. Conferir dependências reais e registrar novo perfil/manifesto de pins a partir de outputs do build confiável, preservando ferramentas/pins originais e demais assets.
- [ ] Executar uma revisão independente única do diff e da composição, incluindo os cinco Review Focus, antes de tocar no Registro/app. Reaproveitar logs verdes com inputs correspondentes. Corrigir somente Critical/Important em uma passagem Red/Green focada; registrar Minor e julgamentos não cobertos.
- [ ] Conferir pacote/manifesto por consumidor real, novo hint opt-in somente no candidato e listener sem conectar/submeter job. Capturar backup fresco do estado/fila e dos valores/tipos/existência de TODOS os aliases próprios YouTube Brave/Google/Chrome e preservar os principais nas views relevantes; fechar apenas RC Music graciosamente.
- [ ] Iniciar candidato com dados novos, trocar somente aliases próprios YouTube e ler de volta. Solicitar um único NOVO envio manual explícito. Exigir JSON booleano completo comprovando inclusão tentada do UA; correlacionar único job e enum/resultados sem gravar valores externos. Não repetir silenciosamente se falhar.
- [ ] Se continuar 403 com UA comprovado, registrar hipótese não confirmada e encerrar esta intervenção. Se houver mídia, comprovar MP3 real (bytes/decodificação/importação no candidato) usando os componentes reais existentes, sem renomear MP4 ou chamar fixture de sucesso; eventual promoção exige outro aceite.
- [ ] Em sucesso/falha/cancelamento: restaurar todos os aliases exatos e conferir valores/tipos, encerrar candidato respeitando parada/ACK e reabrir app original; verificar fila/posição contra backup fresco. Nunca encerrar turno deixando configuração temporária ativa.
- [ ] Registrar checks executados/reutilizados, falhas/limites e pendentes; docs/diff/commit restrito, sem merge/push. Manter evidência sanitizada enquanto o aceite MP3 do produto permanecer aberto.
