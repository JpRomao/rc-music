# Experimentos M0

Este namespace contém somente os spikes comparativos autorizados. Ele não é o scaffold do player e será promovido ou descartado após M0.10.

Regras:

- `protocol/` contém inputs executáveis comuns e schemas;
- `shared/` contém a fixture sintética do shell e receberá o corpus somente em M0.5;
- `candidates/qt` e `candidates/tauri` não compartilham código de produto;
- `harness/` contém instrumentação neutra e testes;
- todo output gerado fica em `out/m0`, fora do Git;
- nenhuma coleta comparativa começa antes de M0.1 ficar verde.

Os contratos interpretativos estão em [`docs/m0`](../../docs/m0/README.md).

## Checks executáveis

Na raiz do repositório:

```powershell
& .\scripts\m0\Invoke-M0HarnessTests.ps1
& .\scripts\m0\Test-M0Fixture.ps1
& .\scripts\m0\Test-M0Statistics.ps1
& .\scripts\m0\Test-M0Markers.ps1
& .\scripts\m0\Test-M0Evidence.ps1
& .\scripts\m0\Test-M0Layout.ps1
```

Esses checks cobrem layout, paths, descriptors, vínculo por hash, a fixture sintética determinística de 1.000 itens, agenda/estatística golden, catálogo de markers externos e o writer recuperável de evidência selada. O snapshot final é o único commit point: runs sem snapshot permanecem invisíveis a readers decisórios, e recovery de crash de processo nunca trunca tails nem inventa bytes. O manifesto canônico valida e deriva os inputs comuns reais, mas a decisão pública de batch permanece fechada até existir também proveniência externa de artifact, toolchains e lockfiles dos candidatos; detector visual, coleta, resumo e piloto ainda pertencem aos próximos incrementos de M0.1. Nenhum check atual faz alegação de desempenho ou de durabilidade contra power loss/directory `fsync`.

O módulo `M0.Evidence.psm1` expõe a sessão stateful `Open-M0EvidenceWriter` / `Add-M0EvidenceEvent -Writer` / `Complete-M0EvidenceRun -Writer` / `Close-M0EvidenceWriter`, além de `Get-M0EvidenceRecoveryState`, `Resume-M0EvidenceRun`, `Assert-M0EvidenceSnapshot` e `New-M0EvidenceSnapshotSet`. Todas exigem `EvidenceRoot`; nomes são derivados do `RunId`, `Add` recusa `run_sealed`, e digest/batch decisório só consome um snapshot-set de 44 commits A/B cujo sidecar foi publicado. O entrypoint público exige um processo PowerShell limpo, rejeita qualquer `M0.Harness` ou `M0.Evidence` preloaded sem removê-lo/recarregá-lo e importa somente os paths literais cobertos pelo manifesto ancorado. A role sintética `P` é estruturalmente legível, mas sempre não decisória. A identidade física pelo handle está validada somente no Windows; fora dele o writer retorna `M0E026` até um adapter `openat`/`fstat` equivalente ser implementado e testado.
