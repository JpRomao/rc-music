# M0.2 — Aplicabilidade por símbolos Qt: PE, PDB e DIA

**Data da auditoria:** 2026-08-27

**Estado:** identidade confirmada e aplicabilidade parcialmente delimitada; `CHK-SEC`/`SEC-014` continuam abertos.

## Escopo e resultado

Complemento da [triagem de segurança](m0-2-security-triage.md), limitado a `Qt6Core.dll` e `Qt6Gui.dll` do pacote privado `out/m0/packages/qt-shell/tray-200b983-clean-01/app`. O manifesto identifica o commit `200b9838dc9e86427e083771ef71bc9f1ee49f4b`, `source_paths_clean=true`, `recipe_dirty=false` e Qt 6.11.2 compilado por MSVC 19.44.35227. O SHA-256 de `manifest.v1.json`, externo a `app`, foi reconfirmado: `a227bb2b23b7ec958b49318183ec399fd9c1e648e640a75e5fa7d06e5a97dc99`.

- Os dois PDBs selecionados correspondem aos PEs do manifesto por GUID/age, não têm símbolos privados removidos (stripped) e não usam FASTLINK, conforme os campos examinados.
- PCRE2: a ausência do objeto e dos símbolos de serialização sustenta a inferência estreita de que esse caminho não foi incorporado nesta `Qt6Core.dll`. Não declara PCRE2 10.47 corrigido nem ausência de outros caminhos ou vulnerabilidades.
- FreeType: as duas funções examinadas estão presentes em `Qt6Gui.dll`; a correção no código de máquina e o alcance do defeito no shell não foram demonstrados.

Não houve execução do player/PoC, alteração das DLLs/PDBs/pacote, instalação ou interação nativa nesta auditoria. O executável de diagnóstico DIA2Dump foi usado na coleta original; a revisão independente leu os dumps e os bytes dos PDBs/PEs. A elaboração deste registro não fez novos downloads nem executou novamente o DIA2Dump.

## Procedência e integridade dos artefatos

Raiz local da evidência: `out/m0/security/qtbase-symbols-20260827-01/`. Todos os paths da tabela seguinte são relativos a ela. Esses arquivos estão em `out`, fora do Git e do payload do pacote local; um clone do repositório não contém o leitor, os PDBs ou os dumps. Não constituem conteúdo para redistribuição.

O arquivo oficial foi obtido no [repositório Qt de símbolos 6.11.2/MSVC2022 x64](https://download.qt.io/online/qtsdkrepository/windows_x86/desktop/qt6_6112/qt6_6112_msvc2022_64/qt.qt6.6112.debug_info.win64_msvc2022_64/), com o nome `6.11.2-0-202608131017qtbase-Windows-Windows_11_24H2-MSVC2022-Windows-Windows_11_24H2-X86_64-debug-symbols.7z`. Houve uma extração inicial acidentalmente ampla em `bin-placeholder/`, incluindo outros PDBs de módulos, plugins e ferramentas. Depois foram selecionados `Qt6Core.pdb` e `Qt6Gui.pdb` em `symbols/` para esta análise. Portanto, não foram extraídos somente dois arquivos, nem a presença dos demais PDBs implica inclusão desses componentes no pacote.

| Artefato local | Bytes | SHA-256 |
|---|---:|---|
| Arquivo `.7z` nomeado acima | 173351628 | `25d73a86ba2f39455a4be1835e1bd5a6ea6f4cfcc44b41ec12ce816f5e0a5256` |
| `symbols/Qt6Core.pdb` | 53448704 | `8b001af2850e3f3fd89bf6b27b4a6137d1a3e760c812509cb25e1427340115c4` |
| `symbols/Qt6Gui.pdb` | 80171008 | `e42d795755c3bbcaee3f8e7b1dd6bf7fce3c05a0dce40159be1a7d316a7cf984` |
| `dia2dump-build/dia2dump.exe` | 158720 | `10bda222b24abab94fd3e0198f64c62dee7cd26e0cf2030e1e296c5d985d3f03` |
| `Read-PdbPeIdentity.ps1` | 9171 | `67016c378c3ca37ee96595811f091bd970f11e617d7e85d3475985f9a78a541e` |
| `Qt6Core-modules.txt` | 40751 | `d76e423d3b32e9233aeee1eff7a0e5cfc6a7a23f3a58c4c4e8c23755e59a1be6` |
| `Qt6Core-publics.txt` | 4510908 | `fccc03d58c93ddc3885eca4b693682daa8204ef48a7e13c6b4406e0f3ca2fdd1` |
| `Qt6Core-symbols.txt` | 27102937 | `1fbdaa1a6146e6894371685ebfc815010307db74f570709bca5b477e949d95fb` |
| `Qt6Gui-modules.txt` | 44427 | `d8bf50ec248181e74192f3bbe3df59045fcb1db9fbf919b3aa10e4aa7a35a67a` |
| `Qt6Gui-publics.txt` | 6592977 | `2d1b826e9ac670387924c2c8dffa9ab7b3867eb2c4fb7e30b5808c2df11a3952` |
| `Qt6Gui-symbols.txt` | 37470913 | `b332a8b98d395a97b6cfd2c0bf1276585f461f3f83d6525ae0a93ef275dba9ba` |

O DIA2Dump veio dos quatro `.cpp` inalterados do sample Microsoft em `C:\Program Files\Microsoft Visual Studio\2022\Community\DIA SDK\Samples\DIA2Dump`: `DIA2Dump.cpp`, `PrintSymbol.cpp`, `regs.cpp` e `stdafx.cpp`. A compilação histórica usou `cl` 19.43, `/O1 /Zi /EHsc /MD /D_UNICODE /DUNICODE` e link com `ole32`, `oleaut32` e `diaguids`. Esse uso diagnóstico não atualiza a toolchain do shell nem resolve seu EOL. O executável é uma compilação local autenticada pelo hash acima, não um binário assinado pela Microsoft ou um build declarado bit a bit reproduzível.

A classe COM `{E6756135-1E65-4D17-8576-610761398C3C}`, em `HKEY_CLASSES_ROOT\CLSID\...\InprocServer32`, aponta para `C:\Program Files\NVIDIA Corporation\Nsight Systems 2024.4.2\host-windows-x64\msdia140.dll`. Foram reconfirmados versão `14.33.31629.0`, 1870736 bytes, SHA-256 `ba67e8b5f387843191ace001ce2ca2ab279458be5e5967e964af2ae62eaaadca` e Authenticode `Valid`, assinante Microsoft Corporation. Não houve alteração do registro COM.

## Pareamento e cobertura estrutural independentes do DIA

O leitor diagnóstico local `out/m0/security/qtbase-symbols-20260827-01/Read-PdbPeIdentity.ps1` reconstrói os streams MSF7, lê Info/DBI e o registro RSDS da Debug Directory dos PEs, sem carregá-los como executáveis. As referências de layout são [MSF](https://llvm.org/docs/PDB/MsfFile.html), [Info/PDB e pareamento GUID/age](https://llvm.org/docs/PDB/PdbStream.html) e [DBI](https://llvm.org/docs/PDB/DbiStream.html), do LLVM.

| PE em `app/` | Bytes | SHA-256 igual à entrada do manifesto |
|---|---:|---|
| `Qt6Core.dll` | 10363704 | `22c113a5644b875b2c85482723cd5d2e6a3a4029169778fcc78adde08ef3b6aa` |
| `Qt6Gui.dll` | 9546552 | `19265a4e76efc12ef5f5bcf59be6c6a5b9b59ec4c89660797738bd2da7f719cc` |

| Par PE/PDB | GUID idêntico | Age PE/Info/DBI | Módulos DBI / DIA | TPI / IPI, bytes |
|---|---|---|---|---|
| Qt6Core | `E02BF479-DDFA-4411-A7CF-3966D0BBFE2A` | 1 / 1 / 1 | 407 / 407 | 14067880 / 2383998 |
| Qt6Gui | `5C4AB8E0-B185-4B19-AF47-1C44112450D6` | 1 / 1 / 1 | 447 / 447 | 25621040 / 3540094 |

Ambos têm blocos de 4096 bytes, Info versão `20000404`, DBI versão `19990903`, máquina `0x8664` e flags DBI `0x0000`: sem `ArePrivateSymbolsStripped` (`0x2`) ou `WasIncrementallyLinked` (`0x1`). A cauda de features contém `0` e `VC140` (`20140508`), sem `MinimalDebugInfo/MINI` (`0x494E494D`, FASTLINK); TPI/IPI são efetivamente presentes. Os paths RSDS são `C:\Users\qt\work\qt\qtbase_build\bin\Qt6Core.pdb` e `Qt6Gui.pdb`.

Foram conferidos os limites e a cobertura do diretório MSF, dos sete substreams DBI e de todos os registros de módulos; 407/447 módulos têm streams de símbolos válidos quanto a índice e extensão. Todos os nomes coincidem, sem diferenças, com `*-modules.txt` e com os cabeçalhos `** Module:` de `*-symbols.txt`. Não foram encontrados erros explícitos do leitor nos dumps. Isso confirma cobertura nominal dos módulos e o formato não stripped/não FASTLINK, não a decodificação de cada registro CodeView ou a sobrevivência de todas as funções às otimizações. GUID/age é pareamento de build, não assinatura criptográfica; a identificação dos bytes depende também dos hashes e da procedência registrados.

## PCRE2 e FreeType: conclusões delimitadas

O Core contém 24 compilands de `BundledPcre2`, todos com streams de símbolos não vazios. Com o prefixo `pcre2_` e o sufixo `.c.obj`, os nomes são: `config`, `context`, `compile`, `pattern_info`, `match_data`, `match`, `jit_compile`, `error`, `string_utils`, `ucd`, `chartables`, `auto_possess`, `chkdint`, `find_bracket`, `newline`, `ord2utf`, `study`, `valid_utf`, `compile_class`, `compile_cgroup`, `tables`, `extuni`, `script_run` e `xclass`. Não há compiland `pcre2_serialize.c.obj`, nem ocorrência de `pcre2_serialize*` nos dumps de módulos, públicos ou símbolos privados.

| Controle positivo | RVA | Evidência local |
|---|---|---|
| `pcre2_compile_16` | `0036C570` | `Qt6Core-symbols.txt:355880`; também público |
| `pcre2_match_16` | `0037A440` | `Qt6Core-symbols.txt:356659`; também público |
| `FT_Get_Var_Design_Coordinates` | `0061C5E0` | `Qt6Gui-symbols.txt:489220`; também público |
| `TT_Get_Var_Design` | `00628990` | `Qt6Gui-symbols.txt:491311`; função privada estática |

O [GHSA-q7rw-r7qq-2hx6](https://github.com/PCRE2Project/pcre2/security/advisories/GHSA-q7rw-r7qq-2hx6) inclui 10.47 na faixa afetada (`>=10.45`) e indica 10.48 como corrigida. O defeito descreve dois bytes não inicializados expostos pela serialização do código compilado. O [target Qt fixado](https://github.com/qt/qtbase/blob/ef55f427f2c8b410d34f8a7681020a3000cf6866/src/3rdparty/pcre2/CMakeLists.txt) inclui `pcre2_serialize.c` no arquivo estático de 16 bits; isso não exige que o objeto entre na DLL. O [stream operator Qt](https://github.com/qt/qtbase/blob/ef55f427f2c8b410d34f8a7681020a3000cf6866/src/corelib/text/qregularexpression.cpp) grava padrão e opções, não o código compilado PCRE2.

**Inferência PCRE2:** neste Core, identificado pelos hashes e pelo PDB correspondente, o serializador não aparece como objeto incorporado ou símbolo; a exposição pelo caminho específico do advisory não foi identificada. Isso é mais forte que ausência de exports, mas não é prova universal de inalcançabilidade, correção de 10.47, ausência de toda cópia/inlining ou resultado transferível a outro build. A fonte afetada continua registrada na triagem.

**FreeType:** os símbolos confirmam presença das funções relacionadas à análise de CVE-2026-50811. Não houve comparação da implementação em código de máquina com o patch, exploração ou análise exaustiva dos chamadores. O [callsite Qt já examinado](https://github.com/qt/qtbase/blob/ef55f427f2c8b410d34f8a7681020a3000cf6866/src/gui/text/freetype/qfontengine_ft.cpp) usa o número de eixos e um buffer desse tamanho; isso não certifica os demais caminhos. Presença não demonstra correção nem exploração do shell. Nenhuma dessas conclusões encerra `CHK-SEC`, `SEC-014`, licenciamento ou a pendência de toolchain.

## Limites concretos do DIA e do leitor

- O [DIA SDK](https://learn.microsoft.com/en-us/visualstudio/debugger/debug-interface-access/debug-interface-access-sdk?view=vs-2022) abstrai um formato em evolução. Não foi encontrada garantia de enumeração integral dos registros produzidos por MSVC 19.44 usando DIA 14.33. A assinatura válida identifica o leitor, não garante essa cobertura.
- O sample usa `loadDataFromPdb` (`DIA2Dump.cpp:112`), não `loadAndValidateDataFromPdb`. A [documentação Microsoft](https://learn.microsoft.com/en-us/visualstudio/debugger/debug-interface-access/idiadatasource-loaddatafrompdb?view=visualstudio) distingue carregar de validar critérios de identidade; o pareamento foi feito separadamente pelo leitor binário.
- Em `DIA2Dump.cpp:791` e `:847`, falhas de `Next` encerram o `while`, mas as funções ainda podem retornar `true`; falhas de `findChildren` dos filhos também podem ser ignoradas. Portanto `exit 0`/ausência de mensagens não provam enumeração exaustiva. Os arquivos `DIA2Dump.cpp` instalados nos SDKs 2022 e 18 são idênticos, SHA-256 `54c47a39b8b41fbbb0af93d05e42e74282c17fcb2a5a6d0b1f6b627ba787baa4`.
- O leitor PowerShell verifica identidade, cabeçalhos, extensões e módulos destes MSF7/PE32+ x64; não é validador geral de PDB, disassembler, scanner de CVEs ou análise de fluxo. As leituras pressupõem arquivos quiescentes; não há proteção por handles contra alterações concorrentes/TOCTOU. Os artefatos locais e hashes precisam ser preservados para repetir a evidência.

## Reprodução local sem executar o payload

PowerShell 7 e `rg`, a partir da raiz do repositório, com os artefatos locais existentes. Não baixar/extrair novamente, substituir arquivos ou executar o player para repetir estes checks. Primeiro conferir os hashes do leitor e do manifesto; os demais resultados devem coincidir com as tabelas:

```powershell
$symbolRoot = Join-Path (Get-Location) 'out/m0/security/qtbase-symbols-20260827-01'
$packageRoot = Join-Path (Get-Location) 'out/m0/packages/qt-shell/tray-200b983-clean-01'
$reader = Join-Path $symbolRoot 'Read-PdbPeIdentity.ps1'
$manifestPath = Join-Path $packageRoot 'manifest.v1.json'
if ((Get-FileHash -LiteralPath $reader -Algorithm SHA256).Hash.ToLowerInvariant() -cne '67016c378c3ca37ee96595811f091bd970f11e617d7e85d3475985f9a78a541e') { throw 'Reader divergente.' }
if ((Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash.ToLowerInvariant() -cne 'a227bb2b23b7ec958b49318183ec399fd9c1e648e640a75e5fa7d06e5a97dc99') { throw 'Manifesto divergente.' }

foreach ($component in @('Qt6Core', 'Qt6Gui')) {
    # Dot-source apenas do reader autenticado: expõe a lista diagnóstica $modules.
    $identityJson = . $reader -PdbPath "$symbolRoot/symbols/$component.pdb" -PePath "$packageRoot/app/$component.dll"
    $identityJson
    if (-not ($identityJson | ConvertFrom-Json).matches_pe) { throw 'Pareamento PE/PDB divergente.' }
    $rawNames = @($modules.module)
    $diaNames = @(Get-Content -LiteralPath "$symbolRoot/$component-modules.txt" | ForEach-Object { if ($_ -match '^[0-9A-Fa-f]{4} (.+)$') { $Matches[1] } })
    $privateNames = @(Get-Content -LiteralPath "$symbolRoot/$component-symbols.txt" | ForEach-Object { if ($_ -match '^\*\* Module: (.+)$') { $Matches[1] } })
    if (@(Compare-Object $rawNames $diaNames -CaseSensitive).Count -ne 0) { throw 'Censo de módulos divergente.' }
    if (@(Compare-Object $rawNames $privateNames -CaseSensitive).Count -ne 0) { throw 'Censo de módulos privados divergente.' }
}

Get-FileHash -LiteralPath (Get-ChildItem -LiteralPath $symbolRoot -File -Filter 'Qt6*-*.txt').FullName -Algorithm SHA256 | Format-List
rg -n 'Function.*pcre2_(compile|match)_16' "$symbolRoot/Qt6Core-symbols.txt"
if ($LASTEXITCODE -ne 0) { throw 'Controles positivos PCRE2 ausentes, ou erro de leitura.' }
rg -n 'Function.*(TT_Get_Var_Design|FT_Get_Var_Design_Coordinates)' "$symbolRoot/Qt6Gui-symbols.txt"
if ($LASTEXITCODE -ne 0) { throw 'Controles positivos FreeType ausentes, ou erro de leitura.' }
rg -n 'pcre2_serialize|pcre2.*serialize\.c' "$symbolRoot/Qt6Core-modules.txt" "$symbolRoot/Qt6Core-publics.txt" "$symbolRoot/Qt6Core-symbols.txt"
if ($LASTEXITCODE -ne 1) { throw 'Ausência do serializador não reconfirmada, ou erro de leitura.' }
```

O último `rg` deve terminar em `1` (nenhuma ocorrência), não `2` (erro); os controles positivos devem imprimir as funções acima. O confronto nominal deve ter zero diferenças; Info/DBI/PE e hashes devem coincidir com as tabelas. O leitor imprime JSON em stdout, sem criar outro manifesto.

Para repetir opcionalmente a coleta DIA, conferir antes o hash do executável e a DLL COM registrada; os comandos abaixo usam somente os PDBs como dados e não escrevem sobre os logs preservados. Não foram reexecutados na elaboração deste documento:

```powershell
$dia = Join-Path $symbolRoot 'dia2dump-build/dia2dump.exe'
if ((Get-FileHash -LiteralPath $dia -Algorithm SHA256).Hash.ToLowerInvariant() -cne '10bda222b24abab94fd3e0198f64c62dee7cd26e0cf2030e1e296c5d985d3f03') { throw 'DIA2Dump divergente.' }
$diaDll = (Get-Item -LiteralPath 'Registry::HKEY_CLASSES_ROOT\CLSID\{E6756135-1E65-4D17-8576-610761398C3C}\InprocServer32').GetValue('')
if ((Get-FileHash -LiteralPath $diaDll -Algorithm SHA256).Hash.ToLowerInvariant() -cne 'ba67e8b5f387843191ace001ce2ca2ab279458be5e5967e964af2ae62eaaadca') { throw 'DIA COM divergente.' }
if ((Get-AuthenticodeSignature -LiteralPath $diaDll).Status -ne 'Valid') { throw 'Assinatura DIA não validada.' }
foreach ($component in @('Qt6Core', 'Qt6Gui')) {
    foreach ($mode in @('-m', '-p', '-s')) {
        & $dia $mode "$symbolRoot/symbols/$component.pdb"
        if ($LASTEXITCODE -ne 0) { throw 'DIA2Dump falhou.' }
    }
}
```

Essas opções correspondem a módulos, públicos e símbolos por compiland. Os hashes dos `.txt` identificam os arquivos da coleta original; outra captura/encoding pode produzir bytes diferentes. Mesmo com exit zero, permanecem os limites de enumeração descritos acima. Uma substituição de Qt, toolchain ou payload exige nova identificação e análise, não reaproveitamento automático desta conclusão.

## Checks de fechamento documental

Esta entrega versiona somente evidência e não altera comportamento; não exige Red/Green artificial. A revisão independente conferiu 14 hashes de artefatos, os links locais, a sintaxe dos dois blocos PowerShell e a reprodução somente leitura. O root releu o leitor, reconferiu tamanho/hash de 12 entradas diretas das tabelas e executou novamente o pareamento: GUID/age idênticos e 407/447 streams de módulos válidos. A validação global passou com 35 Markdown, 83 requisitos, 17 checks e 76 etapas, e a revisão documental não encontrou divergências acionáveis. O commit que incorpora este complemento fecha apenas a análise delimitada; não fecha M0.2 ou `CHK-SEC`.
