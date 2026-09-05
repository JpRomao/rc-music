# ADR-0004: Nome definitivo RC Music e continuidade dos dados locais

**Estado:** Aceito

**Data:** 2026-09-05

**Decisor:** mantenedor do projeto

## Contexto

O mantenedor confirmou explicitamente **RC Music** como nome definitivo. A identidade provisória impedia avançar no registro de associações de M4.6. O candidato Qt já possui preferências, sessão e lock derivados de `QStandardPaths::AppConfigLocation`, cujo caminho depende de `organizationName` e `applicationName`.

## Decisão

- Usar **RC Music** como nome visível da aplicação, inclusive título da janela, bandeja e `applicationDisplayName` do Qt.
- Centralizar esse nome no código; nomes de marca não dependem de tradução.
- Preservar `MusicPlayer` nos identificadores internos de organização/aplicação do Qt usados pelos dados existentes. Uma mudança desses identificadores exigiria migração explícita, com compatibilidade entre instâncias.
- Preservar namespace C++, URI QML `MusicPlayer.M0`, nomes técnicos de targets, fixtures e evidências históricas autenticadas. Eles não são o nome apresentado ao usuário.
- Não criar registro real no Windows neste incremento. As associações futuras devem usar o nome confirmado, declarar somente a matriz validada e seguir o template próprio de instalador por usuário previsto na arquitetura, sem escrever `UserChoice`.

## Alternativas

- Manter nome provisório: rejeitado porque o mantenedor confirmou a identidade.
- Renomear indiscriminadamente paths, módulos e identificadores: rejeitado porque pode ocultar dados anteriores, separar locks entre versões e invalidar fixtures históricas sem benefício para o usuário.
- Escolher automaticamente nova identidade visual ou licença: fora da decisão recebida.

## Consequências

O usuário vê RC Music sem migração de armazenamento. As pastas de configuração podem continuar com o nome técnico anterior. A identidade textual deixa de ser impedimento de M4.6; matriz publicada, integração do instalador, registro e seus testes ainda precisam ser implementados/validados. Esta decisão não aceita stack ou engine, não conclui M0/M4.6 e não autoriza distribuição ou registro no sistema durante o desenvolvimento.

## Critérios de revisão

Revisar somente diante de novo nome solicitado pelo mantenedor ou de uma migração deliberada dos identificadores de armazenamento. Qualquer migração deve preservar preferências/sessão e a coordenação de instância única, incluindo rollback.
