# Recibo do perfil compilado dos hosts nativos

**Estado:** writer validado em **50/50 casos CMake** e executado no `POST_BUILD` dos dois hosts de produção. Ambos os recibos reais registraram `enabled: false`, conforme os defaults vazios. Os executáveis não foram iniciados para produzir esses recibos. Não houve instalação, Registro ou interação com navegador.

O recibo registra a consistência entre um executável recém-linkado e os dados CMake que produziram seu header de configuração. Ele é um artefato local e mutável do build. Não autentica publisher, código carregado ou lançamento pelo Brave, e não é uma configuração JSON que o app lê para conceder autoridade.

## Producer e fonte única

[`RcMusicNativeMessaging.cmake`](../cmake/RcMusicNativeMessaging.cmake) usa as mesmas origens, endpoints e listas simples de basenames para gerar:

1. o `NativeHostBuildConfiguration.hpp` selecionado de cada host;
2. o header C++ compartilhado `NativeHostBuildProfiles.hpp`;
3. os argumentos do writer de recibo de cada target.

O SHA-256 do header selecionado é fixado no configure, imediatamente depois de `configure_file`. Após o link, [`WriteNativeHostBuildProfile.cmake`](../cmake/WriteNativeHostBuildProfile.cmake) verifica esse pin, calcula hash e tamanho do executável, e verifica novamente o header antes de publicar o JSON. Drift do header causa falha, sem substituir um recibo anterior. Uma nova configuração legítima exige novo configure e build.

O writer publica por arquivo temporário e rename, não altera os inputs e recusa saída que sobrescreveria um deles pelo mesmo caminho normalizado, ignorando caixa. O parent da saída precisa existir. Esses cuidados preservam a transação local; não são proteção contra um host de build comprometido nem prova de identidade por arquivo aberto.

Um consumidor de empacotamento precisa conferir um pin explícito do recibo e os artefatos referenciados. Apenas recalcular o hash de um JSON fornecido pelo próprio solicitante não estabelece confiança. Um recibo velho que permaneceu após build falho também não comprova os arquivos atuais.

## Schema 1

O objeto raiz tem **exatamente onze campos**:

| Campo | Tipo e significado |
| --- | --- |
| `schema_version` | Número inteiro `1`. |
| `component` | String fixa `rcmusic-native-host-build-profile`. |
| `protocol` | `direct.media.v1` ou `youtube.audio.v1`. |
| `host_name` | `com.rcmusic.media` para mídia direta; `com.rcmusic.youtube` para YouTube. |
| `origin` | Origem exata compilada, ou string vazia quando desabilitado. |
| `endpoint` | Nome de pipe compilado, ou string vazia quando desabilitado. |
| `enabled` | Booleano derivado da configuração; não é uma flag de entrada. |
| `owner_images` | Array dos basenames permitidos para o owner. |
| `host_images` | Array dos basenames permitidos para o host. |
| `executable` | Objeto com exatamente `path`, `sha256` e `size_bytes`. |
| `configuration` | Objeto com exatamente `path` e `sha256`, referente ao header selecionado desse host. |

Os caminhos são absolutos, normalizados lexicalmente e usam drive local do Windows; UNC e caminhos relativos são recusados. Os hashes emitidos têm 64 dígitos hexadecimais em minúsculas; `size_bytes` é um número positivo. O hash de configuração esperado pode ser fornecido ao writer em caixa alta ou baixa, mas precisa corresponder aos bytes do header.

Origem e endpoint são ambos vazios ou ambos válidos. A origem segue `chrome-extension://[a-p]{32}/`, com correspondência exata; o endpoint tem 1–96 caracteres ASCII em `[A-Za-z0-9_-]`. A validação reutiliza a gramática CMake do perfil compilado. Famílias e nomes lógicos não podem ser trocados entre si.

Cada lista de imagens tem de um a quatro basenames. O stem usa apenas ASCII `[A-Za-z0-9_-]`, com extensão `.exe` sem distinção de caixa. Paths e nomes DOS reservados são recusados. O basename do executável precisa constar em `host_images`, ignorando caixa. As listas permanecem no recibo desabilitado como dados do build; não habilitam o host.

## Defaults, aliases e headers

As opções existentes continuam vazias por padrão:

| Canal | Origem no cache | Endpoint no cache |
| --- | --- | --- |
| Principal | `RC_MUSIC_NATIVE_MAIN_ORIGIN` | `RC_MUSIC_NATIVE_ENDPOINT` |
| YouTube | `RC_MUSIC_NATIVE_YOUTUBE_ORIGIN` | `RC_MUSIC_NATIVE_YOUTUBE_ENDPOINT` |

Assim, um build normal dos hosts produz recibos desabilitados. Gerar o JSON não ativa um manifesto, não escolhe uma extensão instalada e não registra nada no Windows. Configurações parciais são recusadas.

Os aliases `CACHE INTERNAL` apontam para estes outputs, relativos ao diretório de build:

| Alias | Caminho |
| --- | --- |
| `RC_MUSIC_NATIVE_MAIN_BUILD_PROFILE` | `native-host/rc_music_native_host.build-profile.json` |
| `RC_MUSIC_NATIVE_YOUTUBE_BUILD_PROFILE` | `youtube-native-host/rc_music_youtube_native_host.build-profile.json` |
| `RC_MUSIC_NATIVE_BUILD_PROFILES_HEADER` | `native-host/NativeHostBuildProfiles.hpp` |

Cada target também possui a propriedade `RC_MUSIC_NATIVE_HOST_BUILD_PROFILE` com o caminho do seu recibo. Os JSONs são declarados como `BYPRODUCTS` dos respectivos `POST_BUILD`. O writer e o header selecionado são dependências de link, para que mudanças nesses inputs atualizem o recibo.

O header compartilhado fornece `music_player::m0::nativeHostBuildProfiles()`, retornando `NativeHostBuildProfiles` com os membros `principal` e `youtube`. Ele permite comparar a composição C++ revisada do player com os mesmos dados compilados nos hosts, sem carregar recibos JSON em runtime. O header selecionado continua sendo o input de configuração referenciado por cada recibo; o hash desse campo não é o hash do header compartilhado.

`nativeDownloadSiblingImages` resolve os basenames em relação ao executável em execução. Portanto, o layout precisa manter owner e host na disposição de irmãos esperada pelos dois lados. Copiar somente o host para outro subdiretório pode impedir o peer check mesmo com origem e endpoint corretos. O recibo de um host, isoladamente, não comprova correspondência com um provider independente do player nem valida esse layout instalado.

## Build e validação focados

Em um diretório Windows já configurado com `RC_MUSIC_BUILD_NATIVE_MESSAGING=ON`, use `$build_dir` para o caminho desse build:

```powershell
cmake --build "$build_dir" --config Release --target rc_music_native_host rc_music_youtube_native_host
ctest --test-dir "$build_dir" -C Release --output-on-failure -R '^rc_music\.native\.build-profile$'
```

O primeiro comando linka os targets e gera os recibos, sem executar os hosts. O segundo roda `tests/cmake/NativeHostBuildProfileTests.cmake`: usa bytes sintéticos pequenos em uma árvore temporária própria, sem PE real, Qt, mídia, rede ou navegador.

O Red do writer stub apresentou **41/49 casos aprovados**, com os oito casos válidos recusados por `not implemented`. Foi acrescentada uma recusa para o basename DOS `CON.exe`, sem criar um device file. O Green final passou **50/50**. Os casos cobrem perfis habilitados/desabilitados, famílias, campos obrigatórios, normalização, pins, drift, fontes preservadas, recibo anterior preservado em erro e saídas inválidas. Os dois `POST_BUILD` reais também passaram e produziram defaults desabilitados.

Essas evidências não executam Native Messaging real com Brave e não substituem os testes de IPC, a [composição opcional do player](player-downloads-bootstrap.md), a revisão de empacotamento ou os gates de segurança/distribuição. Não há aprovação de produção remota ou de instalação neste recibo.
