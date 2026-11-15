# ADR-0018: Aurora e retirada integral do Lidarr do aplicativo

**Estado:** Aceito

**Data:** 2026-11-15

**Substitui:** [ADR-0014](0014-lidarr-integration.md), somente quanto à integração Lidarr. Os demais ADRs e gates permanecem vigentes nos seus próprios escopos.

## Contexto

O mantenedor pediu a remoção do Lidarr e de todas as suas integrações **do aplicativo**. Isso substitui a prioridade de integração aceita em 2026-11-04; não é um adiamento do smoke autenticado nem autorização para agir no servidor externo.

Em seguida, o mantenedor aprovou o visual Aurora, incluindo as cores do tema escuro: **“Ficou bom o visual com essas cores, pode prosseguir com ele”**. O [contrato visual aceito](../../superpowers/specs/2026-11-15-aurora-shell-design.md) preserva a referência HTML de SHA-256 `cb4f6dce459a9539955b5ae2273aaf45094e511205f154b888a9be2c328aaa6e` e delimita a migração nativa incremental. O aceite visual não qualifica automaticamente a implementação Qt/QML.

## Decisão

### FR-055 — fora de escopo por retirada do Lidarr

1. Retirar integralmente a capacidade Lidarr de Lite e Completa: cliente, binding, painel, ações de entrada, bootstrap, preparação de saída/lifetime, recursos e dependências de build e testes exclusivos. Ocultar apenas o botão não atende ao pedido. Os caminhos históricos `src/adapters/QtLidarrService.*`, `src/presentation/LidarrBinding.*`, `src/ui/qml/LidarrPanel.qml`, `tests/qt/QtLidarrServiceTests.cpp`, `tests/qt/LidarrBindingTests.cpp` e `tests/ui/quick/tst_lidarr.qml` não são referências a componentes ativos.
2. Conservar **FR-055**, sua prioridade histórica **P1** e o destino **R1.11** para rastreabilidade, com estado **fora de escopo**, verificação **documentary**, `tests: []` e esta decisão explícita. Não marcar o antigo requisito como comprovado nem manter aceitação autenticada/download real como próxima tarefa.
3. Preservar Qt Network e seus consumidores compartilhados, letras/versões/romaji, downloads e extensões opcionais, playlists, stores, preferências, sessão e reprodução local. “Todas as integrações” refere-se ao Lidarr; não revoga essas outras capacidades nem seus gates.
4. Não instalar, parar, desinstalar ou configurar Lidarr, indexadores ou clientes externos; não revogar chaves, cancelar pedidos remotos ou apagar arquivos de áudio/dados pessoais. A retirada do app não requer ler credenciais ou contatar a instância indicada no histórico.
5. Prosseguir com Aurora na stack C++20/Qt Quick existente, preservando Sistema/Claro/Escuro, preferências salvas, alto contraste, movimento reduzido e uma única reprodução autoritativa. O aceite do dark não autoriza forçar esse tema sobre uma preferência existente. A fundação visual e as superfícies seguintes têm qualificação nativa própria.
6. Não habilitar novo provedor, pesquisa online, downloads de produção, instalação/Registro, distribuição ou novo ensaio AppContainer/WFP/UAC por consequência desta decisão. Biblioteca permanente e provider de pesquisa exigem seus próprios contratos e decisões.

## Alternativas

- **Somente esconder o painel:** rejeitado, pois conservaria cliente, composição e capacidade ativa que o mantenedor mandou retirar.
- **Continuar a integração ou trocar automaticamente de provedor:** rejeitado; a retirada não autoriza outra integração.
- **Apagar FR-055, ADR-0014 e recibos antigos:** rejeitado, pois destruiria a identidade e a proveniência do trabalho anterior.

## Consequências e validação

O [guia Lidarr](../../lidarr-integration.md) e o ADR-0014 passam a ser históricos, com suas âncoras e evidências preservadas. Contagens, Reds/Greens, resposta 401 e builds anteriores descrevem seus snapshots, não uma nova aprovação do aplicativo sem Lidarr. Setups e runtimes antigos não são regenerados nem requalificados por uma mudança documental e podem conservar o cliente anterior.

A retirada nativa deve conferir ausência da capacidade e das dependências exclusivas na composição e preservar os consumidores compartilhados, com regressões selecionadas pelo risco. Esses checks pertencem à frente de implementação; o delta documental valida conteúdo, links, IDs, decisão e concordância entre matriz e manifesto por `scripts/validate-docs.ps1` e `scripts/validate-traceability.ps1`, sem builds ou testes de produto. Vínculos válidos não demonstram execução atual dos testes nem aprovação de release.

Reintroduzir Lidarr exige novo pedido e decisão explícitos. Não retomar os antigos smokes, extrair uma chave ou promover a evidência histórica apenas para fechar FR-055.
