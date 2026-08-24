# Ambiente e baselines experimentais M0

**Estado:** congelado para M0.0

**Identificador:** `M0-E1`

**Data do inventário:** 2026-08-23

**Escopo:** plataformas, host de benchmark, toolchains, distribuição e licenças dos spikes

Este documento torna o experimento reproduzível sem escolher uma stack. Uma atualização de SO, runtime, toolkit ou dependência após o início de uma coleta cria um novo identificador de ambiente; resultados de ambientes diferentes não são combinados.

## 1. Matriz de plataforma

| Ambiente | Papel no M0 | Cobertura obrigatória |
|---|---|---|
| Windows 11 25H2 x64, atualizado | Tier 1 e benchmark principal | todos os checks de shell, áudio, acessibilidade, integração e pacote |
| Windows 11 24H2 x64 | smoke de compatibilidade enquanto suportado pelo fabricante | instalar, iniciar, abrir arquivo, tray, single-instance e desinstalar |
| Windows 11 26H1 x64 | forward smoke opcional | sem alegação até existir ambiente adequado |
| Windows 10 22H2 x64 | legado/best effort | smoke somente quando houver VM; não é Tier 1 nem plataforma suportada de release |
| Ubuntu 24.04 LTS amd64 atualizado, GNOME | Tier 2 de referência | build/pacote, jornada principal, Orca, MPRIS, MIME e lifecycle |
| Outras distribuições, desktops, Windows ARM e Server | fora do M0 | nenhuma alegação |

O Windows 10 Home/Pro encerrou suporte em 14 de outubro de 2025. A disponibilidade do Qt ou do WebView2 nesse sistema não restaura suporte ao próprio SO. O produto passa a declarar Windows 11 x64 como único Tier 1.

No Linux, GNOME/Wayland é a sessão primária e “Ubuntu on Xorg” é a secundária. WSL2/WSLg serve para diagnóstico antecipado de build e janela, mas não comprova pacote, system tray, acessibilidade, MPRIS ou lifecycle de uma sessão Ubuntu real.

Fontes: [Windows 11 release health](https://learn.microsoft.com/en-us/windows/release-health/windows11-release-information), [ciclo do Windows 10](https://learn.microsoft.com/en-us/lifecycle/products/windows-10-home-and-pro), [ciclo do Ubuntu](https://ubuntu.com/about/release-cycle) e [plataformas Qt](https://doc.qt.io/qt-6/supported-platforms.html).

## 2. Host Windows de benchmark

O identificador público do host é `win-benchmark-01`. Evidências não armazenam hostname, username, serial ou paths absolutos.

| Item | Baseline congelado |
|---|---|
| SO | Windows 11 Pro 25H2 x64, build `26200.9168` |
| Locale/fuso | `pt-BR`, America/Sao_Paulo (`UTC-03:00`) |
| Placa-mãe/firmware | ASUS PRIME B450M-GAMING II, BIOS 3211 |
| CPU | AMD Ryzen 7 5800X, 8 cores/16 threads |
| RAM | 31,91 GiB, 4 × 8 GiB DDR4-3600 |
| GPU | NVIDIA GeForce RTX 3070 8 GiB, driver 610.47 |
| Disco do workspace/build | Kingston NV2 2 TB NVMe, NTFS |
| Tela primária | 1920 × 1080, 165 Hz, escala 100% |
| Energia | alimentação AC, plano `Desempenho Máximo`, GUID `1b546f69-ca86-4ca7-8173-3b4079c9805b` |
| WebView2 Evergreen | `151.0.4129.101` no inventário; recapturar em cada batch |
| PowerShell | 7.6.5 para o harness; Windows PowerShell 5.1 apenas para compatibilidade do validador documental |
| Ferramentas de medição | WPR/WPA, xperf, perfmon/typeperf/logman e contadores de processo do Windows |

O registro legado `ProductName=Windows 10 Pro` não é usado: CIM, edição e build identificam o sistema como Windows 11 Pro. O valor `AdapterRAM` do WMI também não é usado porque truncou a VRAM; o inventário da GPU veio do driver NVIDIA.

O endpoint de áudio não participa do ensaio de shell. Antes de qualquer coleta M0.6–M0.8, o apêndice de áudio deve congelar o endpoint renderizador, driver, formato compartilhado, volume do sistema e capacidade de loopback. Ambos os candidatos usam exatamente esse registro.

## 3. Baseline C++/Qt

| Componente | Versão/configuração congelada |
|---|---|
| Qt | 6.11.2, pacote desktop MSVC 2022 x64 |
| Linguagem/ABI | C++20, MSVC `19.43.34808`, runtime dinâmico de Release |
| CMake | 3.31.5 |
| Ninja | 1.12.1 fornecido pelo Visual Studio 2022, caminho explícito no runner |
| Visual Studio | Community 2022 17.13 completo; a instalação incompleta do VS 2026 não entra no baseline |
| UI | Qt Quick/QML e Qt Quick Controls Basic, assets locais |
| Áudio | Qt Multimedia 6.11.2 com backend FFmpeg 7.1.3 correspondente ao Qt |
| Metadados | TagLib 2.3.1 dinâmico, source e hash registrados no manifesto M0.6 |

Qt não está instalado no inventário inicial. M0.2 só começa após provisionar 6.11.2, registrar origem/hash dos arquivos obtidos, comprovar `qmlformat`, `qmllint`, deployment e módulos efetivos. O `ffmpeg.exe` 8.1.1 já presente no PATH é GPL e não pertence ao candidato; ele pode gerar/analisar fixtures, mas não pode vazar para o pacote Qt.

O candidato usa link dinâmico e somente módulos disponíveis sob LGPLv3 compatíveis com o produto; Qt Multimedia oferece LGPLv3/GPLv2 e segue deliberadamente a opção compatível. O pacote experimental não sai da máquina até existir inventário de DLLs/plugins, textos de licença, source correspondente e configuração FFmpeg sem `--enable-gpl`/`--enable-nonfree`. No Linux/X11 o deployment Qt não resolve automaticamente as bibliotecas FFmpeg, portanto o bundle/runtime é verificado manualmente. Referências: [releases Qt](https://doc.qt.io/qt-6/qt-releases.html), [Qt Multimedia](https://doc.qt.io/qt-6/qtmultimedia-index.html), [licenciamento Qt](https://doc.qt.io/qt-6/licensing.html), [obrigações LGPL](https://www.qt.io/development/open-source-lgpl-obligations), [checklist FFmpeg](https://ffmpeg.org/legal.html) e [TagLib 2.3.1](https://github.com/taglib/taglib/releases/tag/v2.3.1).

## 4. Baseline Rust/Tauri

| Componente | Versão/configuração congelada |
|---|---|
| Rust/Cargo | 1.95.0, canal stable, host `x86_64-pc-windows-msvc` |
| Tauri crate | 2.11.5 |
| Tauri CLI | 2.11.4 |
| Tauri JavaScript API | 2.11.1 |
| Node.js | 22.23.2 LTS |
| pnpm | 11.23.0; lockfile obrigatório |
| Svelte | 5.56.10 |
| Vite/plugin Svelte | 8.2.2 / 7.3.0 |
| TypeScript | 6.0.3, modo estrito; [TS 7](https://devblogs.microsoft.com/typescript/announcing-typescript-7-0/) não é usado enquanto a API para Svelte não for estável |
| Vitest/Testing Library Svelte | 4.1.11 / 5.4.2 |
| Áudio/metadados | Rodio 0.22.2 e Lofty 0.25.1 |

CPAL e Symphonia entram somente pelas features deliberadas do Rodio; não recebem dependência direta duplicada. As versões resolvidas e features efetivas ficam congeladas por `Cargo.lock` e auditadas com `cargo tree -d -e features` antes da coleta M0.7.

O baseline normal usa WebView2 Evergreen do sistema. Um pacote com bootstrapper/offline installer é medido separadamente como custo de pré-requisito e nunca somado à RAM. A versão efetiva do runtime é capturada antes e depois de cada batch/soak; mudança encerra a bateria e invalida sua mistura. O patch [Node 22.23.2](https://nodejs.org/en/blog/release/v22.23.2) é exigido antes do install por conter correções de segurança. Referências: [releases Tauri](https://v2.tauri.app/release/), [pré-requisitos](https://v2.tauri.app/start/prerequisites/), [instalador Windows](https://v2.tauri.app/distribute/windows-installer/) e [WebView2 Evergreen](https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/distribution).

## 5. Linux de referência

A imagem de aceitação deve ser Ubuntu 24.04 LTS amd64 com atualizações congeladas no início do batch e os seguintes registros:

- hash/nome da imagem base, kernel e pacotes atualizados;
- GNOME, Wayland, Xorg, Mesa/driver e PipeWire;
- Qt, FFmpeg e plugins realmente carregados no ramo Qt;
- WebKitGTK 4.1, appindicator e dependências realmente carregadas no ramo Tauri;
- Orca e AT-SPI;
- pacote `.deb`, arquivos `.desktop`, MIME e serviço MPRIS.

O WSL2 local executa Ubuntu 24.04.1 LTS e WSLg, mas não é a imagem de aceitação. Uma VM ou host GNOME completo deve ser provisionado antes de M0.4. O pacote WebKitGTK precisa de verificação de changelog/advisories do Ubuntu na data do teste; comparar apenas o número upstream não comprova presença ou ausência de correção.

## 6. Licença e distribuição experimental

O código próprio permanece privado e sem licença de redistribuição durante M0. Isso significa:

- builds são somente para avaliação local do mantenedor;
- nenhum protótipo, instalador ou source bundle é publicado;
- assets próprios do harness recebem declaração explícita de autoria/licença em M0.1;
- dependências mantêm suas próprias licenças e notices;
- qualquer distribuição pública exige decisão de licença do projeto, SBOM/notices e revisão de Qt/FFmpeg/codecs ou Rust/JavaScript antes do primeiro upload.

Ausência de publicação não autoriza usar fixture comercial ou arquivo pessoal. O corpus M0.5 será sintético ou terá licença expressa.

## 7. Política de congelamento e prontidão

Antes de cada batch, o preflight grava versão/commit, hashes de protocolo, fixture, ambiente, build e lockfiles. Durante um batch:

- não atualizar SO, driver, firmware, WebView2, Qt, Rust, Node ou dependências;
- não misturar resultados anteriores e posteriores a uma atualização automática;
- não executar build, indexação, update, sync ou carga concorrente;
- manter o workspace Git limpo e o build Release identificável;
- recapturar energia, escala, tela, espaço livre, runtime e árvore inicial de processos.

Mudança inevitável encerra o batch, gera novo identificador (`M0-E2`, `M0-E3`...) e exige novo resumo comparável dos dois candidatos.

### Pendências de provisionamento, não de decisão

| Item ausente no inventário | Bloqueia |
|---|---|
| Qt 6.11.2 e ferramentas QML | M0.2 e M0.6 |
| `clang-format` compatível | check de formato C++ de M0.2 |
| Node 22.23.2 LTS e pnpm 11.23.0 (host ainda possui 22.14.0/11.15.0) | M0.3; não executar install com runtime desatualizado |
| NVDA | checklist manual Windows de M0.2–M0.4 |
| VM Windows 11 24H2 | smoke obrigatório e `CHK-WIN` de M0.4 |
| VM/host Ubuntu 24.04 GNOME Wayland/Xorg e Orca | `CHK-LINUX` de M0.4 |
| NSIS e VM Windows descartável | aprofundamento M0.9 |
| endpoint/loopback de áudio congelado | coleta M0.6–M0.8 |

Uma pendência só pode ser marcada como resolvida por evidência do ambiente. Ela nunca é convertida em check aprovado por documentação.
