# Entrega local Completa experimental — 51cd067, 2026-11-04

O pacote privado **CompleteExperimental** foi gerado do commit limpo `51cd067743962abf58bf7d7599b12550fa760052`, com navegação interna e os cinco módulos de integração/downloads remotos OFF. O [ADR-0012 Proposto](architecture/decisions/0012-experimental-edition-packaging.md) registra identidade, troca de edição e pré-requisitos. Esse artefato não representa a edição final ou aprovação de distribuição.

## Artefatos conferidos

- Setup: `out/product/rc-music-installer/rc-music-windows-complete-dev-51cd067-20261104/RCMusic-Setup.exe`, **38.139.221 bytes**, Authenticode `NotSigned`.
- SHA-256: `9a59e347ae8564669780e3a03be1d289019d3f3d370f1d19fbaa8e22910657e9`.
- Runtime: `out/product/rc-music-local/rc-music-windows-complete-dev-51cd067-20261104/app`, **557 arquivos / 96.573.561 bytes**.
- `RCMusic.exe`: **2.333.184 bytes**, x64 GUI/subsistema 2, SHA-256 `3ccedce4bde7e9b81ce2825691e9b95f0e4f91272ed130f56682f71d1fee3b9f`.
- Manifesto: SHA-256 `699158022998331106c204c18b956da98ed6949560ac8d6862df0f82094638c3`.
- Receipt do instalador: SHA-256 `6171a4e098fffe1cd3aeac7cf311083651e510738d519a5aa0d3c16a1448db4d`.
- Payload ID: `d6234abcd5b31eec3f0a9f55c80785b0254bdf1b6b2f099bde820c059ed8b9d3`.

O par de metadados é `edition=CompleteExperimental` e `browser_runtime_requirement=existing-webview2-evergreen-x64`. A navegação requer o WebView2 Evergreen x64 já instalado; não foi copiado runtime do host, incluído bootstrapper ou instalado componente. O pacote contém o aviso próprio e cópias íntegras de `LICENSE.txt`/`NOTICE.txt` do SDK local 1.0.3179.45. O loader é estático, com SHA-256 `298c5d825a63c5e696b5269c99236472c2559d1b368f39546718d344b06030a4`.

## Construção e evidência

Com MSVC 19.44/SDK Windows 10.0.22621.0 e dependências já provisionadas:

```powershell
cmake --build out/product/complete-experimental-release-20261104 --target m0_qt_shell all_qmllint --parallel 3
pwsh -NoProfile -File scripts/Test-ProductionBoundary.ps1 -Edition CompleteExperimental -BuildDirectory out/product/complete-experimental-release-20261104
pwsh -NoProfile -File packaging/windows/New-RcMusicLocalRuntime.ps1 -Edition CompleteExperimental -BuildDirectory out/product/complete-experimental-release-20261104 -OutputDirectory out/product/rc-music-local/rc-music-windows-complete-dev-51cd067-20261104
pwsh -NoProfile -File packaging/windows/New-RcMusicInstaller.ps1 -Edition CompleteExperimental -RuntimeDirectory out/product/rc-music-local/rc-music-windows-complete-dev-51cd067-20261104/app -OutputDirectory out/product/rc-music-installer/rc-music-windows-complete-dev-51cd067-20261104 -MakeNsis out/m0/toolchains/nsis-3.12/makensis.exe
```

As receitas recusam destinos existentes; os comandos registram a entrega e precisam de novos nomes para outra execução. Configure/build Release OFF e lint passaram, com **zero testes registrados**. O grafo possui **289 comandos**, sem fontes de host/download/testes; query do executável e metadata do RSP conferem o loader do SDK. O grafo Lite separado tem **255 comandos**, com ausência do loader inclusive nos inputs escondidos do linker.

O fechamento do incremento passou **382/382 casos de scripts** pelo `check -Diagnostics`, **3/3 checks de `/SAFEPPO`** com NSIS real e **7/7 CTest / 193 casos Qt**, sem skips Qt. O formato conferiu **397 C++ / 62 QML**. O configure OFF inicialmente revelou Qt Widgets ausente no escopo raiz; a dependência explícita corrigiu esse Red. Contagens e outros Reds estão no [checkpoint do plano](development-plan.md#checkpoint-atual--receitas-lite-e-completa-experimental).

Logs e recibos de desenvolvimento: `out/product/test-runs/complete-packaging-20261104/` e `out/product/validation-runs/run-25518bcdac654a20b8f696a0cbac2172/`. Construção e conferência do pacote: `out/product/complete-installer-delivery-20261104/`, com verificador operacional separado, `verification.log` e `independent-verification.json`.

O resultado `passed-read-only-verification` confirma inventário físico, tamanhos/hashes, metadados, **seis fontes da receita do runtime e nove do instalador**, notices por origem, NSIS provisionado, PE e replanejamento integral da configuração sem escrita. O setup Lite de [4125f46](local-delivery-4125f46-20261104.md) permaneceu com SHA-256 `718312a9c37e6bd0092aa13e62b794c982af52f9e830078d86c8ccf0f9cb8672`.

## Limites da entrega

Não houve execução de player, setup, WebView2, site ou perfil AppContainer; nenhuma instalação ou alteração de Registro/associações foi realizada. Não houve limpeza adicional, cópia do perfil Brave, benchmark ou publicação.

Lite e Completa experimental usam a mesma identidade/instalação; o novo setup informa substituição de versão preservando os dados, sem criar coexistência. A troca instalada, rollback/uninstall, apresentação nativa, comportamento real da engine, privacidade/perfis, identidade efetiva do runtime, acessibilidade e distribuição continuam sem prova física nesta entrega. Integração externa opcional, execução remota protegida e YouTube separado permanecem pendentes; downloads e associações estão OFF. O teste físico de AppContainer continua aguardando a autorização específica solicitada.
