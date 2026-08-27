# M0.2 — Preparação do refresh MSVC

**Data:** 2026-08-27
**Estado no checkpoint `b59a247`:** validadores preparados; primeira instalação cancelada na confirmação de administrador; receita ainda usava a toolchain histórica

**Atualização posterior:** a nova tentativa explicitamente autorizada concluiu a [instalação e o build com MSVC 14.44](m0-2-msvc-installation.md), incluindo contrato e receipt reais. As seções abaixo preservam o escopo histórico desta preparação; a integração à receita de pacote continua pendente.

## Resultado e escopo

A autorização do mantenedor para atualizar o MSVC permitiu inventariar as instalações e preparar uma atualização mínima. O Windows cancelou a solicitação de elevação antes de iniciar o instalador. Não existe novo compilador validado, contrato operacional de refresh, receipt real, rebuild ou pacote com MSVC atualizado. O cancelamento não foi contornado nem seguido de nova tentativa automática.

O código entregue nesta fatia prepara a autenticação do refresh sem ativá-lo. Não altera a [família histórica de preflight](m0-2-preflight.md), o ambiente comparativo `M0-E1`, Qt 6.11.2 ou os stages já autenticados. `CHK-SEC` e M0.2 continuam abertos.

## Escolha do procedimento

O inventário encontrou Community 2022 e Build Tools 2022 em 17.13.0, ambos completos, e Community 2026 em 18.5.1, marcado incompleto pelo instalador. A instância 2026 possui ferramentas 14.50, mas isso não a torna uma instalação completa nem coloca MSVC 2026 na matriz oficialmente listada pelo [Qt 6.11](https://doc.qt.io/qt-6/supported-platforms.html), que especifica MSVC 2022 para Windows x64.

A [tabela de versões Microsoft](https://learn.microsoft.com/en-us/cpp/overview/compiler-versions?view=msvc-170) identifica 14.43 como fora de suporte e 14.44 como linha ainda suportada. O plano mínimo selecionado foi uma **nova instância Build Tools 2026 com o componente legado MSVC v143 14.44**, mantendo o compilador MSVC 2022. Esse componente consta no [catálogo oficial](https://learn.microsoft.com/en-us/visualstudio/install/workload-component-id-vs-build-tools?view=visualstudio).

- Destino: `C:\Program Files\Microsoft Visual Studio\18\BuildTools`, ausente antes e depois da tentativa.
- Componente: `Microsoft.VisualStudio.Component.VC.14.44.17.14.x86.x64` e suas dependências obrigatórias.
- Não foram solicitados workloads Web/Node, recomendados/opcionais, o toolset mais recente da série 14.5x, atualização de todas as instâncias, reparo da Community 2026, remoção de toolsets ou reinício automático.
- MSBuild/Core/Roslyn/NuGet e CRT podem fazer parte das dependências obrigatórias; a proposta não promete isolamento absoluto de componentes compartilhados.
- O SDK Windows existente seria reaproveitado inicialmente, sujeito a novo teste de compilação/linkagem. Nenhuma completude foi inferida apenas pela existência de diretórios.

O procedimento usa os [parâmetros oficiais do instalador](https://learn.microsoft.com/en-us/visualstudio/install/use-command-line-parameters-to-install-visual-studio?view=visualstudio): nova instalação com `--installPath`, somente o `--add` acima, `--removeOos false`, `--passive`, `--norestart` e `--wait` do bootstrapper. A elevação foi solicitada por `Start-Process -Verb RunAs`; nenhuma confirmação de segurança foi automatizada.

## Origem e tentativa de instalação

| Campo | Evidência |
|---|---|
| Origem oficial | [Build Tools Stable](https://aka.ms/vs/stable/vs_buildtools.exe), identificado no [histórico Microsoft](https://learn.microsoft.com/en-us/visualstudio/releases/2026/release-history) |
| Bootstrapper | `out/m0/toolchains/msvc-14.44-refresh-20260827-01/vs_buildtools.exe` |
| FileVersion / produto | `18.9.12120.119` / Visual Studio 2026 |
| Tamanho | 5.690.848 bytes |
| SHA-256 | `352ac5776d592a4870c15756f9e140507b9ee64de62b401152e5929f7c0c52a6` |
| Authenticode | `Valid`, assinante Microsoft Corporation; hash e assinatura reconferidos imediatamente antes da tentativa |
| Resultado da chamada | exit 1 de PowerShell; `Start-Process` informou “A operação foi cancelada pelo usuário” |
| Verificação posterior | `2026-08-27T07:04:26.9202247Z`: destino ausente e nenhum instalador/consentimento/player identificado em execução |

O executável baixado foi preservado para rastreabilidade; nenhuma ferramenta nova foi instalada. O resultado não é sucesso, reboot pendente ou versão MSVC atualizada. Uma retomada exige autorização para uma nova tentativa e aprovação manual da confirmação do Windows.

## Preparação com TDD

`QtShellPackageBuilder.psm1` ganhou dois contratos públicos que **ainda não são chamados pela receita**:

- `Get-QtShellMsvcRefreshIdentity`: confronta um contrato fechado com CMake, qconfig e observações de cl/link/dumpbin. Exige família 14.44, x64, versões/paths coerentes, linker não anterior ao usado por Qt, hashes, tamanhos e assinatura Microsoft pinada. Não executa texto CMake.
- `New-QtShellMsvcRefreshReceipt`: lê arquivos quiescentes, autentica o contrato por hash externo e os bytes/assinaturas das três ferramentas, rejeita reparse e gera um registro separado. Registra hashes do contrato de refresh, compiler info, cache e qconfig. Não compila, instala, executa as ferramentas ou escreve o receipt no disco por conta própria.

O hash histórico `ac00a56584f2aed24732d44f9b26107084d37a73467fc56a860feeb850181952` permanece obrigatório, e os oito membros autenticados pela família histórica são conferidos por regressão. O probe `CMAKE_CXX_COMPILER_LINKER` pode usar grafia 8.3; sua versão é confrontada, enquanto os caminhos efetivos de `CMAKE_LINKER` e `CMAKE_CXX_COMPILER` precisam corresponder ao contrato no cache e no compiler info.

Os testes usam versões, certificados e hashes **sintéticos e explicitamente identificados**, não pins de uma instalação real. Nenhum contrato operacional com placeholders foi criado. A receita `New-QtShellPackage.ps1` e seu entrypoint anterior permanecem inalterados.

### Checks

- Red: teste público falhou por ausência de `Get-QtShellMsvcRefreshIdentity`.
- Green: `ToolchainRefresh.Tests.ps1`, **16/16**; `Builder.Tests.ps1`, **14/14**, reconferidos pelo root. Regressões do empacotador: `Manifest.Tests.ps1`, **113/113**, e `Smoke.Tests.ps1`, **38/38**; total **181/181**.
- Parser PowerShell: nove arquivos `.ps1`/`.psm1` do empacotador, zero erros. PSScriptAnalyzer não está instalado; não se alega lint por essa ferramenta.
- Casos incluem CRLF/LF, 19.43/19.50/patch divergente, linker trocado, x86, campos duplicados, drift Qt, três identidades de ferramentas, contrato adulterado, reparse e arquivos sintéticos sem assinatura/versão, sem executá-los.
- A geração bem-sucedida de receipt com ferramentas reais, configure/build/link e smoke atualizado **não foram executados**. Os testes puros não substituem esses checks.

O commit desta preparação fecha somente os validadores e a documentação associada. Para efetivar a atualização: obter o MSVC autorizado, medir versão/tamanho/hash/assinante reais, criar o contrato separado sem modificar o histórico, executar preflight em diretório novo, integrar receipt/manifesto à receita e refazer build/pacote/checks. Nenhum gate de desktop, runtime, licença, redistribuição ou decisão de stack é encerrado por esta fatia.
