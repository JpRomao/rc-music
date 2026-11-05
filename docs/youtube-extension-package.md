# Pacote opcional da extensão YouTube

O gerador prepara um diretório separado com as fontes executáveis da extensão YouTube e um recibo dos arquivos copiados. Esse diretório pode ser consumido por uma etapa posterior de empacotamento. Ele ainda não representa uma extensão instalada e funcional no Brave.

O componente permanece com `identity_state: identity_pending`. O gerador não inventa ID ou chave da extensão, não produz um manifesto Native Messaging ativável e não instala nem registra o host `com.rcmusic.youtube`. Também não altera o instalador NSIS, o Registro do Windows ou o navegador. A extensão principal permanece separada.

## Gerar um candidato

Na raiz do repositório, com PowerShell 7, escolha um nome de saída que ainda não exista:

```powershell
pwsh -NoProfile -File .\packaging\windows\New-RcMusicYoutubeExtensionPackage.ps1 `
    -OutputDirectory out/product/youtube-extension-20261105-01
```

O caminho relativo é resolvido a partir do repositório do próprio script, mesmo quando o comando é chamado de outra pasta. A saída precisa ficar abaixo de `out/product`. O gerador recusa uma saída existente; não sobrescreve nem remove candidatos anteriores.

O resultado tem esta estrutura:

```text
youtube-extension-20261105-01/
  extension/
    manifest.json
    native-client.mjs
    policy.mjs
    popup-controller.mjs
    popup.css
    popup.html
    popup.mjs
  package-receipt.json
```

Os sete arquivos vêm de `extensions/chromium/youtube`, sem alterações. README, arquivos de teste, outros arquivos da pasta e a extensão principal não entram no conteúdo copiado. O `manifest.json` listado acima é o manifesto da extensão Chromium; ele não é um manifesto de host Native Messaging.

## Inspecionar o plano antes de copiar

O módulo também permite separar a inspeção da gravação. Execute este exemplo na raiz do repositório e escolha outra saída fresca se o nome já estiver em uso:

```powershell
Import-Module .\packaging\windows\RcMusicYoutubeExtensionPackage.psm1 -Force
$repo = (Get-Location).Path
$output = Join-Path $repo 'out/product/youtube-extension-reviewed-01'
$plan = New-RcMusicYoutubeExtensionPackagePlan -RepositoryRoot $repo -OutputDirectory $output
$plan
```

Criar o plano apenas lê os arquivos e o estado do Git. Para materializar o plano revisado:

```powershell
Copy-RcMusicYoutubeExtensionPackagePlan -Plan $plan
```

As funções do módulo exigem caminhos absolutos. O repositório informado precisa ser a raiz real do checkout e conter a receita carregada. Antes de criar a saída, a cópia verifica novamente o plano, os hashes e a disponibilidade do destino. Um plano adulterado ou uma fonte alterada desde a inspeção é recusado.

## O que o recibo informa

O `package-receipt.json` usa `schema_version: 1` e identifica o componente como `youtube-extension-source`. Ele registra:

- o commit de origem, em `source_commit`;
- `extension_dirty`, para alterações na pasta da extensão YouTube;
- `recipe_dirty`, para alterações no módulo e no script gerador;
- `source_dirty`, que combina os dois grupos;
- os três caminhos considerados em `source_inputs`;
- tamanho e SHA-256 dos sete arquivos copiados e das duas receitas.

O estado da extensão principal não participa desse recorte. Um candidato pode ser gerado a partir de fontes alteradas, mas o recibo informa essa condição. Para os sete assets e as duas receitas, um arquivo não versionado — inclusive ignorado pelo Git — ou com bytes diferentes do `HEAD` torna seu grupo alterado. Assim, um resultado vazio de `git status` sozinho não basta para declarar esses arquivos limpos.

O recibo mantém `native_host_name: com.rcmusic.youtube` apenas como identificação do componente e `native_manifest: null`. Esses campos não comprovam um perfil compilado de host nem autorizam registro ou ativação. Os hashes identificam os bytes usados; não substituem assinatura, revisão de licenças ou qualificação de distribuição.

Durante a cópia, os arquivos críticos ficam abertos para leitura sem compartilhamento de escrita ou exclusão no Windows, e os destinos são criados sem sobrescrita. Se houver falha depois da criação da saída, o diretório parcial é preservado para revisão. Essas medidas evitam alterações acidentais no fluxo local; não oferecem proteção geral contra um processo malicioso do mesmo usuário que controle o host.

## Validação e limites

O ciclo observado foi Red com 14 casos, Red com 15 após acrescentar a regressão de arquivo crítico ignorado e Green com os 15 casos passando. Os testes cobrem inventário, proveniência, alterações após o plano, isolamento da extensão principal, preservação de fontes e saídas e execução do script a partir de outra pasta. As fixtures são locais; essa validação não instala componentes nem depende do Brave ou de mídia pública.

O candidato local `out/product/youtube-extension-20261105-01/` foi gerado após o commit da receita `de73e985ab4729fcc4f3b412932fee6910404660`: **7 assets/21.197 bytes**, com `extension_dirty`, `recipe_dirty` e `source_dirty` falsos no recorte declarado. A conferência independente confirmou inventário, tamanhos e hashes contra as fontes, incluindo as duas receitas. O SHA-256 de `package-receipt.json` é `e9aa509677051485298c3afcb5720bbd18f4f77d70ec1c92776587b065eae626`. Plano e verificação ficam em `out/product/test-runs/download-bootstrap-20261105/`. O candidato é preservado como `identity_pending`; não é setup ou integração instalada.

O [gerador separado de manifest Native](youtube-native-manifest.md) já prepara os dois JSONs candidatos quando recebe ID explícito, recibo de build confiável habilitado e host correspondente. Ele não altera este pacote-fonte ou seu recibo. O ID real e a configuração habilitada ainda estão pendentes, assim como identidade estável, integração opcional com o instalador e validação no navegador. Os gates de segurança, privacidade, licenças e distribuição também permanecem abertos. Consulte os contratos de [integração com o navegador](browser-extension.md) e de [áudio YouTube e letras](youtube-audio-and-lyrics.md) antes de ativar o componente.
