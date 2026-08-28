# SDK Qt composto para M0.2

Esta receita v2 constrói um SDK privado para o candidato Qt sem alterar o contrato histórico v1. Ela combina, por cópia física isolada, o Qtbase reconstruído com o backport FreeType e os arquivos integrais dos archives oficiais `qtdeclarative`, `qtsvg` e `d3dcompiler_47`.

O contrato histórico `qt-shell-toolchain-contract.v1.json` permanece com 1.412 bytes, SHA-256 `ac00a56584f2aed24732d44f9b26107084d37a73467fc56a860feeb850181952` e oito membros. A receita nova está em `qt-shell-composite-contract.v2.json`: 10.845 bytes, SHA-256 `f75eb9bec13639f8b5201c0db749dd67cc4840732578c159d09218224e357f28`, também registrado no sidecar adjacente. O sidecar não substitui as âncoras externas exigidas pelo launcher.

## Limites e propriedade

O SDK fecha quatro donos sem fallback ou overwrite:

- `qtbase-refresh-v2`: a instalação autenticada em `q/96bvah3m0t0t71m990olv5gpvs/install`;
- `qtdeclarative`: o archive oficial autenticado de Qt Declarative 6.11.2;
- `qtsvg`: o archive oficial autenticado de Qt SVG 6.11.2;
- `d3d`: o archive oficial autenticado de `d3dcompiler_47.dll`.

Sobreposição de diretórios é permitida; colisão de qualquer folha, inclusive alias somente por caixa, é rejeitada antes da publicação. Reparse points, hardlinks de origem, ADS, paths inseguros, drift de bytes e árvores não fechadas também falham. O resultado publicado contém somente `sdk/` e `evidence/`, e cada folha do SDK deve ser uma cópia privada com um único link físico.

A execução pressupõe workspace exclusivo e quiescente. Handles retidos e revarreduras fechadas bloqueiam ou detectam as substituições exercitadas pelos testes, sem alcançar árvores externas, mas esta receita não é uma fronteira de sandbox contra administrador ou outro escritor concorrente da mesma conta. Uma mutação imediatamente após a última revarredura ainda pode causar rejeição/DoS e precisa ser detectada pela verificação independente do receipt.

A identidade de plataforma desta receita é Qt 6.11.2 Desktop x86_64, ABI `msvc2022_64` e Windows SDK `10.0.26100.0`. Ela não prova build, runtime, empacotamento, compatibilidade ABI geral, licenças/notices completos, SBOM, redistribuição ou encerramento de `CHK-SEC`/M0.2.

## Checks rápidos

```powershell
pwsh -NoLogo -NoProfile -NonInteractive -File .\experiments\m0\provisioning\qt-shell\tests\QtShellComposite.Tests.ps1
pwsh -NoLogo -NoProfile -NonInteractive -File .\experiments\m0\provisioning\qt-shell\tests\QtShellCompositeNative.Tests.ps1
pwsh -NoLogo -NoProfile -NonInteractive -File .\experiments\m0\provisioning\qt-shell\tests\QtShellCompositeLauncher.Tests.ps1
```

## Execução autenticada

Use um `RunRoot` novo, filho direto de `q`, e mantenha o workspace exclusivo durante a composição:

```powershell
$repo = (Resolve-Path .).Path
$contract = Join-Path $repo 'experiments\m0\provisioning\qt-shell\qt-shell-composite-contract.v2.json'
$runRoot = Join-Path $repo 'q\<novo-id-curto>'
pwsh -NoLogo -NoProfile -NonInteractive -File .\experiments\m0\provisioning\qt-shell\Invoke-M0QtShellComposite.ps1 `
  -ContractPath $contract `
  -ExpectedContractSha256 f75eb9bec13639f8b5201c0db749dd67cc4840732578c159d09218224e357f28 `
  -ExpectedQtBaseReceiptSha256 8a4e3c13b2cbf1103ef2a7c417160eddc17db4fdc00dcea505e249eccab28619 `
  -RepositoryRoot $repo `
  -RunRoot $runRoot
```

Capture o `receipt_sha256` retornado fora do `RunRoot`. Em outro processo PowerShell limpo, use o modo de verificação do mesmo launcher autenticado; não importe o módulo por path diretamente:

```powershell
$repo = (Resolve-Path .).Path
$contract = Join-Path $repo 'experiments\m0\provisioning\qt-shell\qt-shell-composite-contract.v2.json'
$receipt = Join-Path $repo 'q\<id-do-run>\evidence\qt-shell-composite-receipt.v2.json'
$expectedReceiptSha256 = '<receipt_sha256 capturado externamente>'
pwsh -NoLogo -NoProfile -NonInteractive -File .\experiments\m0\provisioning\qt-shell\Invoke-M0QtShellComposite.ps1 `
  -ContractPath $contract `
  -ExpectedContractSha256 f75eb9bec13639f8b5201c0db749dd67cc4840732578c159d09218224e357f28 `
  -ExpectedQtBaseReceiptSha256 8a4e3c13b2cbf1103ef2a7c417160eddc17db4fdc00dcea505e249eccab28619 `
  -RepositoryRoot $repo `
  -ReceiptPath $receipt `
  -ExpectedReceiptSha256 $expectedReceiptSha256
```

O launcher mantém leases físicos do contrato e de toda a receita antes do `Import-Module` e durante a chamada ao verificador público. Os três hashes externos são encaminhados e devolvidos sem serem substituídos por hashes derivados dos paths. A verificação relê o manifesto, inventários por dono, inventário global, ownership e todas as folhas físicas do SDK; ela não reexecuta a composição.
