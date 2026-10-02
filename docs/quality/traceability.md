# Rastreabilidade executável — CHK-TRACE

O [manifesto](traceability.json) relaciona cada ID dos [requisitos](../product/requirements.md) ao destino literal da matriz 15.1 do [plano](../development-plan.md), testes, evidências e lacunas. É o inventário de M1.2, não uma porcentagem de implementação nem aprovação de release.

## Executar e consultar

Sem Qt, build, rede ou dependências novas, em PowerShell 7:

```powershell
pwsh -NoProfile -File scripts/validate-traceability.ps1
pwsh -NoProfile -File scripts/tests/Traceability.Tests.ps1
```

Os comandos `check` e `validate` também executam os testes do validador e o manifesto real. Ambos interrompem os passos seguintes se algum vínculo ficar inválido. O check lê arquivos; não executa os testes C++/QML citados nem modifica o manifesto. Os testes do próprio validador usam somente fixtures temporárias em `out/product`, removidas ao terminar após conferir destino e ancestrais.

O relatório JSON contém `records` (ID → destino/testes/evidências/lacunas), `references` (tipo/caminho/marker → IDs) e contagens por estado. A consulta inversa evita duplicar manualmente uma segunda matriz:

```powershell
$trace = pwsh -NoProfile -File scripts/validate-traceability.ps1 -Json | ConvertFrom-Json
$trace.records | Where-Object id -eq 'FR-001'
$trace.references | Where-Object path -eq 'tests/ui/quick/tst_empty_shell.qml'
```

O caminho da consulta é apenas um exemplo de filtro. Use os caminhos presentes em `references`. `allRequirementsProven` só reflete os estados declarados, nunca uma avaliação independente do cumprimento.

## Contrato do manifesto

`schemaVersion` é 1; `requirementsFile` e `planFile` apontam obrigatoriamente para os contratos canônicos. `records` tem exatamente uma entrada por ID, sem desconhecidos ou duplicatas. Os destinos preservam integralmente a célula da matriz humana, incluindo intervalos. Cada registro contém:

- `id`, `plan`, `status` e `verification`;
- `tests` e `evidence`: arrays de `{ "path": "caminho/relativo", "marker": "texto literal existente" }`;
- `remaining`: critérios concretos ainda não comprovados, sem descrever função existente como ausente;
- `decision`: referência documental obrigatória somente para `excepcionado` ou `fora de escopo`, com o ID explícito no marker. P0 funcional não pode ser dispensado por esses estados: exige correção ou alteração contratual explícita antes do RC.

Os estados são `planejado`, `em andamento`, `comprovado`, `excepcionado` e `fora de escopo`. Apenas `comprovado` tem `remaining` vazio; os demais precisam registrar a lacuna, justificativa ou condição de retomada. Todos precisam de evidência documental ou de execução já registrada. Evoluções futuras podem ter testes vazios, sem testes artificiais. Nenhum estado de exceção é inferido de orçamento, ausência de ambiente ou adiamento.

`verification` diferencia `automated`, `manual`, `documentary`, `mixed` e `future`. Um requisito comprovado por automação ou de forma mista exige ao menos um teste; uma decisão documental não exige teste artificial. `future` nunca pode ser `comprovado`.

Markers são literais e sensíveis a caixa, não regex nem números de linha. Testes usam nomes de casos/funções específicos; evidências usam headings ou trechos que identifiquem o recorte. Fontes podem complementar evidências de implementação, sem comprovar execução. Caminhos são relativos, nas árvores versionáveis `docs`, `tests`, `scripts`, `src`, `include`, `cmake`, `tools`, `experiments` ou nos contratos raiz `README.md`, `agents.md`, `CMakeLists.txt`; caminhos absolutos, externos, saída raiz ignorada, traversal e reparse points são recusados. Campos desconhecidos, tipos inválidos e chaves JSON duplicadas também falham. Referências históricas são permitidas como evidência, não tornam o experimento uma dependência executável.

## Revisão sem falsa comprovação

A existência de um marker não prova que uma assertion cobre o critério inteiro, que o teste foi executado neste commit ou que uma evidência histórica continua aplicável. A revisão humana do diff deve conferir esses três pontos antes de mudar `status`, junto aos critérios de aceitação e limites do registro citado. Uma referência a ADR proposto não o torna aceito. Exceções exigem a decisão autorizada e revisão do seu alcance; o validador confere a referência, não concede autoridade.

O inventário inicial mantém funcionalidades implementadas como `em andamento` quando o requisito completo ainda depende de jornada nativa/empacotada, corpus, acessibilidade ou release. Linux, benchmarks, evoluções M6 e fontes remotas continuam nos limites do plano. Não usar o número de requisitos `comprovado` como estimativa de quanto código falta.

## Evidência do incremento

Em 2026-09-06, o Red confirmou a ausência do validador/CLI e dois contratos de ordenação do runner falharam. O Green inicial passou **36/36** testes de rastreabilidade e **33/33** do runner. Os casos adicionais de junction isolada, decisão explícita e manifesto ausente elevaram a suíte a **39/39**, sem privilégios novos. Os testes cobrem inventários inválidos, destino divergente, referências inexistentes, markers literais, estados incoerentes, JSON duplicado, consulta inversa e códigos de saída reais da CLI.

A referência complementar de implementação teve Red **39 passagens/1 falha** e Green **40/40**. A revisão independente identificou que um array com o destino correto passava pela comparação PowerShell sem validar o tipo; a regressão confirmou o Red **40 passagens/1 falha** e passou **41/41** após exigir string. O manifesto real passou com **83 IDs: 62 em andamento, 19 planejados e 2 comprovados documentalmente** (somente as decisões NFR-STACK-001 e NFR-STACK-006). Nenhum requisito foi excepcionado nem declarado concluído por contagem de testes.

A segunda revisão encontrou `decision` em array aceito indevidamente, inclusive vazio: Red **41 passagens/2 falhas**, corrigido pela exigência do objeto antes de enumerar referências. O fechamento executou `develop.ps1 check -ToolchainFile tools/windows.example.psd1`: rastreabilidade **43/43**, runner **33/33**, formatter **18/18**, manifesto **83/83 vínculos válidos**, formato **172 C++/52 QML**, documentos **56 Markdown/83 requisitos/17 checks/76 etapas** e diff verdes. Os dois achados de revisão foram corrigidos sem relaxar os testes; não houve build/CTest novo neste recorte de scripts e documentação.

Este incremento não altera comportamento C++/QML nem fecha M1, cobertura de código, CI Windows ou gates globais. As execuções anteriores de CTest permanecem registradas nos guias de testes, sem reapresentá-las como nova execução.

## Atualização do ingresso Native Messaging de 2026-10-02

FR-051/FR-053 agora apontam para os testes reais de launch confiável, codec de resposta correlacionada, framing/I/O binário, deadlines/clock pós-read, transporte limitado/reentrante e processo do host/watchdog, além da [composição instrumentada de quatro hosts em dois boots](../process-tests.md#composição-instrumentada-do-ingresso-native-messaging). Os leitores independentes observam recibos/IDs após cada resposta e os documentos após NormalExit, sem confundir o instante físico do ACK com essa observação. Somente publicação é sintética: não comprova rede/conversão, ownership físico, Brave instalado ou workflow remoto de produção.

O manifesto mantém **83 IDs: 66 em andamento, 15 planejados e 2 comprovados documentalmente**, sem alterar estados por contagem de testes. Host e transporte opcionais estão implementados/OFF por padrão, com identidade confiável de produção vazia; a composição protegida de produção, identidade/registro/upgrade instalados, primeira instância/colisão do pipe Windows, HWND/Brave real, ferramentas/sandbox, Completa e instaladores continuam nas lacunas. Origem/pipe do mesmo usuário não autentica navegador. Os resultados focados e o fechamento Release ON **470/470 CTest** ficam nos guias de qualidade/processos; vínculos válidos não aprovam esses requisitos nem os gates remotos/release.

## Reserva cooperativa do dono de downloads

FR-051/FR-053 também vinculam recusa do segundo servidor/processo, aliases de caixa/origem, DACL protegida do marcador, colisões preexistentes e recuperação após destruição ou término do dono. [O ensaio de processos](../process-tests.md#exclusividade-cooperativa-do-dono-native-messaging) conserva contendores recusados vivos e distingue ingresso STOPPED do objeto RELEASED. A reserva não autentica navegador/processo, não protege o pipe contra cliente local hostil e não resolve identidade instalada ou disponibilidade entre usuários. Os estados e gates dos requisitos permanecem inalterados.

## Revisão explícita dos arquivos interrompidos

FR-051/FR-053 acrescentam vínculos para revisão correlacionada por ID, slot incluindo completion não observada, worker/parada física, projeção segura e ação explícita por teclado. A composição QML usa painel/binding/model/controller/service reais e worker Windows sobre TEMP próprio; um resultado de ownership não autoriza importação/exclusão/retomada nem muda o histórico Interrupted. [O contrato de revisão](../download-ownership.md#revisão-explícita-assíncrona) mantém produção remota OFF e separa presença de testes dos resultados executados no checkpoint. Nenhum estado de requisito ou gate amplo foi promovido.

## Descarte confirmado de temporários interrompidos

FR-051/FR-053 acrescentam markers literais de consentimento job/token one-shot, slot/action matching, fresh proof por DELETE handles, ausência física+Saved, final preservado e ambiguidade de remoção/save. O painel real composto tem casos de confirmação e substituição dos objetos após consentimento; fakes e TEMP Windows reais continuam evidências separadas. [O contrato limitado](../download-ownership.md#descarte-explícito-somente-de-temporários) não altera Interrupted/checkpoint/recibos/quotas; não importa candidatos ou ativa produção remota. Estados dos requisitos e evidências anteriores permanecem intactos; existência desses vínculos não anuncia execução nem aprovação de checks/release.
