# Entrega local Windows — 2144cfd, 2026-11-04

As edições **Lite** e **Completa experimental** foram compiladas do commit concluído `2144cfd1db81ce49934f73bf47af1cf7eebc5740`, em worktree limpa e isolada. As 21 alterações pendentes do checkout principal foram preservadas e não integram os pacotes. Foram reutilizados MSVC 19.44, Windows SDK 10.0.22621.0, Qt/Multimedia, TagLib, NSIS e o SDK WebView2 já disponíveis, sem instalação ou reconstrução de dependências.

## Artefatos

Ambos os setups ficam em `out/product/rc-music-installer/`, em diretórios separados:

| Edição | Diretório | Setup, bytes | SHA-256 de `RCMusic-Setup.exe` |
|---|---|---:|---|
| Lite | `rc-music-windows-lite-dev-2144cfd-20261104` | 38.064.987 | `08a2b4b340177d027f70042930bd3cec59897f418cb87d7ef8fe23a41c497345` |
| Completa experimental | `rc-music-windows-complete-dev-2144cfd-20261104` | 38.139.648 | `541e25501efed50e6919d8e27cd9f49ef0a451de468a1c4e20f343b3b1331031` |

Os runtimes correspondentes ficam em `out/product/rc-music-local/`, com os mesmos nomes de diretório:

| Edição | Arquivos | Bytes | SHA-256 do manifesto |
|---|---:|---:|---|
| Lite | 554 | 96.346.743 | `7002e9f36fac2e69a8d4a61562c7f591f8f3d32071fa7e1a2cae06e5c71f81ab` |
| Completa experimental | 557 | 96.573.070 | `57296eec8d6a5aae654a693c1dc6acaf7e7ca14c1fef55f081b6e3ff9930fa4d` |

São pacotes privados, sem assinatura Authenticode. A Lite não inclui engine de navegação. A Completa experimental usa o loader estático e requer WebView2 Evergreen x64 já instalado, conforme o [ADR-0012 Proposto](architecture/decisions/0012-experimental-edition-packaging.md); o setup não instala esse runtime. As edições compartilham a identidade de instalação e substituem a versão ativa. Downloads, Native Messaging e associações permanecem OFF.

## Construção e checks

`out/product/setup-delivery-2144cfd-20261104/Build-Delivery.ps1` registra os argumentos exatos. Cada edição usou cache novo, CMake Release/x64, `BUILD_TESTING=OFF`, build de `m0_qt_shell` e `all_qmllint`, seguido das receitas de [runtime](windows-runtime.md) e [instalador](windows-installer.md). Todas essas operações terminaram com exit 0.

- Grafo de produção: **255 comandos Lite**, **289 Completa**, com **zero testes registrados** em cada cache de produção. Ausência de módulos de downloads/host/testes conferida; presença/ausência do loader WebView2 conferida também nos inputs do linker.
- Checks puros: parser PE **5/5**, fontes NSIS **5/5**, receita do instalador **4/4**.
- Verificador independente: inventário físico completo, tamanho/hash de cada arquivo, PEs x64, player x64 GUI sem console adicional, stub NSIS x86 GUI, cinco/seis fontes da receita de runtime, nove fontes do instalador e pin do compilador.
- Replanejamento sem escrita reproduziu identidade e configuração NSIS completas, normalizando somente o `OutFile` para o destino original de compilação. `build-result.json` vincula os três arquivos de cada instalador às saídas finais com hashes idênticos; os recibos originais não foram reescritos.

Evidências em `out/product/setup-delivery-2144cfd-20261104/`: logs, `installer-signatures.json`, `independent-verification.json`, verificador e resultados da limpeza.

## Limpeza posterior

Após a conferência dos novos pacotes, a seleção por caminhos literais e SHA-256 removeu **2.740 arquivos / 512.054.569 bytes** e **557 diretórios vazios**, incluindo a varredura final dos dois caches temporários:

| Seleção | Arquivos | Bytes |
|---|---:|---:|
| Binários regeneráveis de `browser-integration-tests` | 169 | 64.019.644 |
| Binários regeneráveis de `embedded-browser-tests-20261103` | 232 | 105.917.923 |
| Payloads anteriores Lite `4125f46` e Completa `51cd067` | 1.111 | 192.920.816 |
| Dois caches temporários desta compilação | 1.228 | 149.196.186 |

A worktree isolada e limpa foi removida pelo Git: mais **921 arquivos / 15.944.058 bytes**. Total removido: **3.661 arquivos / 527.998.627 bytes**, aproximadamente **503,54 MiB**, sem novo backup. Os manifests e instaladores anteriores permanecem como histórico; seus payloads removidos deixam de ser runtimes executáveis completos. Os binários de testes podem ser reconstruídos quando necessários.

A seleção recusou arquivos versionados, paths fora das seis raízes revisadas, reparse points, writers ativos e divergências de tamanho/hash. Cada arquivo foi novamente conferido imediatamente antes da exclusão; diretórios foram removidos somente vazios. A pasta `installer-build-531c5cc-20261103`, anteriormente bloqueada, ficou fora da operação.

Foram preservados SDKs/fontes de `q` e dependências de `out/m0`, caches desktop ON/OFF e da Completa usados no desenvolvimento e todos os instaladores. Nos caches históricos selecionados, `_deps`, `Testing`, CompilerId, configurações, fixtures e logs/evidências ficaram fora da remoção. Metadados dos caches temporários foram copiados para a evidência antes de removê-los integralmente. Dados pessoais, sessões e filas não entraram na seleção.

O resultado `absence-and-preservation-verified` confirma ausência dos 2.740 candidatos, hashes de **1.153 artefatos preservados** e **933 arquivos da árvore inicial** sem diferenças. Isso inclui as 21 alterações de desenvolvimento ainda incompletas; permaneceram fora do commit documental desta entrega. O grafo e o replano foram conferidos antes de remover os caches; a verificação posterior comprova ausência e preservação dos bytes, sem alegar nova compilação. Os recibos `cleanup-plan.json`, `cleanup-preservation.json`, `cleanup-result.json`, `isolated-source-removal-result.json`, `empty-temporary-trees.json` e `post-cleanup-verification.json` registram a operação.

O fechamento documental passou em 84 Markdown, 83 requisitos, 17 checks e 80 etapas; a rastreabilidade foi validada sem promover requisitos amplos a comprovados. O diff documental foi revisado separadamente do desenvolvimento pendente.

## Limites

Nenhum player, setup, WebView2, site ou perfil AppContainer foi executado nesta entrega. Não houve instalação, Registro/associações do host, benchmark, teste de áudio novo ou publicação. Builds e inspeção de pacote não comprovam funcionamento instalado, navegação física, lifecycle, áudio audível ou aprovação de release. Os gates restantes continuam no [plano](development-plan.md).
