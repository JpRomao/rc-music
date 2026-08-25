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
pwsh -NoLogo -NoProfile -NonInteractive -File .\scripts\m0\Invoke-M0HarnessTests.ps1
pwsh -NoLogo -NoProfile -NonInteractive -File .\scripts\m0\Test-M0Fixture.ps1
pwsh -NoLogo -NoProfile -NonInteractive -File .\scripts\m0\Test-M0Statistics.ps1
pwsh -NoLogo -NoProfile -NonInteractive -File .\scripts\m0\Test-M0Markers.ps1
pwsh -NoLogo -NoProfile -NonInteractive -File .\scripts\m0\Test-M0Evidence.ps1
pwsh -NoLogo -NoProfile -NonInteractive -File .\scripts\m0\Test-M0Visual.ps1
pwsh -NoLogo -NoProfile -NonInteractive -File .\scripts\m0\Test-M0Process.ps1
pwsh -NoLogo -NoProfile -NonInteractive -File .\scripts\m0\Test-M0Summary.ps1
pwsh -NoLogo -NoProfile -NonInteractive -File .\scripts\m0\Test-M0Layout.ps1
```

Esses checks cobrem layout, paths, descriptors, vínculo por hash, a fixture sintética determinística de 1.000 itens, agenda/estatística golden, catálogo de markers externos, detector visual sintético, contrato de árvore/identidade, summary comum puro e o writer recuperável de evidência selada. O detector visual fecha BGRA8/stride, ROI racional, mediana B/G/R, thresholds inteiros, persistência, QPC e transição UIA pt-BR; ainda não captura Desktop Duplication live nem implementa o buffer externo de coleta. O snapshot final é o único commit point: runs sem snapshot permanecem invisíveis a readers decisórios, e recovery de crash de processo nunca trunca tails nem inventa bytes. O manifesto canônico ancora os inputs comuns reais e os contratos transitivos. O summary golden transforma somente uma fixture agregada previamente validada; o adapter físico dos 44 snapshots A/B, a coleta comparativa e a publicação do piloto pertencem aos próximos incrementos. Nenhum check atual faz alegação de desempenho ou de durabilidade contra power loss/directory `fsync`.

`Test-M0Visual.ps1` exige processo PowerShell limpo, rejeita `M0.Harness` ou `M0.Visual` preloaded e importa somente os paths literais do repositório cobertos pelo manifesto. `M0.Visual.psm1` é um detector puro/sintético; o tie-break `visual-change` antes de `uia-change` no mesmo QPC está congelado para o futuro coletor, não é apresentado como captura já implementada.

`Test-M0Process.ps1` também exige processo limpo e rejeita módulos ou tipos nativos homônimos preloaded. No Windows, ele cria o root suspenso, atribui-o a um Job kill-on-close sem breakaway, observa root → child → grandchild por handles com birth token, fecha uma única capability escopada e exige zero survivors. A suíte focada separada prova que unload normal e crash abrupto do owner preservam um sentinel independente; o Close direto prova zero survivors em outro caso. O snapshot `partial/etw_live_not_implemented` vira artifact P canônico somente em memória e é descartado: nada é gravado, publicado, selado ou inserido em snapshot-set/summary. A matriz pura fecha ETW/nonce, PID reuse, cleanup direto futuro e identidade Linux `(pid,start_ticks,boot_id)`; ETW/breakaway live e Linux live continuam não provados.

`Test-M0Summary.ps1` exige processo limpo, valida o manifesto antes de importar `M0.Summary.psm1` pelo path exato e prova descriptor, schemas, input e golden físicos no Windows. `New-M0Summary` e `Assert-M0Summary` são APIs puras: normalizam A/B, recompõem manifests/inventários, calculam HF7/bootstrap, média/desvio e budgets e emitem JSON canônico sem carregar o adapter nativo de processo. Duas execuções independentes em roots/culturas distintos reproduzem os bytes do golden; isso não é uma alegação de igualdade entre evidências live. O reader físico Linux permanece `not_proven`.

O módulo `M0.Evidence.psm1` expõe a sessão stateful `Open-M0EvidenceWriter` / `Add-M0EvidenceEvent -Writer` / `Complete-M0EvidenceRun -Writer` / `Close-M0EvidenceWriter`, além de `Get-M0EvidenceRecoveryState`, `Resume-M0EvidenceRun`, `Assert-M0EvidenceSnapshot` e `New-M0EvidenceSnapshotSet`. Todas exigem `EvidenceRoot`; nomes são derivados do `RunId`, `Add` recusa `run_sealed`, e digest/batch decisório só consome um snapshot-set de 44 commits A/B cujo sidecar foi publicado. O entrypoint público exige um processo PowerShell limpo, rejeita qualquer `M0.Harness` ou `M0.Evidence` preloaded sem removê-lo/recarregá-lo e importa somente os paths literais cobertos pelo manifesto ancorado. A role sintética `P` é estruturalmente legível, mas sempre não decisória. A identidade física pelo handle está validada somente no Windows; fora dele o writer retorna `M0E026` até um adapter `openat`/`fstat` equivalente ser implementado e testado.
