# Pacotes opcionais das extensões Chromium

O gerador prepara diretórios separados com as fontes executáveis das extensões principal e YouTube e um recibo dos arquivos copiados. Eles podem ser consumidos por uma etapa posterior de empacotamento. Não representam extensões instaladas e funcionais no Brave.

Após autorização do mantenedor, cada manifesto contém uma chave pública fixa própria. O gerador deriva e registra o ID dessa chave como `identity_state: public_key_bound`; manifestos sem chave continuam produzindo `identity_pending`. O gerador não cria chaves, produz manifesto Native Messaging ou registra hosts. As duas extensões continuam separadas.

## Identidades públicas estáveis

| Canal | ID Chromium | Host |
|---|---|---|
| Principal | `kggjfjbjhklkbpelddffdjelbiomoolc` | `com.rcmusic.media` |
| YouTube | `lhcdmfamadilnahlckpalijljfohjgkc` | `com.rcmusic.youtube` |

O [`manifest.key`](https://developer.chrome.com/docs/extensions/reference/manifest/key) conserva o ID ao mover ou atualizar a pasta. `RcMusicChromiumIdentity.psm1` aceita UTF-8/JSON estrito até 64 KiB e profundidade 16, recusando campos duplicados, inclusive aninhados. A política deste empacotador exige Base64 canônico de uma chave pública RSA SPKI DER de 2048–4096 bits; bytes extras ou outro formato são recusados. O ID usa os primeiros 16 bytes de SHA-256 do DER, com nibbles mapeados para `a`–`p`, conforme o [algoritmo Chromium](https://chromium.googlesource.com/chromium/src/+/main/components/crx_file/id_util.cc). Isso fixa a identidade, sem autenticar um publicador ou aprovar uma instalação.

Os pares RSA 2048 foram gerados localmente. Somente as chaves públicas estão no repositório e nos pacotes. As privadas foram guardadas em `C:\Users\Joao\.codex\rc-music-extension-keys`, com DACL protegida para usuário atual/SYSTEM; não são inputs dos geradores. Substituir uma chave pública mudaria o ID e exigiria atualizar os perfis dos hosts.

## Gerar um candidato

Na raiz do repositório, com PowerShell 7, escolha um nome de saída que ainda não exista:

```powershell
pwsh -NoProfile -File .\packaging\windows\New-RcMusicYoutubeExtensionPackage.ps1 `
    -OutputDirectory out/product/youtube-extension-novo-candidato
pwsh -NoProfile -File .\packaging\windows\New-RcMusicChromiumExtensionPackage.ps1 `
    -Channel Main -OutputDirectory out/product/main-extension-novo-candidato
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

Os sete arquivos YouTube vêm de `extensions/chromium/youtube`, sem alterações. A principal copia nove arquivos de `extensions/chromium/main`, incluindo `detector.js` e `media-collector.js`. README, testes e arquivos extras, inclusive `.pem`, ficam fora dos dois inventários. O `manifest.json` copiado é o manifesto da extensão Chromium; ele não é um manifesto de host Native Messaging. O CLI genérico aceita somente os canais `Main` e `Youtube`; os entrypoints antigos YouTube continuam limitados a esse canal.

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
- canal, `identity_state` e `extension_identity`, com ID, origem e SHA-256 do DER público;
- `extension_dirty`, para alterações na pasta da extensão YouTube;
- `recipe_dirty`, para alterações no módulo e no script gerador;
- `source_dirty`, que combina os dois grupos;
- os caminhos considerados em `source_inputs`;
- tamanho e SHA-256 dos assets e receitas: três receitas na principal e quatro no YouTube, incluindo o leitor de identidade e os respectivos CLIs.

O estado do outro canal não participa do recorte. Um candidato pode ser gerado a partir de fontes alteradas, mas o recibo informa essa condição. Para os assets e receitas do canal, um arquivo não versionado — inclusive ignorado pelo Git — ou com bytes diferentes do `HEAD` torna seu grupo alterado. Assim, um resultado vazio de `git status` sozinho não basta para declarar esses arquivos limpos.

O recibo mantém `native_host_name` fixo por canal e `native_manifest: null`. Esses campos não comprovam um perfil compilado de host nem autorizam registro ou ativação. A cópia reconfere a identidade derivada com o plano antes de criar a saída, mantendo o manifesto byte a byte. Os hashes identificam os bytes usados; não substituem assinatura, revisão de licenças ou qualificação de distribuição.

Durante a cópia, os arquivos críticos ficam abertos para leitura sem compartilhamento de escrita ou exclusão no Windows, e os destinos são criados sem sobrescrita. Se houver falha depois da criação da saída, o diretório parcial é preservado para revisão. Essas medidas evitam alterações acidentais no fluxo local; não oferecem proteção geral contra um processo malicioso do mesmo usuário que controle o host.

## Validação e limites

Identidades fixas: manifesto Node **6 passados/2 falhos → 8/8**; reader **1/12 → 13/13**; pacote YouTube **14/6 → 20/20**; pacote principal/genérico **0/9 → 9/9**. O fechamento reúne **206 testes Node e 42 casos PowerShell** verdes, sem skips, mais documentação. Dois cálculos independentes, Python hashlib e Node crypto, conferem os IDs derivados das chaves públicas. As fixtures exercitam cópia exata, mudança de pasta sem mudança de ID, JSON/chaves inválidos, plano adulterado e CWD alheio; não leem as chaves privadas. Evidência em `out/product/test-runs/extension-identity-20261105/`.

O ciclo original do gerador YouTube foi Red com 14 casos, Red com 15 após acrescentar a regressão de arquivo crítico ignorado e Green com os 15 casos passando. Os testes cobrem inventário, proveniência, alterações após o plano, isolamento da extensão principal, preservação de fontes e saídas e execução do script a partir de outra pasta. As fixtures são locais; essa validação não instala componentes nem depende do Brave ou de mídia pública.

O candidato local `out/product/youtube-extension-20261105-01/` foi gerado após o commit da receita `de73e985ab4729fcc4f3b412932fee6910404660`: **7 assets/21.197 bytes**, com `extension_dirty`, `recipe_dirty` e `source_dirty` falsos no recorte declarado. A conferência independente confirmou inventário, tamanhos e hashes contra as fontes, incluindo as duas receitas. O SHA-256 de `package-receipt.json` é `e9aa509677051485298c3afcb5720bbd18f4f77d70ec1c92776587b065eae626`. Plano e verificação ficam em `out/product/test-runs/download-bootstrap-20261105/`. O candidato é preservado como `identity_pending`; não é setup ou integração instalada.

O [gerador separado de manifest Native](youtube-native-manifest.md) prepara os dois JSONs candidatos quando recebe ID explícito, recibo de build confiável habilitado e host correspondente. Ele não altera este pacote-fonte ou seu recibo. Os IDs agora estão definidos; a [montagem dos hosts](windows-runtime.md#montagem-opcional-dos-dois-hosts-native-messaging), integração opcional das extensões com o instalador e validação no navegador continuam com seus próprios gates. Segurança, privacidade, licenças e distribuição permanecem abertos.
