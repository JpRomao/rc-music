# Estado do projeto — RC Music

**Revisado em:** 2026-11-12.

Este arquivo é um índice do estado vigente, não um diário de desenvolvimento. A sequência de trabalho e as evidências de cada incremento ficam na [seção 17 do plano](development-plan.md#17-próxima-sequência-segura). Relatos históricos não substituem esse checkpoint. Requisitos e decisões continuam nas suas fontes de verdade, conforme [agents.md](../agents.md).

## 1. Estado vigente

- **Stack aceita:** C++20, Qt Quick/QML, Qt Multimedia/FFmpeg e TagLib, conforme [ADR-0005](architecture/decisions/0005-windows-product-stack.md). Rust/Tauri e A/B não são pré-requisitos do produto Windows.
- **Unidade de produto existente:** núcleo, aplicação gráfica, testes e receitas Windows estão na raiz. Não é necessário promover novamente o experimento ou reimplementar o player. Consulte o [build desktop](desktop-build.md), o [núcleo independente](core-build.md) e os [comandos locais](development-commands.md).
- **Identidade preservada:** RC Music é o nome aceito; identificadores e dados locais `MusicPlayer` permanecem conforme [ADR-0004](architecture/decisions/0004-rc-music-identity.md).
- **Prontidão:** implementação, teste automatizado, validação nativa, instalação e aprovação de distribuição são estados distintos. M1 e a release 0.1 continuam abertos pelos seus gates residuais; isso não torna ausentes as funcionalidades já implementadas.

| Área | Implementação existente | Aceitação ainda separada |
| --- | --- | --- |
| Player local | Reprodução, fila, seek, metadados/capas, temas, preferências, sessão pausada, bandeja e integrações Windows. [Manual](user-guide.md) | Jornadas nativas/empacotadas, acessibilidade final e cenários físicos aplicáveis. |
| Organização e letras | Playlists persistentes, LRC local, consulta explícita ao LRCLIB e versões de letras. [Playlists](playlists.md) · [Versões](lyrics-variants.md) | Aceitação nativa e cobertura de provedores; Letras.mus.br automático não está qualificado. |
| Lidarr | Cliente, confirmação de pedidos e consulta de álbuns/faixas cadastrados. [Integração](lidarr-integration.md) | Aceitação autenticada; pedido aceito não significa mídia baixada ou importada no RC Music. |
| Extensões Chromium | Clientes MV3 principal e YouTube, identidades/canais separados, seleção explícita e reenvio consentido após incerteza. [Principal](browser-extension.md) · [YouTube](youtube-audio-and-lyrics.md) | Instalação e jornada real no Brave; detecção não comprova aquisição. |
| Ponte e aquisição | Hosts Native Messaging, IPC, admissão durável, workers, conversão, publicação, recuperação e importação sem autoplay existem em composição opcional. [Bootstrap](player-downloads-bootstrap.md) · [Jobs](download-jobs.md) | Ativação instalada coerente, qualificação de isolamento e aquisição pública. Fixtures não comprovam esses gates. |
| Edições e pacotes | Lite sem engine interna, Completa com navegação WebView2 experimental e setups privados de ambas já gerados. [ADR-0006](architecture/decisions/0006-browser-editions-and-extensions.md) · [Instalador](windows-installer.md) | Navegação real, instalação/upgrade/uninstall, assinatura e distribuição. O guia do instalador identifica os candidatos atuais e seus checks. |

## 2. Prioridade atual: navegador → aplicativo

O objetivo do mantenedor é navegar no Brave/Chromium, selecionar a mídia da página pela extensão e acompanhar sua aquisição no RC Music, com entrega final em MP3. Quando a fonte for MP4, extrair e converter seu áudio; não salvar o vídeo como resultado alternativo. O exemplo indicado é a [página de Bleach no Letras](https://www.letras.mus.br/bleach/1222774/). A aceitação deve exercitar a jornada completa, não apenas o popup simulado ou o recebimento de um pedido.

O candidato de desenvolvimento da extensão YouTube acrescentou descoberta no Letras. O primeiro teste nos dois sites isolou uma recusa de foco; com a versão 0.1.2 o mantenedor confirmou vídeo e campos. O pedido chegou ao app, mas a aquisição falhou com a mensagem ampla de conteúdo privado/protegido, sem MP3 publicado. Uma tentativa diagnóstica posterior observou HTTP 403; a causa da recusa continua desconhecida e o status não prova DRM. O [teste temporário de User-Agent](superpowers/plans/2026-11-12-youtube-user-agent-probe.md) tem implementação default-off, candidato autenticado e revisão independente, mas sua tentativa real ainda não ocorreu: o encerramento automático do player não foi confirmado e a confirmação manual expirou. O original segue em execução, sem alteração dos aliases, da fila ou da posição. Estado e checks estão nos [guias da extensão](browser-extension.md#vídeo-youtube-declarado-no-letras) e do [transporte](download-network.md#hint-user-agent-temporário-e-default-mínimo). Descoberta e admissão comprovadas não encerram o aceite de conversão/importação.

A continuidade deve reaproveitar o que existe e distinguir:

1. **Descoberta:** identificar a fonte real e reconferir a seleção. A principal encontra mídia direta e frames acessíveis; frames de outra origem, blob/MSE e fontes protegidas não estão automaticamente cobertos. Não presumir a tecnologia do player de um site sem observá-la.
2. **Integração instalada:** conferir identidade da extensão, host registrado, canal compilado e ativação do componente no app. Compilar os opcionais não basta. O [perfil empacotado](packaged-download-profile.md) documenta o marcador e a configuração confiável.
3. **Formato de saída:** preservar o contrato existente de publicação/importação em MP3, inclusive extraindo áudio de MP4. Validar os bytes e a conversão real, sem apenas trocar a extensão do arquivo e sem ampliar o escopo para saída de vídeo.
4. **Conclusão observável:** progresso, cancelamento, erro acionável e arquivo final validado, sem sobrescrever arquivos pessoais, repetir pedidos automaticamente ou trocar a faixa em reprodução.

A separação principal/YouTube do ADR-0006 continua vigente. Um embed YouTube não vira mídia direta da principal. DRM, conteúdo restrito, cookies do navegador e bloqueios de acesso não devem ser contornados para produzir uma aparente aprovação.

## 3. Gates ainda abertos

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
