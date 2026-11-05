# Bootstrap opcional de downloads no player Windows

**Estado:** composição opcional implementada no `main` real, com o mesmo presenter e painel do produto. O perfil padrão permanece ausente e os downloads continuam desabilitados no app normal. Os testes deste incremento validam mídia direta local e configuração C++ explícita; não comprovam aquisição pública do YouTube nem autorizam instalação ou distribuição.

## Configuração de build

`RC_MUSIC_PLAYER_DOWNLOAD_PROFILE_HEADER` é um cache CMake do tipo `FILEPATH`, vazio por padrão. Compilar os módulos opcionais de downloads ou Native Messaging não basta para habilitar a composição no executável.

Quando fornecido explicitamente, o valor identifica um **header C++ absoluto e revisado**. O executável e `rc_music_player_downloads` precisam existir. A composição chama `rc_music_configure_player_downloads(target, profile_header)` depois da criação desses targets; a função aceita uma única configuração por executável. Headers normais precisam existir; o header da fixture pode ser declarado `GENERATED` e produzido pelo build.

A função adiciona o provider e liga o owner somente ao target consumidor. As definições `RC_MUSIC_PLAYER_DOWNLOADS_CONFIGURED` e `RC_MUSIC_PLAYER_DOWNLOAD_PROFILE_HEADER` são `PRIVATE`: não alteram a ABI da UI nem propagam configuração para os outros executáveis. O cache vazio não adiciona esse bootstrap ao player. A funcionalidade exige o shell Windows com áudio e os módulos de execução/persistência; um ingresso nativo ativo também exige o módulo Native Messaging.

Com Native Messaging disponível, a composição usa por padrão `RC_MUSIC_NATIVE_BUILD_PROFILES_HEADER`, um caminho interno gerado pelo build a partir dos mesmos valores que configuram os hosts. O argumento opcional `NATIVE_PROFILE_HEADER` de `rc_music_configure_player_downloads` permite um header C++ revisado específico do consumidor, como nas fixtures; exige suporte Native e caminho absoluto existente ou declarado `GENERATED`. A definição correspondente também é `PRIVATE`. Esse argumento não cria configuração por JSON, ambiente ou CLI do produto.

O header revisado fornece esta função em `music_player::m0`:

```cpp
inline std::optional<WindowsPlayerDownloadProfile>
configuredPlayerDownloadsBuildProfile(const QString& config_root);
```

Retornar `std::nullopt` mantém o player local sem construir executor, owner ou listeners. Esse retorno é distinto de configurar um executor com caminhos vazios e esperar que ele descubra ferramentas.

O provider é chamado uma vez pelo bootstrap. `config_root` é o diretório de configuração já escolhido pelo app; não é um documento remoto ou um novo argumento de produção. O header é código confiável de composição, não uma configuração carregada de JSON, página, extensão, variável de ambiente ou CLI. Nenhuma busca em `PATH` escolhe executáveis autorizados. Caminhos, hashes, ambiente explícito e identidades dos peers precisam ser fornecidos pela composição revisada.

## Contrato C++

[`WindowsPlayerDownloadProfile`](../src/app/PlayerDownloadsBuildProfile.hpp) reúne:

| Campo | Contrato |
| --- | --- |
| `execution` | `WindowsDownloadExecutionProfile`: fontes e pins da rede/conversão, workspace, journal e resolver YouTube opcional. |
| `checkpoint_directory` | Diretório privado existente no modo padrão; com preparo opt-in, usa o `state` do layout fixo. |
| `options` | `WindowsPlayerDownloadsOptions`: projeção, namespace, dependências emprestadas e perfis Native Messaging opcionais. |
| `prepare_storage` | `false` por padrão; habilita explicitamente o preparo do layout privado pelo worker de checkpoint. |

[`makeWindowsConfiguredDownloadExecution`](../src/adapters/WindowsConfiguredDownloadExecution.hpp) reutiliza o executor e as tarefas existentes. Mídia direta e YouTube, quando configurado, compartilham a fila e o destino de transferência/conversão/publicação. Os preparadores continuam no worker, com leases próprios por job; não há cache de runtime preparado entre jobs. A fábrica copia strings e hashes, inclusive dados Qt que poderiam ter vindo de `fromRawData`.

`job.ownership_store` emprestado é recusado pela fábrica. Um perfil YouTube precisa de relógio de época explícito. Um ingresso Native YouTube com origem **ou** endpoint preenchido exige `execution.youtube`; o getter recusa essa combinação antes de criar o executor. O ingresso principal continua válido sem resolver YouTube. A presença do resolver e o matching abaixo não substituem a validação dos perfis nativos pelo owner.

Com `prepare_storage=false`, os diretórios continuam sendo entradas confiáveis previamente provisionadas. Com a opção ligada, o worker pode criar somente os diretórios do [layout privado fixo](player-download-storage.md#configuração-e-layout), a partir do `config_root` existente. O bootstrap não instala ferramentas, registra hosts ou repara ACLs. Hashes e identidades de arquivo não equivalem a confiança no publisher, autenticação do Brave ou sandbox.

## Perfil relocável do pacote

A [receita do perfil empacotado](packaged-download-profile.md) gera um provider C++ com os tipos existentes e paths de origem relativos: nove artefatos em `download-tools/network`, o worker em `download-tools/media/RCMusicMediaWorker.exe` e, quando selecionado, o inventário em `download-tools/youtube`. Hashes são fixados durante o build; o player não os calcula na UI nem carrega um manifesto como autoridade. O provider chama `makePackagedPlayerDownloadProfile(std::move(profile), config_root)`.

A sobrecarga de quatro argumentos recebe também `application_directory` e `windows_directory`, permitindo compor um perfil com raízes ainda inexistentes. Faz somente validação lexical e junção: recusa fontes absolutas, traversal, ADS, nomes reservados, controles e pins fora de SHA-256 hexadecimal minúsculo. Rebasa as origens de rede/mídia e `source_path` do YouTube; `relative_path` do inventário privado, roles, pins, limites e opções Native são preservados. Caminhos e pins Qt, inclusive dados originalmente emprestados, são copiados para memória própria.

A sobrecarga usada pelo provider obtém os paths reais com `GetModuleFileNameW` e `GetWindowsDirectoryW`, em buffers limitados. Não deriva localizações de CWD, argv ou ambiente e não usa stat, hashing ou consulta ao drive. Raiz de drive permanece absoluta, sem duplicar separadores; namespace não suportado é recusado sem fallback. Existência, identidade, permissões e hashes físicos continuam nos preparadores da worker.

O helper liga `prepare_storage` e aplica o mesmo layout privado puro, deixando o perfil utilizável também antes do getter. Quando o resolver YouTube já existe, reconstrói o ambiente com somente `PATH` vazio e `SystemRoot` explícito normalizado, e fornece clock UTC por callback. Ambiente e clock do prototype não são herdados. O provider usa `compiledPlayerNativeIngressProfiles()`; no perfil direto, desliga explicitamente o canal YouTube quando não há resolver. O default sem header continua sem downloads.

## Armazenamento privado opt-in

`playerDownloadsBuildProfile()` aplica o layout puro quando `prepare_storage=true`: checkpoint e journal usam `downloads/state`; os pais dos runtimes são `downloads/network`, `downloads/media` e, quando há resolver YouTube, `downloads/youtube`; o workspace usa `downloads/staging` e `downloads/published`. Pins, limites, backend de conversão, identidades Native e demais opções são preservados. Derivar esses caminhos não abre arquivos ou consulta o drive, nem cria um resolver ausente.

`makePlayerDownloadCheckpointFactory()` captura cópias próprias de `config_root` e do caminho de checkpoint, incluindo strings Qt apoiadas em memória externa. No opt-in, deriva sempre o `state` desse mesmo root, mesmo que receba um perfil com `checkpoint_directory` customizado ainda não atualizado. Criar a factory ou construir seu store permanece sem provisionamento.

Somente `load()` no worker de checkpoint chama o preparador confiável, cujo default é `prepareWindowsDownloadStorage()`. Callback ausente, retorno `false` ou exceção produz `Unreadable`, sem tornar o checkpoint saudável ou admitir downloads; um novo load explícito, pelo retry existente, pode tentar novamente. `save()` antes do preparo bem-sucedido retorna `Failed` sem chamar o preparador. Após o primeiro sucesso, o wrapper não reprovisiona ou repara: o backend conserva as verificações de cada load/save. O modo padrão mantém o store/caminho anteriores e nunca chama esse preparador.

Criação e validação físicas permanecem fora da UI. O [contrato do storage](player-download-storage.md#preparação-e-persistência) exige DACL privada exata, preserva diretórios preexistentes e qualquer progresso parcial e recusa objetos incompatíveis sem alteração de permissões. Esse preparo não substitui Saved ou o reconhecimento de drenagem dos workers.

## Perfis Native compilados

`NativeHostBuildProfiles` reúne os canais `principal` e `youtube`. O header gerado fornece `nativeHostBuildProfiles()`; o helper público `compiledPlayerNativeIngressProfiles()` os converte nas opções de ingresso do owner, sem definir callbacks de peer check. Um provider pode usar diretamente:

```cpp
profile.options.native_ingress = compiledPlayerNativeIngressProfiles();
```

Sem header Native compilado, ou com ambos os canais desabilitados, o helper retorna `std::nullopt`. Os caminhos das imagens são resolvidos ao lado do executável atual pelo helper existente; o layout revisado precisa conservar essa correspondência entre player e hosts.

Antes de criar o executor, `playerDownloadsBuildProfile()` compara cada canal ativo retornado pelo provider com o canal compilado correspondente. Um canal é ativo quando origem ou endpoint está preenchido. A origem precisa ser exatamente igual; o nome do pipe ignora caixa. As listas de imagens esperadas são comparadas como conjuntos que ignoram caixa e ordem, sem permitir ampliar ou substituir o conjunto compilado. As factories de peer check não fazem parte da identidade comparada.

O provider pode desligar um canal compilado ativo, inclusive ambos. Não pode ativar canal compilado desabilitado nem fornecer canal ativo quando o header Native está ausente. Divergência faz o getter retornar `std::nullopt`, preservando o player local; não escolhe outro host ou configuração de fallback. As identidades de produção continuam vazias por padrão.

## Sessão, UI e fechamento

O `main` preserva uma cópia do `SessionLoadResult` antes de mover o snapshot para o presenter. O owner recebe os mesmos recibos duráveis, inclusive quando a restauração da fila está desativada. Isso permite reconciliar um replay sem importar novamente a faixa.

O owner é criado depois do presenter e do writer de letras, e antes do `QQmlApplicationEngine`. Os objetos QML são destruídos antes do owner; presenter, autosave, writer de sessão e writer de letras sobrevivem a ele. O `main` cria `makePlayerDownloadCheckpointFactory(*profile, config_root)` antes de mover `profile.execution`, preservando o snapshot independente dos caminhos, e entrega essa factory ao worker existente do owner.

`options.lyrics_writer` recebe o `LyricsBinding` existente do player; `options.lyrics_network` fornecido pelo perfil é preservado. Dependências emprestadas precisam sobreviver ao owner. A mesma propriedade `downloads` do `Main.qml` recebe `owner.binding()`, sem novo painel, API de submissão QML ou segundo presenter.

O bootstrap chama `owner.start()` uma única vez, depois de validar o root QML e antes de publicar `startupReady`. Um retorno `false`, por exemplo por colisão de listener, conserva o owner e sua projeção de histórico: a view pode já ter instalado o gate de saída. Não há destruição/recriação nem segunda chamada a `start()` para contornar essa falha. O encerramento normal continua por `presenter.requestExit()`, com fechamento do ingresso antes da drenagem e dos ACKs existentes.

Perfil ausente, combinação YouTube incompatível, recusa da fábrica ou falha na construção opcional preservam o player local. Não há fallback para comandos ou configuração volátil. O smoke legado e este owner nunca são compostos juntos, pois ambos seriam donos do callback de preparação de saída.

## Evidência deste incremento

Recibos locais: `out/product/test-runs/download-bootstrap-20261105/`.

| Recorte | Red observado | Green observado |
| --- | --- | --- |
| Bootstrap pelo `main` real | 3 passaram, 2 falharam: provider stub não chamado/sem binding configurado | 5/5 Qt, sem skip |
| Provider e vínculo ingresso YouTube → resolver | 2 passaram, 6 falharam pelo stub `None` | 8/8 Qt, sem skip |

Os dois recortes passaram em **2/2 CTest**. O fechamento proporcional passou **7/7 CTest, 36 Qt**, sem falhas/skips: fábrica 14, perfil 8, bootstrap 5, composições legadas 3 + 3 e startup 3, mais a fronteira de processos. Formato de **12 arquivos C++**, incluindo o template do perfil, passou; não houve mudança QML. O marcador de substituição do template fica fora da formatação para preservar a geração CMake.

Compilaram em Release com `BUILD_TESTING=OFF` o módulo de rede standalone e o player com Native Messaging OFF, tanto com um header explícito que retorna `None` quanto após restaurar o cache ao header vazio. Ambos os diretórios listaram zero CTest. O grafo final do player restaurado não inclui perfil configurado, hooks/Qt Test ou a biblioteca Native Messaging. O executável de produção não foi iniciado; a jornada acima usa os executáveis privados de teste.

A jornada positiva compila o mesmo `main` com a ABI de UI de produção e um header sintético privado do teste. Os pins desse header são calculados **depois** do build das dependências, não a partir de outputs antigos durante configure nem no thread da UI. No ensaio original, o driver preprovisionava os diretórios TEMP privados dos stores. A evolução opt-in abaixo retira esse preprovisionamento dos diretórios geridos e exige sua criação pelo bootstrap.

O cliente Native C++ real do suporte de teste usa um perfil explícito que permite a imagem do próprio executável nos dois lados. Não é um host instalado no Brave. O pedido percorre ingresso real, admissão durável, owner, execução preparada e o mesmo painel. Um helper de transporte fornece DNS e bytes AAC/MP4 da fixture local; Media Foundation realiza a conversão, e validação, publicação MP3, checkpoint e sessão usam os adapters reais.

O suporte só declara a jornada concluída após dois ACKs idempotentes e um job com ID correspondente em `Completed`/`Imported`, além da fila esperada. O driver observa os recibos duráveis de admissão e de importação (`Saved` nos respectivos contratos), confirma MP3 publicado e ausência de autoplay. Após a saída, verifica checkpoint final, fechamento do cliente e remoção dos runtimes/staging próprios. A segunda execução com restauração de fila desativada preserva os recibos, aceita replay sem novo arquivo/importação e mantém a fila vazia. As variantes sem header e com provider `None` permanecem sem binding e sem diretórios de downloads.

Os targets focados são `rc_music_configured_downloads_bootstrap_tests` e `rc_music_configured_downloads_profile_tests`; os testes CTest são `rc_music.process.configured-downloads-bootstrap` e `rc_music.process.configured-downloads-profile`. O recorte depende dos módulos e fixtures de teste já configurados, sem novo protocolo ou flag de smoke no produto.

### Vínculo com os perfis Native compilados

O matching teve Red de **12 casos Qt passados/8 falhos**, mantendo os oito oráculos anteriores verdes, seguido de **20/20 Qt Green**. A fixture sem header compilado teve Red **2 passados/1 falho** e Green **3/3 Qt**. O novo target é `rc_music_configured_downloads_profile_off_tests`, registrado como `rc_music.process.configured-downloads-profile-off`; cobre a ausência do header e a recusa de canais ativos, além do opt-out local. Os testes do matching também verificam helper, divergência de origem/endpoint/imagens, comparação sem depender de ordem/caixa e exclusão dos callbacks da identidade. O Red está registrado em `matching-red-tests.log`.

O target sem header roda numa configuração com Native Messaging disponível. Ele **não é prova de um build com o módulo Native OFF**, nem substitui o build Release separado; os resultados Release OFF descritos acima pertencem ao fechamento anterior. Esses checks não executam aquisição pública, instalação ou qualificação do Brave.

### Preparo assíncrono do armazenamento privado

O perfil teve Red de **21 casos Qt passados/7 falhos**, preservando os 20 anteriores e o novo caso do default, seguido de **28/28 Qt Green**. O target sem header conservou **3/3 Qt**. Os novos testes cobrem layout/preservação, cópias de strings emprestadas, passividade da factory/store, callback na worker, falha/ausência/exceção, retry explícito, save antes do preparo, sucesso cacheado sem reparo e uso do mesmo `state` apesar de checkpoint customizado.

A jornada do bootstrap teve Red **4 passados/1 falho**, pela ausência de `downloads/state`, e passou **5/5 Qt** após a integração. O driver agora não precria as pastas geridas: exige `downloads/state`, os demais pais fixos e ausência dos caminhos antigos, mantendo as verificações de persistência e replay. O adaptador de storage passou **22/22 Qt**, com sua [evidência própria](player-download-storage.md#evidência-do-adaptador).

O fechamento integrado deste recorte passou **8/8 CTest, 143 Qt**, sem falhas, com dois skips antigos de symlink do checkpoint por política do host; os junctions novos passaram sem skip. Formato dos **9 C++** e player Release Native OFF com provider independente `None`, depois restaurado/recompilado com cache vazio e zero CTest, passaram. Esses resultados são separados do fechamento histórico de **36 Qt** acima. Não ativam o perfil distribuído, instalação ou aquisição pública. Recibos e limites ficam no [guia do storage](player-download-storage.md#evidência-do-adaptador).

### Perfil relocável e header gerado real

O helper teve Red de **55 casos Qt passados/5 falhos**, com os 28 oráculos anteriores preservados; os cinco positivos novos receberam `None` contra os stubs. O Green passou **60/60 Qt**. A seleção inicial passou **4/4 CTest, 68 Qt**, incluindo os **3** casos sem header e os **5** do bootstrap, sem falhas/skips. A receita Python passou **18 testes**; suas evidências e o candidato físico pertencem ao [guia do pacote](packaged-download-profile.md).

A fixture do `main` agora compila o header real emitido pela receita Python em `--plan-only`, com fontes relativas copiadas após o build das dependências. Os dois canais Native estão ativos somente na configuração de teste; o provider direto faz opt-out do YouTube sem resolver e mantém a principal funcional. O driver lança o processo com CWD no TEMP de configuração, conservando a jornada de execução, persistência e replay. Isso testa a relocação efetiva e a composição do header, sem executar o runtime YouTube ou adquirir mídia pública.

O fechamento passou **4/4 CTest, 68 Qt**, sem falhas/skips, com formato dos **5 C++**. O header completo do candidato real compilou no player Release sem testes/Native OFF; depois o cache foi restaurado ao header vazio e o app recompilado, com zero CTest e grafo de **337 comandos** sem perfil/Native/hooks/Qt Test. O executável de produção não foi iniciado. Os fechamentos históricos acima permanecem separados; identidade real da extensão e aquisição pública não foram ativadas por esses testes. A [receita e seus limites](packaged-download-profile.md#evidência-da-receita-e-candidato-local) também registram Python 3.13+ na geração da fixture, independente do local do build.

## Limites de aceitação

Este bootstrap de mídia direta não executou o runtime YouTube. As provas anteriores do broker/runtime offline e do owner YouTube continuam separadas; somá-las não comprova download de um vídeo público. Não houve acesso a mídia pública, instalação/registro de host, criação de AppContainer, UAC ou qualificação do navegador neste incremento.

Identidade instalada/origem autenticada, segurança e distribuição do runtime, aquisição remota de produção e os gates de publisher/privacidade continuam abertos. Letras.mus.br continua sem parser de produção após a resposta HTTP 403; o fallback e o salvamento revisado de letras têm seu próprio [contrato](download-lyrics.md). O [ADR-0017](architecture/decisions/0017-youtube-audio-and-lyrics-acquisition.md) permanece `Proposto`.
