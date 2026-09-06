# Guia de trabalho do projeto

Este arquivo é o contrato operacional para qualquer pessoa ou agente que trabalhe neste repositório. Leia-o por completo antes de analisar, documentar, implementar, testar ou publicar qualquer mudança.

## 1. Objetivo do produto

Construir um player de música desktop pessoal, leve, rápido e agradável de usar, com prioridade para Windows e suporte planejado para Linux.

O produto deve privilegiar:

1. interface moderna, minimalista e responsiva;
2. reprodução local confiável;
3. baixo consumo de recursos e inicialização rápida;
4. integração natural com o sistema operacional;
5. código simples, seguro e sustentável para um projeto pessoal.

Suporte a áudio proveniente de URLs, incluindo YouTube quando técnica e juridicamente viável, é uma evolução futura e não deve complicar prematuramente o núcleo local.

## 2. Fontes de verdade

Antes de alterar o projeto, consulte nesta ordem:

1. `agents.md` — regras de trabalho;
2. `README.md` — visão geral e comandos principais;
3. `docs/product/requirements.md` — requisitos e critérios de aceitação;
4. `docs/architecture/architecture.md` — arquitetura vigente;
5. `docs/architecture/decisions/` — decisões arquiteturais (ADRs);
6. `docs/quality/testing-strategy.md` — estratégia de TDD e qualidade;
7. `docs/development-plan.md` — sequência executável, checks e checkpoint atual;
8. `docs/roadmap.md` — fases e limites de escopo.

Em caso de conflito, a fonte mais específica prevalece, desde que não contradiga este contrato. Requisitos aceitos não devem ser tratados como opcionais. Recomendações e ADRs propostos não devem ser apresentados como decisões finais.

## 3. Regras obrigatórias de trabalho

### 3.1 Preservar o trabalho existente

- Inspecione `git status` antes e depois de trabalhar.
- Considere alterações preexistentes como pertencentes ao usuário.
- Não use `git reset --hard`, `git clean`, descarte de arquivos, rebase destrutivo ou sobrescrita de trabalho sem autorização explícita.
- Não misture mudanças não relacionadas no mesmo commit.

### 3.2 Sempre trabalhar com commits

- Toda alteração concluída deve terminar em um commit coerente e verificável.
- Faça commits pequenos, atômicos e com mensagem no imperativo.
- Use, preferencialmente, Conventional Commits: `docs:`, `test:`, `feat:`, `fix:`, `refactor:`, `build:` ou `chore:`.
- Antes de cada commit, revise o diff e execute as validações proporcionais à mudança.
- Nunca declare uma etapa concluída deixando mudanças próprias sem commit.
- Não faça amend, force-push ou alteração de histórico sem pedido explícito.

### 3.3 Desenvolvimento guiado por testes

Qualquer mudança de comportamento deve seguir o ciclo TDD:

1. **Red:** escrever primeiro um teste pequeno que expresse o comportamento desejado e confirmar que ele falha pelo motivo correto;
2. **Green:** implementar apenas o necessário para fazer o teste passar;
3. **Refactor:** melhorar o desenho mantendo todos os testes verdes.

Correções de bugs devem começar por um teste de regressão. Refatorações não devem alterar comportamento observável e exigem a suíte relevante verde antes e depois. Mudanças exclusivamente documentais ou mecânicas não exigem um teste artificial, mas precisam de validação apropriada.

Não enfraqueça testes para acomodar uma implementação. Testes devem observar contratos públicos sempre que possível, evitando dependência de detalhes internos.

### 3.4 Simplicidade e limites

- Prefira a solução mais simples que preserve os requisitos e as fronteiras arquiteturais.
- Não adicione abstrações, serviços, bancos de dados, telemetria, contas ou sincronização sem necessidade documentada.
- Mantenha o núcleo de domínio independente da UI, do sistema operacional e do mecanismo concreto de áudio.
- Isole integrações de Windows, Linux, codecs, metadados e futuras fontes remotas atrás de portas explícitas.
- Dependências novas precisam de justificativa, licença compatível e manutenção ativa.
- Não introduza coleta de dados. O padrão do projeto é local-first e privado.

## 4. Prioridades de plataforma e UX

- Windows é a plataforma primária e deve receber a validação mais profunda.
- Linux é plataforma suportada; diferenças inevitáveis devem ser documentadas e testadas quando houver ambiente disponível.
- A aplicação deve continuar utilizável sem rede.
- Operações de arquivo e decodificação não podem bloquear a interface.
- Controles essenciais devem funcionar com mouse e teclado e ter estados de foco visíveis.
- Temas claro e escuro devem respeitar contraste e preferências do sistema.
- Bandeja, associação de arquivos, atalhos de mídia e notificações devem usar capacidades nativas sem vazar detalhes de plataforma para o domínio.

## 5. Arquitetura esperada

Use uma arquitetura modular em camadas, sem transformar o projeto pessoal em uma plataforma genérica:

- **domain:** regras e tipos puros de reprodução, fila, posição, preferências e metadados;
- **application:** casos de uso e coordenação;
- **ports:** contratos para áudio, biblioteca de mídia, relógio, preferências e integração do sistema;
- **adapters:** implementações concretas para codecs, arquivos e Windows/Linux;
- **ui:** apresentação e estado visual, sem regras essenciais de negócio.

Dependências devem apontar para dentro: UI e adaptadores dependem dos contratos centrais; o núcleo não depende deles.

## 6. Estratégia de testes

A pirâmide de testes esperada é:

- muitos testes unitários rápidos para domínio e casos de uso;
- testes de contrato para cada porta/adaptador;
- testes de integração focados em arquivos e áudio com fixtures pequenas e licenciadas para o projeto;
- poucos testes de UI/E2E para jornadas críticas;
- smoke tests de empacotamento e integração do sistema por plataforma.

Testes não devem depender de internet, YouTube, dispositivos específicos ou arquivos pessoais. Use relógio, sistema de arquivos e engine de áudio falsos quando isso tornar o teste determinístico.

## 7. Qualidade e validação

O [ADR-0005](docs/architecture/decisions/0005-windows-product-stack.md) aceita C++20/Qt Quick, Qt Multimedia/FFmpeg e TagLib para o produto Windows. A validação atual inclui format/check de C++ e QML, build CMake Release reproduzível, Catch2/CTest e Qt Test aplicável, sempre proporcionais à mudança. A/B e checks do ramo não escolhido deixam de ser pré-requisitos do produto; benchmarks e Linux permanecem adiados, sem aprovação de desempenho ou de release.

No histórico M0, cada candidato executava seus checks nativos e o protocolo comparativo comum; validar somente um ramo não comprova o A/B. Se um experimento Rust/Tauri for retomado explicitamente, sua validação mínima inclui, conforme aplicável:

```powershell
cargo fmt --all -- --check
cargo clippy --workspace --all-targets --all-features -- -D warnings
cargo test --workspace --all-features
```

Os comandos executáveis exatos de cada scaffold, além de UI, empacotamento e smoke tests, serão mantidos no `README.md`. Depois da decisão por ADR, os checks do protótipo descartado não permanecem como dependências do produto. Se uma validação não puder ser executada, registre claramente o que faltou e o risco residual; nunca declare que passou.

## 8. Documentação e decisões

- Atualize documentação no mesmo commit da mudança que altera o contrato.
- Registre decisões significativas em ADRs com estado `Proposto`, `Aceito`, `Substituído` ou `Rejeitado`.
- Inclua contexto, decisão, alternativas, consequências e critérios de revisão.
- Não marque uma escolha como aceita sem decisão explícita do mantenedor ou adoção inequívoca no código.
- Não marque uma etapa do plano como concluída sem commit e evidência dos checks indicados.
- Roadmaps descrevem ordem e limites, não garantias de data.

## 9. Definição de pronto

Uma mudança só está pronta quando:

- o escopo e os critérios de aceitação foram atendidos;
- o ciclo TDD foi seguido quando há mudança de comportamento;
- testes e verificações relevantes passaram;
- documentação e ADRs foram atualizados;
- o diff foi revisado e não contém arquivos acidentais;
- a mudança foi commitada;
- riscos residuais foram informados.

## 10. Restrições atuais

Após a decisão explícita registrada no ADR-0005:

- desenvolver o produto Windows com a stack aceita, promovendo a implementação existente sem duplicar lógica ou romper os contratos do núcleo;
- não apresentar migração, budgets, acessibilidade nativa, Linux ou release como concluídos apenas porque a stack foi aceita;
- limitar navegação/download remoto ao escopo opcional do [ADR-0006](docs/architecture/decisions/0006-browser-editions-and-extensions.md), após as decisões técnicas, revisão de segurança/licenças e testes próprios; não integrar outros serviços remotos;
- não adicionar telemetria, atualização automática ou persistência complexa;
- preservar protocolos, fixtures e evidências históricas M0; checks do experimento não viram dependências permanentes do produto;
- manter requisitos de segurança/licenças/distribuição e o ADR-0003 de fontes remotas nos seus próprios gates; esta decisão não autoriza instalação/registro no host ou retomada de interação nativa interrompida.
