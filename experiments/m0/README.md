# Experimentos M0

Este namespace contém somente os spikes comparativos autorizados. Ele não é o scaffold do player e será promovido ou descartado após M0.10.

Regras:

- `protocol/` contém inputs executáveis comuns e schemas;
- `shared/` conterá somente dados/assets da fixture e do corpus;
- `candidates/qt` e `candidates/tauri` não compartilham código de produto;
- `harness/` contém instrumentação neutra e testes;
- todo output gerado fica em `out/m0`, fora do Git;
- nenhuma coleta comparativa começa antes de M0.1 ficar verde.

Os contratos interpretativos estão em [`docs/m0`](../../docs/m0/README.md).

## Checks executáveis

Na raiz do repositório:

```powershell
& .\scripts\m0\Invoke-M0HarnessTests.ps1
& .\scripts\m0\Test-M0Layout.ps1
```

Esses checks cobrem apenas layout, paths, descriptors e vínculo por hash com os contratos congelados. Fixture, agenda, evidência e piloto ainda pertencem aos próximos incrementos de M0.1.
