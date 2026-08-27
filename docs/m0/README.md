# Registro de autorização e evidências M0

**Estado:** M0 autorizado; M0.1 concluído e M0.2 em andamento com jornada Qt Quick vazia/tocando/erro, transporte, fila, navegação manual, preferências/temas e alto contraste implementados no candidato; checks automatizados da fatia HC verdes; stack e engines não escolhidos

**Data:** 2026-08-27

**Escopo:** autorização, contratos M0.0–M0.1, progresso do candidato Qt em M0.2 e índice das evidências experimentais

## 1. Autorização D0.9

O mantenedor instruiu: “Siga completamente o plano em `docs/development-plan.md`. Faça checks sempre que necessário”. Para fins do contrato do projeto, essa instrução:

- aprova o baseline atual de requisitos confirmados e derivados para orientar a implementação;
- autoriza executar M0.0–M0.10 e criar os dois candidatos comparáveis;
- confirma fila e restauração pausada de sessão como P0 do MVP;
- exige TDD para comportamento observável e checks/commits em cada incremento;
- não aceita antecipadamente Qt, Tauri, Qt Multimedia, Rodio ou qualquer empacotador final;
- não autoriza publicação externa, telemetria, auto-update ou fontes remotas no MVP.

M1 continua bloqueado até M0.10 produzir evidência comparável e uma decisão explícita nos ADR-0001/0002 ou em ADR sucessor.

## 2. Inventário aprovado de preferências do MVP

| Preferência | Default inicial | Valores/regras |
|---|---|---|
| Tema | sistema | sistema, claro, escuro |
| Movimento | sistema | sistema, reduzido |
| Volume | 80% | 0–100%; sem amplificação |
| Mudo | desligado | booleano; desmutar recupera último volume diferente de zero |
| Retrocesso | 5 s | inteiro entre 1 e 300 s |
| Avanço | 5 s | inteiro entre 1 e 300 s |
| Ao fechar no Windows | minimizar para bandeja | minimizar para bandeja ou encerrar |
| Ao fechar no Linux | minimizar para bandeja quando disponível | sem tray funcional, minimizar normalmente ou encerrar; nunca ficar invisível |
| Restaurar última sessão | ligado | restaura fila/item/posição sempre pausado |
| Iniciar minimizado | desligado | só pode ser ligado quando tray funcional |
| Fim da fila | parar e manter seleção | parar e manter seleção ou parar e limpar seleção; repeat continua fora do MVP |

O botão para abrir Aplicativos Padrão é uma ação, não preferência persistida. Escolha de dispositivo, shuffle, repeat, velocidade e editor de tema continuam M6.

## 3. Contratos M0.0

- [Ambiente `M0-E1`](environment.md): Windows 11 Tier 1, Ubuntu 24.04 LTS Tier 2, host, toolchains e distribuição privada.
- [Protocolo `M0-P1`](protocol.md): amostras, métricas externas, descartes, estatística, áudio, soak e evidência.
- [Layout `M0-L1`](layout.md): isolamento dos candidatos, inputs comuns, raw ignorado e runners previstos.

Decisões que permanecem abertas:

- tecnologia final de instalador;
- versão portátil no 0.1;
- nome e identidade visual;
- licença para eventual distribuição pública.

Enquanto o repositório permanecer local/pessoal e os protótipos não forem publicados, nenhum direito de redistribuição do código próprio é concedido. Dependências e fixtures continuam exigindo licença/proveniência conhecida.

## 4. Índice M0

| Evidência | Estado |
|---|---|
| D0.9 — autorização e preferências | concluído neste documento |
| M0.0 — [ambiente](environment.md), [protocolo](protocol.md), [layout](layout.md) e [evidência](evidence/m0-0.md) | concluído em `81f9f2f` |
| M0.1 — fixture comum, schemas, roteiro e harness | [concluído](evidence/m0-1.md) em `24d12ea`; piloto sintético `P` não decisório, zero coleta A/B |
| M0.2 — shell C++20/Qt Quick/QML com backend falso | em andamento; [preflight concluído](evidence/m0-2-preflight.md), core/presenter determinísticos, jornada vazia/tocando/erro, transporte, fila com virtualização funcional e navegação manual anterior/próxima; recursos `qrc` autenticados; preferências/temas e alto contraste implementados sob as políticas comuns das seções 6–7, com checks automatizados da fatia HC verdes; tray, pacote, alto contraste nativo, acessibilidade nativa/NVDA e medições pendentes |
| M0.3 — shell Rust/Tauri equivalente | pendente |
| M0.4 — resultados de shell | pendente |
| M0.5–M0.8 — corpus e áudio | pendente |
| M0.9 — integração/pacote | pendente |
| M0.10 — combinação e decisão | pendente |

O teste funcional da fila verifica menos de 64 delegates associados a índices via `itemAtIndex` nos pontos testados da navegação; não conta todo o pool de reutilização nem as instanciações ao longo da execução e não fecha NFR-PERF-006. A inspeção manual na fatia de fila virtualizada não foi realizada porque a sessão Computer Use foi interrompida; nenhuma nova verificação manual é alegada para a navegação. NVDA e o protocolo externo de desempenho continuam pendentes.

Resultados brutos grandes não entram no Git sem necessidade. O repositório preserva schemas, protocolo, resumos e hashes suficientes para reproduzir ou auditar cada decisão.

## 5. Política experimental de navegação manual

Para manter paridade entre M0.2 e M0.3, os dois shells usam esta política sobre as ocorrências da fila congelada. Ela não altera a fixture nem o protocolo M0-P1 e não aceita antecipadamente uma stack. As guardas de estado e seek têm precedência sobre as regras de direção.

| Comando/condição | Resultado no shell experimental |
|---|---|
| Anterior, posição maior que 3.000 ms e seek permitido | seek para zero na mesma ocorrência |
| Anterior, posição até 3.000 ms ou sem seek | procurar a ocorrência anterior disponível, sem circular a fila |
| Próxima | procurar a ocorrência seguinte disponível, pulando indisponíveis, sem circular a fila |
| Busca de vizinho sem ocorrência disponível no sentido solicitado | operação inerte |
| Vazio/`Unloaded`, `Loading`, `Error` ou seek em preview/aguardando ack | anterior e próxima inertes |
| Troca manual de ocorrência | posição zero e duração da linha de destino; preservar `Ready`/`Paused`/`Playing`, volume, mudo e identidade do modelo da fila |

Com a fila aberta, seleção e rolagem acompanham a ocorrência atual sem roubar o foco do transporte. O título longo da faixa atual permanece dentro da área livre ao lado do painel. No candidato Qt, regressões automatizadas cobrem a preservação do foco e os limites do título de `queue-0512`; isso não substitui inspeção manual/NVDA.

EOF permanece `clamp_paused`, sem avanço automático. “Pular” no cenário de erro é uma recuperação separada: `queue-0999` com contexto `queue-0017` volta ao seed `queue-0017` em 84 s; não é o comando Próxima. CRUD da fila, avanço automático por `ended`, política real de fim da fila e persistência não pertencem a esta fatia. FR-011/FR-016/FR-017 continuam destinados às etapas do produto, sem alegação de conclusão pelo experimento.

## 6. Política experimental de preferências, tema e movimento

Esta política completa o recorte de apresentação comum a M0.2/M0.3 sem alterar a fixture, seus defaults ou o protocolo M0-P1. As preferências do shell são somente em memória. O painel exibe uma nota que informa essa limitação e que restauração de sessão e comportamento real de fim da fila pertencem a etapas futuras. Alterar esses valores não modifica o EOF falso `clamp_paused`; FR-042/FR-043/FR-044 não são declarados concluídos.

### Painel, foco e atalhos

- O painel é modal, limitado à área de conteúdo; o transporte permanece visível.
- Diálogo, botões, seletores, campos e toggles possuem roles explícitos. `Tab`/`Shift+Tab` ficam no painel, e somente uma categoria permanece selecionada.
- Abrir pelo botão congelado `preferences` ou por `Ctrl+,` fecha o painel da fila, seleciona “Geral” e foca a categoria. As três categorias são “Geral”, “Reprodução” e “Aparência”.
- `Ctrl+,` abre ou refoca o painel; não alterna para fechado. `Esc` ou “Fechar preferências” fecha e devolve o foco ao botão `preferences`; se a lista de um seletor estiver aberta, o primeiro `Esc` fecha somente essa lista e mantém o painel modal.
- Enquanto o painel está aberto, atalhos de transporte e de fila não atuam. `Space` continua pertencendo à edição ou ao controle focado.
- Volume e mudo permanecem no transporte, sem controles duplicados no painel. Aplicativos Padrão não aparece como ação sem efeito; sua integração fica em M0.9.

### IDs, textos e edição

Os textos já presentes na fixture continuam vindo dela. Os IDs e textos adicionais abaixo ficam centralizados em cada candidato e obedecem à mesma tabela; não são novas chaves da fixture congelada.

| ID | Nome | Opções/regra |
|---|---|---|
| `preferences-close` | Fechar preferências | fecha e devolve foco ao acionador |
| `preferences-general` | Geral | categoria inicial |
| `preferences-playback` | Reprodução | categoria |
| `preferences-appearance` | Aparência | categoria |
| `close-behavior` | Ao fechar | Geral; “Minimizar para bandeja” / “Encerrar” |
| `restore-session` | Restaurar última sessão | Geral; booleano |
| `start-minimized` | Iniciar minimizado | Geral; booleano, desabilitado sem tray funcional |
| `skip-backward-seconds` | Segundos para retroceder | Reprodução; inteiro de 1 a 300 s |
| `skip-forward-seconds` | Segundos para avançar | Reprodução; inteiro de 1 a 300 s |
| `queue-end-behavior` | Ao terminar a fila | Reprodução; “Parar e manter seleção” / “Parar e limpar seleção” |
| `theme-mode` | Tema | Aparência; “Sistema” / “Claro” / “Escuro” |
| `motion-mode` | Movimento | Aparência; “Sistema” / “Reduzido” |

Cada campo de salto é avaliado a cada tecla e aceita dígitos decimais ASCII que representem um inteiro entre 1 e 300, aplicando cada valor válido imediatamente e de forma independente. Vazio, número fracionário, texto não aceito ou valor fora do intervalo permanece como draft inválido com a mensagem junto ao campo “Use um número inteiro entre 1 e 300.”; essa avaliação inválida não altera o último valor válido no core. Prefixos válidos já podem ter sido aplicados antes de o draft ficar inválido: ao digitar `350`, por exemplo, `3` e `35` são válidos, mas `350` não é. Não há rollback automático da edição. Fechar e reabrir descarta o draft e apresenta o último valor válido em memória, não necessariamente o valor anterior à edição. Os botões, nomes acessíveis e atalhos de salto usam esses mesmos valores.

### Resolução do sistema e limites da evidência

No candidato Qt, o tema “Sistema” consulta `QStyleHints::colorScheme`; esquema desconhecido usa a paleta clara. “Claro” e “Escuro” substituem a escolha do sistema. A resolução ocorre antes de criar o QML e é atualizada quando o esquema ou a preferência muda, usando os 12 tokens de cada paleta congelada sem reconstruir o estado do player. As cores também são aplicadas à palette dos controles Basic e aos sliders. O candidato Tauri deve reproduzir essa semântica e o mesmo fallback por seu adapter. A [documentação oficial de QStyleHints](https://doc.qt.io/qt-6/qstylehints.html#colorScheme-prop) descreve o acompanhamento do esquema do sistema e o sinal de alteração; cores explicitamente definidas pelo aplicativo exigem atualização própria. O fallback claro e as paletas da fixture são políticas deste experimento, não garantias da API.

No Windows, movimento “Sistema” usa uma consulta somente leitura `SPI_GETCLIENTAREAANIMATION`. A [referência oficial de SystemParametersInfoW](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-systemparametersinfow) define essa ação como a consulta de um booleano que informa se animações na área cliente estão habilitadas; o candidato não usa a operação de alteração da preferência. Resultado desconhecido ou API indisponível usa movimento reduzido por segurança, como política do experimento; a opção “Reduzido” prevalece sobre a preferência do sistema. Os valores vêm da fixture: painel em 160 ms e feedback em 120 ms no modo normal, ambos em 0 ms no reduzido, sem animação contínua em nenhum modo. A mesma política de resultado/fallback vale para os dois candidatos; detalhes de plataforma não entram no core.

### Evidência automatizada de preferências/temas (`ab87803`)

UI, presenter e adapter de sistema estão implementados no candidato Qt com Red/Green e testes de categorias exclusivas, semântica, foco modal, edição dos saltos e aparência. Os testes de `QtSystemAppearance` usam leitores, sinais e eventos controlados. O harness de `FixtureRepository` e `PlayerPresenter` passou a usar `QGuiApplication` offscreen: `QAbstractItemModelTester` exercitava papéis que geravam warnings de `QPixmap` com `QCoreApplication`. O core permanece sem Qt.

Dois Reds adicionais dos itens de seletor Basic reproduziram contraste 1:1 no item destacado e altura de 38 px abaixo do mínimo de 40 px. O delegate passou a usar os pares `Highlight`/`HighlightedText` e `Base`/`Text`, com altura preferida de 44 px que acomoda o conteúdo. A regressão também cobre `Esc` dentro do dropdown preservando o modal e `Tab`/`Shift+Tab` em Aparência. Isso não comprova o modo de alto contraste nativo do Windows.

- CTest Release estrito após os ajustes de lifecycle QML e delegates: 58/58 em 7,25 s; suíte isolada de `QtSystemAppearance`: 20/20.
- Quick Test estrito/offscreen após o ajuste final dos delegates: 7/7 com `QT_SCALE_FACTOR=1.5` e 7/7 com `QT_SCALE_FACTOR=2`; dez repetições do Quick e dez do smoke `shell-executable.error-recovery` passaram.
- Formato: 35 arquivos C++ e 15 QML aprovados; `qmllint` final silencioso, exit 0; contratos de fixture/markers aprovados com hashes inalterados.
- Build Qt OFF: 51/51 testes e 473 asserções; auditoria de 113 comandos, cache e imports sem Qt.
- Build novo Qt ON com `BUILD_TESTING=OFF`, seguido de rebuild do `main` e dos delegates finais: aprovado, zero testes registrados e nenhum executável de teste ou macro de controle de teste nos 152 comandos auditados; cenário indisponível e smoke artificiais rejeitados com exit 1.

O smoke do build sem testes observou o default vivo por 1,5 s sem output; o alias normal `--m0-scenario=empty` também foi aceito. Os processos dos smokes foram encerrados por PID e não restaram processos de validação. Essa observação não comprova saída graciosa do default. Os fatores de escala offscreen não comprovam DPI nativo nem NVDA.

No fechamento de preferências/temas em `ab87803`, alto contraste permanecia pendente para a fatia seguinte; as paletas claras/escuras e os fallbacks não comprovavam esse gate. Nenhuma validação manual nativa, NVDA, escala ou movimento reduzido foi executada nessa fatia, que não encerrou M0.2 nem aprovou integralmente UX-006 ou desempenho. Seus resultados acima são históricos; a evidência de alto contraste é registrada separadamente a seguir.

## 7. Política experimental de alto contraste do sistema

Esta política define a fatia de alto contraste implementada no candidato M0.2 e a paridade exigida do candidato M0.3. Os checks automatizados e a revisão de código da fatia foram aprovados, com evidência abaixo. A fixture e o protocolo congelados permanecem inalterados.

Quando o sistema solicita alto contraste, sua paleta prevalece temporariamente sobre “Sistema”, “Claro” e “Escuro”, sem alterar a preferência guardada em memória nem criar um quarto tema selecionável. Ao sair do alto contraste, reaplica-se a preferência atual, inclusive se ela tiver sido alterada enquanto o modo estava ativo. Movimento continua independente: alto contraste não muda a escolha “Sistema”/“Reduzido” nem os valores de duração definidos na seção 6.

Enquanto o modo estiver ativo, o painel de preferências apresenta a nota “O alto contraste do sistema está ativo. O tema escolhido será usado quando ele for desativado.”, com ID de automação e `Accessible.id` iguais a `preferences-high-contrast-note`. Esse texto adicional é centralizado no candidato e comum ao A/B, sem alteração da fixture.

### Cores e estados

A projeção usa cinco papéis de cor do sistema, sempre em pares opacos:

| Uso | Fundo | Primeiro plano |
|---|---|---|
| Conteúdo e controles em estado normal | `Window` | `WindowText` |
| Realce de seleção ou interação | `Highlight` | `HighlightedText` |
| Conteúdo e controles desabilitados | `Window` | `GrayText` |

Foco, hover, pressionado, seleção e feedback devem respeitar esses pares; não se derivam cores com alpha ou `darker`. O estado desabilitado usa a cor mapeada, sem reduzir a opacidade do controle para indicá-lo. A orientação oficial da Microsoft para [alto contraste](https://learn.microsoft.com/en-us/windows/win32/winauto/high-contrast-parameter) recomenda consultar as cores escolhidas pelo usuário e preservar pares de primeiro plano/fundo. A referência de [GetSysColor](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getsyscolor) identifica os pares de janela, realce e conteúdo desabilitado usados acima.

Sliders e scrollbar controlados pelo candidato seguem a mesma paleta. Em alto contraste, a scrollbar mantém opacidade 1; fora dele, seu fade usa o tempo de feedback de 120 ms no modo normal e 0 ms no “Reduzido”, sem fade residual de aparecimento ou desaparecimento. Essa política não pressupõe cobertura dos componentes internos nativos do Qt.

### Consulta e atualização por plataforma

No candidato Qt, a detecção usa `QAccessibilityHints::contrastPreference`, acessível por `QStyleHints::accessibility`; a [documentação de QAccessibilityHints](https://doc.qt.io/qt-6/qaccessibilityhints.html#contrastPreference-prop) descreve a preferência de contraste como informação somente leitura do sistema. No Windows, as cinco cores vêm de `GetSysColor` (`COLOR_WINDOW`, `COLOR_WINDOWTEXT`, `COLOR_HIGHLIGHT`, `COLOR_HIGHLIGHTTEXT` e `COLOR_GRAYTEXT`), não de um accent WinRT. Essa escolha evita usar a cor de destaque comum como substituta da paleta de alto contraste. No Linux, `QPalette` é o fallback experimental, sem evidência de validação nativa; o candidato M0.3 deve preservar a mesma precedência e os mesmos pares por seu adapter. Nenhuma dessas consultas altera as configurações do sistema ou introduz dependência de plataforma no core.

A primeira resolução ocorre antes da criação do QML. Em execução, notificações de contraste, esquema e paleta agendam uma atualização coalescida após o processamento do Qt, para ler o estado atualizado. A [documentação de QStyleHints](https://doc.qt.io/qt-6/qstylehints.html#colorScheme-prop) alerta que o sinal de mudança do esquema ainda pode observar a paleta anterior e orienta acompanhar eventos de paleta. No Windows, a atualização também considera `WM_SYSCOLORCHANGE`, que [notifica as janelas sobre mudanças nas cores do sistema](https://learn.microsoft.com/en-us/windows/win32/gdi/wm-syscolorchange). A coalescência é política do adapter, não garantia dessas APIs.

### Evidência automatizada da fatia de alto contraste

`HighContrastPalette` transporta as cinco cores tipadas entre adapter e apresentação, sem entrar no core. O contrato rejeita cores inválidas ou não opacas antes de publicar a aparência; a atualização é atômica e idempotente. `TransportSlider` reúne a apresentação controlada de progresso/volume, e `tst_high_contrast.qml` exercita paletas injetadas escuras/claras, foco, hover, seleção da fila, controles desabilitados, nota de precedência e saída do modo sem perder tema escolhido, modelo ou posição. A scrollbar controlada cobre opacidade 1 em alto contraste e transição imediata em movimento reduzido.

A auditoria independente encontrou três P2, cada um reproduzido por um Red Quick de 9 passagens/1 falha: contorno do handle focado em `Highlight` (`#f8f800`) sobre `Window`, em vez de `WindowText` (`#fafafa`); `Accessible.id` vazio na nota HC; e anel de foco visível do ComboBox com contraste de aproximadamente 1,09:1 sobre o realce. As correções usam os pares correspondentes ao fundo e o ID acessível explícito, com regressões de foco/contraste/identidade. A revisão independente de código confirmou os três achados resolvidos; isso não constitui inspeção nativa.

- Contrato C++ de apresentação: Red com 12 passagens/6 falhas; Green 18/18, repetido três vezes. Os testes cobrem precedência sobre os três temas, movimento independente, saída do modo, alterações de cada cor e rejeição sem mutação.
- Adapter `QtSystemAppearance`: Red com 17 passagens/16 falhas; Green 33/33, com 30 repetições por CTest. Leitores, sinais Qt e mensagens injetadas verificam coalescência após o turno de eventos, snapshot coerente e cancelamento de atualização pendente na destruição.
- Quick Test: o primeiro Red acusou o método de cenário ausente; após sua introdução, o Red mostrou HC ainda falso. A nova jornada ficou Green, com suíte completa 10/10 sem skips.
- CTest Release estrito após as três correções da auditoria: 58/58 em 8,38 s; `qmllint` silencioso. Formato final aprovado para 36 arquivos C++ e 17 QML, com `git diff --check` limpo.
- Quick estrito/offscreen reexecutado após as correções: 10/10 em `QT_SCALE_FACTOR=1.5` (2,748 s) e 10/10 em `QT_SCALE_FACTOR=2` (2,763 s). Quick e smoke `shell-executable.error-recovery` passaram dez vezes cada, 20 execuções finais por `--repeat until-fail:10` em 31,96 s.
- Core Qt OFF: 51/51 por CTest em 1,50 s e execução direta com 51 casos/473 asserções; 113 comandos de compilação, cache e imports auditados sem Qt. Fixture e markers revalidados com hashes inalterados.

O build Qt ON com `BUILD_TESTING=OFF` recebeu rebuild incremental após as três correções QML: 11 ações em 7,82 s, zero testes registrados pelo CTest, 153 comandos de compilação sem hooks ou fontes de teste, nenhum executável de teste e nenhuma DLL Qt Test. O default estrito permaneceu vivo por 1,5 s com stdout/stderr vazios; foi encerrado pelo próprio PID e não deixou processos residuais. Isso não comprova saída graciosa. O cenário indisponível, isolado e combinado com o smoke artificial, foi rejeitado com exit 1 no build HC anterior às três correções visuais; o parsing não mudou entre esses builds. Esta evidência é incremental, não um configure/build novo, e não reutiliza os resultados de preferências/temas da seção 6.

Rastreabilidade de `CHK-CLOSE`: commit que introduz a fatia HC e este registro, com os checks acima e revisão do diff.

As paletas e notificações injetadas comprovam a boundary e o QML do candidato, não a leitura real de `GetSysColor`, a alternância do Windows ou os ramos internos de alto contraste do estilo Qt Basic. Também não comprovam Linux, NVDA, DPI real ou a jornada nativa de movimento reduzido. Nenhuma nova inspeção manual da UI foi realizada. Alto contraste nativo requer evidência própria; UX-006 e M0.2 permanecem em andamento, sem novos requisitos funcionais declarados concluídos e sem aprovação de desempenho.
