# Diagnóstico da recusa nativa — plano

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans para executar diretamente, preservando o método já escolhido. Passos marcados exigem evidência, não intenção.

**Goal:** observar a razão específica de Restricted em uma tentativa real, sem expor dados remotos ou mudar a política.
**Architecture:** observador opcional tipado no transporte, composto somente por um header diagnóstico confiável; candidato com dados isolados, host temporário e restauração.
**Tech Stack:** C++20/Qt 6.11.2, CMake 3.31/Ninja/MSVC 14.44; receitas PowerShell e Python existentes.
**Spec:** [desenho diagnóstico](../specs/2026-11-12-native-restricted-diagnostic-design.md).

## Global Constraints

- Preservar WIP do principal, instalações e dados pessoais; trabalhar na worktree existente.
- Não mudar DNS/TLS, allowlists, headers, ferramentas, pins, NoChild, ACKs, UAC ou WFP para produzir sucesso.
- Registrar somente enum fechado, uma vez por sessão; composição normal sem observador permanece silenciosa.
- Testes focados não comprovam E2E; tentativa diagnóstica não é release aprovada.

## Review Focus

- Callback lançando exceção não altera recusa, cancelamento ou libera o sink.
- Mensagem não recebe dados de URL/corpo/headers, nem cresce a partir de dados remotos.
- Snapshot já terminal não emite repetidamente em ticks posteriores.
- Composição do candidato não substitui workers por fixtures nem altera suas políticas.
- Falha ou interrupção após alterar o host exige restauração exata antes de fechar o trabalho.

### Task 1: Observação opcional do transporte

**Files:** `src/adapters/DirectDownloadSession.hpp`, `.cpp`; `tests/qt/DirectDownloadSessionTests.cpp`.
**Interfaces:** `DirectDownloadLimits::on_restricted` recebe somente `DirectDownloadRestriction` (enum); padrão vazio. `fail` conserva erro e cancela antes de observar; callback não pode reentrar/destruir a sessão.

- [ ] Escrever casos focados que esperam uma razão para target, DNS, redirect, 401/403 e IO; esperar zero bytes e erro Restricted, emissão única e exceção isolada.
- [ ] Acrescentar apenas o contrato/stub necessário para compilar e observar Red comportamental, sem executar callback ainda.
- [ ] Implementar a notificação, mantendo falhas e restrições; obter Green.
- [ ] Rodar `cmake --build out/dg/build --target rc_music_DirectDownloadSession_tests --parallel 4` e `ctest --test-dir out/dg/build -C Release --output-on-failure -R '^rc_music\.download-network\.DirectDownloadSession$'`; ampliar aos consumidores pertinentes disponíveis no build após a última edição.
- [ ] Revisar diff/documentação e commitar somente os próprios arquivos.

### Task 2: Candidato isolado e prova real

**Files:** artefatos novos ignorados em `out/dg/`; consumir header confiável `out/product/download-tools-expat210-20261111/PlayerDownloadPackageProfile.hpp` do checkout principal, sem editar o original.
**Interfaces:** copiar pins/configuração revisados, compor callback em header novo; shell diagnóstico usa main/presenter reais e dados em `out/dg/data`; Native YouTube mantém origem `chrome-extension://lhcdmfamadilnahlckpalijljfohjgkc/`.

- [ ] Configurar/buildar candidato pela composição existente; conferir fontes e dependências reais, sem adivinhar flags ou caminhos.
- [ ] Conferir pacote e destino original do host; preservar backup de registro e estado operacional.
- [ ] Fechar somente o RC Music graciosamente, abrir candidato com dados isolados e alterar somente host YouTube próprio. Ler de volta os destinos.
- [ ] Executar uma nova seleção/envio explícitos pela extensão; guardar razão enumerada e correlacionar o único pedido do candidato. Não gravar URLs/headers/corpo, não reenviar automaticamente.
- [ ] Restaurar exatamente o host e reabrir o app original; verificar ambos. Se possível, concluir o aceite MP3 sem contornar restrições; caso contrário, relatar o bloqueio observado.
- [ ] Registrar checks executados, falhas e pendentes; atualizar estado e fechar sem merge/push.
