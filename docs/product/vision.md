# Visão do produto

**Estado:** baseline inicial  
**Última revisão:** 2026-08-23  
**Plataforma primária:** Windows 10/11 x64  
**Plataforma secundária:** Linux desktop x64

## Resumo

O projeto é um player de música desktop pessoal, local-first, rápido para abrir e simples de manter. Sua proposta não é substituir uma biblioteca musical ou serviço de streaming completo: é oferecer a melhor experiência possível para abrir um arquivo, ouvir, navegar no áudio e manter uma pequena fila, sem contas, anúncios ou coleta de dados.

O Windows é a referência de qualidade e integração. O Linux deve ser suportado por uma arquitetura e um toolkit realmente multiplataforma, com diferenças de ambiente documentadas em vez de escondidas.

## Problema

Players generalistas costumam acumular biblioteca, serviços online, publicidade e configurações que prejudicam a jornada básica de abrir e ouvir um arquivo. Players mínimos, por outro lado, frequentemente sacrificam interface, acessibilidade, metadados e integração com o sistema.

Este projeto busca o ponto de equilíbrio:

- leve como uma ferramenta local;
- cuidadoso como um produto de uso diário;
- pequeno o bastante para continuar agradável como projeto pessoal;
- estruturado o bastante para evoluir sem reescrita total.

## Visão

> Abrir qualquer áudio comum e estar ouvindo em poucos instantes, com controles claros, interface serena e comportamento previsível no sistema operacional.

## Público e cenários centrais

O usuário principal é o próprio mantenedor e pessoas com uma necessidade parecida:

- ouvir arquivos avulsos recebidos ou baixados;
- revisar gravações, aulas, podcasts e músicas usando saltos curtos;
- inspecionar rapidamente tags e propriedades técnicas;
- manter a reprodução ativa com a janela escondida na bandeja;
- usar o mesmo aplicativo principal no Windows e, quando necessário, no Linux.

Não haverá personas artificiais ou funcionalidades orientadas a crescimento. Decisões devem partir dos cenários reais e de testes de usabilidade.

## Princípios de produto

1. **Abrir e ouvir primeiro.** A jornada principal deve exigir uma única ação clara.
2. **Interface sem surpresas.** Fechar, pausar, buscar, restaurar e sair devem ter resultados previsíveis.
3. **Local e privado por padrão.** Nenhuma conta, telemetria ou rede é necessária para arquivos locais.
4. **Desempenho perceptível.** Inicialização, troca de faixa e controles devem responder imediatamente.
5. **Detalhe sob demanda.** Informações técnicas existem, mas não competem com a faixa e seus controles.
6. **Acessibilidade é comportamento básico.** Teclado, foco, contraste, escala e leitor de tela fazem parte do MVP.
7. **Windows excelente, Linux honesto.** A plataforma primária recebe integração profunda; limitações do desktop Linux são detectadas e explicadas.
8. **Evolução por evidência.** Formatos, toolkit, consumo e integrações são validados por spikes e medições.

## Escopo do primeiro produto utilizável

O MVP entrega:

- reprodução de arquivos de áudio locais comuns;
- abrir por seletor, arrastar e soltar, linha de comando e associação do sistema;
- fila pequena e explícita;
- play/pause, anterior/próximo, volume/mudo, seek e saltos configuráveis;
- metadados editoriais e técnicos;
- temas do sistema, claro e escuro;
- preferências e recuperação segura de sessão;
- bandeja, instância única, teclas de mídia e associação de arquivos;
- instalador por usuário no Windows e pacote Linux documentado.

## Evoluções previstas

- playlists M3U8 e biblioteca opcional por pasta;
- shuffle, repeat, mini-player e seleção de saída;
- personalização limitada de tokens de tema;
- fonte remota por URL e, se viável, resolução de áudio do YouTube.

Esses itens não podem tornar o player local dependente de rede ou de ferramentas externas.

## Fora de escopo

- conta, login, perfil social ou sincronização em nuvem;
- loja, catálogo ou assinatura de streaming;
- download ou gerenciamento de direitos digitais;
- edição de tags no MVP;
- equalizador, efeitos, visualizador e DSP avançado no MVP;
- aplicativo móvel ou web;
- plugins de terceiros antes de existir um caso real;
- banco de dados antes de existir biblioteca indexada;
- telemetria ou publicidade.

## Plataformas e níveis de suporte

| Nível | Ambiente | Compromisso |
|---|---|---|
| Tier 1 | Windows 11 x64 | Desenvolvimento, testes funcionais, acessibilidade, instalador e performance antes de cada release |
| Tier 1 | Windows 10 22H2 x64 | Compatibilidade e smoke test enquanto a versão for tecnicamente suportável |
| Tier 2 | Linux x64 em distribuição de referência | Build, reprodução, atalhos, integração XDG e smoke test; limitações do shell/desktop são documentadas |
| Não planejado no MVP | macOS, ARM, mobile e web | A arquitetura não deve bloquear deliberadamente, mas não existe compromisso de entrega |

A distribuição Linux de referência será escolhida no spike de empacotamento. Wayland e X11 devem entrar na matriz, sem prometer bandeja quando o ambiente não oferece o protocolo necessário.

## Indicadores de sucesso

- a jornada abrir → tocar é descoberta sem instrução;
- controles principais respondem dentro dos orçamentos de performance;
- a reprodução local permanece estável em sessão longa;
- o usuário consegue operar o fluxo principal apenas por teclado;
- crashes ou encerramentos não perdem a fila e nunca retomam som inesperadamente;
- o conjunto de formatos declarado corresponde ao corpus testado;
- o projeto continua executável e compreensível por uma única pessoa.

## Hipóteses e decisões em aberto

- C++20 + Qt 6 Quick/QML é a hipótese principal de desempenho para o M0, especialmente para cold start e memória no Windows; Rust + Tauri 2 + Svelte é o desafiante A/B direto.
- A ordem dos candidatos não aceita linguagem ou toolkit antecipadamente. O mesmo recorte de UX deve provar desempenho, acessibilidade, integração e sustentabilidade antes da decisão por ADR, nos termos de NFR-STACK-001–006.
- Shell/UI e backend de áudio serão avaliados separadamente. O corpus determinará qual engine atende formatos, seek, estabilidade e consumo sem atribuir ao toolkit o custo do decoder ou da saída.
- Rust + Slint permanece uma alternativa possível caso a evidência justifique outro experimento, sem ampliar prematuramente o M0.
- YouTube depende de viabilidade técnica, termos do serviço, licenças e estratégia de distribuição; não é compromisso do MVP.
- Nome, identidade visual, licença do código e canal de distribuição ainda serão definidos pelo mantenedor.
