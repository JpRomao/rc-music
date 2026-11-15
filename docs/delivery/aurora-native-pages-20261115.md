# Aurora — páginas nativas e composição musical

**Data:** 2026-11-15. **Estado:** recorte técnico implementado e qualificado após revisão independente e correção dos três Important. Dois Minor permanecem explícitos abaixo. Este incremento não conclui o redesenho global e não autoriza novo instalador.

## Escopo entregue

- Playlists, atividade de downloads e preferências compartilham o workspace como páginas reais, sem duplicar formulários. Consentimentos e revisões continuam modais; fixtures congeladas conservam o host legado.
- Cabeçalho com contexto da página e troca de tema pelo comando de preferências existente.
- Lateral de 224/196/68 px, destaque horizontal, tipografia editorial, inicial rolável e prateleira virtualizada da fila real. Duplicatas permanecem ocorrências distintas; a arte atual não é atribuída a outra ocorrência.
- Placeholder Aurora neutro quando não há capa. Sem biblioteca, álbuns, resultados ou imagens fictícios no produto.
- Player persistente com identidade, capa/artista, aleatório/repetição, navegação, saltos, seek e volume. O rodapé amplo fica próximo de 112 px; no mínimo ganha altura sem reduzir os alvos.
- Revelação do foco ao redimensionar e percorrer a prateleira, sem comandos de áudio por renderização, navegação ou tema.

**Commits:** páginas `26e2f0e`; composição `86e37c6a67cb1266e51adcfceb12869b8193772b`; consumidores/validador `ab002aecfcd4f580fce23b66336b95bddc6a57e9`; correções de ciclo de vida e teclado `6dba923ecdef8f4c0d4f2e6df1fbbc40079079f7`.

[Contrato aprovado](../superpowers/specs/2026-11-15-aurora-shell-design.md) · [Plano desta frente](../superpowers/plans/2026-11-15-aurora-native-pages.md) · [Estado do projeto](../project-status.md).

## Preservação e autorização

O mantenedor pediu: “Pode continuar com a refatoração. Gere o instalador apenas quando tiver terminado completamente o redesenho da aplicação”. Não houve novo setup, coleta de runtime, instalação, Registro/UAC, publicação, merge/push nem substituição ou reinício do player pessoal. O setup de `d4137db` permanece histórico e não representa estas fontes.

A implementação continua C++20/Qt Quick/QML 6.11.2, sem dependência nova. Domínio, aplicação, portas, adaptadores, PlayerPresenter, stores, modelos, assets e corpus/fixtures foram preservados. A exceção dirigida é `PlaylistsBinding.cpp/.hpp`: a UI passou a poder revogar a seleção de arquivos já existente, sem mudar schema ou cancelar gravação aceita. Configuração, músicas e clipboard pessoais não foram usados nos ensaios. Processos de smoke usaram composições próprias e configuração temporária isolada; os novos checks direcionam temporários ao scratch do Hermes.

## Checks executados

Evidência integral no workspace ignorado `.superpowers/sdd/2026-11-15-aurora-native-pages/`, identificado por comando, ambiente, fontes, hashes e exit code. Ambiente: Windows/x64, Qt 6.11.2, MSVC14.44, Ninja, árvore ON `out/product/dl-settings-tests`. A árvore OFF e os pacotes históricos não foram recompilados ou requalificados.

| Gate | Execução e resultado |
| --- | --- |
| Páginas / TDD | `01-pages-red` falhou por modalidade/navegação/Tab; `23-*` passou em sete arquivos, **136 casos Qt /122 comportamentais**. Evidência histórica do commit `26e2f0e`, não uma execução nova sobre a composição. |
| Composição / TDD | `25-composition-red`:2/9/0, antes de implementar cabeçalho/prateleira. `32-home-focus-red`:2/2/0 em resize e foco horizontal. `39-composition-final-*`: oito arquivos, **72 casos Qt /56 comportamentais**, zero falhas/skips/diagnósticos. |
| Build e lint nativos | `41-native-integration-build`: produto `m0_qt_shell`, módulos QML de produto/teste, contrato, Quick e dois shells/driver de processo; **exit0, sem diagnósticos**. |
| Integração UI agrupada | `42-native-integration`: seleção exata de cinco CTests; **3 passaram/2 falharam**. Quick **446/5/0** e contrato **55/1/0**. Falhas originais e `LastTest.log` foram preservados. |
| Correção do classificador | IDs de artwork e códigos de `currentPage` são dados técnicos, não textos visíveis. `44-catalog-ids-red`:25/4/0; `48-catalog-contract-green`: **63/0/0**. Sete linhas de dados novas também garantem que textos visíveis e ramos mistos não sejam liberados. |
| Consumidores corrigidos | `49-consumers-green-*`: ErrorMicrocopy **15/0/0** e PlaylistsPanel **27/0/0**, com os mesmos asserts de efeito, tamanho e acessibilidade. O primeiro passou a aguardar polish antes de medir; o segundo verifica tema no cabeçalho e navegação no seu viewport, no mesmo sistema de coordenadas. |
| Consolidação auditada | `51-integration-audit.log`/`audit-integration.py`: **451 casos Quick /371 comportamentais** reconciliados sem falhas/skips, contrato63/0/0 e três CTests aprovados reaproveitados. Isto é reuso auditado, **não outra execução ampla verde**. |
| Formato e preservação | `52-source-format-preservation.log`: clang-format19.1.1 em1C++, qmlformat6.11.2 em23QML alterados, comparação somente-leitura, `git diff --check` e preservação dos diretórios sensíveis; **exit0**. Avisos do Git sobre LF→CRLF não são diagnósticos de compilação/QML. |
| Ciclo de vida de arquivos / TDD | `59-*`: quatro falhas com Main e PlaylistsBinding reais, picker/locator controláveis e resposta Selected após navegar/trocar owner. `60-*`: API ausente em duas linhas. `62-*`: **26/0/0** no binding inteiro, incluindo preservação de save já aceito; `63-*`: **6/0/0** na composição real, sem saves, alteração de entradas, áudio ou foco tardio. `58` foi falha de descoberta do comando antes de atualizar o registro, não Red de produto. |
| Foco / virtualização / TDD | `53-*`:3/2/0, com falha em playlists e recovery+letras após resize; `54-*`:2/1/0 por Tab fora da ordem após churn. `56-*`: páginas **9/0/0**, composição **14/0/0**, incluindo fila24/cache0, ida/volta exata, reorder/removal, saída nos extremos e zero áudio. |
| Build/lint após correções | `64-review-integration-build`: produto, módulos QML/lints, contrato, Quick e shells/driver; **exit0 sem diagnósticos**. |
| Integração final atual | `65-review-integration`: **5/5 CTest**, contrato **63/0/0**, Quick **461/0/0 (379 comportamentais)**, processos **3/0/0 cada** e build-boundary aprovado. Esta é uma nova agrupada Green sobre o runtime corrigido, não uma reclassificação da42. |
| Preservação/auditoria atuais | `66-*`: formato somente-leitura em **5C++/24QML**, preservação com exceção explícita do binding e setup histórico com bytes/hash intactos. `67-*`: **822 entradas**, seleção/resultados/logs/Reds, seis PNGs e manifesto original conferidos. Reuso do binding62 por entradas iguais; a auditoria não reexecuta produto. |

A união51 reconcilia nomes completos de casos/linhas de dados e substitui exatamente os grupos corrigidos quando somente três testes haviam mudado. Ela permanece evidência histórica desse estado. Após a revisão, PageSurface, QueuePreview, PlaylistsPanel e PlaylistsBinding mudaram: o risco transversal de foco/lifetime/API C++–QML justificou a agrupada65, sem repetir core/rede/pacote por hábito.

Na65 foram executados exatamente `rc_music.ui.quick`, `rc_music.ui.contract`, `rc_music.ui.build-boundary`, `rc_music.process.combined-startup` e `rc_music.process.tray-lifecycle`. O binding62 foi reaproveitado com as mesmas822 entradas e ambiente registrado. Isso não comprova instalação, bandeja física ou áudio em hardware.

## Comparação visual nativa

Seis PNGs atuais do próprio Qt, claro/escuro em **1440×900,1024×768,720×520**, estão em `captures/65-review-integration/aurora-redesign-*.png`; a39 permanece preservada. Dimensões e SHA-256 foram conferidos na67. As seis imagens atuais foram inspecionadas contra a composição aprovada em `visual-v1/content/proposta-a.png`/`aurora-v3.html`, com as cores de `visual-v2`. O cenário de composição não injeta integrações opcionais, por isso não mostra todos os destinos que os bindings reais habilitam.

O destaque deixou de ser um player compacto centralizado; cabeçalho, navegação, hero, prateleira e transporte ocupam regiões distintas. Em720×520 o documento rola e a ação focada é revelada, enquanto o player permanece fora desse viewport. Os screenshots usam cenário sintético do harness, não músicas pessoais. Cartões representam fila temporária, não biblioteca permanente. A repetição do placeholder identifica ausência de capas, não álbuns gerados.

Diagnóstico de composição: deixou de haver pilha central sem justificativa; não foram adicionados métricas decorativas, gradientes/glassmorphism ou feature tiles fictícios. A inspeção é técnica do executor; **aceite visual humano continua aberto**.

## Revisão independente

Concluída: `deleg_3d4c52e1`, somente-leitura, snapshot de runtime `86e37c6`, manifesto `review-v1/manifest.json` SHA-256 `0f70391354ebce1216047fae352b928c70908b8fa5821cf4b7b0ab778bc4cae8`,821 insumos. **Veredicto original: com correções**, nenhum Critical, três Important e dois Minor. Original preservado em `review-v1/ORIGINAL-REVIEW.md`, com ponteiro ao transcript completo; disposição do coordenador em `REVIEW-DISPOSITION.md`.

I1 era resposta tardia de seletor/localizador gravando o owner anterior; I2 era foco recortado após resize; I3 era Tab seguindo a ordem de criação dos delegates, não a fila virtualizada. O coordenador confirmou as causas e fez uma passagem Red→Green, seguida da integração65. O parecer não foi reetiquetado como aprovação de `6dba923`; as correções posteriores são sustentadas pelos testes, não por uma segunda revisão.

### Deferred minors

- **M1:** clique nativo repetido no destino já selecionado pode desmarcar sua indicação, sem trocar a página. Precisa de polimento de seleção e regressão por clique real.
- **M2:** cartão indisponível permanece desabilitado, mas ainda precisa de apresentação visual/descrição acessível mais clara em Claro/Escuro/HC. Recuperação na fila e ativação inerte permanecem.

## Decisões do executor e custos

1. Prosseguir nativamente com a direção já aprovada, sem reabrir escolhas a cada detalhe reversível; custo de uma escolha visual incorreta: nova iteração, não migração de dados.
2. Um formulário por painel com host compartilhado. A ideia inicial de `Control` foi substituída após diagnóstico: custo de erro no host compartilhado é afetar os três consumidores, por isso há cobertura dirigida e agrupada.
3. Preservar o workspace de evidências desta frente em vez de apagá-lo no fechamento genérico; custo: espaço de cache, com ganho de rastreabilidade/reuso.
4. Usar `FocusScope`, não `Control`, para que apenas o viewport ou o Dialog legado possua a geometria do formulário; custo: rotina pequena de layout compartilhado, exercitada também nos consumidores legados.
5. Usar alvos explícitos de foco da casca quando playback está indisponível, preservando retorno à página; custo: duas referências de foco de UI, sem alteração de áudio/dados.
6. Medir o placeholder e o viewport nativos, não forjar `Image.status` nem exigir que um documento rolável caiba inteiro; custo: risco geométrico, coberto por bounds/foco e renders. Oráculos congelados e asserts de metadados/ações foram preservados.
7. Reconciliar a ampla42 com recapturas exatas dos consumidores afetados, porque apenas três testes mudaram e o runtime permaneceu idêntico; custo residual: uma interação exclusiva de execução conjunta não reproduzida no recorte. O auditor exige igualdade dos demais insumos e dos conjuntos de casos, sem alegar uma nova ampla.
8. Expor somente a revogação transitória no PlaylistsBinding, apesar do limite inicial de Task1 sem C++; o estado pendente pertence a ele. Custo: uma API mínima a manter, não schema/persistência nova. Save aceito permanece independente.
9. Executar uma agrupada final após essas mudanças de runtime; custo: rodada de integração selecionada, sem ampliação para core/rede/pacote. Reutilizar o binding62 apenas mediante identidade de entradas.

O reviewer não julgou dez frentes; as decisões e seus custos estão registradas na disposição: (1) aceite visual humano continua aberto — risco de retrabalho; (2) biblioteca/busca/provider aguardam seus contratos — jornada nova ausente, não simulada; (3) rede/sandbox/instalação das extensões não foram qualificados — integração real pode falhar; (4) setup/UAC/publicação não autorizados — sem candidato instalado novo; (5) NVDA/DPI/multimonitor continuam pendentes — limitações físicas ainda podem surgir; (6) áudio audível/codecs/dispositivos/budgets não medidos — nenhum passe de hardware/desempenho; (7) não houve auditoria universal de C++/stores — só a exceção de seleção é coberta; (8) Task3 não encerra onze fases — gate global continua; (9) ampla42 e delta pós-snapshot conservam sua proveniência — sem Green retroativo; (10) regressões sugeridas foram executadas pelo coordenador com portas controladas — interação física/instalada do seletor continua pendente.

## Pendências e limite da entrega

- Esta frente não encerra as onze fases do plano diretor. Biblioteca permanente/ingresso e busca/provider mantêm desenho técnico, persistência e gates próprios; não foram substituídos por fila ou resultados fictícios.
- Paridade visual de todas as páginas/jornadas e aceite humano do redesenho completo continuam abertos, mesmo com os contratos desta frente exercitados.
- NVDA/acessibilidade física, DPI e escala reais, Linux, benchmarks, corpus/segurança/distribuição, aquisição pública e instalação/upgrade/uninstall não foram qualificados aqui.
- O gate do instalador continua bloqueado até a conclusão integral do redesenho. Nenhum setup intermediário deve ser produzido a partir deste checkpoint.
