# M0.2 — SDK Qt composto autenticado

**Data:** 2026-08-28

**Estado:** SDK diagnóstico Qt 6.11.2 composto, publicado e aprovado por verificação independente; build/pacote v2, `CHK-SEC`, validações nativas e M0.2 permanecem abertos

## Resultado

A receita autenticada publicou o `RunRoot` privado `q/f75eb9bea8280844c6d1a2b3f4` com somente `sdk/` e `evidence/` no nível superior. O SDK combina o Qtbase reconstruído com FreeType corrigido e os componentes oficiais autenticados de Qt Declarative, Qt SVG e D3D, sem fallback para o SDK oficial e sem colisões de folhas entre proprietários.

A receita base foi introduzida no commit `351cc475d93f44f6959b78e1c334fea8b9414b6b` (`feat(m0): authenticate composite Qt SDK`) e a execução aprovada usou o hotfix autenticado descrito abaixo.

O launcher capturou externamente o receipt publicado por último. Depois do encerramento da composição, um novo processo PowerShell executou o modo público `Verify` com os hashes esperados do contrato, do receipt composto e do receipt Qtbase. A verificação terminou com exit code `0`, `verified=True` e receipt SHA-256 `30399b3099cf04bdd50d6f083d077e82a590afefeec0617fda89ef5266a1d63e`.

| Âncora | Bytes | SHA-256 |
|---|---:|---|
| Contrato composto v2 | 10.845 | `f75eb9bec13639f8b5201c0db749dd67cc4840732578c159d09218224e357f28` |
| `experiments/m0/provisioning/qt-shell/M0.QtShellComposite.psm1` | 148.684 | `6290e5ef8a5b336cb6a3ad3f8e01632a6f1e8ce6e7cb71ed406a8e10d729cce6` |
| `experiments/m0/provisioning/qt-shell/M0.QtShellCompositeNative.psm1` | 64.084 | `bdf14b206180c95a28d5c262192530deb657c8c191911362af32ed69f0ec8596` |
| `experiments/m0/provisioning/qt-shell/Invoke-M0QtShellComposite.ps1` | 42.709 | `51217755fd30658095de32bc4941674f8eb83bbda2cc802f22aee5f065fc3f7a` |
| Manifesto composto v2 | 12.384 | `221afa70d4077eeee6c3bf689f0a8992dc88427d4b0ba3479503d9a28ae5a213` |
| Receipt composto v2 | 1.430 | `30399b3099cf04bdd50d6f083d077e82a590afefeec0617fda89ef5266a1d63e` |

O sidecar de 102 bytes do contrato contém exatamente o hash acima e `qt-shell-composite-contract.v2.json`. O manifesto registra `run_id=f75eb9bea8280844c6d1a2b3f4`, `composite_id=m0.2-qt-shell-composite-sdk-v2` e captura em `2026-08-28T12:57:22.1832912+00:00`. O receipt registra exit code `0`, stdout e stderr vazios e vincula o manifesto por tamanho e hash.

## Red, Green e falha fechada

A primeira execução real usou o contrato com prefixo `c421e625` e o run ID `c421e625a8280710d4f8b9c2e1`. Após autenticar o receipt Qtbase por aproximadamente 40,9 minutos, ela falhou fechada ao iniciar o scratch dos arquivos com `OpenDirectoryAnchor`: `O arquivo já está sendo usado por outro processo.` O handle retido com direito de exclusão não compartilhava delete, e a receita tentava reabrir o mesmo diretório com compartilhamento incompatível. Nenhum `RunRoot`, processo ou scratch dessa tentativa permaneceu.

O Red de integração percorreu `archiveScratch -> extractRoot -> snapshot -> runner nativo real -> RunRoot -> inventários por proprietário -> manifesto -> receipt -> cleanup`. Somente contrato, receita, parent, processo do 7-Zip e conteúdo mínimo foram substituídos por fixtures; handles, snapshots, publicação atômica, evidência e limpeza continuaram reais. O teste também expôs que `GetNewClosure()` removia a resolução da função privada usada pelo callback de produção.

O Green passou a reutilizar os anchors autenticados do pai e da raiz extraída, preservou a sessão privada do módulo no callback e manteve publicação e cleanup relativos a handles retidos. A correção está no commit `8dcd833a1057801deada539efdaed41c1cd42e76` (`fix(m0): preserve composite directory anchors`). O ciclo final aprovou:

- módulo central: **31/31**;
- runner nativo: **11/11**;
- launcher: **15/15**;
- parser PowerShell: seis arquivos, zero erros AST;
- contrato histórico v1: **38/38**;
- seis suítes históricas do pacote v1: **206/206** no total;
- `git diff --check`: aprovado.

O teste de integração completo tem orçamento interno de 15 segundos; a suíte central final levou aproximadamente 17,4 segundos. Esses checks validam contratos da receita e regressão do fluxo; não substituem a verificação física independente do SDK publicado.

## Composição e propriedade

O inventário global contém 9.880 folhas e soma 2.400.301.667 bytes. A auditoria independente de metadados enumerou a árvore real e obteve as mesmas contagem e soma. Os quatro inventários autenticados particionam exatamente esse conjunto:

| Proprietário | Origem | Folhas | Bytes | Bytes do inventário | SHA-256 do inventário |
|---|---|---:|---:|---:|---|
| `qtbase-refresh-v2` | `q/96bvah3m0t0t71m990olv5gpvs/install` | 4.289 | 526.177.624 | 736.125 | `839bf07b2fcf533e3a16720347863f1039cff922f8c02902f10152908b6f964e` |
| `qtdeclarative` | arquivo oficial Qt 6.11.2 | 5.470 | 1.865.393.737 | 1.164.718 | `0041f7bd4d28484d5003314adce317b43e9c5a766754eb52836c77a3c4049163` |
| `qtsvg` | arquivo oficial Qt 6.11.2 | 120 | 4.556.378 | 19.472 | `2b357e9945ee27a0a447e77579427ae858538daa0d2503901edbedade36843ca` |
| `d3d` | arquivo oficial Qt 6.11.2 | 1 | 4.173.928 | 115 | `a13e084fa429f9929adf9b4a0d37fdee9fb083a90b47a4c4fb8f7bbf970086b9` |

O inventário global tem 1.920.430 bytes e SHA-256 `4e90550253cb4e07a579bf7706d1b1d68736e4999ab454da7171a2c73d428933`. A política usa comparação `OrdinalIgnoreCase`, rejeita colisões de folhas mesmo quando os bytes seriam idênticos, permite somente sobreposição de diretórios e proíbe hardlinks, reparse points e ADS. Os 26 artefatos obrigatórios foram recalculados individualmente após a publicação; **26/26** coincidiram em tamanho e SHA-256.

## Proveniência e imutabilidade

O receipt Qtbase de 4.369 bytes permaneceu em SHA-256 `8a4e3c13b2cbf1103ef2a7c417160eddc17db4fdc00dcea505e249eccab28619`; seu manifesto de 13.866 bytes permaneceu em `dc26c0962e927145466b879e352a4c99d9fc7da54d335567373c353739afbf6b`. Ambos coincidiram com as âncoras do manifesto composto.

As entradas oficiais foram relidas depois da composição e continuaram idênticas ao contrato:

| Entrada | Bytes | SHA-256 |
|---|---:|---|
| Qt Declarative | 172.484.222 | `5591ca564c1a9299a45a15b6b1d324c86e0aaed7cf6c47b7e6fe464aecae2587` |
| Qt SVG | 684.353 | `417f44499c835b2303f3ff78043179bb23442f33d5d2816fbf7e8dbf275b1c89` |
| D3D compiler | 1.380.117 | `b1c379f739fa59a495a388c0878757e3d90fb72e0df771237a1ee4a2c1675d15` |
| 7-Zip 26.02 x64 | 1.334.784 | `35d4d69d7cd6cb44558f208c3b1334268013f9daf82d2dda848893a1c30c59c2` |

O contrato histórico `qt-shell-toolchain-contract.v1.json` permaneceu com 1.412 bytes e SHA-256 `ac00a56584f2aed24732d44f9b26107084d37a73467fc56a860feeb850181952`. Seus oito membros foram recalculados: **8/8** mantiveram tamanhos e hashes, sem diff local. Portanto, o incremento é aditivo e não reescreve o contrato, a receita ou os pacotes v1.

## Isolamento e estado operacional

A execução publicou o `RunRoot` somente depois de autenticar as fontes e montar o candidato privado. O receipt foi publicado por último. Após a composição:

- `q/.f75eb9bea8280844c6d1a2b3f4.composite-archives` estava ausente;
- `q/.f75eb9bea8280844c6d1a2b3f4.composite-native` estava ausente;
- não havia processos de produção relacionados a 7-Zip, CMake, Ninja, MSVC, `dumpbin`, `windeployqt`, `qmllint` ou CTest;
- durante a auditoria, somente os dois processos esperados do verificador independente permaneciam; ambos encerraram com a aprovação descrita acima;
- o disco C: tinha 93.437.095.936 bytes livres, aproximadamente 87,020 GiB;
- o worktree estava limpo em `8dcd833a1057801deada539efdaed41c1cd42e76` antes desta evidência documental.

## Limites e próxima etapa

Esta evidência autentica a composição física e a propriedade das 9.880 folhas do SDK diagnóstico. Ela não comprova build ou carregabilidade do shell, runtime, pacote original ou relocável, compatibilidade ABI geral, CRT/PCRE2 corrigidos, notices/licenças completos, SBOM, redistribuição, máquina limpa, acessibilidade nativa, foco, NVDA, alto contraste/DPI, bandeja/Explorer ou Linux. Não aprova definitivamente Qt, não fecha `CHK-SEC` e não fecha M0.2.

O próximo incremento é uma receita **v2 aditiva** de build e pacote, sem alterar o pacote v1. Ela deve consumir somente este receipt autenticado, fixar Windows SDK `10.0.26100.0` e MSVC `19.44.35228`, executar builds frescos Qt ON e core Qt OFF, `qmllint`, CTest, deploy e smokes nas localizações original e relocada com espaços/acentos. A ausência atual de SBOM do Qtbase e a redistribuição não autorizada devem permanecer explícitas; o pacote continua diagnóstico e privado enquanto `CHK-SEC` estiver aberto.

Nenhum áudio pessoal, inclusive os arquivos disponibilizados em `C:\Users\Joao\Downloads\anime-openings`, foi lido ou usado nesta etapa. A interação desktop nativa permanece pausada.

Esta atualização é exclusivamente documental e não exige Red artificial. Antes de adicionar este arquivo, o validador global havia aprovado **42 Markdown, 83 requisitos, 17 checks e 76 etapas**. Com esta evidência presente, ele aprovou **43 Markdown, 83 requisitos, 17 checks e 76 etapas**; `git diff --check` também passou.
