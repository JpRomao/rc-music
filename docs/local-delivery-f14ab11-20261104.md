# Entrega local Windows — f14ab11, 2026-11-04

O instalador **Lite** foi gerado do commit limpo `f14ab11b72b255b02daf897022ec4fb2e9744a58`, reutilizando o cache Release/OFF e as dependências existentes. O pacote conserva navegação, integração externa e downloads remotos OFF. Não houve instalação, execução do player/setup, alteração de Registry/associações ou publicação. O artefato é privado e não possui assinatura Authenticode; instalação, áudio no pacote e gates de distribuição continuam sem validação nesta entrega.

## Artefatos

- Setup: `out/product/rc-music-installer/rc-music-windows-dev-f14ab11-20261104/RCMusic-Setup.exe`, **38.064.606 bytes**.
- SHA-256: `d0151a01b433c7628c0a41dcc2f7225fd1ccf8f67eb06c566ce0f82f722b03d1`.
- Runtime: `out/product/rc-music-local/rc-music-windows-dev-f14ab11-20261104/app`, **554 arquivos / 96.347.255 bytes**.
- SHA-256 do manifesto: `a0b8dce3a680cebf9083fb05d27fec197d0796fbef4f19c2e1febad904462db4`.
- SHA-256 do receipt: `7cd324a8e132c93f22ea021127e3599e513d2df99af48b01aeb3415d2a49c0bb`.

## Build e verificações

No ambiente MSVC 19.44/SDK 10.0.22621.0 já provisionado:

```powershell
cmake --build out/product/m1-desktop-release-off-20260906 --target m0_qt_shell all_qmllint --parallel 3
pwsh -NoProfile -File scripts/Test-ProductionBoundary.ps1 -BuildDirectory out/product/m1-desktop-release-off-20260906
pwsh -NoProfile -File packaging/windows/New-RcMusicLocalRuntime.ps1 -BuildDirectory out/product/m1-desktop-release-off-20260906 -OutputDirectory out/product/rc-music-local/rc-music-windows-dev-f14ab11-20261104
pwsh -NoProfile -File packaging/windows/New-RcMusicInstaller.ps1 -RuntimeDirectory out/product/rc-music-local/rc-music-windows-dev-f14ab11-20261104/app -OutputDirectory out/product/rc-music-installer/rc-music-windows-dev-f14ab11-20261104 -MakeNsis out/m0/toolchains/nsis-3.12/makensis.exe
```

Build/lint e ambas as receitas terminaram com exit 0. O grafo Lite possui **255 comandos e zero testes**, sem fontes opcionais remotas. Parser PE **5/5**, fontes NSIS **5/5** e receita **3/3** passaram. A verificação independente conferiu o inventário físico completo, hashes, cinco fontes da receita do runtime, nove do instalador, compilador NSIS e configuração integral por replano sem escrita. O executável do player é x64 GUI, subsistema 2, sem console adicional; o stub NSIS é x86 GUI. Associações permanecem OFF.

O incremento opcional commitado antes do pacote tem evidência própria em `out/product/test-runs/media-foundation-read-input-20261104/`: **14/14 CTest**, 493 casos Qt e quatro skips históricos de symlink, seguidos de **23/23** na conversão após acrescentar a regressão de entrada ausente, totalizando 494 casos distintos verdes. Builds Release opcionais de rede/worker e persistência passaram, ambos com zero testes registrados. Formato: **385 C++ / 62 QML**. Esses módulos não entram na Lite; não representam workflow remoto habilitado ou sandbox concluída.

## Limpeza posterior

O inventário e o resultado da limpeza ficam em `out/product/installer-delivery-20261104/`, junto aos logs do build e à verificação independente. A seleção preserva o novo runtime/setup, instaladores e recibos históricos, SDK/toolchains/fontes ainda usados, configurações dos caches, evidências, fixtures e dados pessoais. A [entrega anterior](local-delivery-20261104.md) permanece como registro histórico.

O plano revisado tem SHA-256 `037606398735e4e0ee00b97331f39e27e5deed8d971f07b66b5fd4f9da94ae66` e seleciona **3.083 arquivos / 160.985.439 bytes** em nove raízes canônicas:

| Seleção | Arquivos | Bytes |
|---|---:|---:|
| Runtime anterior `rc-music-windows-dev-d51c18c-20261104/app` | 554 | 96.347.255 |
| Cinco payloads antigos parciais, conferidos pelos respectivos manifestos | 2.415 | 11.148.995 |
| Binários regeneráveis de `download-network-release` | 62 | 28.060.647 |
| Binários regeneráveis de `download-checkpoint-release` | 26 | 13.498.567 |
| Binários regeneráveis de `browser-integration-release` | 26 | 11.929.975 |

O plano recusa reparse, arquivos tracked, processos escritores ou divergências de hash/tamanho. Não seleciona configurações, fontes, logs ou evidências dos caches; diretórios só podem ser removidos quando vazios. O inventário de preservação contém **989 arquivos**, incluindo todo o runtime novo, setups/recibos históricos e sentinelas das dependências.

A execução terminou com `removed-and-preservation-verified`: **3.083 arquivos / 160.985.439 bytes** e **234 diretórios vazios** removidos, **989 arquivos preservados** conferidos novamente, sem falhas. O setup manteve o SHA-256 registrado acima. `cleanup-plan.json`, `cleanup-preservation.json` e `cleanup-result.json` guardam a seleção e seu resultado; o aplicador com consultas diretas de filesystem utilizou o mesmo plano revisado. Nenhuma exclusão atingiu `q`, os caches desktop ON/OFF atuais, o diretório anteriormente bloqueado ou dados pessoais.
