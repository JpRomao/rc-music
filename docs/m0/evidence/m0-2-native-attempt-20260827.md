# M0.2 — Tentativa nativa de 2026-08-27

**Estado:** parcial e inconclusivo; não encerra `CHK-UI`, `CHK-A11Y` ou o ciclo de vida nativo

## Escopo e identidade

O mantenedor autorizou retomar o controle do desktop e atualizar o MSVC para uma versão suportada. A autorização não aceita uma stack, dispensa gates, aprova riscos de segurança ou permite redistribuição. Esta tentativa usou o pacote **existente, com o compilador histórico**, apenas para diagnóstico e identificação de regressões. A [primeira tentativa de instalação de MSVC](m0-2-msvc-refresh.md) foi cancelada na confirmação de administrador. A [instalação posterior autorizada](m0-2-msvc-installation.md) terminou com sucesso, sem modificar este pacote ou reclassificar esta tentativa nativa como aprovada.

| Campo | Valor observado |
|---|---|
| Etapa | M0.2; preparação de FR-012, FR-030, FR-040 e UX-001–010 |
| Execução | Codex, pela skill Computer Use, com janela retornada pelo Windows e ações de teclado/UIA |
| Host | Microsoft Windows 11 Pro, 25H2, build 26200.9168 |
| Pacote | `out/m0/packages/qt-shell/tray-200b983-clean-01` |
| Commit da fonte/receita | `200b9838dc9e86427e083771ef71bc9f1ee49f4b` |
| Manifesto SHA-256 | `a227bb2b23b7ec958b49318183ec399fd9c1e648e640a75e5fa7d06e5a97dc99` |
| Executável SHA-256 | `0147347fd669695de84058dd05e0e03c6e098f81f98c6d1d348752ab035a35a4` |
| Build | Release x64, hooks de teste habilitados, Qt 6.11.2, MSVC do shell 19.43.34808.0 |
| Início da segunda abertura, UTC | `2026-08-27T06:39:39.8087318Z` |
| Revalidação do pacote, UTC | `2026-08-27T06:49:25.0761422Z` |
| Áudio e dados | fixture falsa autenticada; nenhum áudio pessoal ou dispositivo de áudio usado |

O compilador antigo e suas ressalvas continuam descritos na [triagem de segurança](m0-2-security-triage.md). A tentativa não atualizou o contrato histórico `M0-E1` ou o pacote autenticado. A escala nativa do display não foi medida nesta execução; o tamanho da captura não foi usado como substituto dessa medição.

## Observações e limites

1. Na primeira abertura, a ferramenta recusou entradas por detectar interação do usuário. Uma renovação trouxe árvore acessível do player com imagem de outra janela. Nenhuma ação foi aplicada à janela alheia; as entradas foram suspensas. O player deixou de estar disponível posteriormente, sem causa de encerramento determinada.
2. Na segunda abertura, a imagem inicial mostrou o estado vazio escuro. A árvore acessível identificou Preferências e Abrir áudio habilitados, e fila/transporte desabilitados. Duas ações de Tab deslocaram o foco acessível para Preferências e depois Abrir áudio. Return abriu a fixture: a árvore passou a apresentar título, artista, álbum, controles habilitados e a ação Pausar.
3. As capturas de imagem continuaram exibindo o quadro vazio inicial, apesar da mudança da árvore acessível. Renovar a seleção da janela e a captura não resolveu a divergência. Isso impede usar essas imagens para aprovar atualização visual, foco, layout ou responsividade. A origem da divergência — captura, foco ou renderização — não foi determinada.
4. Space fora de um botão teve resultado imediato insuficiente para conclusão: a árvore ainda mostrava Pausar na captura seguinte e mostrou Reproduzir minutos depois. EOF da fixture não foi isolado nessa observação. Não é um teste causal aprovado ou reprovado de Space.
5. A solicitação de Ctrl+Q não encerrou o processo. A inspeção do código correspondente confirmou ausência desse atalho em `Main.qml`; a correção exige Red causal e Green automatizados, seguidos de nova validação nativa. A tentativa não demonstra que todas as demais entradas chegaram ao aplicativo.
6. A abertura de Preferências e a solicitação posterior de Alt+F4 não produziram um encerramento verificável. Após a espera de dez segundos, o processo de teste ainda existia. Sua identidade foi reconferida pelo caminho absoluto e PID antes do encerramento forçado. Não foram encerrados outros programas.

O processo observado antes do cleanup respondia à consulta `Responding`; isso não prova que a UI estava atualizando normalmente. O encerramento forçado foi exclusivamente limpeza, **não** aprovação do fluxo Sair/Fechar/bandeja. A consulta posterior encontrou zero processos desse player.

## Checks e decisão

- `Test-QtShellPackageManifest` reconferiu integralmente o payload original usando o hash externo acima após a tentativa. Executável, manifesto e 1.378 arquivos permaneceram íntegros; nenhum stage histórico foi reescrito.
- As observações da árvore UIA não substituem teste com NVDA. Não houve jornada NVDA, alternância nativa de alto contraste, matriz de DPI, movimento reduzido, menu da bandeja ou prova de saída normal aprovada.
- Nenhum resultado de performance, Linux, máquina limpa, instalador ou reprodução real pode ser inferido desta tentativa.
- Os smokes offscreen previamente aprovados continuam válidos no seu escopo, conforme o [registro M0](../README.md); não foram reinterpretados como resultados nativos.

Esta evidência é documental e não exige teste artificial. Seu commit de fechamento e os checks documentais acompanham o incremento que a incorpora. A correção dos atalhos mantém seu próprio registro Red/Green.

**Continuação:** os atalhos receberam correções e testes conforme a seção 5 do [registro M0](../README.md), ainda fora deste pacote histórico. O [novo pacote MSVC 14.44](m0-2-msvc-package.md) já passou com receita commitada e smokes original/relocado. Sua observação nativa posterior foi parcial, com entradas pausadas ao detectar uso manual; os resultados próprios não reclassificam esta tentativa. Faltam os gates nativos/manuais e a resolução de segurança. `M0.2` e `CHK-SEC` continuam abertos; `M0.3` não foi liberado.
