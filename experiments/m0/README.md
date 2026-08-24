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
& .\scripts\m0\Test-M0Evidence.ps1
& .\scripts\m0\Test-M0Layout.ps1
```

Esses checks cobrem layout, paths, descriptors, vínculo por hash, a fixture sintética determinística de 1.000 itens, agenda/estatística golden e contratos estruturais de evidência selada. A validação pública de batch permanece fechada até o manifesto canônico derivar os inputs reais; writer durável, catálogo de marcadores, coleta, resumo e piloto ainda pertencem aos próximos incrementos de M0.1. Nenhum check atual faz alegação de desempenho.
