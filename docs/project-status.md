# Estado do projeto — RC Music

**Revisado em:** 2026-11-15.

Este arquivo é um índice do estado vigente, não um diário de desenvolvimento. A sequência de trabalho e as evidências de cada incremento ficam na [seção 17 do plano](development-plan.md#17-próxima-sequência-segura). Relatos históricos não substituem esse checkpoint. Requisitos e decisões continuam nas suas fontes de verdade, conforme [agents.md](../agents.md).

## 1. Estado vigente

- **Stack aceita:** C++20, Qt Quick/QML, Qt Multimedia/FFmpeg e TagLib, conforme [ADR-0005](architecture/decisions/0005-windows-product-stack.md). Rust/Tauri e A/B não são pré-requisitos do produto Windows.
- **Unidade de produto existente:** núcleo, aplicação gráfica, testes e receitas Windows estão na raiz. Não é necessário promover novamente o experimento ou reimplementar o player. Consulte o [build desktop](desktop-build.md), o [núcleo independente](core-build.md) e os [comandos locais](development-commands.md).
- **Identidade preservada:** RC Music é o nome aceito; identificadores e dados locais `MusicPlayer` permanecem conforme [ADR-0004](architecture/decisions/0004-rc-music-identity.md).
- **Prontidão:** implementação, teste automatizado, validação nativa, instalação e aprovação de distribuição são estados distintos. M1 e a release 0.1 continuam abertos pelos seus gates residuais; isso não torna ausentes as funcionalidades já implementadas.
- **Decisão vigente:** [ADR-0018](architecture/decisions/0018-aurora-and-lidarr-removal.md): retirada integral do Lidarr e migração incremental para Aurora. Após a fundação, foram implementadas páginas integradas de playlists/atividade/preferências, cabeçalho, destaque horizontal, prateleira da fila real e player persistente compacto; [resultado, evidências e limites](delivery/aurora-native-pages-20261115.md). O redesenho inteiro e o aceite humano Qt continuam abertos. **Sem novo instalador até concluir integralmente o redesenho.**

| Área | Implementação existente | Aceitação ainda separada |
| --- | --- | --- |
| Player local | Reprodução, fila, seek, metadados/capas, temas, preferências, sessão pausada, bandeja e integrações Windows. [Manual](user-guide.md) | Jornadas nativas/empacotadas, acessibilidade final e cenários físicos aplicáveis. |
| Organização e letras | Playlists persistentes, LRC local, consulta explícita ao LRCLIB e versões de letras. [Playlists](playlists.md) · [Versões](lyrics-variants.md) | Aceitação nativa e cobertura de provedores; Letras.mus.br automático não está qualificado. |
| Lidarr | Retirado do escopo do aplicativo. FR-055 permanece **fora de escopo**, em R1.11; [histórico preservado](lidarr-integration.md). | Não há aceitação autenticada ou download real a continuar. Reintrodução exige nova decisão; servidores e dados pessoais ficam intactos. |
| Extensões Chromium | Clientes MV3 principal e YouTube, identidades/canais separados, seleção explícita e reenvio consentido após incerteza. [Principal](browser-extension.md) · [YouTube](youtube-audio-and-lyrics.md) | Instalação e jornada real no Brave; detecção não comprova aquisição. |
| Ponte e aquisição | Hosts Native Messaging, IPC, admissão durável, workers, conversão, publicação, recuperação e importação sem autoplay existem em composição opcional. [Bootstrap](player-downloads-bootstrap.md) · [Jobs](download-jobs.md) | Ativação instalada coerente, qualificação de isolamento e aquisição pública. Fixtures não comprovam esses gates. |
| Edições e pacotes | Lite sem engine interna, Completa com navegação WebView2 experimental e setups privados de ambas já gerados. [ADR-0006](architecture/decisions/0006-browser-editions-and-extensions.md) · [Instalador](windows-installer.md) | Navegação real, instalação/upgrade/uninstall, assinatura e distribuição. O guia do instalador identifica os candidatos atuais e seus checks. |

## 2. Prioridade atual: páginas e composição musical Aurora

As páginas nativas foram consolidadas em `26e2f0e`, a composição editorial em `86e37c6` e os consumidores/validador em `ab002ae`. A revisão independente retornou com correções; os três Important de seleção tardia/foco/virtualização foram corrigidos em `6dba923`, com Red→Green e nova integração agrupada. Dois Minor de apresentação/seleção continuam explícitos. Builds, testes, revisão e seis capturas Qt atuais estão no [relatório desta frente](delivery/aurora-native-pages-20261115.md). Não é conclusão das onze fases nem aceite visual humano.

A fatia 1 da paridade visual adicionou ao contrato os tokens Seleção, Hover e Divisória, a escala tipográfica e os breakpoints nomeados, e corrigiu o item selecionado da lateral, que no tema claro tinha a mesma cor do fundo. Detalhes no [mapeamento nativo da spec](superpowers/specs/2026-11-15-aurora-shell-design.md#tokens-da-referência-aceita). Decisões de 2026-11-16 no [ADR-0019](architecture/decisions/0019-internal-search-library-and-extension-retirement.md): busca online pelo próprio yt-dlp, biblioteca permanente aprovada (abre em Lista) e retirada da extensão quando o app cobrir buscar, baixar e importar. A fatia 2 trocou os glifos Unicode e o emoji da lateral, do botão de tema e do player por ícones de linha tingidos pelo tema e acrescentou a marca de três barras ([detalhes na spec](superpowers/specs/2026-11-15-aurora-shell-design.md#tokens-da-referência-aceita)). A fatia 3 deu ao player o transporte discreto do protótipo, com Letras, Fila e volume com porcentagem, e limitou o rodapé a 120 px. A fatia 4 deu às páginas o cabeçalho editorial e o breadcrumb, tirou o cartão e renomeou Downloads e Preferências para Atividade e Configurações. A fatia 5 transformou a fila e o leitor de letras em gavetas laterais sobre a tela inicial esmaecida. A fatia 6 deu a Playlists a lista de escolha discreta com cabeçalho de detalhe e a Configurações o índice de seções com títulos. Próximas: atividade (fatia 7) e o interior da gaveta da fila.

A fundação `d4137db` e seu [instalador privado histórico](windows-installer.md#setup-aurora-de-d4137db) permanecem registrados, sem representar a fonte nova. A condição posterior do mantenedor bloqueia instaladores intermediários. Reprodução, dados, Qt Network, letras, downloads opcionais, playlists e stores foram preservados no recorte exercitado. Biblioteca permanente/ingresso, pesquisa/provider, paridade visual restante, instalação e gates de produção conservam seus próprios aceites.

### Continuidade preservada: navegador → aplicativo

O objetivo do mantenedor é navegar no Brave/Chromium, selecionar a mídia da página pela extensão e acompanhar sua aquisição no RC Music, com entrega final em MP3. Quando a fonte for MP4, extrair e converter seu áudio; não salvar o vídeo como resultado alternativo. O exemplo indicado é a [página de Bleach no Letras](https://www.letras.mus.br/bleach/1222774/). A aceitação deve exercitar a jornada completa, não apenas o popup simulado ou o recebimento de um pedido.

A extensão YouTube 0.1.2 detectou Asterisk na página Letras e entregou o pedido ao RC Music. O primeiro teste com User-Agent ainda retornou HTTP 403. Em 2026-11-13, o candidato isolado com yt-dlp 2026.10.26 recebeu o áudio, publicou um MP3 decodificável e persistiu uma importação na fila de teste. A instalação padrão e a jornada instalada da extensão principal com MP4 continuam com validação própria. [Resultado e evidência](youtube-audio-and-lyrics.md#atualização-do-yt-dlp-20261026)

A continuidade deve reaproveitar o que existe e distinguir:

1. **Descoberta:** identificar a fonte real e reconferir a seleção. A principal encontra mídia direta e frames acessíveis; frames de outra origem, blob/MSE e fontes protegidas não estão automaticamente cobertos. Não presumir a tecnologia do player de um site sem observá-la.
2. **Integração instalada:** conferir identidade da extensão, host registrado, canal compilado e ativação do componente no app. Compilar os opcionais não basta. O [perfil empacotado](packaged-download-profile.md) documenta o marcador e a configuração confiável.
3. **Formato de saída:** preservar o contrato existente de publicação/importação em MP3, inclusive extraindo áudio de MP4. Validar os bytes e a conversão real, sem apenas trocar a extensão do arquivo e sem ampliar o escopo para saída de vídeo.
4. **Conclusão observável:** progresso, cancelamento, erro acionável e arquivo final validado, sem sobrescrever arquivos pessoais, repetir pedidos automaticamente ou trocar a faixa em reprodução.

A separação principal/YouTube do ADR-0006 continua vigente. Um embed YouTube não vira mídia direta da principal. DRM, conteúdo restrito, cookies do navegador e bloqueios de acesso não devem ser contornados para produzir uma aparente aprovação.

## 3. Gates ainda abertos

- Conclusão integral do redesenho e aceite humano das páginas/jornadas restantes; só depois gerar outro instalador. Fila temporária não substitui biblioteca permanente nem dados reais de pesquisa.
- Qualificação física do isolamento e perfis de execução; um header, marcador de instalação ou ensaio offline não aprova produção remota.
- Brave/Native Messaging, WebView2, Explorer, Aplicativos Padrão e instalação/upgrade/uninstall reais, com escopo e autorização próprios.
- Corpus hostil, segurança/dependências, licenças/fontes, cobertura transitiva do SBOM, CRT e máquina limpa. [Gates](quality/quality-gates.md) · [Release](delivery/release-process.md)
- Evidência residual de cobertura e CI Windows, sem presumir provedor/runner ou apresentar testes focados como regressão global.
- Acessibilidade nativa/NVDA, DPI, alto contraste e cenários físicos ainda não aceitos. [Estratégia de testes](quality/testing-strategy.md)
- Linux e benchmarks continuam adiados, sem resultado presumido e sem reabrir a stack aceita.

Autorizações anteriores para gerar artefatos ou executar probes específicos não são autorização permanente para instalar, alterar Registro, elevar processos ou retomar interação nativa interrompida.

## 4. Evidência e manutenção deste índice

O [plano executável](development-plan.md), a [rastreabilidade](quality/traceability.md), o [runtime](windows-runtime.md) e os guias de cada componente conservam resultados e lacunas. Git preserva o histórico retirado deste resumo. Não reatribuir checks de um build a outro nem manter aqui uma segunda lista concorrente de próximas tarefas.

Ao atualizar este índice, mantenha contagens e recibos nos guias responsáveis. Separe checks executados agora, evidências anteriores, falhas conhecidas e validações pendentes.
