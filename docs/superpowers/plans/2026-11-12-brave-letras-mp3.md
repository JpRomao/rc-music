# Brave/Letras → MP3 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Selecionar o vídeo na página Letras pelo Brave e obter MP3 final no RC Music, preservando mídia direta e o WIP do checkout.

**Architecture:** Ampliar somente a descoberta da extensão YouTube, reutilizando seu controller, codec, Native Messaging e pipeline do desktop. A principal continua recusando YouTube. Um coletor serializável lê dados públicos do documento autorizado no mundo isolado; envio/reenvio revalidam a identidade do documento/vídeo.

**Tech Stack:** Chromium MV3/JavaScript ESM, Node test runner sem npm, PowerShell 7 para empacotamento existente; desktop C++20/Qt e ferramentas já compiladas.

**Spec:** [Desenho aprovado](../specs/2026-11-12-brave-letras-mp3-design.md).

## Global Constraints

- Saída exclusivamente MP3; MP4 somente entrada para conversão real. Sem cookies, scraping de letras, servidor novo, permissões globais ou código remoto.
- Acrescentar somente `scripting` ao YouTube; manter `activeTab`, `nativeMessaging`, CSP e identidades/hosts separados.
- Registro por usuário e janela dedicada autorizados, sem UAC, fechamento do Brave, interferência em outras extensões ou substituição da instância pessoal do player.
- Não relaxar AppContainer, NoChild, validação de rede/TLS, pins, quotas ou confirmação durável para completar uma demonstração.
- Preservar WIP preexistente. Trabalhar isoladamente se autorizado; commits restritos aos arquivos desta feature conforme `agents.md`. Não fazer push ou integração automática na branch principal.
- Seguir a política proporcional: Red/Green por comportamento e checks dos consumidores atingidos; suíte C++ global, rebuild de instaladores e benchmarks não são pré-requisitos de alteração JS.

## Review Focus

- Página Letras com publicidade/mais de um vídeo: não selecionar fonte alheia ou ambígua.
- Descriptor público inválido, enorme ou contendo JavaScript: recusar sem executar e sem travar varredura ilimitada.
- Mesma URL com documento ou vídeo substituído: revogar seleção e reenvio incerto antes do Native handoff.
- Mudança de foco/navegação durante leitura assíncrona: resposta atrasada não rearma popup nem autoriza envio.
- Asset novo ausente do pacote/recibo ou perfil Native incompatível: falhar antes da instalação/execução, preservando o estado local.

---

### Task 1: Seleção YouTube na página Letras

**Files:**
- Create: `extensions/chromium/youtube/page-video.mjs`, `tests/extensions/youtube-page-video.test.mjs`.
- Modify: `extensions/chromium/youtube/policy.mjs`, `popup-controller.mjs`, `manifest.json`, `popup.html`.
- Test: `tests/extensions/youtube-popup.test.mjs`, `youtube-manifest.test.mjs`, `youtube-policy.test.mjs`.

**Interfaces:**
- Consumes: `videoFromTab(tab)`, `buildYoutubeAudioRequest(candidate, fields, requestId)` e `chrome.scripting.executeScript()`.
- Produces: `collectLetrasYoutubeVideo(document = globalThis.document) -> {pageUrl, videoId, title} | null`, função serializável sem imports/globals próprios; `isLetrasVideoPage(url) -> boolean`; `videoFromLetrasPage(tab, snapshot) -> candidate` com os campos existentes e `lyricsPage`.
- A leitura do controller retém `documentId` devolvido pelo Chromium e o confere junto de aba, janela, URL e `videoId` antes de enviar ou reenviar.

- [ ] **Step 1: Write failing tests.** No controller, abrir `https://www.letras.mus.br/bleach/1222774/` com snapshot `videoId: 'wW9TwZdWpjw'` e documentId estável deve oferecer candidato, preencher `lyricsPage`, deixar artista vazio e não enviar. Somente clique selecionado envia um item `youtube.audio.request` com esse ID. Documento ou vídeo trocado na mesma URL, origem divergente e erro de scripting devem produzir zero chamadas nativas; reenvio preserva UUID e é revogado por troca de fonte.
- [ ] **Step 2: Run RED.** `node --test --test-name-pattern='Letras' tests/extensions/youtube-popup.test.mjs`. Expected: falha por `refresh()` retornar false/ausência do candidato, não por import quebrado.
- [ ] **Step 3: Implement collector and integration.** Parsing restrito a hosts Letras exatos, HTTPS e página válida, somente descriptor `_omq.push(['ui/player', JSON])` e iframes YouTube reconhecidos no player. Sem eval; limites de 64 scripts, 64 KiB por script e 1 MiB total; até oito iframes. Recusar ambiguidades, JSON inválido e IDs fora de 11 caracteres ASCII. Usar `documentId` nas leituras frescas; reconsultar a aba após scripting e antes do envio para impedir navegação silenciosa. O título passa pela política existente; artista nunca inferido.
- [ ] **Step 4: Collector tests.** Testar dados públicos observados, iframe reconhecido, ID divergente, host/lookalike/URL inválidos, script enorme/contagem excessiva, malformed JSON e tentativa de executar código. Executar a função serializada em VM para provar independência de imports. Observar Red de cada ramo antes de seu Green.
- [ ] **Step 5: Run focused GREEN.** `node --test tests/extensions/youtube-page-video.test.mjs tests/extensions/youtube-popup.test.mjs tests/extensions/youtube-policy.test.mjs tests/extensions/youtube-manifest.test.mjs`. Expected: todos passam, zero skips. Atualizar mensagens para YouTube/Letras sem anunciar aquisição concluída.
- [ ] **Step 6: Commit.** Revisar diff e commitar somente implementação/testes da Task 1 e a explicação do comportamento no guia de integração.

### Task 2: Pacote fechado e regressão dos consumidores

**Files:**
- Modify: `packaging/windows/RcMusicYoutubeExtensionPackage.psm1`, `RcMusicExtensionComponent.psm1`.
- Test: `packaging/windows/tests/YoutubeExtensionPackage.Tests.ps1`, `ChromiumExtensionPackage.Tests.ps1`, `ExtensionComponent.Tests.ps1`, `ExtensionInstallerFixture.psm1`.

**Interfaces:**
- Consumes: asset novo `page-video.mjs` da Task 1.
- Produces: pacote/recibo YouTube com oito assets exatos; principal conserva nove. Schemas, pins, key/ID/origin e receitas não são ampliados por fallback.

- [ ] **Step 1: Write failing package regression.** Acrescentar o novo asset às fixtures YouTube e exigir oito arquivos, incluindo seu hash físico. Principal continua nove. Ausência, substituição ou arquivo extra devem ser recusados pelo consumidor real.
- [ ] **Step 2: Run RED.** `pwsh -NoProfile -File packaging/windows/tests/YoutubeExtensionPackage.Tests.ps1`. Expected: a whitelist antiga de sete não produz o pacote esperado.
- [ ] **Step 3: Extend producer/consumer whitelists together.** Incluir `page-video.mjs` somente na família YouTube e ajustar suas fixtures, sem aceitar arquivos arbitrários ou receipts antigos adulterados.
- [ ] **Step 4: Run GREEN.** Executar `YoutubeExtensionPackage.Tests.ps1`, `ChromiumExtensionPackage.Tests.ps1` e `ExtensionComponent.Tests.ps1` com `pwsh -NoProfile -File`. Expected: todos passam; conferir as contagens reais dos scripts.
- [ ] **Step 5: Grouped JS regression.** `node --test tests/extensions/*.test.mjs`; sintaxe dos JS/MJS alterados; `pwsh -NoProfile -File scripts/validate-docs.ps1`; `pwsh -NoProfile -File scripts/validate-traceability.ps1`; `git diff --check`. Expected: sem falhas/skips. Não rodar C++ global: schema/serviços nativos não mudaram.
- [ ] **Step 6: Commit.** Commit restrito ao pacote/testes/documentação; manter evidências isoladas em `out/product`.

### Task 3: Configuração autorizada e prova real

**Files:**
- Read: perfis Native e recibos de runtime preservados, `docs/windows-installer.md`, guias Native/download existentes.
- Modify: `docs/browser-extension.md`, `docs/project-status.md` e este plano para registrar o resultado, sem promover gates não exercitados.
- Evidence: diretório novo `out/product/test-runs/brave-letras-mp3-20261112/` e pacote-fonte novo sob `out/product`.

**Interfaces:**
- Consumes: extensão/testes verdes das Tasks 1–2 e identidades compiladas do runtime escolhido.
- Produces: evidência de configuração e jornada, ou bloqueio exato sem falso sucesso. A saída requerida é MP3 decodificável publicado/importado; ACK não equivale à conclusão.

- [ ] **Step 1: Inspect prerequisites.** Localizar runtime instalado/candidato, opções compiladas, marcador, IDs de extensão e manifests Native. Conferir bytes/recibos sem instalar ou recompilar por hábito. Determinar se um app isolado pode iniciar sem interferir na instância pessoal e se os gates do pipeline permitem a jornada pública.
- [ ] **Step 2: Materialize trusted extension package.** Usar a receita existente com o novo asset, chave pública/ID correspondentes e inventário íntegro. Se a identidade não corresponder ao host compilado, parar antes do registro e relatar a exigência de um build específico.
- [ ] **Step 3: Configure authorized bridge.** Carregar a extensão em desenvolvimento por UI no Brave dedicado e registrar somente hosts RC Music por usuário usando os componentes já revisados. Preservar conflitos. Ler de volta o destino exato e conferir ID/erros do Brave e estado do app.
- [ ] **Step 4: Exercise actual media.** Fixture própria MP4 via principal → MP3, depois página Letras → extensão YouTube → app → MP3. Não trocar dados pessoais por fixtures nem usar URL resolvida/CLI paralela como substituto da jornada. Conferir bytes/decodificação, recibos/durabilidade, ausência de duplicação/autoplay e comportamento de erro/cancelamento aplicável.
- [ ] **Step 5: Fresh review.** Um revisor independente do diff inteiro, com escopo e evidências compartilhados; sem repetir as mesmas suítes. Corrigir achados importantes por Red/Green focado e ampliar somente pelo consumidor afetado.
- [ ] **Step 6: Close accurately.** Atualizar docs/plano, validar documentação/diff e commitar somente os registros próprios. Entregar checks executados, prova real, falhas conhecidas e pendentes em seções separadas. Não integrar/push automaticamente; nenhuma jornada bloqueada é marcada como concluída.
