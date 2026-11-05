# Manifesto candidato do host YouTube

O gerador prepara um manifesto Native Messaging para `com.rcmusic.youtube` e um recibo local com estado `installation_pending`. Ele exige o ID explícito da extensão, um recibo confiável do build do host e o executável correspondente. Não executa o host nem altera o Registro, o instalador ou o Brave.

O resultado contém dois JSONs. A [receita do runtime](windows-runtime.md#montagem-opcional-dos-dois-hosts-native-messaging) agora pode montar os dois hosts e manifestos portáveis com o player/DLLs, mediante recibos pinados e configuração coerente. Este gerador separado conserva seu manifesto de path absoluto. Empacotamento/identidade das extensões, registro, instalação e ativação continuam pendentes.

## Entradas necessárias

Use PowerShell 7 e preencha estes valores a partir dos artefatos revisados:

| Parâmetro | Entrada |
| --- | --- |
| `BuildReceipt` | Caminho absoluto do recibo de build do host YouTube. |
| `BuildReceiptSha256` | SHA-256 esperado desse recibo, com 64 dígitos hexadecimais minúsculos. |
| `ExtensionId` | ID exato da extensão, com 32 letras minúsculas de `a` a `p`. |
| `HostExecutable` | Caminho absoluto de um executável existente, original ou cópia com mesmo basename, tamanho e hash do recibo. |
| `OutputDirectory` | Diretório novo abaixo de `out/product`. Na CLI, um caminho relativo é resolvido a partir do repositório do próprio script. |

A confiança no produtor do recibo é uma pré-condição. O pin verifica que os bytes lidos correspondem ao documento escolhido; ele não autentica o publicador nem comprova lançamento pelo Brave. O [recibo do perfil compilado](native-host-build-profile.md) descreve seu produtor e os campos que vinculam o executável à configuração do build.

O recibo precisa identificar `youtube.audio.v1`, `com.rcmusic.youtube` e um perfil habilitado. A origem compilada deve coincidir exatamente com `chrome-extension://` + `ExtensionId` + `/`. Perfis desabilitados, da extensão principal, com wildcard ou divergentes são recusados. Os defaults de build continuam desabilitados.

O arquivo original indicado em `executable.path` pode já ter sido removido, desde que a cópia explícita em `HostExecutable` corresponda ao recibo. Se o header em `configuration.path` ainda existir, seu hash também precisa coincidir. Quando ele estiver ausente, o candidato registra `configuration_present: false`, preservando o caminho e o hash declarados pelo recibo confiável.

## Uso pela CLI

Na raiz do repositório, substitua as strings entre `<...>` pelos valores revisados. Os placeholders abaixo não são um ID ou pin de produção:

```powershell
$buildReceipt = '<caminho absoluto do recibo confiável do host YouTube>'
$receiptPin = '<SHA-256 esperado do recibo, 64 caracteres minúsculos>'
$extensionId = '<ID exato de 32 letras mostrado para a extensão escolhida>'
$hostExecutable = '<caminho absoluto do host existente escolhido para o pacote>'

pwsh -NoProfile -File .\packaging\windows\New-RcMusicYoutubeNativeManifest.ps1 `
    -BuildReceipt $buildReceipt `
    -BuildReceiptSha256 $receiptPin `
    -ExtensionId $extensionId `
    -HostExecutable $hostExecutable `
    -OutputDirectory out/product/youtube-native-manifest-reviewed-01
```

Escolha uma saída que ainda não exista. O gerador não sobrescreve nem remove candidatos anteriores. Se houver falha após criar a saída, o diretório parcial é preservado para revisão.

## Inspecionar o plano antes de gravar

Na API do módulo, todos os caminhos devem ser absolutos, incluindo `RepositoryRoot` e `OutputDirectory`:

```powershell
Import-Module .\packaging\windows\RcMusicYoutubeNativeManifest.psm1 -Force
$repo = (Get-Location).Path
$output = Join-Path $repo 'out/product/youtube-native-manifest-reviewed-02'
$plan = New-RcMusicYoutubeNativeManifestPlan -RepositoryRoot $repo `
    -BuildReceipt $buildReceipt -BuildReceiptSha256 $receiptPin `
    -ExtensionId $extensionId -HostExecutable $hostExecutable -OutputDirectory $output
$plan
```

O plano apenas lê e valida as entradas. Depois de revisá-lo, a cópia revalida seus dados e os arquivos antes de criar a saída:

```powershell
Copy-RcMusicYoutubeNativeManifestPlan -Plan $plan
```

## Conteúdo gerado

```text
youtube-native-manifest-reviewed-01/
  com.rcmusic.youtube.json
  native-manifest-receipt.json
```

O manifesto Chromium tem exatamente cinco campos: `name`, `description`, `path`, `type` e `allowed_origins`. O nome é `com.rcmusic.youtube`, o tipo é `stdio`, o caminho aponta para o `HostExecutable` escolhido e a lista de origens contém somente a origem exata validada. O executável não é copiado para esse diretório.

O recibo do candidato registra `installation_state: installation_pending`, o pin do recibo de build, os metadados do host escolhido, a origem, o endpoint, as listas de imagens, o estado do header original e os hashes das receitas e do manifesto produzido. Esses dados permitem revisar a consistência dos arquivos; não autorizam configuração do app ou instalação.

O parser aceita somente o schema fechado do recibo de build, com UTF-8 estrito, até 64 KiB e profundidade máxima 8. Campos duplicados, tipos incorretos e listas de imagens inválidas são recusados. Caminhos de dispositivo, UNC, nomes DOS reservados e redirecionamentos por reparse points também são recusados. Na materialização, os inputs são mantidos abertos para leitura sem compartilhamento de escrita ou exclusão, e as saídas usam criação sem sobrescrita. Essas medidas atendem ao fluxo local revisado; não oferecem proteção geral contra um host malicioso do mesmo usuário.

## Identidade e composição ainda pendentes

O ID real da extensão no Brave permanece uma entrada pendente desta entrega. O [pacote de fontes YouTube](youtube-extension-package.md) não inventa uma `key`. Para uma extensão carregada como unpacked, uma cópia em outro caminho pode ter outro ID; escolha primeiro a pasta que será usada e confira o ID mostrado pelo navegador. O perfil compilado do host e a composição do player precisam corresponder a essa identidade. Um ID sintético dos testes não deve ser usado como identidade de produção.

O peer check resolve as imagens em relação ao diretório do executável em execução. O layout final precisa manter o host e `RCMusic.exe` como irmãos, com a composição correspondente nos dois lados. O recibo isolado do host não comprova essa correspondência nem torna o diretório candidato autossuficiente.

As DLLs também precisam entrar no inventário e na análise de dependências PE já existente na [receita de runtime Windows](windows-runtime.md#implementação-única). Este gerador não monta nem verifica essa coleção de dependências. Registro do host, integração opcional com NSIS, ativação no Brave, licenças, segurança e distribuição continuam em seus próprios gates.

## Evidência dos testes

O Red observado teve **0/14 casos aprovados** contra os stubs; o Green passou **14/14**. Os testes usam recibos e arquivos locais próprios, incluindo bytes sintéticos que deliberadamente não são executáveis PE. Nenhum host é executado por essa suíte.

Uma revisão encontrou aceitação indevida de LF ao fim de endpoints e nomes de imagens. As regressões de LF/CRLF produziram **12 passagens e 2 falhas**; após exigir o fim absoluto da string nas validações, o Green voltou a **14/14**.

Os casos verificam pins, identidade, schema, limites, paths, correspondência dos bytes, perda dos originais do build, alterações após o plano e preservação de fontes e saídas. Esse resultado comprova o gerador de candidatos; não constitui validação de instalação, navegador, dependências do runtime ou aquisição pública de mídia.
