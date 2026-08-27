# Rebuild isolado do Qtbase para M0.2

Esta pasta materializa uma identidade nova para reconstruir o Qtbase 6.11.2 com o backport FreeType `5a280ec`. Ela não modifica, reetiqueta ou substitui o SDK oficial, o contrato histórico `ac00a565…1952`, o pacote `msvc-63717e5-clean-01` ou qualquer evidência anterior.

O contrato operacional fixa o archive Qtbase da revisão `ef55f427`, o patch de alvo único, Vulkan-Headers `sdk-1.2.182.0`, MSVC 14.44, Windows SDK 10.0.26100.0 e os bytes relevantes de CMake/Ninja/SDK. A configuração usa Qt compartilhado, `Debug;RelWithDebInfo`, bibliotecas 3rd-party embutidas, OpenGL dinâmico, Vulkan, DirectWrite/Direct2D, acessibilidade, bandeja, CET, `/MD` e informação de debug; somente `RelWithDebInfo` será instalado. LTCG, PCH, OpenSSL, PostgreSQL e o pipeline SBOM oficial ficam explicitamente fora desta receita. OpenSSL/PostgreSQL não pertencem ao closure implantado do candidato; essa limitação não equivale a paridade integral com o SDK Qtbase oficial.

Não há instalação global nesta etapa. Os headers Vulkan são extraídos em uma árvore privada sob `out/m0`, Schannel atende o backend TLS efetivamente implantado, e o NVDA já permanece provisionado separadamente em modo portátil.

## Contrato de orquestração

O módulo aceita somente JSON fechado e autenticado por SHA-256 externo. Antes de criar um `RunRoot`, autentica fontes e ferramentas, rejeita variáveis de build injetadas e exige um filho direto novo de `out/m0/qtbase-refresh-runs`. O callback roda exatamente uma vez. Receipt só é publicado por rename sem overwrite depois de resultado nativo zero e observações exatas de patch, configuração, features e 15 artefatos do closure.

```powershell
pwsh -NoLogo -NoProfile -NonInteractive -File .\experiments\m0\provisioning\qtbase-refresh\tests\QtBaseRefresh.Tests.ps1
```

SHA-256 do contrato operacional: `2023dd7621015906d86d5956474e828f4e1ef74bedda52907c5e827aa378c2c6`.

O runner nativo, os inventários de árvore/install, a comparação de exports/imports e o pacote corrigido são incrementos posteriores. Até eles passarem, este contrato não demonstra um `Qt6Gui.dll` corrigido, não encerra `CHK-SEC`/M0.2 e não autoriza distribuição nem escolha de stack.
