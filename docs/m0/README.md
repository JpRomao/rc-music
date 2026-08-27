# Registro de autorização e evidências M0

**Estado:** M0 autorizado; M0.1 concluído e M0.2 em andamento com jornada Qt Quick vazia/tocando/erro, transporte, fila, navegação manual, preferências/temas, alto contraste e bandeja implementados no candidato; checks automatizados de ciclo de vida verdes; stack e engines não escolhidos

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
| M0.2 — shell C++20/Qt Quick/QML com backend falso | em andamento; [preflight concluído](evidence/m0-2-preflight.md), core/presenter determinísticos, jornada vazia/tocando/erro, transporte, fila com virtualização funcional e navegação manual anterior/próxima; recursos `qrc` autenticados; preferências/temas, alto contraste e bandeja implementados sob as políticas comuns das seções 6–8, com checks automatizados de ciclo de vida verdes; pacote, bandeja/alto contraste nativos, acessibilidade nativa/NVDA e medições pendentes |
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

## 8. Política experimental de bandeja e ciclo de vida

Esta seção define a política implementada no candidato Qt, comum aos shells Qt/M0.2 e Tauri/M0.3. Não altera a fixture, o protocolo ou os defaults congelados. A [UX, seção 7](../product/ux-specification.md), continua como referência para o aviso e o menu mínimo.

### Ações, menu e restauração

Fechar a janela respeita a preferência de fechamento: “Encerrar” solicita saída; “Minimizar para bandeja” segue o fluxo de ocultação. A ação explícita “Ocultar” do menu ignora a escolha “Encerrar”, sem alterá-la, e não pede o aviso de primeiro fechamento. “Sair” sempre solicita encerramento real e idempotente, sem alterar preferências. Reprodução/pausa e anterior/próxima da bandeja usam os mesmos casos de uso e capacidades do transporte principal, inclusive as restrições da navegação da seção 5.

| ID comum | Conteúdo e ação |
|---|---|
| `tray-current-track` | título atual ou contextual, quando disponível, ou “Nenhuma faixa selecionada”; restaura a janela |
| `tray-play-pause` | rótulo de reproduzir/pausar da fixture, conforme o estado; aciona o transporte comum |
| `tray-previous` | anterior, com a capacidade atual do transporte |
| `tray-next` | próxima, com a capacidade atual do transporte |
| `tray-show-hide` | “Ocultar” com a janela exibida; “Mostrar” quando oculta ou minimizada |
| `tray-exit` | “Sair” |

O título do menu é uma projeção de texto plano, elidida à direita em 280 DIP, com quebras de linha/tabs e espaços normalizados; ampersands permanecem literais. Não se modifica a metadata original. No Qt, `&` é escapado apenas na projeção de `QAction`, cuja [documentação de texto](https://doc.qt.io/qt-6/qaction.html#text-prop) distingue mnemônicos de ampersands literais.

`Trigger` e `DoubleClick` no ícone solicitam restauração e ativação idempotentes, nunca alternância de visibilidade. A restauração preserva o último modo não minimizado — normal, maximizado ou fullscreen —, seleção, fila, posição/estado de reprodução e foco válido. Repetir o gesto não pode voltar a ocultar a janela. A [API de QSystemTrayIcon](https://doc.qt.io/qt-6/qsystemtrayicon.html#ActivationReason-enum) distingue os motivos de ativação; a escolha de restaurar nos dois casos é a política comum do experimento.

### Avisos por execução, sem persistência

Na primeira tentativa de fechar para a bandeja de cada execução, com o aviso habilitado, um aviso aparece na própria janela **antes de ocultá-la**: Popup QML no Qt, com equivalente no Tauri. Título: “Continuar na bandeja”. Corpo exato da UX: “O player continuará na bandeja. Use Sair para encerrar.” “Continuar” confirma a ocultação e consome o primeiro aviso; “Cancelar” ou `Esc` mantêm a janela e não consomem esse primeiro aviso. Não há confirmação ou ocultação automática por tempo. Um balão do sistema não substitui esse fluxo: a [documentação de showMessage](https://doc.qt.io/qt-6/qsystemtrayicon.html#showMessage) informa que a mensagem pode não aparecer conforme a configuração do usuário.

O checkbox “Avisar ao fechar para a bandeja nesta sessão” começa habilitado em cada execução e aplica a escolha imediatamente em memória. Desmarcar e depois cancelar deixa o aviso desabilitado para o próximo fechamento, embora a tentativa cancelada não tenha consumido o primeiro aviso. Confirmar “Continuar” já consome o primeiro aviso mesmo mantendo o checkbox habilitado. Não há alegação de opt-out persistente entre execuções.

Sem bandeja disponível, ações de ocultação — inclusive fechar configurado para bandeja — resultam em minimização normal da janela, nunca apenas em ocultação. A ausência conhecida pelo core gera `minimize_window`; se ela for observada somente pelo adapter durante a tentativa de ocultar, o evento `hideFellBackToMinimize` publica a nota após a minimização efetiva, mesmo sem mudança do cache de disponibilidade. Isso não substitui a saída explícita de “Sair” ou de fechar configurado para “Encerrar”. A limitação fica disponível em um Popup na restauração normal da janela, com título “Bandeja indisponível” e corpo “Não há bandeja disponível neste ambiente. Ao fechar, a janela é minimizada normalmente.” “Entendi” apenas dispensa a nota, sem ocultar, encerrar ou alterar a preferência. Perda de disponibilidade observada enquanto a janela está oculta causa a mesma minimização e disponibiliza a mesma nota.

O aviso conserva seu texto durante o fade de saída. Uma nova tentativa de fechar durante esse fade reabre o aviso sem perder o foco de retorno; callbacks de foco da fila e do fechamento de Preferências não podem retirar foco do modal ativo. Atalhos da janela ficam suspensos enquanto o aviso está visível. Movimento normal/reduzido continua usando 160/0 ms, sem timeout de confirmação.

Os textos adicionais de menu e avisos acima ficam centralizados em `LifecycleText` C++ no candidato Qt e devem ter os mesmos textos no candidato Tauri; rótulos de transporte já congelados continuam vindo da fixture.

### Adapter Qt, capacidades e limites

`QtShellPlatform` usa `QSystemTrayIcon`, `QMenu` e `QApplication`, acrescentando Qt Widgets somente ao adapter/shell experimental. A justificativa é usar o ícone e o menu do toolkit com integração de plataforma, mantendo a UI principal em Qt Quick e o core sem Qt. [QSystemTrayIcon](https://doc.qt.io/qt-6/qsystemtrayicon.html) e [QMenu](https://doc.qt.io/qt-6/qmenu.html) pertencem a Widgets; [QApplication](https://doc.qt.io/qt-6/qapplication.html#details) fornece a inicialização específica necessária aos widgets. Widgets vem do `qtbase` já provisionado; os SBOMs com `NOASSERTION` e demais bloqueios de distribuição do [preflight](evidence/m0-2-preflight.md) permanecem. O ícone provisório reutiliza `cover-track.svg` autenticado, sem alterar a fixture. O menu segue o estilo/paleta do sistema; não se forçam nele as cores do QML nem se alteram preferências do SO.

O adapter consulta disponibilidade com um timer Qt coarse de 1 s **somente enquanto a janela estiver oculta**, interrompido ao mostrar, sair ou destruir a janela/adapter. Isso não prova presença real da bandeja no Windows: no [backend Windows do Qt 6.11.2](https://github.com/qt/qtbase/blob/v6.11.2/src/plugins/platforms/windows/qwindowssystemtrayicon.h), `isSystemTrayAvailable()` retorna `true`; o Qt já possui [tratamento de TaskbarCreated](https://github.com/qt/qtbase/blob/v6.11.2/src/plugins/platforms/windows/qwindowssystemtrayicon.cpp). O fallback responde à indisponibilidade observada, sem alegar detecção garantida de queda/reinício do Explorer. Tauri deve preservar o comportamento comum por suas capacidades e adapter, registrando as limitações próprias.

Uma trava de saída é marcada antes de agendar o fechamento explícito da `QWindow` e `quit`, impedindo pedidos duplicados ou reativação posterior. Esse fechamento é necessário porque o [caminho de saída de QApplication 6.11.2](https://github.com/qt/qtbase/blob/v6.11.2/src/widgets/kernel/qapplication.cpp#L1551) trata janelas QWidget e pula o fechamento de QWindows de `QGuiApplication`. `quitOnLastWindowClosed` fica `false`: a política de fechar é explícita, não o encerramento implícito da última janela descrito pela [API de QGuiApplication](https://doc.qt.io/qt-6/qguiapplication.html#quitOnLastWindowClosed-prop). O smoke exige saída real em código 0 e janela QML fechada, sem encerramento forçado como evidência de sucesso.

Essa política não aprova bandeja nativa, recuperação do Explorer, NVDA, DPI real, Linux ou empacotamento, nem conclui integralmente FR-030/FR-031/FR-032. Áudio real, salvamento persistente antes de sair e sessão completa continuam fora desta fatia experimental. M0.2 permanece em andamento.

### Evidência automatizada da fatia Qt de ciclo de vida — 2026-08-27

O incremento parte de `4b4bf42` (HC) e usa Qt 6.11.2/MSVC x64, CMake Release e a mesma fixture autenticada. Os resultados abaixo pertencem ao código desta fatia, não à bandeja real do Windows:

- **Core:** Red de APIs ausentes e depois quatro falhas comportamentais na distinção fechar/ocultar/sair; Green Qt OFF 55/55, 1.849 asserções. As 113 entradas de compilação do build isolado continuam sem Qt.
- **Presenter:** Red com 20 passagens/24 falhas nas novas transições; Green 44/44. Cinco repetições do teste após refactor passaram. Há cobertura de aviso inicial, confirmação/cancelamento, opt-out, reentrância, saída idempotente com timer parado, preservação de fila/modelo/seek e whitelist QML.
- **Adapter:** Red de comportamento seguido de Green 24/24, com menu, intenções, modos da janela, disponibilidade, polling restrito, saída e destruição. Snapshots de transporte idênticos não repetem atualização de ações nem leitura de disponibilidade.
- **Binding integrado:** três Reds reproduziram minimização sem nota quando a bandeja desaparecia entre a consulta do core e a guarda do adapter. Green final 8/8 cobre cache previamente verdadeiro/falso, confirmação do primeiro aviso, preservação de preferências/modelo/seek e lifetimes/saída reentrante. Adapter, binding e presenter passaram três repetições cada, em 17,24 s.
- **Opções de lançamento:** Reds de smoke desconhecido e de combinação com `Unavailable` nas duas ordens; Green 24/24 e dez repetições. `tray-lifecycle` exige `Empty`; duplicatas e combinações inválidas são rejeitadas.
- **QML:** o Red inicial confirmou que fechar escondia a janela sem política. Reds adicionais reproduziram troca indevida de texto durante fade, perda do novo aviso ao reabrir durante saída, foco de Preferências e foco enfileirado da fila escapando do modal. As regressões foram corrigidas; o teste de sobreposição exige uma amostra durante a entrada do aviso, sem aprovação vacuamente verdadeira.
- **Release completo:** CTest estrito 65/65 em 14,95 s, com `QT_FATAL_WARNINGS=1`; `qmllint` silencioso. Formato de 43 arquivos C++ e 20 QML aprovado.
- **Jornadas repetidas:** Quick e os smokes reais de erro/ciclo de vida passaram dez vezes cada — 30 execuções, 56,40 s. Quick passou 16/16 em cada escala offscreen `1.5` e `2`, em 4.760 ms e 4.837 ms. Isso não é prova de DPI nativo.
- **Saída real:** o smoke primeiro falhou sem adapter/política explícita e depois detectou que `QApplication::quit` sozinho deixava a QWindow visível. Com fechamento enfileirado explícito, sucesso exige retorno 0, trava de saída e janela fechada; não se usa kill para aprovar essa jornada.
- **Build novo sem testes:** `tray-no-tests-20260827`, Qt ON/Release/`BUILD_TESTING=OFF`, completou 106 ações Ninja e gerou 156 entradas de compilação. CTest descobriu zero testes; não há executáveis de teste, define de hooks ou Qt Test nas dependências do app. Qt Widgets foi confirmado nos imports e módulos carregados. Cenário indisponível e ambos os smokes foram rejeitados com saída 1.
- **Startup sem testes:** permaneceu vivo por 1,5 s, offscreen/estrito, com stdout/stderr vazios. O PID retido foi encerrado à força somente para cleanup; esse check não comprova saída graciosa. A inspeção final não encontrou processos Qt do workspace remanescentes.
- **Contratos:** fixture `21a150477811aa62f59f4f958d594786bcef4a7c2dfd2bfda3ef9d29dee7c8e5` e markers `60d73a785cfeb67c0cec45fb2c77d1687edf151e8d44a122d669b4800e345d4c` permanecem inalterados; validação documental e diff check aprovados.

Logs locais ficam em `out/m0/builds/qt-shell/final-release`, incluindo `lifecycle-full-ctest.log`, `lifecycle-repeat-10.log` e os Reds/Greens QML, além dos builds isolados `navigation-core`, `lifecycle-presenter`, `tray-adapter`, `tray-binding` e `lifecycle-launch-options`. Nenhuma inspeção manual foi retomada após a interrupção de Computer Use; pacote, bandeja/Explorer nativos, NVDA, HC/DPI reais, Linux e medições continuam pendentes.

## 9. Política experimental de pacote Qt para avaliação local

Esta fatia prepara uma pasta executável para avaliação privada do mantenedor, não um instalador nem uma release redistribuível. Usa o shell determinístico, sem áudio ou rede reais, e preserva a fixture e o protocolo. O pacote inclui os controles fechados de cenário/smoke de `BUILD_TESTING=ON`; não deve ser apresentado como build de produção sem hooks. M0.2 continua em andamento.

### Receita, estrutura e proveniência

A receita [New-QtShellPackage.ps1](../../experiments/m0/candidates/qt/package/New-QtShellPackage.ps1) recebe um build e um nome de destino, exige CMake/Ninja Release x64 com Qt e hooks habilitados e reconstrói o alvo `m0_qt_shell`. Shell, fixture e protocolo devem estar commitados e limpos antes e depois do build. A receita cria um destino novo sob `out/m0/packages/qt-shell`, rejeitando sobrescrita, escape de paths, aliases Windows inseguros e reparse points. Os diretórios de entrada e saída devem permanecer quiescentes; não há garantia contra um escritor concorrente hostil.

| Caminho relativo ao destino | Conteúdo |
|---|---|
| `app/` | executável, DLLs, `plugins/`, `qml/`, `qt.conf`, aviso `LOCAL-EVALUATION.txt` e inventários `third-party/*.spdx` |
| `evidence/` | log de build, plano de deployment e relatório de imports PE; a evidência do smoke é registrada separadamente |
| `manifest.v1.json` | metadata de proveniência e inventário exato de `app/`, com paths relativos, tamanhos e SHA-256 |
| `manifest.v1.json.sha256` | digest do manifesto, que deve ser confrontado com uma referência externa antes da verificação |

O manifesto fica fora do payload para não incluir a si próprio. A verificação rejeita arquivos ausentes, extras ou alterados. Quantidade de arquivos e bytes do payload são relatadas separadamente dos pré-requisitos externos, sem convertê-las em métricas de RAM ou desempenho.

A proveniência registra `source_commit`, `source_paths_clean` e `recipe_dirty`. A limpeza dos diretórios de fonte não significa que todo o worktree ou a receita estejam commitados. O manifesto também inclui `configuration`, `architecture`, `test_hooks`, as versões Qt/MSVC e hashes de cache, identidade do compilador, contrato de toolchain, fixture, protocolo, plano de deployment, relatório PE, ferramentas e arquivos da receita. `external_msvc_runtime`, `redistribution_allowed=false` e `known_limits` tornam os pré-requisitos e limites explícitos.

### Dependências QML, plugins e imports PE

O `windeployqt` executa scan real com `--qmldir` apontando para o QML do shell e `--qmlimport` para o QML gerado pelo build. O plano do dry-run e o plano da cópia devem coincidir, e cada arquivo copiado deve conservar tamanho e hash da origem. `--no-patchqt` evita alteração dos bytes de Qt durante o deployment. A [documentação oficial de deployment Windows](https://doc.qt.io/qt-6/windows-deployment.html) descreve o scan por `qmlimportscanner` e as opções de seleção de dependências.

As origens permitidas são a toolchain Qt e o diretório explícito `build/qml/MusicPlayer/M0`, identificado como `application-qml`; essa exceção não libera outros arquivos do build nem DLLs do aplicativo. A receita conserva a cópia do módulo QML e os estilos opcionais encontrados pelo scanner, sem otimização desse conjunto nesta fatia. Os recursos `qrc` continuam dentro do executável.

O `qt.conf` usa `Prefix=.`, `Plugins=plugins` e `QmlImports=qml`. Assim, os paths são relativos ao executável, conforme a [documentação de qt.conf](https://doc.qt.io/qt-6/qt-conf.html). O payload mantém o plugin Windows, o plugin offscreen usado nos checks e os plugins SVG. A independência efetiva dos paths de desenvolvimento deve ser comprovada pelo smoke com ambiente saneado.

A seleção automática de D3D compiler do sistema é desativada com `--no-system-d3d-compiler`. A receita adiciona explicitamente `QtRoot/d3dcompiler_47.dll`, já provisionado, ao plano e à cópia; não usa a cópia descoberta no Windows SDK ou em System32. `--no-system-dxc-compiler` exclui DXC/Direct3D 12 desta avaliação. Compiler runtime, FFmpeg, traduções e plugins de debug QML não entram pela seleção automática; ferramentas, bibliotecas de teste e arquivos de áudio são rejeitados no payload.

O relatório PE verifica x64 e imports diretos e de delay-load. Cada dependência deve estar no payload ou ser um pré-requisito Windows identificado. Arquivos de System32 são somente lidos e inventariados, nunca copiados. Essa análise não comprova todas as cargas dinâmicas nem execução em máquina limpa.

### Compilador, runtime e limites

A receita exige Qt 6.11.2 compilado com MSVC 19.44.35227 e shell compilado com MSVC 19.43.34808.0. A identidade registrada pelo CMake é comparada com a versão física de `cl.exe`, e o manifesto inclui o hash desse binário. Divergência exige revisão da receita.

O CRT x64 é um pré-requisito externo. O pacote não inclui nem instala o redistributable local 14.42 ou DLLs do runtime retiradas do Windows. A versão usada no host é registrada no inventário. A Microsoft exige runtime compatível com o componente compilado pelo toolset mais recente e impõe condições também à versão do linker. Portanto, o uso do linker 19.43 com Qt compilado por 19.44 permanece uma ressalva de compatibilidade, não resolvida por um smoke local. Referências: [Visual C++ Redistributable](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist?view=msvc-170) e [compatibilidade binária MSVC](https://learn.microsoft.com/en-us/cpp/porting/binary-compat-2015-2017?view=msvc-170).

As cópias dos SBOMs Qt não substituem notices, fontes correspondentes ou revisão de licenças. O fechamento de licenças/notices de terceiros, D3D e a licença do projeto continuam pendentes antes de redistribuir. `NOASSERTION` não é uma licença nem prova de incompatibilidade. Mesa não está neste payload; sua pendência histórica no preflight não é resolvida por essa ausência.

Os smokes devem usar ambiente saneado, sem depender dos paths de desenvolvimento do Qt, e verificar o manifesto antes e depois da execução. A jornada de ciclo de vida exige saída normal em código 0. Encerramento forçado serve somente ao cleanup e não aprova essa jornada.

Esses checks locais não concluem `CHK-PACKAGE` integral nem M0.2. Comparação, métricas e gates comuns dos dois candidatos pertencem a M0.4; instalador, associações, upgrade e uninstall ficam em M0.9. Bandeja/Explorer, NVDA, HC/DPI reais e Linux continuam pendentes. O preflight histórico conserva seu escopo e seus resultados; esta política não os reinterpreta.

### Evidência desta fatia — 2026-08-27

O stage `out/m0/packages/qt-shell/tray-34edef1-local-02` deriva do shell de `34edef16e98b576e292c2b66b0349d2da07e583a`, Qt 6.11.2/MSVC x64, Release/`BUILD_TESTING=ON`. Shell/fixture/protocolo estavam limpos; a receita ainda estava em desenvolvimento e registra `recipe_dirty=true`. O manifesto externo tem SHA-256 `5e1368e58431fb9f6ef7908ac25a0b68ebe668eff8b2a8b9d37587379acb1d90`. Isso identifica este artefato de diagnóstico, não um batch comparativo nem uma release pública.

| Origem | Arquivos | Bytes |
|---|---:|---:|
| Toolchain Qt | 1.360 | 75.285.179 |
| Cópia QML da aplicação | 12 | 71.521 |
| Executável, `qt.conf`, aviso e três SPDX | 6 | 7.031.647 |
| Payload total | 1.378 | 82.388.347 |

A revisão independente conferiu o manifesto, todas as origens do plano e os 69 PEs x64 por `dumpbin`, sem executar o payload. Há 29 DLLs Qt na raiz, 12 plugins nativos, 26 plugins QML e um D3D compiler, além do executável. As 733 referências PE se dividem em 228 internas, 109 CRT, 133 Windows e 263 API sets. Os 41 arquivos externos conferem em hash/versão no host; os cinco componentes MSVC CRT são `14.51.36247.0`. Não há Qt Test, executáveis de teste, CRT/redist, ferramentas, áudio, arquivos debug, DXC ou Mesa no payload. O D3D compiler confere com a cópia provisionada na toolchain, não com Windows Kits/System32.

Cinco componentes da aplicação aparecem somente como recursos `qrc`, preferidos pelo `qmldir`, e não como cópia QML solta. Seus nomes de recursos estão no executável, idêntico ao build. A inspeção estática não identificou componente ausente, mas somente o smoke pode comprovar a resolução efetiva e a jornada desse pacote.

Checks da receita:

- **Builder:** Red inicial 0/10; Green final 14/14. Casos cobrem origem/destino, sobrescrita, aliases/nomes reservados, reparse, cache Release/hooks, imports PE, arquivos proibidos e versões. Reds adicionais reproduziram a origem QML gerada fora da toolchain e o parsing do compiler com CRLF. A primeira tentativa de deployment com ambiente VS foi corretamente rejeitada por selecionar D3D de Windows Kits; a receita passou a usar explicitamente a cópia Qt provisionada.
- **Manifesto:** Red inicial de inventário vazio e Red adicional de igualdade cultural indevida entre caminhos Unicode distintos; Green 113/113, repetido três vezes. Há cobertura de conjunto exato, hashes/tamanhos, UTF-8/JSON, duplicatas, caminhos, ADS, hardlinks, junctions e arquivos bloqueados. A comparação final de paths é ordinal.
- **Smoke com fakes:** Green final 38/38, repetido três vezes, sem executar Qt. Cobre ambiente filho, DLLs/paths, captura/timeout/cleanup, guard contra diálogos do loader e revalidação do manifesto após ambas as jornadas. Red de pós-validação confirmou que uma alteração no payload impedia aprovação; a whitelist não foi relaxada.
- **Release comum:** CTest estrito 65/65 em 13,45 s após o rebuild do alvo. Esses testes usam o ambiente de desenvolvimento e não substituem o smoke isolado do pacote.
- **Checks finais:** core Qt OFF 55/55, 1.849 asserções; formato de 43 C++/20 QML e `qmllint` silencioso; parser de oito scripts/módulos PowerShell aprovado. Validação documental passou com 30 Markdown, 83 requisitos, 17 checks e 76 etapas; diff check aprovado.
- **Relocação de arquivos:** cópia para `out/m0/packages/qt-shell/relocação com espaços 34edef1-01`, com os mesmos 1.378 arquivos e o mesmo hash de manifesto, aprovada pelo validador. Não houve execução do aplicativo nessa cópia.

A primeira tentativa real de `tray-lifecycle` do pacote foi rejeitada porque o processo carregou o módulo externo `RTSSHooks64.dll`, injetado pelo RivaTuner Statistics Server. O runner encerrou somente o processo retido; a inspeção posterior não encontrou players do workspace remanescentes. O smoke `error-recovery` **do pacote** não foi executado. A retomada aguarda o fechamento do RivaTuner pelo usuário, sem alterar sua configuração nem relaxar a whitelist do runner. Os testes unitários do runner não substituem essa integração bloqueada.

As evidências locais ficam em `evidence/` desse stage: `deployment-plan.v1.json`, `pe-closure.v1.json`, `smoke-blocked-rtss.v1.json`, `release-ctest.txt` e logs dos testes PowerShell. Parser/whitespace e validação documental fazem parte do fechamento. PSScriptAnalyzer não está instalado e não é alegado como lint aprovado. A rastreabilidade de `CHK-CLOSE` é o commit que introduz a receita, testes e este registro; o artefato acima preserva a proveniência anterior explicitamente indicada.

### Regeneração com a receita commitada

Após o commit `200b9838dc9e86427e083771ef71bc9f1ee49f4b`, a receita gerou `out/m0/packages/qt-shell/tray-200b983-clean-01` com o worktree limpo, `source_paths_clean=true` e `recipe_dirty=false`. O novo manifesto tem SHA-256 `a227bb2b23b7ec958b49318183ec399fd9c1e648e640a75e5fa7d06e5a97dc99`. A verificação dos dois manifestos confirmou os mesmos 1.378 paths, tamanhos e hashes de payload, totalizando 82.388.347 bytes; os hashes da receita conferem com os arquivos commitados. O relatório PE continua com 69 arquivos e 41 pré-requisitos externos.

O build existente foi reutilizado (`ninja: no work to do.`); esta evidência prova a regeneração do stage pela receita limpa, não um rebuild do zero nem reprodução binária em outra máquina. O resultado está em `evidence/committed-recipe-check.v1.json` do novo stage. Não houve execução do aplicativo ou nova tentativa de smoke: o RivaTuner continuava ativo e a autorização para retomar o desktop não foi recebida. A relocação registrada acima continua sendo a cópia do stage anterior, não uma execução relocada do novo pacote.

M0.2 permanece em andamento. Execução isolada/relocada, bandeja/Explorer nativos, NVDA, HC/DPI reais, Linux, máquina limpa, integração de instalador e medições continuam pendentes. Não houve retomada de Computer Use, leitura dos áudios pessoais, distribuição ou decisão de stack.
