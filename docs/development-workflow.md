# Workflow de desenvolvimento

**Estado:** contrato complementar a `../agents.md`  
**Última revisão:** 2026-08-23

## 1. Antes de trabalhar

1. ler `agents.md` e os documentos relevantes;
2. executar `git status --short --branch`;
3. identificar alterações preexistentes e não tocá-las sem necessidade;
4. escolher um requisito/caso pequeno e seu critério de aceitação;
5. verificar se a stack/decisão necessária está `Aceita` ou ainda `Proposta`;
6. definir o teste Red e a validação final antes de editar.

Se a decisão estrutural estiver proposta, trabalhar apenas no spike autorizado ou pedir decisão; não transformar recomendação em dependência definitiva silenciosamente.

## 2. Unidade de trabalho

Uma unidade ideal cabe em uma frase:

- “limitar salto ao fim da faixa”;
- “persistir valor de avanço”;
- “anunciar nome acessível do botão pause”;
- “tratar capa acima do limite”.

Evite unidades como “fazer o player” ou “implementar configurações”. Divida por comportamento observável.

## 3. Ciclo padrão

### Descobrir

- localizar requisito e arquitetura;
- confirmar estado atual por código/teste, não por suposição;
- identificar portas e risco de plataforma;
- atualizar ADR primeiro se a mudança altera uma decisão.

### Red

- escrever um teste que falha pelo motivo correto;
- usar fake/fixture mínima;
- não introduzir implementação disfarçada no helper de teste.

### Green

- implementar somente o necessário;
- manter regra no domínio/application, não duplicada em UI/adapter;
- executar o teste focado.

### Refactor

- melhorar nomes e remover duplicação;
- preservar contrato e manter suíte verde;
- atualizar docs junto da mudança observável.

### Verificar

- executar gates proporcionais de `quality/quality-gates.md`;
- revisar diff completo e `git diff --check`;
- checar que arquivos gerados/cache não entraram;
- registrar checks não executados.

### Commitar

- stage somente arquivos da unidade;
- mensagem imperativa e específica;
- confirmar working tree e histórico;
- nunca deixar mudança própria concluída sem commit.

## 4. Commits

Formato preferido:

```text
<tipo>(<escopo opcional>): <ação curta no imperativo>
```

Tipos:

- `feat`: comportamento de produto;
- `fix`: correção com regressão;
- `test`: infraestrutura/casos sem mudança de produto;
- `refactor`: desenho sem mudança observável;
- `docs`: documentação;
- `perf`: performance comprovada;
- `build`: manifests, packaging e CI;
- `chore`: manutenção realmente não enquadrada acima.

Exemplos:

```text
feat(playback): clamp skip at track boundaries
fix(queue): ignore stale ended event
test(audio): add AAC seek contract fixture
docs(adr): accept Tauri desktop stack
```

Regras:

- teste e implementação ficam no mesmo commit verde;
- não misturar formatação global com feature;
- manifest e lockfile ficam juntos;
- mudança de contrato inclui docs/ADR no mesmo commit;
- não usar amend/rebase/force sem pedido explícito;
- um spike também é commitado, identificado e depois promovido ou removido por commit.

## 5. Branches

Como projeto pessoal, trabalhar diretamente em `main` é aceitável quando cada commit é pequeno e verde. Use branch para:

- spike com chance alta de descarte;
- migração estrutural longa;
- experimento de stack/fallback;
- trabalho paralelo que não deve interromper o baseline.

Mesmo em branch, faça commits regulares. Nunca dependa de stash como armazenamento duradouro.

## 6. Bugs

1. reproduzir e registrar ambiente/entrada;
2. escrever teste de regressão Red no nível mais baixo que observa o contrato;
3. corrigir causa, não sintoma visual;
4. executar testes adjacentes e de integração relevantes;
5. adicionar fixture mínima se o bug veio de arquivo;
6. atualizar risco/matriz se a alegação de formato mudou;
7. commitar como `fix`.

Bug sem reprodução automatizável ainda exige procedimento manual documentado e justificativa.

## 7. Dependências

Toda dependência nova exige:

- problema concreto e alternativa padrão considerada;
- manutenção e licença verificadas;
- features mínimas;
- adapter quando for I/O/plataforma;
- teste que demonstre o valor;
- impacto em bundle/build observado;
- manifest e lockfile no commit;
- atualização de stack/ADR quando estrutural.

Não instalar ferramenta global como solução do projeto. Piná-la em manifest, script ou documentação reproduzível.

## 8. Spikes

Spikes respondem uma pergunta e possuem limite explícito.

Cada spike começa com:

- hipótese;
- opções;
- cenário de teste;
- métricas/gate;
- limite de escopo;
- decisão esperada.

Termina com:

- evidência reproduzível;
- commit(s) identificáveis;
- ADR atualizado como Aceito/Rejeitado ou ainda Proposto com motivo;
- código promovido com testes ou removido por commit;
- nenhuma dependência experimental esquecida.

## 9. Trabalho com mudanças preexistentes

- não resetar, limpar, stashear ou sobrescrever automaticamente;
- limitar buscas e comandos ao workspace;
- se um arquivo necessário já está modificado, ler diff e trabalhar ao redor;
- se não for possível separar com segurança, parar e pedir direção;
- no handoff, distinguir suas mudanças das preexistentes.

## 10. Documentação viva

Atualizar:

- requisitos quando comportamento/critério muda;
- UX quando interação/microcopy muda;
- ADR quando decisão/alternativa muda;
- arquitetura quando fronteira/fluxo muda;
- estratégia de teste quando gate muda;
- roadmap quando marco entra/sai;
- README quando comandos/status mudam.

Não atualizar datas/estados mecanicamente sem mudança real. Nunca marcar “Aceito” apenas porque uma biblioteca foi testada em um arquivo.

## 11. Pausa e handoff

Ao pausar:

- parar novos processos do workspace, salvo pedido para mantê-los;
- commitar unidade concluída;
- se houver Red/incompleto, não mascarar: descrever branch/commit/estado;
- informar validações, riscos e próximo passo exato;
- não continuar commits/checks após pedido explícito de pausa.

## 12. Definição de pronto

Além de `agents.md`, confirmar:

- requisito e critério satisfeitos;
- Red–Green–Refactor aplicado quando há comportamento;
- suíte proporcional verde;
- performance/acessibilidade revisadas quando afetadas;
- docs e ADR coerentes;
- diff sem conteúdo acidental;
- commit criado;
- working tree sem mudança própria pendente;
- risco residual declarado.
