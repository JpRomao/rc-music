# Instalador Windows — receita de produto

**Estado:** setups privados Lite e Completa experimental atualizados e conferidos em 2026-11-11, com ferramentas e preparação separada das extensões. Associações OFF, sem assinatura. Execução/lifecycle e gates de distribuição permanecem pendentes.
**Data:** 2026-11-05.

A [limpeza de 2026-11-11](development-commands.md#remoção-de-versões-substituídas-e-caches) preservou os dois setups de `a469f8d` e retirou os executáveis substituídos de `cf34cd8` e `26cc641`. Os recibos, as configurações e as evidências continuam disponíveis. Os registros históricos abaixo documentam a geração original.

A [entrega anterior de `2144cfd`](local-delivery-2144cfd-20261104.md) documenta os dois setups privados, Lite e Completa experimental, gerados de worktree limpa e conferidos antes da limpeza dos artefatos descartáveis. Nenhum setup foi executado ou instalado.

A [entrega Lite de `4125f46`](local-delivery-4125f46-20261104.md) registra o setup de 38.064.587 bytes, inventário/hashes, checks e limpeza posterior. As receitas de edição abaixo e os registros históricos preservam seus próprios limites.

A [entrega Completa experimental de `51cd067`](local-delivery-complete-51cd067-20261104.md) registra o setup de **38.139.221 bytes**, SHA-256 `9a59e347ae8564669780e3a03be1d289019d3f3d370f1d19fbaa8e22910657e9`, com manifesto/receipt e notices conferidos. O setup não inclui/instala Evergreen, não foi executado e mantém integração externa e downloads OFF.

## Fonte única e proveniência

[`packaging/windows`](../packaging/windows/New-RcMusicInstaller.ps1) reúne o builder, o planejador, os leitores dos componentes e os cinco templates/helpers NSIS. Os três includes históricos conservam os bytes promovidos de `3f539d6`; o script principal e `RcMusicBrowserHosts.nsh` acrescentam os opcionais de navegador. O fluxo de ownership, handoff e limpeza permanece compartilhado. LF é fixado por `.gitattributes` para conservar os bytes em outro checkout.

O produto não importa o experimento. Os caminhos antigos são encaminhadores, e a compatibilidade com a declaração v1 é explícita. O template corrente do harness histórico aponta diretamente para a fonte promovida; os snapshots de versões antigas continuam vindo de seus commits fixos. Os includes dos encaminhadores são ancorados em `__FILEDIR__`, com diretório de busca explícito, conforme a [documentação do pré-processador NSIS](https://nsis.sourceforge.io/Docs/Chapter5.html). Os encaminhadores foram inspecionados estaticamente; sua compilação não foi exercitada. A compilação direta da receita de produto está registrada abaixo.

`Get-RcMusicInstallerRecipeHashes` é o inventário único de quatorze fontes reais, com caminhos relativos à raiz e SHA-256. Planejamento, identidade do payload, builder e revalidação pós-compilação usam essa mesma lista, não hashes dos encaminhadores. O recibo mantém schema 1 e acrescenta `recipe_path_base=repository`. Corrigir somente um helper muda a identidade; runtime e recibos antigos não são reatribuídos a um commit novo.

O builder exige `-MakeNsis` explícito e confere seu hash contra a cópia byte-idêntica de `nsis-toolchain.json`. Esse recibo conserva a observação histórica, não autentica publicador nem autoriza instalação/download. PowerShell/NSIS são ferramentas de build, não dependências do player entregue.

## Setup da Completa experimental

`-Edition Lite` continua padrão; manifestos antigos sem edição são Lite. `-Edition CompleteExperimental` exige o manifesto correspondente, o pré-requisito `existing-webview2-evergreen-x64` e os três notices do [runtime](windows-runtime.md#edições-e-montagem-da-completa-experimental). Metadados divergentes ou inválidos são recusados antes do compilador. Edição e pré-requisito entram na identidade do payload e no receipt; os nove inputs da receita continuam conferidos antes/depois da compilação.

```powershell
pwsh -NoProfile -File packaging/windows/New-RcMusicInstaller.ps1 -Edition CompleteExperimental -RuntimeDirectory out/product/rc-music-local/nova-completa-experimental/app -OutputDirectory out/product/rc-music-installer/nova-completa-experimental -MakeNsis out/m0/toolchains/nsis-3.12/makensis.exe
pwsh -NoProfile -File packaging/windows/tests/InstallerEdition.Tests.ps1 -MakeNsis out/m0/toolchains/nsis-3.12/makensis.exe
```

O segundo comando usa somente `/SAFEPPO` sobre fixtures sintéticas; confere textos/guards NSIS sem gerar ou executar um setup. O compilador precisa ter o hash provisionado. Esse check manual complementa os testes puros e não é dependência do runner diário sem NSIS.

O setup informa edição, substituição da versão atual e necessidade de WebView2 já instalado para navegar; não baixa/instala esse runtime. Lite e Completa experimental compartilham a identidade, root, atalho, mutex e único payload ativo; não criam coexistência ou migração de dados. A política está no [ADR-0012 Proposto](architecture/decisions/0012-experimental-edition-packaging.md). Troca instalada, preservação física de dados, rollback/uninstall e apresentação nativa ainda precisam de provas próprias. Sem matriz explícita, associações continuam OFF; integração externa e YouTube separado permanecem pendentes.

## Catálogo factual e declaração por runtime

[`tests/audio/corpus/catalog.v1.json`](../tests/audio/corpus/catalog.v1.json) descreve sete fixtures básicas já promovidas: três MP3, AAC em M4A, FLAC, WAV PCM e Ogg/Vorbis. Registra tamanho/hash, contêiner/perfil, classificação `baseline` e capacidades exigidas. Seus bytes concordam com os pins de `RcMusicAudioFixtures.cmake`; a licença privada é preservada. Não contém resultado de execução, aprovação de runtime ou promessa de suporte público. Opus, ALAC, entradas hostis e extensão disfarçada não entram nessa elegibilidade.

Uma declaração de produto é **schema 2**, scope `windows-x64-product-local-development`, backend `QtMultimedia/FFmpeg`. Ela vincula explicitamente:

- `runtime_manifest_sha256`: o runtime exato;
- `fixture_catalog_sha256`: o catálogo de entradas;
- `test_source_sha256`: a fonte atual de `LocalAudioIntegrationTests.cpp`;
- `result_sha256`: o resultado revisado daquela execução;
- `formats`: extensões, descrições e IDs das fixtures correspondentes.

O leitor exige `validation_status=passed-local`, mas esse texto e `result_sha256` são referências de evidência revisada, não assinatura ou prova automática de aprovação. Conferir o resultado real continua obrigatório. O leitor rejeita tipos inválidos, hashes divergentes, candidatos, aliases indevidos, duplicatas e nomes inseguros; revalida também os bytes das fixtures referenciadas. Catálogo e declaração são reconferidos ao terminar o planejamento.

Na promoção original, nenhuma declaração de produto aprovada foi criada; os testes constroem declarações fictícias exclusivamente em diretórios GUID. A declaração real posterior está registrada abaixo. A matriz v1 histórica permanece inalterada, vinculada ao seu corpus/runtime. Só o entrypoint antigo ou `-LegacyBaseline` explícito escolhe essa baseline; a entrada padrão recusa v1 antes de consultar o histórico. Não basta trocar o número de schema ou recalcular hashes para aprovar outro runtime.

### Declaração local v2 validada

A [declaração `rc-music-windows-dev-3f22fda.v2.json`](../packaging/windows/formats/rc-music-windows-dev-3f22fda.v2.json) registra o resultado revisado do [smoke de codecs do runtime](windows-runtime.md#smoke-automatizado-do-runtime): 16 linhas/18 testes sem falhas ou skips. Seu SHA-256 é **`d066ac79e1f21d2d0769d1cad3ad64ef9c910e579c193197cb31d0a042a8a851`**. Ela vincula o manifesto do runtime `3f22fda`, catálogo factual, fonte do teste e `smoke-result.json` pelos quatro hashes reais, sem transferir aprovação da matriz v1.

Declara somente os perfis básicos das sete fixtures: MP3 CBR/VBR e tags Unicode, AAC LC em M4A/MP4, FLAC 16-bit, WAV PCM 16-bit e Vorbis em Ogg/OGA. As sete extensões são `flac`, `m4a`, `mp3`, `mp4`, `oga`, `ogg`, `wav`; `.audio` foi usada apenas nas cópias de teste, não declarada como associação. Isso não promete todos os codecs/perfis desses contêineres.

Leitura e planejamento reais passaram sem escrita: sete formatos/callbacks exatos, associações ON somente com argumento explícito, assinatura **`10e0c7a4087c3a1fab787b0a91d071234bf19c26d43dea71a75100d01070d6ef`** e payload ID **`15343435b378183f2ecd1fb5c46c4af61d285034cefb7dfc6c4c15d3a6104fae`**. Uma chamada com hash de outro runtime foi recusada antes de saída. Esses valores descrevem dados/configuração revisados, não execução de instalação ou alteração do Windows; o setup OFF já entregue permanece inalterado.

## Seleção opcional das ferramentas de download

Com um [runtime que declara o componente](windows-runtime.md#componente-opcional-de-downloads), o setup oferece **Ferramentas de download (opcional)**, inicialmente desmarcada, e mantém o player obrigatório. Os tamanhos exibidos separam base e ferramentas. O modo silencioso conserva a opção desligada. A opção instala ferramentas locais de aquisição/conversão; não instala a extensão, registra host no Brave ou substitui as provas de aquisição pública.

O planejador compara o inventário opcional com `download-tools/` do manifesto, valida o descritor de marcador e reconfere a consistência dos relatórios PE sem acessar os recibos/headers originais. A [montagem com hosts](windows-runtime.md#montagem-opcional-dos-dois-hosts-native-messaging) acrescenta somente os dois executáveis e dois manifestos fixos ao lado do player, mediante metadados próprios e runtime YouTube completo. Também permanecem opcionais, sem registro automático. A verificação existente dos bytes continua obrigatória. Caminhos com barras invertidas conservam a classe opcional; arquivos de ferramentas não podem virar parte obrigatória por diferença de separador.

Cada escolha tem seu próprio ID de payload, incluindo seleção, inventário efetivo, marcador lógico e proveniência do pacote. A escolha é congelada antes do preflight de instalação. Alterá-la cria outra árvore versionada e usa o fluxo existente de handoff/limpeza. O desinstalador deriva a variante somente de um dos dois diretórios compilados, conforme o [`$INSTDIR` da invocação NSIS](https://nsis.sourceforge.io/Reference/$INSTDIR), e exige o campo de ownership `DownloadComponent` coerente. Nunca escolhe arquivos apenas por um valor arbitrário do INI.

O builder materializa um arquivo de 24 bytes LF, sem sobrescrita. O setup copia e verifica os arquivos selecionados antes de extrair esse marcador e verifica novamente o payload ativado antes de publicar o ownership/estado da instalação. A escolha desligada exige ausência de marcador. Remoção e limpeza de tentativa nova enumeram somente a variante selecionada, incluindo seu marcador; diretórios desconhecidos e dados pessoais conservam o tratamento anterior.

Evidência de 2026-11-05: **135 casos** nos cinco checks focados: planejador 94, componente 15, builder 6, fontes 5 e edições 15. O último inclui quatro compilações completas de fixtures (Lite/Completa × componente ausente/disponível), sem executar os binários. Reds: componente 1/13, planejador 86/7, builder 4/2 e pré-processamento 4/6. Regressões adicionais confirmaram classificação por separadores (91/3 com tamanhos), nome real `pyinstaller-COPYING.txt` (13/2) e tamanhos separados da UI (10/1). Correções do harness prepararam o input do compilador e aceitaram a sintaxe real do pré-processador sem mudar os contratos. Logs em `out/product/test-runs/installer-download-selection-20261105/`.

Essas provas verificam planejamento, fluxo gerado e compilação. Interação real com checkbox, instalação, upgrade e remoção continuam pendentes; nenhum setup, hive de Registro, perfil AppContainer ou app foi executado por esses checks.

### Extensões principal e YouTube como opções separadas

O builder e `New-RcMusicInstallerPlan` aceitam `-MainExtensionPackageReceipt`/`-MainExtensionPackageReceiptSha256` e `-YoutubeExtensionPackageReceipt`/`-YoutubeExtensionPackageReceiptSha256`, independentes e explícitos; os caminhos dos recibos devem ser absolutos. Os pacotes precisam estar limpos e possuir [identidades públicas fixas](youtube-extension-package.md#identidades-públicas-estáveis) iguais às origens dos hosts declarados pelo runtime. O leitor verifica JSON estrito, pin, árvore fechada e hashes dos nove/sete arquivos; não segue os caminhos de proveniência. O builder revalida o pacote inteiro após compilar.

O instalador oferece **Preparar extensão principal** e **Preparar extensão YouTube**, inicialmente desmarcadas, com tamanhos próprios. Qualquer uma requer as ferramentas compartilhadas, que são selecionadas e ficam obrigatórias enquanto uma extensão estiver marcada. A seleção é congelada antes do preflight. Sem os parâmetros dos pacotes, continuam disponíveis somente as escolhas anteriores.

As máscaras válidas são 0 (base), 1 (ferramentas), 3 (principal + ferramentas), 5 (YouTube + ferramentas) e 7 (ambas + ferramentas), conforme os pacotes presentes. Cada combinação tem ID próprio. As fontes ficam em `browser-extensions/main` e `browser-extensions/youtube` no payload versionado; o inventário de extração, verificação e remoção usa a mesma seleção. `BrowserExtensionsSelection` no INI precisa conferir com a variante inferida do diretório compilado. Um INI editado não escolhe arquivos ou canais.

Somente os canais selecionados registram o valor padrão `REG_SZ` do host em HKCU, apontando ao seu manifesto absoluto. Os sufixos sob `Software` são `BraveSoftware\Brave-Browser\NativeMessagingHosts`, `Google\Chrome\NativeMessagingHosts` e `Microsoft\Edge\NativeMessagingHosts`. A chave do Edge e seu fallback estão na [documentação oficial](https://learn.microsoft.com/en-us/microsoft-edge/extensions-chromium/developer-guide/native-messaging); a integração Brave usa seu caminho próprio, também presente no [cliente Bitwarden](https://github.com/bitwarden/clients/blob/main/apps/desktop/src/main/native-messaging.main.ts). Não depende do fallback do Brave ou escreve em HKLM/políticas.

O preflight recusa valores estrangeiros antes de extrair/publicar. O registro acontece após verificar payload/ownership e publicar `PendingPayload`, já sem autoridade para apagar a cópia como tentativa fresca. Uma falha parcial conserva arquivos e estado para retry. `/VERIFY` termina sem alterar o Registro. Na remoção, após verificar ownership e antes dos arquivos, o unregistro remove apenas o default que ainda aponta ao seu próprio manifesto e a chave se estiver vazia, preservando outros valores/subchaves e alvos de versões novas.

**Preparar não carrega a extensão no navegador.** Para o pacote local, o usuário abre `brave://extensions` (ou a página equivalente Chrome/Edge), ativa o modo desenvolvedor e escolhe **Carregar sem compactação** na pasta selecionada. A tela final explica os caminhos e a necessidade de recarregar a pasta da nova versão após atualizar o player. O [Chrome restringe instalação externa comum à Web Store](https://developer.chrome.com/docs/extensions/how-to/distribute/install-extensions); o setup não força preferências, perfis ou políticas do navegador. Os engines/hosts de download continuam compartilhados no componente de ferramentas, mesmo quando só uma extensão é escolhida.

Fechamento proporcional em `out/product/test-runs/installer-extensions-20261105/`: reader **0/14 → 14/14**, novo plano **3 passados/5 falhos → 8/8**, builder **6/1 → 7/7**, helper Native **1/6 → 7/7**, regressão do plano **97/97**, fontes **6/6**, **18/18** checks de `/SAFEPPO` e **8 compilações completas** de fixtures. São **165 casos** verdes dentro desses recortes. O primeiro fechamento teve duas falhas do helper de testes por importação `-Force` e uma comparação do fim de linha do pré-processador; foram corrigidas sem relaxar os contratos, mantendo as oito compilações válidas. Não houve mudança C++/QML ou rebuild do app neste incremento.

O [harness nativo separado](../packaging/windows/tests/InstallerExtensions.Native.ps1) exige explicitamente `-PrepareOnly` ou `-RunPrepared` com recibo/hash revisado. A preparação compila cinco setups sintéticos com a receita corrente e mudanças de seleção somente em cópias privadas do template. Ela concluiu sem executar setup, abrir Registro ou alterar o produto: recibo `out/product/test-runs/installer-extensions-native/4c0a15fe23fa43e7a8367120a41f99f9/prepared.json`, SHA-256 `2b6be63ecfe3befcb9d05c014cda4ee3967d881e3a7d26dd2f8650d7e10f8517`.

A execução preparada ainda exige autorização específica: testa as cinco combinações, `/VERIFY`, upgrade principal → YouTube e remoção/preservação de valores alheios, somente sob `HKCU\Software\RCMusicExtensionTests\4c0a15fe23fa43e7a8367120a41f99f9` e sua árvore GUID em `out/product/test-runs`. Não inicia app/hosts/navegador, mídia, UAC ou AppContainer. Registro real, checkbox físico e jornada Brave continuam pendentes; compilar não os comprova nem autoriza aquisição pública.

### Setups atualizados em 2026-11-11

A revisão `a469f8d` reúne os [runtimes atualizados](windows-runtime.md#runtimes-atualizados-em-2026-11-11) e as duas [extensões reempacotadas](youtube-extension-package.md#pacotes-atualizados-em-2026-11-11). Inclui a seleção dos formatos de letra online, o indicador de capacidade dos downloads e os novos avisos nas extensões. Ferramentas e preparação de cada extensão são opcionais.

| Edição | Diretório em `out/product/rc-music-installer/` | Bytes de `RCMusic-Setup.exe` | SHA-256 |
|---|---|---:|---|
| Lite | `rc-music-a469f8d-extensions-Lite-20261111` | 89.969.454 | `6d03dd546f13371a1e047ab3bcd06bd1a7e87fa97325c91dfb08ece48bc2550c` |
| Completa experimental | `rc-music-a469f8d-extensions-CompleteExperimental-20261111` | 90.041.357 | `20fdb045a65d78dbd23f301af52e966dbbb57556178c3404ef3b513002397dae` |

A inspeção passou para 619/622 arquivos planejados, cinco variantes de seleção, recibos, hashes, associações OFF e setups PE x86 GUI sem assinatura. A leitura com 7-Zip confirmou os players, hosts, manifestos e popups embutidos nas duas edições e o notice WebView2 na Completa. Evidências em `out/product/test-runs/windows-candidates-a469f8d-20261111/`. Os setups aguardam aceitação instalada.

### Setups atualizados em 2026-11-10

A receita limpa `26cc641` empacotou os [runtimes atualizados](windows-runtime.md#runtimes-atualizados-em-2026-11-10), preservando os dois candidatos anteriores. As duas edições incluem a adição direta de arquivos às playlists e a correção da fila restaurada. Ferramentas e preparação das extensões principal e YouTube continuam opcionais.

| Edição | Diretório em `out/product/rc-music-installer/` | Bytes de `RCMusic-Setup.exe` | SHA-256 |
|---|---|---:|---|
| Lite | `rc-music-26cc641-extensions-Lite-20261110` | 89.938.956 | `913eed0eada6a1ed66f810b89dff99e5a5bf7b631257bb43038b79dce33c656a` |
| Completa experimental | `rc-music-26cc641-extensions-CompleteExperimental-20261110` | 90.004.365 | `3b975657b7a0ced4f9447759bd0d430b54846ef9da6b9f117a52e3c92e525a61` |

O verificador conferiu recibos, fontes da receita, inventários de 619/622 arquivos, cinco variantes de seleção e PE x86 GUI dos setups. A leitura dos arquivos embutidos por 7-Zip confirmou os hashes dos players novos, dos dois hosts e dos manifestos das extensões. Na Completa, também confirmou o notice WebView2. Evidências em `out/product/test-runs/windows-candidates-26cc641-20261110/`. Os setups aguardam execução e aceitação instalada.

### Setups com extensões preparados em 2026-11-05

A receita limpa `cf34cd8daf185fa65a31d3fcd158bfccd21258fa` gerou os setups sobre os runtimes e pacotes-fonte preservados de `7601247`, sem rebuild do player. Cada um contém cinco variantes, 49 arquivos de ferramentas/hosts e 16 assets de extensões, com três opções inicialmente desmarcadas, associações OFF e sem assinatura. A conferência independente passou para setup, configuração, marcador, 14 fontes da receita, recibos de componentes e todos os arquivos de entrada.

| Edição | Diretório em `out/product/rc-music-installer/` | Bytes de `RCMusic-Setup.exe` | SHA-256 |
|---|---|---:|---|
| Lite | `rc-music-cf34cd8-extensions-Lite-20261105` | 89.936.126 | `92c312f516bc1f59380901c1886c8aedec567e9d5b64955409d98e122a91cb96` |
| Completa experimental | `rc-music-cf34cd8-extensions-CompleteExperimental-20261105` | 90.001.880 | `c52aa68c43c9d0455609dda22d5b495a1afb3c7ee4b2e5cc1ee85d32f29f11c2` |

Recibos SHA-256: Lite `978dd56f1cd162f5fdc7c71a768fd148855591dd1b4ccd2a9f4f3ecc038c03ba`; Completa `db787f580133fc7ad1a909490a558786cebf58e5b8d2e3e128796e7eecb5c407`. O inventário reúne 619/622 arquivos, respectivamente. `out/product/test-runs/installer-extensions-20261105/setup-verification.json` registra a conferência, incluindo PE x86 GUI do setup e ausência de assinatura. Os runtimes de origem continuam sem marcador; nenhum setup foi executado. Candidatos anteriores e trabalho AppContainer permanecem preservados.

### Setups locais de 2026-11-05

Os runtimes preservados de `bbff186` foram empacotados pela receita limpa `54e25a926bd2d6cb20a16eef5a84888cf040423b`, sem recompilar o player. Os dois setups têm componente disponível com seleção inicial desligada, IDs base/downloads distintos, 45 ferramentas e o input do marcador de 24 bytes. Associações permanecem OFF.

| Edição | Diretório em `out/product/rc-music-installer/` | Bytes de `RCMusic-Setup.exe` | SHA-256 |
|---|---|---:|---|
| Lite | `rc-music-54e25a9-tools-Lite-20261105` | 89.720.105 | `8740ec58ea599118c8ae4fd856401337e688c818a3eab1062147e36da1a72533` |
| Completa experimental | `rc-music-54e25a9-tools-CompleteExperimental-20261105` | 89.784.654 | `4b9ec92bb2574664854be6f112f87f2cbff0315c9ef09972b0e5cce8d4021c2b` |

SHA-256 dos recibos: Lite `c97c236c3cd49903dee46946a2ae13b2c59a038b23a0ceaf0988470632c44682`; Completa `9047b6a78f6f936701e88fb003d06f8bdacf3d299723238e682825ff64ea9468`. A conferência independente vinculou os 11 inputs de receita, setup, configuração, marcador gerado e os manifestos dos runtimes. Os runtimes de origem continuam sem marcador; não houve instalação ou execução dos setups. Authenticode retornou `NotSigned` para ambos. A evidência fica em `out/product/test-runs/installer-download-selection-20261105/delivery-verification.json` e `signatures.json`.

Estes são candidatos privados para revisão. O lifecycle instalado e o checkbox em UI real ainda não foram exercitados; extensão, host Brave e aquisição pública permanecem etapas próprias. Dados pessoais, SDKs, candidatos anteriores e as seis alterações preexistentes foram preservados.

<a id="checks-sem-compilação-ou-instalação"></a>

## Checks sem instalação

O guard introduzido em `67af88e` recusava todo componente enquanto o inventário era obrigatório único. Agora são aceitos somente metadados e inventários válidos para a seleção acima; declarações inválidas e ferramentas sem declaração continuam recusadas antes da saída.

Regressão de 2026-11-05: Red **71 passados/16 falhos**, Green **87/87** em `WindowsInstallerPlan.Tests.ps1`, com fontes/manifestos preservados e zero saída nas recusas. Os testes usam arquivos sintéticos, sem executar NSIS, setup ou Registro. Logs em `out/product/test-runs/component-activation-20261105/installer-{red,green}.log`.

Na raiz, PowerShell 7 x64:

```powershell
pwsh -NoProfile -File packaging/windows/tests/FormatMatrix.Tests.ps1
pwsh -NoProfile -File packaging/windows/tests/WindowsInstallerPlan.Tests.ps1
pwsh -NoProfile -File packaging/windows/tests/InstallerSources.Tests.ps1
pwsh -NoProfile -File packaging/windows/tests/InstallerRecipe.Tests.ps1
pwsh -NoProfile -File packaging/windows/tests/InstallerComponent.Tests.ps1
pwsh -NoProfile -File packaging/windows/tests/InstallerEdition.Tests.ps1 -MakeNsis out/m0/toolchains/nsis-3.12/makensis.exe -Compile
```

`-IncludeLegacy` nos três primeiros acrescenta compatibilidade histórica. Os cinco primeiros não executam NSIS; o último usa o compilador pinado para pré-processar e compilar fixtures sem iniciar os setups. Usam arquivos sintéticos, cópias próprias das fixtures licenciadas e exportações sem `experiments`, a partir de diretório corrente alheio. OFF não precisa de catálogo/corpus/fonte de teste. O preflight do builder recebe um arquivo de texto como compilador e deve rejeitá-lo pelo hash antes de tentar executá-lo ou criar saída.

As quatro entradas padrão integram agora `develop.ps1 check` e `validate`, junto aos quatro checks puros do runtime. Ordem, isolamento e evidência ficam no [guia do runner](development-commands.md#checks-das-receitas-windows). `-IncludeLegacy` e os harnesses nativos históricos de arquivos, associações e lifecycle continuam separados; esta integração não executa esses gates nativos.

## Montagem real, somente quando necessária

Depois de montar/verificar um [runtime local](windows-runtime.md), com fontes commitadas e compilador já provisionado, o comando abaixo compila um setup privado sem executá-lo. Escolha uma saída nova; o exemplo não habilita associações:

```powershell
pwsh -NoProfile -File packaging/windows/New-RcMusicInstaller.ps1 -RuntimeDirectory out/product/rc-music-local/nova-revisao/app -OutputDirectory out/product/rc-music-installer/nova-revisao -MakeNsis out/m0/toolchains/nsis-3.12/makensis.exe
```

O caminho do compilador no exemplo é o provisionamento local existente, não dependência de código/fixtures M0. Outro ambiente fornece explicitamente seu caminho verificado. `-FormatMatrix` somente deve apontar para uma declaração revisada para aquele runtime; sem argumento, associações ficam OFF. O diretório de saída deve ser novo sob `out/product` ou `out/m0` para compatibilidade. Falhas podem deixar saída parcial sem receipt aprovado; não há rollback, reutilização ou limpeza automática.

## Instalador atualizado de bb826b4

Em 2026-11-03, o [runtime do commit limpo `bb826b4`](windows-runtime.md#runtime-atualizado-de-bb826b4) gerou `out/product/rc-music-installer/rc-music-windows-dev-bb826b4-20261103/RCMusic-Setup.exe`. A compilação direta usou NSIS já provisionado, sem `-FormatMatrix`, com exit 0, `recipe_dirty=false` e associações OFF. As 17 alterações de desenvolvimento do backend Media Foundation foram preservadas fora da worktree de origem e não integram esse pacote.

- Setup: **38.062.463 bytes**, SHA-256 `b7b48018a91003aa6a575af6b5b2df9686d9c87a280c5e82821e1e8f39229a70`, Authenticode **NotSigned**.
- Receipt: SHA-256 `4652e1758101550e59820a4d70057c9d3b7c1f761e2bdba8acb3f63eb64ba3d6`.
- Configuração: SHA-256 `495cec5d752ae53502b64cf8820ddfeaad4b57cf170ea1bdabf8a7b3e40c1622`.

A verificação independente conferiu os 554 arquivos físicos, cinco fontes da receita do runtime, nove do instalador, compilador e recibos. O replano OFF sem escrita reproduziu a configuração completa e payload ID `fc14f97fd2b35c879f45d6af0f4817d005a3e83b64dc3f265866f089c5e66c03`. O stub NSIS é x86 GUI; o player é x64 GUI, sem console. Helpers remotos, engine de navegação, extensões e CLI de conversão estão ausentes do payload.

Parser PE **5/5**, fontes NSIS **5/5**, receita **3/3**, Release, lint e grafo de produção passaram. Inventário/hashes foram reconferidos após a [limpeza de 307 MB](development-commands.md#limpeza-após-atualização-de-bb826b4). Evidências em `out/product/installer-refresh-bb826b4-20261103/`, incluindo `independent-verification.json`, `after-cleanup-verification.json` e logs de build. Nenhum setup/player foi iniciado; não houve instalação, Registry, benchmark ou smoke novo de áudio. Lifecycle, assinatura e demais gates de distribuição permanecem pendentes.

## Instalador atualizado de c2d7c88

Em 2026-11-03, a receita e o [runtime do commit limpo `c2d7c88`](windows-runtime.md#runtime-atualizado-de-c2d7c88) geraram `out/product/rc-music-installer/rc-music-windows-dev-c2d7c88-20261103/RCMusic-Setup.exe`. NSIS já provisionado, exit 0, `recipe_dirty=false`, associações OFF e nenhuma matriz antiga reaproveitada. Os dois arquivos parciais de testes da extensão ficaram fora da worktree usada na montagem.

- Setup: **38.062.449 bytes**, SHA-256 `9c7342ed6956521fce7191f3561ac2a9c9e1e8bc8195bde036e3968c371e224a`, Authenticode **NotSigned**.
- Receipt: SHA-256 `4ea1548583883409e439369ccc5517935c2196f1e3bfe9d2325a9d45b02d6181`.
- Configuração: SHA-256 `e4800df775f3989ae7d6ef973c241749f083a41de6202112b96c6bfe85d20717`.

A conferência independente validou os 554 arquivos físicos, as cinco fontes de receita do runtime, nove do instalador, compilador e recibos. O replano OFF sem escrita reproduziu payload ID `fddc5983762ee1c1633301a346ee0ff8828eac1d8953b0403172bbfc9d53ff44` e configuração integral, normalizando apenas o `OutFile` para o destino original de build registrado. Os três arquivos do setup foram movidos para a saída final sem alterar bytes; `installer-relocation.json` vincula os dois paths e hashes. O stub NSIS é x86 GUI; o player e seu runtime são x64.

Checks puros: parser PE **5/5**, fontes NSIS **5/5**, receita do instalador **3/3**. Release/lint/grafo de produção passaram separadamente; a verificação completa foi repetida após a [limpeza](development-commands.md#limpeza-após-atualização-de-c2d7c88). Evidência ignorada em `out/product/installer-refresh-c2d7c88-20261103/`, especialmente `independent-verification.json` e `after-cleanup-verification.json`. Nenhum setup/player foi executado, instalado ou publicado, sem alteração de Registry/associações. O pacote entrega o player local com opcionais remotos OFF; assinatura, lifecycle e demais gates de distribuição continuam pendentes.

## Instalador atualizado de f589040

Em 2026-11-03, o [runtime atualizado](windows-runtime.md#runtime-atualizado-de-f589040) e a receita do checkout limpo `f5890405e103d29591c5aca871318446ec6b0395` geraram o novo setup, com as ferramentas provisionadas:

```powershell
pwsh -NoProfile -File packaging/windows/New-RcMusicInstaller.ps1 -RuntimeDirectory out/product/rc-music-local/rc-music-windows-dev-f589040-20261103/app -OutputDirectory out/product/rc-music-installer/rc-music-windows-dev-f589040-20261103 -MakeNsis out/m0/toolchains/nsis-3.12/makensis.exe
```

Compilação exit 0, associações OFF, `recipe_dirty=false`, sem matriz de formatos antiga. Conferência independente de inventário, cinco hashes da receita do runtime, nove do instalador, compilador, configuração e manifesto passou. Replanejamento sem escrita reproduziu o inventário, a configuração e o payload ID `8abc0d1a46b3303dd4fc1826a269ce5623aeafb8a6ccd76f7bfffaaa6464fa4e`.

- `RCMusic-Setup.exe`: **38.061.983 bytes**, SHA-256 `0b24d602a084f20373e44a8a49656fcb52940a96a5233263ab9e935af105ae9f`, Authenticode **NotSigned**.
- `installer-receipt.json`: SHA-256 `4d123b946ad60a46365297d5d9fad136596561cbc32a3ceba2816830ed011421`.
- `installer-config.nsh`: SHA-256 `a95a95fa65bfde1ec8c5b3542eeaa29f54ec752f1bd19e46fbe675a5dc2c5a4f`.

Checks puros proporcionais: parser PE **5/5**, fontes NSIS **5/5**, receita do instalador **3/3**. Evidência em `out/product/installer-refresh-f589040-20261103/independent-verification.json`; build/lint e [limpeza posterior](development-commands.md#limpeza-após-atualização-de-f589040) têm registros próprios. Setup e runtime novos permaneceram íntegros após a limpeza. Nenhum setup/player foi executado, instalado ou publicado, nem houve mudança de Registry/associações. O pacote contém o player local; módulos remotos continuam OFF. Lifecycle, assinatura e demais gates de distribuição permanecem pendentes.

## Instalador local gerado em 2026-11-03

O [runtime Release/OFF do commit limpo `531c5cc`](windows-runtime.md#runtime-local-gerado-em-2026-11-03) alimentou o novo setup privado. A geração isolada preservou integralmente os 14 arquivos pendentes do checkout principal. A receita do instalador permaneceu limpa, no mesmo commit, e o NSIS existente foi conferido pelo pin de `nsis-toolchain.json`.

```powershell
pwsh -NoProfile -File packaging/windows/New-RcMusicInstaller.ps1 -RuntimeDirectory out/product/rc-music-local/rc-music-windows-dev-531c5cc-20261103/app -OutputDirectory out/product/rc-music-installer/rc-music-windows-dev-531c5cc-20261103 -MakeNsis out/m0/toolchains/nsis-3.12/makensis.exe
```

A compilação terminou com exit 0, sem `-FormatMatrix`, execução de setup ou instalação. O diretório de saída era novo. A verificação independente conferiu:

- `RCMusic-Setup.exe`: **38.052.800 bytes**, SHA-256 `6c762e7267218fa8d82cc5b7bd7783c4c3bb6f2fe1ef9227a2fdec0a93ff4f73`, Authenticode **NotSigned**.
- `installer-receipt.json`: SHA-256 `2d93f4f236591e093e67263b266ad54ac79f9f76b5b501b52613a88eb3069097`.
- `installer-config.nsh`: SHA-256 `f89b7102090aa55b6e31dce09a4f4d6c851ddba92c424b05b60b23d7661d0bdb`.
- Inventário físico dos **554 arquivos**, manifesto, cinco fontes de receita do runtime, nove do instalador e hash do compilador correspondentes ao receipt.
- Replanejamento sem escrita reproduziu o payload ID `dd660783590b02c19de9f78043da1a0cb672f8b46293e5755a0bec4e35b81974`, associações OFF, formatos/callbacks vazios e `format_matrix=null`.

Evidência local ignorada: `out/product/cleanup-20261103/installer-verification.json`. O novo setup e runtime foram preservados na [limpeza subsequente](development-commands.md#limpeza-após-build-do-instalador--2026-11-03). Nenhum setup foi executado, Registry/associação alterado ou componente de navegador instalado. O pacote contém o player local do commit `531c5cc`; a integração remota pendente não foi ativada. Instalação/lifecycle, máquina limpa, assinatura, segurança/licenças e distribuição continuam gates separados.

## Instalador local gerado em 2026-10-24

A receita de produto compilou, sem executar o setup, o [runtime atualizado](windows-runtime.md#runtime-local-gerado-em-2026-10-24) em `out/product/rc-music-installer/rc-music-windows-dev-1c79d81-20261024/RCMusic-Setup.exe`. Receita e runtime vinculam o commit limpo `1c79d8187e8acd0cb9d0d295640f22f1e21ea25f`; `recipe_dirty=false`, runtime Release/x64 com `BUILD_TESTING=OFF` e associações OFF. Não foi fornecida matriz de formatos nem transferida a declaração do runtime antigo.

```powershell
pwsh -NoProfile -File packaging/windows/New-RcMusicInstaller.ps1 -RuntimeDirectory out/product/rc-music-local/rc-music-windows-dev-1c79d81-20261024/app -OutputDirectory out/product/rc-music-installer/rc-music-windows-dev-1c79d81-20261024 -MakeNsis out/m0/toolchains/nsis-3.12/makensis.exe
```

O destino era novo e o comando terminou com exit 0. A revisão independente somente leitura confirmou:

- Setup: **38.052.419 bytes**, SHA-256 `ff809cde4ffd6ad608e455ea6f9ed0155f120c8411f97ec2e0a52ef4b8756009`.
- `installer-receipt.json`: SHA-256 `f8a23026510da3ec06ab4b0d64219b563dc4ac9f9f530cfd84fdb8ed8322da07`.
- `installer-config.nsh`: SHA-256 `9862fd803b5eeafebd7aa98c833edbf0636cc247fed6907d208549958f1ff519`.
- Manifesto do runtime: SHA-256 `d09940046752a96968d291976243bac050b5c62c3113a937cee103ca88caf113`.
- Inventário físico de **554 arquivos / 96.301.175 bytes**, coincidente com manifesto/receipt; cinco hashes de receita do runtime, nove da receita do instalador e compilador NSIS conferidos.
- Replanejamento sem escrita reproduziu o payload ID `8ea215cfd0e5824b1f6e655033c2c40dcddaa67aff1b3ec5e767a9d249e46c5c`, formatos vazios, assinatura do conjunto vazio e `format_matrix=null`.
- Authenticode **`NotSigned`**; executável do player com subsistema Windows GUI (2).

O [registro do runtime](windows-runtime.md#runtime-local-gerado-em-2026-10-24) documenta o build aprovado e as seis falhas de criação de junction por `Acesso negado` nos checks puros. Não houve execução do novo player/setup, instalação, Registry, áudio, Brave/extensão ou publicação. O artefato permanece privado/experimental; lifecycle, máquina limpa, assinatura, segurança/licenças e distribuição continuam pendentes. A limpeza posterior preservou o novo setup, os setups históricos e os respectivos receipts.

## Instalador local gerado em 2026-09-06

Após autorização explícita para **gerar sem instalar**, a entrada de produto compilou o [runtime atualizado](windows-runtime.md#runtime-local-gerado-em-2026-09-06) do commit limpo **`3f22fda7ae58e6ca2a2bf0587c8f0a620ad44cb9`**, usando NSIS 3.12 já provisionado e conferido pelo recibo. Comando executado com exit 0, sem `-FormatMatrix` e em destino então inexistente:

```powershell
pwsh -NoProfile -File packaging/windows/New-RcMusicInstaller.ps1 -RuntimeDirectory out/product/rc-music-local/rc-music-windows-dev-3f22fda/app -OutputDirectory out/product/rc-music-installer/rc-music-windows-dev-3f22fda -MakeNsis out/m0/toolchains/nsis-3.12/makensis.exe
```

Conferência posterior somente leitura do artefato, configuração e recibo:

- `RCMusic-Setup.exe`: **37.980.884 bytes**, SHA-256 **`83166f6cb8d056ceb283ec781046cda45c2b77253672e5d3eee053c356e12ea2`**;
- `installer-receipt.json`: SHA-256 **`33f20a4c55ba4e1082884299decd13d1eabaff83660a9a2b3dbdd73a46778339`**, receita/runtime vinculados ao commit acima e `recipe_dirty=false`;
- inventário de **554 arquivos** coincide com o manifesto do runtime; nove hashes de fontes e hash do compilador coincidem com os inputs atuais. Hashes do setup, configuração e manifesto foram reconferidos;
- associações **OFF**, lista/callbacks de formatos vazios e assinatura do conjunto vazio conferida independentemente; nenhum corpus/matriz histórico foi usado para inferir formatos;
- replanejamento sem escrita reproduziu o payload ID **`ca04ad5303fd47c690b5a6cffa1075ffad59266e21fb30d8383498a8cfd762e9`**;
- Authenticode: **`NotSigned`**. Artefatos privados e ignorados pelo Git, sem publicação.

Essa execução comprova compilação da receita direta e consistência dos artefatos, não instalação, atualização, remoção, launch ou playback. Na geração OFF nenhum setup/player/probe foi executado, nenhum Registry/menu/associação foi alterado e nenhum runtime/pacote anterior foi sobrescrito. `InstallerBuild.Tests.ps1` histórico não foi executado: o builder de produto foi chamado diretamente, seguido das verificações acima. A validação local posterior dos perfis e o setup separado constam abaixo; lifecycle, CRT/máquina limpa, segurança/licenças e validações nativas seguem abertos, sem release aprovada.

## Instalador com formatos v2

Após o smoke e o commit da declaração **`cc1de1c4145af1c81c840d4f4689ffa4955d69db`**, a mesma receita de produto compilou um setup separado com `-FormatMatrix` explícito. Não foi remontado o runtime nem substituído o setup OFF. Comando executado em fonte limpa, exit 0:

```powershell
pwsh -NoProfile -File packaging/windows/New-RcMusicInstaller.ps1 -RuntimeDirectory out/product/rc-music-local/rc-music-windows-dev-3f22fda/app -OutputDirectory out/product/rc-music-installer/rc-music-windows-dev-3f22fda-formats-v2 -MakeNsis out/m0/toolchains/nsis-3.12/makensis.exe -FormatMatrix packaging/windows/formats/rc-music-windows-dev-3f22fda.v2.json
```

Conferência posterior somente leitura aprovada:

- setup **37.989.715 bytes**, SHA-256 **`bcdd56db5fc3a0c71c90357f5e5a4017d09cb89a730c48c73a9c275040f60f2a`**;
- recibo SHA-256 **`6f67139c7b168b08e26aca49ae3515ba06356bf347d77194518986966ca939d4`**; receita `cc1de1c`, runtime `3f22fda`, `recipe_dirty=false`;
- nove hashes de fontes, compilador, manifesto, declaração/resultado e 554 arquivos do runtime coincidem com os inputs; configuração compilada contém exatamente os sete callbacks, associações ON e a assinatura da declaração;
- replanejamento sem escrita reproduziu inventário, declaração completa, formatos, assinatura e payload ID registrados; nenhuma saída de verificação foi criada;
- Authenticode **`NotSigned`**; o setup OFF conserva SHA-256 `83166f6cb8d056ceb283ec781046cda45c2b77253672e5d3eee053c356e12ea2`.

ON aqui significa código/configuração de cadastro incluídos no setup, **não cadastro executado ou app escolhido como padrão no Windows**. Nenhum setup foi iniciado; não houve install/upgrade/uninstall, mudança de Registry/associações, Computer Use ou publicação. A versão continua privada/experimental, restrita aos perfis locais testados, e exige seus gates de lifecycle, integração nativa e distribuição.

## Evidência e limites da promoção original

- Planner: baseline **20/20**; Red arquitetural por entrada promovida ausente; Green **25/25** produto e **26/26** com legado. Após integrar o leitor final, **26/26** novamente. Exportações ON/OFF não dependem do experimento; modificar somente um helper copiado muda seu hash e o ID, sem modificar a origem.
- Leitor: Red inicial **2 passes/11 falhas**, Green **14/14** produto e **15/15** com legado. Red escalar **13 passes/2 falhas** reproduziu aceitação de schema textual; ambos os modos agora exigem inteiro. Catálogo, aliases, tipos e hashes têm regressões próprias.
- Fontes NSIS: Red **0/5**, Green **5/5** produto e **9/9** com encaminhadores, apenas leitura/hash/exportação. Recibo da toolchain preservado por SHA-256.
- Builder: caracterização **3/3**, cobrindo export OFF, compilador explícito, rejeição antes de execução/saída e inventário compartilhado antes/depois da compilação.

Não houve runtime/instalador novo, execução de NSIS, install/uninstall, Registry, atalhos reais, áudio/UI/CTest global, build C++, benchmark ou Computer Use. A compilação/preprocessamento e o lifecycle da receita promovida ainda precisam de execução própria; os resultados históricos não foram transferidos. Segurança, licenças/notices/SBOM, assinatura, CRT/máquina limpa, declaração de formatos do runtime novo e integração nativa permanecem gates abertos. Esta promoção não conclui M1, M4.6/M4.8 ou release.
