# Entrega local Windows — 2026-11-04

O instalador **Lite** foi compilado a partir do commit limpo `d51c18c88a51b0ef74cd671ca782e9c9bd44de94`, sem instalar ou executar o player/setup. Navegação interna, host e downloads remotos permanecem OFF; associações não foram habilitadas. O artefato é privado, sem assinatura Authenticode. Instalação/lifecycle, áudio no novo pacote e gates de distribuição não foram validados nesta entrega.

## Artefatos

- Setup: `out/product/rc-music-installer/rc-music-windows-dev-d51c18c-20261104/RCMusic-Setup.exe`, **38.064.613 bytes**.
- SHA-256 do setup: `f94a25e9df1efd5f2481536283535d4ef6406f4d418bbaacb97226160e984d5b`.
- Runtime: `out/product/rc-music-local/rc-music-windows-dev-d51c18c-20261104/app`, **554 arquivos / 96.347.255 bytes**.
- SHA-256 do manifesto: `fff4cf54e1cee645cf8704cce9098dea6138b4e4b7666856e0941a294ad63a35`.
- SHA-256 do receipt do instalador: `840a658a3fa4f0592cc15bce00d7059ad742518528ae2138bc639178ed9c4dae`.
- SHA-256 da configuração NSIS: `e1127752f107f413ee955d6f8c2b9d2544965f800cedeff770aa1efcd1199deb`.

## Build e verificações

Foi reutilizado o cache Release/OFF atual, sem reconstruir o SDK Qt ou criar outra worktree. Após build e `all_qmllint`, o ambiente MSVC provisionado executou as receitas existentes:

```powershell
pwsh -NoProfile -File packaging/windows/New-RcMusicLocalRuntime.ps1 -BuildDirectory out/product/m1-desktop-release-off-20260906 -OutputDirectory out/product/rc-music-local/rc-music-windows-dev-d51c18c-20261104
pwsh -NoProfile -File packaging/windows/New-RcMusicInstaller.ps1 -RuntimeDirectory out/product/rc-music-local/rc-music-windows-dev-d51c18c-20261104/app -OutputDirectory out/product/rc-music-installer/rc-music-windows-dev-d51c18c-20261104 -MakeNsis out/m0/toolchains/nsis-3.12/makensis.exe
```

Ambas terminaram com exit 0, receitas limpas e sem matriz de formatos antiga. A verificação independente conferiu o inventário físico completo, hashes, cinco fontes da receita do runtime, nove do instalador, compilador NSIS e configuração integral reproduzida por replano OFF sem escrita. Payload ID: `75a7d8b2b371d767a3922f8577c237e3aee94bbc0f7f59258e262a757b091458`. O player é x64 GUI (subsistema 2), sem console; o stub NSIS é x86 GUI.

Checks proporcionais: Release Lite/lint, grafo de produção com **255 comandos e zero testes**, Release opcional com zero testes; parser PE **5/5**, fontes NSIS **5/5** e receita **3/3**. O incremento de código incluído passou previamente **163/163 CTest de core e 4/4 CTest de UI, 407 casos Qt**. Formato: **368 fontes C++ / 62 QML**. Essas provas não equivalem a instalação ou smoke nativo do novo pacote.

## Limpeza posterior

Após validar o setup, um plano com SHA-256 `5ba45cb3ac922a113d999785949b077b96acc4e4996c370c78781ea402a13b85` limitou a exclusão a quatro raízes canônicas, sem reparse, arquivos tracked ou processos escritores. Foram removidos **16.668 arquivos / 338.918.941 bytes** e **3.044 diretórios vazios**:

| Alvo | Arquivos | Bytes removidos |
|---|---:|---:|
| Binários regeneráveis de `out/product/download-network-release` | 72 | 31.039.182 |
| Payload manifestado anterior `out/product/rc-music-local/rc-music-windows-dev-bb826b4-20261103/app` | 554 | 96.338.039 |
| Build Qtbase obsoleto `q/96bvah3m0t0t71m990olv5gpvs/b` | 8.021 | 105.770.122 |
| Build Qtbase obsoleto `q/hla2221pa11rncc9e2fhsta6r4/b` | 8.021 | 105.771.598 |

O payload antigo só foi selecionado quando tamanho/hash correspondiam ao manifesto vinculado ao receipt anterior e havia contraparte no runtime novo. Conteúdo adicional ou alterado seria preservado. Permaneceram os instaladores históricos, manifests/receipts, SDK composto atual, fontes Qt usadas pelo builder TLS, evidências, toolchains, caches ativos de desenvolvimento e dados pessoais. As duas árvores `/b` não eram referenciadas pelos builds atuais; seus diretórios irmãos `/source`, `/evidence` e `/sdk` ficaram fora da seleção.

A verificação posterior confirmou **1.683 arquivos preservados**, estado Git e hash do novo setup inalterados. O recibo terminou com `removed-and-preservation-verified`, sem falhas. Evidências ignoradas em `out/product/installer-refresh-20261104/`: logs, `independent-verification.json`, `cleanup-plan.json`, `cleanup-preservation.json` e `cleanup-result.json`. Não houve instalação, alteração de Registry/associações ou publicação.
