# Registro de autorização e evidências M0

**Estado:** M0 autorizado; M0.1 concluído e M0.2 em andamento com jornada Qt Quick vazia/tocando/erro, transporte, fila, navegação manual e preferências/temas implementados no candidato; checks automatizados da fatia verdes; stack e engines não escolhidos

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
| M0.2 — shell C++20/Qt Quick/QML com backend falso | em andamento; [preflight concluído](evidence/m0-2-preflight.md), core/presenter determinísticos, jornada vazia/tocando/erro, transporte, fila com virtualização funcional e navegação manual anterior/próxima; recursos `qrc` autenticados; preferências/temas implementados sob a política comum da seção 6, com checks automatizados da fatia verdes; tray, pacote, alto contraste, acessibilidade nativa/NVDA e medições pendentes |
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

### Evidência automatizada da fatia

UI, presenter e adapter de sistema estão implementados no candidato Qt com Red/Green e testes de categorias exclusivas, semântica, foco modal, edição dos saltos e aparência. Os testes de `QtSystemAppearance` usam leitores, sinais e eventos controlados. O harness de `FixtureRepository` e `PlayerPresenter` passou a usar `QGuiApplication` offscreen: `QAbstractItemModelTester` exercitava papéis que geravam warnings de `QPixmap` com `QCoreApplication`. O core permanece sem Qt.

Dois Reds adicionais dos itens de seletor Basic reproduziram contraste 1:1 no item destacado e altura de 38 px abaixo do mínimo de 40 px. O delegate passou a usar os pares `Highlight`/`HighlightedText` e `Base`/`Text`, com altura preferida de 44 px que acomoda o conteúdo. A regressão também cobre `Esc` dentro do dropdown preservando o modal e `Tab`/`Shift+Tab` em Aparência. Isso não comprova o modo de alto contraste nativo do Windows.

- CTest Release estrito após os ajustes de lifecycle QML e delegates: 58/58 em 7,25 s; suíte isolada de `QtSystemAppearance`: 20/20.
- Quick Test estrito/offscreen após o ajuste final dos delegates: 7/7 com `QT_SCALE_FACTOR=1.5` e 7/7 com `QT_SCALE_FACTOR=2`; dez repetições do Quick e dez do smoke `shell-executable.error-recovery` passaram.
- Formato: 35 arquivos C++ e 15 QML aprovados; `qmllint` final silencioso, exit 0; contratos de fixture/markers aprovados com hashes inalterados.
- Build Qt OFF: 51/51 testes e 473 asserções; auditoria de 113 comandos, cache e imports sem Qt.
- Build novo Qt ON com `BUILD_TESTING=OFF`, seguido de rebuild do `main` e dos delegates finais: aprovado, zero testes registrados e nenhum executável de teste ou macro de controle de teste nos 152 comandos auditados; cenário indisponível e smoke artificiais rejeitados com exit 1.

O smoke do build sem testes observou o default vivo por 1,5 s sem output; o alias normal `--m0-scenario=empty` também foi aceito. Os processos dos smokes foram encerrados por PID e não restaram processos de validação. Essa observação não comprova saída graciosa do default. Os fatores de escala offscreen não comprovam DPI nativo nem NVDA.

Alto contraste continua explicitamente pendente para a próxima fatia; as paletas claras/escuras e os fallbacks não comprovam esse gate. Nenhuma validação manual nativa, NVDA, escala ou movimento reduzido foi executada nesta fatia; M0.2 permanece em andamento e sem alegação de aprovação integral de UX-006 ou desempenho.
