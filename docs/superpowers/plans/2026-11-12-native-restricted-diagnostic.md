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

- [x] Escrever casos focados que esperam uma razão para target, DNS, redirect, 401/403 e IO; esperar zero bytes e erro Restricted, emissão única e exceção isolada.
- [x] Acrescentar apenas o contrato/stub necessário para compilar e observar Red comportamental, sem executar callback ainda.
- [x] Implementar a notificação, mantendo falhas e restrições; obter Green.
- [x] Rodar `cmake --build out/dg/build --target rc_music_DirectDownloadSession_tests --parallel 4` e `ctest --test-dir out/dg/build -C Release --output-on-failure -R '^rc_music\.download-network\.DirectDownloadSession$'`; ampliar aos consumidores pertinentes disponíveis no build após a última edição.
- [x] Revisar diff/documentação e commitar somente os próprios arquivos.

### Task 2: Candidato isolado e prova real

**Files:** artefatos novos ignorados em `out/dg/`; consumir header confiável `out/product/download-tools-node24223-20261112/PlayerDownloadPackageProfile.hpp` do checkout principal, sem editar o original. O cache anterior apontava ao perfil expat210, mas a composição instalada usa o perfil Node24223; não atualizar pins antigos para esconder a diferença.
**Interfaces:** copiar pins/configuração revisados, compor callback em header novo; shell diagnóstico usa main/presenter reais e dados em `out/dg/data`; Native YouTube mantém origem `chrome-extension://lhcdmfamadilnahlckpalijljfohjgkc/`.

- [x] Configurar/buildar candidato pela composição existente; conferir fontes e dependências reais, sem adivinhar flags ou caminhos.
- [x] Conferir pacote e destino original do host; preservar backup de registro e estado operacional.
- [x] Fechar somente o RC Music graciosamente, abrir candidato com dados isolados e alterar somente host YouTube próprio. Ler de volta os destinos.
- [x] Executar uma nova seleção/envio explícitos pela extensão; guardar razão enumerada e correlacionar o único pedido do candidato. Não gravar URLs/headers/corpo, não reenviar automaticamente.
- [x] Restaurar exatamente o host e reabrir o app original; verificar ambos. Se possível, concluir o aceite MP3 sem contornar restrições; caso contrário, relatar o bloqueio observado.
- [x] Registrar checks executados, falhas e pendentes; atualizar estado e fechar sem merge/push.

## Resultado observado

Red/Green do observador e consumidores: 72/72, 22/22 e 22/22 Qt, 3/3 CTest, sem skips. Build Release e planejador Native validados. A primeira tentativa real não admitiu pedidos (`bridge_unavailable`); após conferir/incluir o alias compatível do mesmo host no registro Chrome, uma segunda preparação isolada recebeu um novo envio manual. O único job `aa06c29fbf9e4f9188c413249fc5df30-1` registrou `http_403`, sem bytes de áudio ou publicação. Candidato encerrado graciosamente, ambos os aliases YouTube restaurados exatamente e registros principais preservados nas views 32/64; app original reaberto com fila de 17 itens e posição intactas. Evidências filtradas e decisões no ledger deste plano, em `real-attempt-2.json` e `restoration-2.json`.

**Limite:** o plano conclui a observação do ramo de recusa e a restauração. Não identifica a causa de o servidor retornar 403, não reconstitui o job antigo nem comprova aquisição/conversão/importação MP3 ou distribuição. Não foram alterados headers/políticas por hipótese. A regressão global não foi repetida neste diagnóstico.
