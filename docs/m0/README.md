# Registro de autorização e evidências M0

**Estado:** M0 autorizado; stack e engines não escolhidos

**Data:** 2026-08-23

**Escopo:** autorização, contratos M0.0 e índice das evidências experimentais

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
| M0.1 — fixture comum, schemas, roteiro e harness | em andamento; contratos comuns e piloto sintético `P` materializados, gates finais pendentes |
| M0.2/M0.3 — shells Qt/Tauri | pendente |
| M0.4 — resultados de shell | pendente |
| M0.5–M0.8 — corpus e áudio | pendente |
| M0.9 — integração/pacote | pendente |
| M0.10 — combinação e decisão | pendente |

Resultados brutos grandes não entram no Git sem necessidade. O repositório preserva schemas, protocolo, resumos e hashes suficientes para reproduzir ou auditar cada decisão.
