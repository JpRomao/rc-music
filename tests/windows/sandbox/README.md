# Probe manual do worker em AppContainer

Este programa executa o ensaio físico do [ADR-0011](../../../docs/architecture/decisions/0011-media-worker-appcontainer.md). O driver compõe owner, store, fence e porta nativa de perfil; ele não integra o player ou o instalador. Somente os autotestes puros de parser/protocolo/política de cleanup são registrados como modos automáticos do probe; não criam perfil, diretório, token ou entrada de Registro. O [ensaio separado autorizado mais recente](../../../docs/media-profile-lifecycle.md#ensaio-separado-autorizado-sem-evento-correlacionado) reconheceu transporte/encerramento, sem evento de rede correlacionado; sua autorização foi consumida. Cada nova execução física exige autorização específica.

## Compilação e autoteste

No [ambiente MSVC/Qt existente](../../../docs/desktop-build.md), com os opcionais de rede e testes configurados:

```powershell
cmake --build out/product/m1-desktop-release-on-20260906 --target rc_music_media_sandbox_probe rc_music_AppContainerProbeProtocol_tests rc_music_AppContainerNetworkObserver_tests --parallel 3
ctest --test-dir out/product/m1-desktop-release-on-20260906 -R '^rc_music\.download-network\.(MediaSandboxProbeParser|AppContainerProbeProtocol|AppContainerNetworkObserver)$' -V
```

O helper é Win32 `/MT`, sem Qt; o driver usa os adapters do produto. `--help`, argumentos ausentes/inválidos e `--self-test` não alcançam o modo que altera o host. O autoteste trata nomes DOS reservados, formato e limites do protocolo e confirmação de limpeza somente após criação bem-sucedida e ACKs.

O [protocolo privado v2](AppContainerProbeProtocol.hpp) mantém o header do Request em 60 bytes e exige duas portas não nulas: TCP e UDP. A porta UDP ocupa o antigo campo reservado no offset 26; o Report possui 70 bytes e acrescenta `udp_error` após os onze erros originais. `errors[5]` conserva o resultado bruto da tentativa de descendente e `errors[6]` conserva TCP. A máscara original `all_checks=0x7fff` permanece; versões anteriores, truncamentos, bytes adicionais e limites inválidos são recusados. O teste Qt do codec é puro e inclui vetores independentes e preservação desses campos.

## Execução física — autorização específica necessária

`CreateAppContainerProfile` cria pastas e armazenamento no Registro do usuário. O contrato do projeto e a autorização anterior para gerar sem instalar não concedem essa operação. Executar o comando abaixo somente após aprovação específica do mantenedor para esses testes temporários.

O owner deriva `RCMusic.Media.<UUID>` de um UUID v4 gerado internamente; não há opção para fornecer nome, SID, perfil existente ou reutilizar uma criação anterior. O destino de evidência precisa ser novo, local, sem reparse e dentro de `out/product` do workspace compilado. O programa usa fixtures próprias e listeners efêmeros TCP/UDP de loopback, com controle positivo no processo principal; UDP também possui controle posterior. Não consulta sites ou mídia pessoal.

```powershell
$rcQtBin = (Resolve-Path 'q/f75eb9bea8280844c6d1a2b3f4/sdk/bin').Path
$env:PATH = "$rcQtBin;$env:PATH"
$rcEvidence = Join-Path (Get-Location).Path ('out/product/test-runs/media-appcontainer-owner-native-' + [Guid]::NewGuid().ToString('D'))
& 'out/product/m1-desktop-release-on-20260906/rc_music_media_sandbox_probe.exe' --allow-profile-changes --evidence-dir $rcEvidence
```

O teste confere token/SID/Low/capabilities vazias, stdio e entrada de leitura; tenta acesso a um sentinela externo, escrita/alteração de ACL/owner/delete no runtime, criação de descendente e loopback. Só negações específicas passam; timeout ou sharing violation são inconclusivos. O cenário padrão confirma os quatro handles autorizados; a ausência de herança adicional tem cobertura separada no supervisor.

A restrição de descendentes pertence ao Job Object. O erro bruto 1816 (`ERROR_NOT_ENOUGH_QUOTA`) não comprova sozinho qual quota atuou. O supervisor associa uma completion port privada ao Job antes da execução e pode observar `JOB_OBJECT_MSG_ACTIVE_PROCESS_LIMIT`. O driver aceita essa evidência específica junto de 1816 somente sem notificações de limite de memória de processo/Job; registra `job_active_process_limit_witnessed`, os flags de memória e `effective_checks`, preservando `checks` e o erro bruto. A notificação é evidência de limite, nunca ACK de processo/pipes; ausência da mensagem significa não observada, sem aprovação por inferência.

UDP é um diagnóstico separado: o helper faz `sendto` somente para `127.0.0.1:udp_port`, e `udp_explicit_denial` exige `WSAEACCES` explícito. Sucesso do envio ou qualquer outro erro não confirma negação. UDP não preenche `LoopbackDenied`, não altera `errors[6]` e não transforma timeout TCP em aprovação. O resultado global continua exigindo os checks estritos, as jornadas do owner/mídia e cleanup reconhecido.

## Observação de bloqueio de rede pelo Windows

O driver possui um observador WFP somente para o ensaio. A biblioteca, as APIs e o link `fwpuclnt` ficam nos alvos de teste, fora do player, worker de mídia e instaladores. O preflight abaixo abre uma sessão local, consulta coleta e máscara existentes e fecha a sessão; não cria perfil, assinatura de eventos ou listener:

```powershell
& 'out/product/m1-desktop-release-on-20260906/rc_music_media_sandbox_probe.exe' --network-preflight
```

O JSON registra erros Win32 de abertura, consultas e fechamento. `available` exige coleta U32 igual a 1, bit `FWPM_NET_EVENT_KEYWORD_CAPABILITY_DROP` na máscara U32 e fechamento confirmado. Isso avalia os pré-requisitos de leitura; `network_isolation_passed` continua falso. Coleta desligada, tipo desconhecido, acesso negado ou erro preservam a indisponibilidade. Não há `SetOption`, mudança de ACL, regra ou configuração de firewall. O preflight aceita somente esse argumento isolado, sem destino ou opções de perfil.

No host de 2026-11-04, a execução normal recebeu AccessDenied 5. Uma única leitura elevada especificamente autorizada confirmou coleta 1 e máscara 0; o observador de CAPABILITY_DROP permaneceu indisponível. As duas chamadas fecharam suas sessões. Os [recibos e limites](../../../docs/media-profile-lifecycle.md#observador-de-eventos-de-rede-do-ensaio) distinguem essa leitura da prova de tráfego; nenhum perfil ou assinatura foi criado. A autorização dessa execução não inclui mudar opções globais para obter um resultado favorável.

No ensaio físico, o driver tenta observar somente `CAPABILITY_DROP`, com filtro nativo por app ID do executável privado, TCP, destino `127.0.0.1` e porta efêmera do listener. O callback exige os flags de versão/protocolo/destino/porta/app ID/usuário/pacote, compara binariamente os três identificadores e confere uma janela FILETIME de até 15 segundos. A capability ausente precisa ser conhecida, o filter ID não nulo e `isLoopback` exatamente TRUE. São retidos somente timestamp, filter ID e capability ID da primeira correspondência; não há gravação ou cópia de tráfego de outras aplicações. `CLASSIFY_DROP` e eventos de allow são recusados. O evento aceito prova bloqueio do tráfego correlacionado por capability ausente; sua estrutura não identifica direção, PID ou layer. [Evento de bloqueio](https://learn.microsoft.com/en-us/windows/win32/api/fwpmtypes/ns-fwpmtypes-fwpm_net_event_capability_drop0), [campos de identidade](https://learn.microsoft.com/en-us/windows/win32/api/fwpmtypes/ns-fwpmtypes-fwpm_net_event_header2).

Uma correspondência só complementa o timeout TCP 10060 depois de start e stop confirmados. Nunca altera o erro bruto nem aprova uma conexão que teve sucesso. Sem evento ou com teardown incerto, timeout permanece inconclusivo. `WSAEACCES` conserva o oráculo original, sem exigir WFP. O observador não reconhece parada de processo/pipes e não concede autoridade de cleanup de perfil.

O contexto está completo antes da assinatura, inclusive se o callback competir com o retorno de Subscribe. Unsubscribe ocorre fora do callback e sem seus locks; sucesso aguarda callbacks ativos. Falha conserva contexto, engine e assinatura até a saída do driver, fecha admissão de correspondências e não é repetida pelo destrutor. Os testes automáticos injetam chamadas e eventos; não consultam WFP real. [Contrato de unsubscribe](https://learn.microsoft.com/en-us/windows/win32/api/fwpmu/nf-fwpmu-fwpmneteventunsubscribe0).

### Oráculo separado de ClassifyIsolation

O padrão CAPABILITY_DROP continua exigindo keyword 4 e recusa CLASSIFY_DROP. O modo explícito separado usa coleta geral ligada e registra a máscara existente sem exigir esse bit. Seu preflight também é isolado, sem perfil/assinatura:

```powershell
& 'out/product/m1-desktop-release-on-20260906/rc_music_media_sandbox_probe.exe' --network-preflight-classify
```

Para escolher o oráculo no ensaio de perfil autorizado, acrescentar `--classify-isolation` no fim dos três argumentos existentes. Não há escolha automática em caso de indisponibilidade; os modos não reinterpretam recibos anteriores.

Um candidato CLASSIFY_DROP exige todas as identidades/tupla/janela e ainda layerID não nulo, direção 0x3901 e loopback exatamente TRUE. Ele não é prova antes de Unsubscribe. Depois, na mesma engine e antes de Close, o driver consulta exatamente o filtro e a camada informados. Só aprova com IDs iguais, filtro habilitado e BLOCK exato, provider/sublayer App Isolation e ambas as camadas ALE_AUTH_CONNECT_V4. Metadata divergente/ausente, outro provider, callout, erro ou Close incerto conservam a inconclusão. O callback não faz essas consultas. A prova se limita à tentativa correlacionada e à regra atual observada, sem snapshot retroativo ou diagnóstico de capability ausente. `capability_id` é nulo nesse modo; candidato, regra verificada, GUIDs brutos SDK, IDs e horários são registrados separadamente. [Contrato e resultados](../../../docs/media-profile-lifecycle.md#bloqueio-por-regra-de-isolamento).

O preflight Classify compilado e executado normalmente retornou AccessDenied 5; nenhum evento real foi aceito. Executar todo o driver elevado provaria somente esse contexto. O ensaio posterior manteve driver/owner/worker normais e usou observador elevado separado, após composição/testes e autorização específica, sem obter evento correlacionado. A autorização da leitura elevada anterior e a autorização posterior do observador foram consumidas em execuções distintas; este modo não concede elevação, instala serviços ou altera permissões WFP.

### Preparação anterior dos componentes de troca

No incremento `1c6dfd6`, os testes `AppContainerObserverProtocol`, `AppContainerObserverFiles` e `AppContainerObserverIdentity` prepararam [mensagens, arquivos privados e leitura de token](../../../docs/media-profile-lifecycle.md#preparação-do-transporte-separado), então sem link no probe. A composição posterior abaixo continua restrita a `BUILD_TESTING` e não autoriza execução elevada. A janela opcional do observador começa imediatamente antes de Subscribe, com relógio injetado nos testes.

```powershell
cmake --build out/product/m1-desktop-release-on-20260906 --target rc_music_AppContainerObserverProtocol_tests rc_music_AppContainerObserverFiles_tests rc_music_AppContainerObserverIdentity_tests rc_music_AppContainerNetworkObserver_tests
ctest --test-dir out/product/m1-desktop-release-on-20260906 -R 'rc_music.download-network.(AppContainerObserver(Protocol|Files|Identity)|AppContainerNetworkObserver)$' --output-on-failure
```

O codec é puro. Os testes de arquivos criam somente fixtures TEMP próprias, inclusive hardlinks/junctions entre objetos da mesma fixture e ACLs para testar recusa; os testes de identidade leem o token do próprio processo. Nenhum deles cria perfil, assina WFP, eleva, modifica privilégios ou usa arquivos pessoais. Outro usuário de UAC e writers SYSTEM/Administrators não foram exercitados fisicamente; recusa de acesso deve permanecer falha, sem reparo de permissões.

### Driver normal e observador separado

O [fluxo composto](../../../docs/media-profile-lifecycle.md#composição-separada-do-ensaio) possui dois modos manuais, que só devem ser executados após a autorização correspondente. Eles não se lançam nem elevam automaticamente. No processo normal:

```powershell
& 'out/product/m1-desktop-release-on-20260906/rc_music_media_sandbox_probe.exe' --allow-profile-changes --evidence-dir '<novo path absoluto sob workspace/out/product>' --external-classify-observer
```

O driver cria seu próprio perfil/journal, prepara o helper e escreve `observer-request:<path absoluto>` em stdout com flush. Conserva o runtime e aguarda Ready. O processo separado com elevação temporária autorizada recebe exatamente o path publicado:

```powershell
& 'out/product/m1-desktop-release-on-20260906/rc_music_media_sandbox_probe.exe' --allow-wfp-subscription --observer-request '<path absoluto publicado>'
```

O path publicado já inclui `wfp-request.json`; passe-o inteiro, sem acrescentar outra folha. O ator valida raiz, arquivos, processos e imagens antes de assinar WFP. Request/Ready/Result têm nomes fixos e não são substituídos. O driver exige observador ainda vivo e janela com headroom antes do helper; ACK de saída usa o mesmo handle aberto. Consultas/assinatura não alteram coleta, keywords, regras ou auditoria. A autorização anterior do preflight cobria somente leitura; a execução separada abaixo recebeu outra autorização específica, agora também consumida. Esses comandos não autorizam sua repetição.

Os testes `AppContainerObserverActor` e `AppContainerObserverDriver` usam WFP/processos injetados, frames e arquivos reais próprios; o teste `AppContainerObserverProcess` consulta o próprio processo e cria um helper somente suspenso, que não é retomado, para verificar sinal nativo e exit code 259. O cenário de Stop incerto conserva pins até o fim do executável Qt e publica `actor-retained-fixture:<TEMP próprio>`; somente após confirmar o término do processo e inventariar essa árvore sem reparse é possível limpar a fixture, preservando seu recibo. Não usar esse marcador para remover perfis ou outros diretórios.

### Execução separada autorizada de 2026-11-04

Uma execução dos binários/launcher congelados de `48d8542` usou driver Medium/não elevado e observador High via UAC, com hashes preservados. Os recibos em `out/product/test-runs/media-observer-native-authorized-20261104-01/` e `out/product/test-runs/media-observer-composition-20261104/elevated-actor-01/` registram Ready/Result e peer ACK reconhecidos, janela de 15 s e Subscribe/Unsubscribe/EngineClose 0. Ator/launcher exit 0 confirmam seu encerramento; driver exit 1 conserva o resultado global falso.

`proof_eligible=true` não é match: candidato e correspondência ficaram nulos, `matched=false` e `rule_verified=false`. TCP 10060, UDP 0 e helper bruto/efetivo 29695/30719 não demonstram negação; a rede continua inconclusiva. MP3/AAC/WAV 3/3, NoChild e cancelamento/drenagem passaram, com `all_native_acks=true`. O perfil próprio `RCMusic.Media.2f038dbf-921a-4bee-a105-23048f26a23e` teve Delete 0/Removed Saved, e `08-independent-postflight.json` confirmou ausência das folhas exatas de pacote/Mappings/Storage, RegClose 0 e nenhum processo remanescente.

O TEMP `RCMusic-AppContainer-Probe-SisyTE` foi preservado pelo resultado global falso, sem cleanup adicional ou alteração de firewall/coleta/auditoria. A autorização foi consumida; não há execução elevada pendente de aprovação para repetir esse mesmo ensaio. [Resultado completo e limites](../../../docs/media-profile-lifecycle.md#ensaio-separado-autorizado-sem-evento-correlacionado). Inventário nativo de Registro/quatro raízes, política de storage/MIC, composição instalada e aprovação de produção continuam pendentes; downloads OFF e ADR-0011 Proposto.

## Inventário de metadados do storage

O [modelo de inventário de segurança do storage](../../../docs/media-profile-lifecycle.md#modelo-limitado-de-captura-de-metadados) tem alvo isolado de teste, sem usar o probe ou criar perfis. Valida o consumidor de uma sessão confiável falsa. Os [bindings parciais de leitura](../../../docs/media-profile-lifecycle.md#bindings-parciais-de-leitura-win32) acrescentam coordenação com o owner/fence, consulta limitada de owner/DACL e leitura de `PackageRoot`/`AC`. A implementação posterior de Registry/factory está abaixo; inventário físico em perfil e aplicação de política continuam pendentes:

```powershell
cmake --build out/product/m1-desktop-release-on-20260906 --target rc_music_AppContainerStorageInventory_tests rc_music_AppContainerStorageSecurity_tests rc_music_AppContainerStorageFiles_tests rc_music_AppContainerStorageLiveSource_tests
ctest --test-dir out/product/m1-desktop-release-on-20260906 -R '^rc_music.profile-storage.AppContainerStorage(Inventory|Security|Files|LiveSource)$' --output-on-failure
```

Os alvos não dependem do probe/helper WFP congelados. Security/Files usam somente fixtures TEMP próprias para operações de arquivo; `RegGetKeySecurity` é compilado, mas suas chamadas são falsas nos testes. LiveSource usa owner/backend com portas de perfil falsas, compara a mesma instância e reserva um ticket até Close explícito. Não há execução de perfil, Registro, elevação, WFP, instalação ou alteração de ACL de dados pessoais por esses alvos. Fixtures de fechamento incerto são identificadas no output e ficam preservadas até uma limpeza autorizada após a saída física do processo Qt; o destrutor não inventa ACK.

O [compositor de sessão](../../../docs/media-profile-lifecycle.md#composição-de-sessão-e-consulta-de-registro-somente-de-leitura) namespaceia refs/identidades de duas portas falsas e exige Close reconhecido por ambas; não implementa o leitor nativo de Registry. O preflight separado só registra o parser como teste automático:

```powershell
cmake --build out/product/m1-desktop-release-on-20260906 --target rc_music_AppContainerStorageSession_tests rc_music_registry_readonly_preflight
ctest --test-dir out/product/m1-desktop-release-on-20260906 -R '^rc_music.profile-storage.(AppContainerStorageSession|RegistryReadOnlyPreflightParser)$' --output-on-failure
```

`rc_music_registry_readonly_preflight.exe --readonly` é uma consulta manual a componentes fixos existentes. Não aceita paths/chaves adicionais e não escreve no Registro, cria perfil, lê dados de valores/SD, enumera ou solicita UAC. O diagnóstico local preserva somente retornos, tipo/tamanho do indicador reservado e fechamento de handles próprios; não qualifica link incompleto, identidade persistente de chave ou política. Sua execução não repete o ensaio WFP cuja autorização foi consumida.

### Leitor de Registro e factory de sessão de quatro raízes

`AppContainerStorageRegistry` e `appContainerRegistryNativeCalls()` acrescentam leitura de Registro em alvos de teste. A factory `appContainerStorageNativeSession()` copia os quatro locators e compõe Files/Registry na sessão existente, sem I/O ou proveniência nova na construção. O vínculo vivo continua dependendo do mesmo owner/backend e ticket de fence. Os limites são 512 objetos, profundidade 16, SD bruto de 64 KiB/cache variável de 4 MiB, componentes até 255 e NativeName até 32767 unidades UTF-16. Nomes/watch/rechecks não são identidade persistente, snapshot atômico ou detecção de ABA/RegRestoreKey.

```powershell
cmake --build out/product/m1-desktop-release-on-20260906 --target rc_music_AppContainerStorageRegistry_tests rc_music_AppContainerRegistryNative_tests rc_music_AppContainerStorageNativeSession_tests
ctest --test-dir out/product/m1-desktop-release-on-20260906 -R '^rc_music.profile-storage.(AppContainerStorageRegistry|AppContainerRegistryNative|AppContainerStorageNativeSession)$' --output-on-failure
```

Reader passou **100/100 Qt** usando Registry falso. Native admission passou **19/19 Qt** sem chamar open_software ou abrir chaves reais; NativeSession **7/7 Qt** usa arquivos/permissões reais somente em TEMP próprio e chamadas de perfil/Registro falsas. A seleção ampliada passou **9/9 CTest, 389 Qt + 13 casos puros de parser**, sem falhas/skips; Files reteve duas fixtures próprias intencionais, preservadas sem delete. Cobre cancelamento e manutenção da fence até Close explícito, inclusive retenção em fechamento incerto. Registre watches antes de info/enum/SD e preserve Changed/Unknown; enumerações comparam conjuntos e nunca aceitam MORE_DATA como nome truncado. Feche keys em ordem reversa uma vez antes de eventos; qualquer falha de Close de chave/ownership incerto retém os eventos, sem retry/destrutor. Erro de leitura com todos os recursos conhecidos fechados pode ter Closed/0.

O diagnóstico separado `out/product/test-runs/media-storage-registry-binding-20261104/NativeBindingReadOnly.cpp` foi compilado/executado manualmente uma vez; fonte/objeto/imagem exata e recibos foram preservados. Aceita somente --readonly, sem path/chave livre, usando Software imediato sob HKCU_LOCAL_SETTINGS/OPEN_LINK e child Microsoft fixo. Bootstrap só consulta nome/watch; work keys usam Native DONT_REPARSE/opções 0 e recebem consultas/SD limitados. Há no máximo uma enumeração do primeiro child, sem imprimir/abrir o nome. Closes respeitam a origem Win32→RegCloseKey, Native→NtClose, evento→CloseHandle.

`native-binding-readonly.json`/`native-binding-run.json` registram imagem SHA-256 `195a39eb6e0a4e9558d7dbbd13275d826c17d0f1e5357f3a5a11a52d639fc577`, exit 0, três Notify 0/polls Wait0 Unchanged, três key closes 0/três event closes 0 e apenas erros/contagens na saída. Não foi solicitado UAC e não houve captura de token, chamadas de perfil ou WFP. O resultado não qualifica mudança real pelos watches, tipo comum, links incompletos, identidade/atomicidade/ABA, MIC ou política. O preflight histórico --readonly e seus sete RegClose 0 não foram ampliados nem substituídos por esse diagnóstico. Inventário físico das quatro raízes em perfil, produção e demais gates continuam pendentes; ADR-0011 Proposto, downloads OFF e autorização WFP consumida.

### Probe separado do inventário de storage

`rc_music_storage_inventory_probe` é outro executável, dedicado ao inventário do perfil próprio. Compõe Source viva, sessão nativa File/Registry, Recorder e owner durável; não depende do probe/helper WFP congelados ou de worker/rede. Na preparação `c840fe1`, o modo de perfil ainda não tinha sido executado; a [execução autorizada posterior](#inventário-autorizado-com-storage-ausente) está abaixo e seu consentimento já foi consumido. Cada nova execução exige autorização específica após build/revisão/commit, conforme [ADR-0011](../../../docs/architecture/decisions/0011-media-worker-appcontainer.md#contexto). O `--allow-profile-changes` é somente uma opção de admissão, não substitui a autorização do mantenedor nem reutiliza o consentimento WFP consumido.

Os comandos abaixo compilam/testam apenas a preparação. O CTest do probe chama exclusivamente `--self-test`, sem consultas de token, diretório, Registro, perfil ou storage nativo; help/recusas também não entram no modo host:

```powershell
cmake --build out/product/m1-desktop-release-on-20260906 --target rc_music_storage_inventory_probe rc_music_AppContainerStorageProbeOptions_tests rc_music_AppContainerStorageInventoryRun_tests rc_music_AppContainerStorageInventoryJson_tests rc_music_AppContainerStorageProbeEvidence_tests --parallel 3
ctest --test-dir out/product/m1-desktop-release-on-20260906 -R '^rc_music.profile-storage.(AppContainerStorage(ProbeOptions|InventoryRun|InventoryJson|ProbeEvidence)|StorageInventoryProbeSelfTest)$' --output-on-failure
```

Options passou **54/54 Qt**, Run **26/26**, JSON **44/44**, Evidence **25/25** e self-test **20/20 casos puros**. Chamadas de perfil/sessão são falsas no Run; Evidence usa filesystem/token somente em fixtures TEMP próprias, com store real/owner falso. Os detalhes de Red/Green e limites ficam no [registro central](../../../docs/media-profile-lifecycle.md#probe-separado-do-inventário-de-storage). Esses testes não confirmam um inventário AppContainer real ou sua política de escrita/MIC.

O fechamento ampliado passou **14/14 CTest, 538 Qt + 33 casos puros**, sem falhas/skips, formato **473 C++/62 QML**, build Lite e fronteira de **255 comandos**. O executável preparado tem SHA-256 `5a1bc6d3985be5291e4da230eab3eb2e398116781cbe0f0d373dc36a00529d1e`; isso identifica o artefato para revisão, sem autorizar seu modo de perfil. As duas novas fixtures Files intencionais e as dez históricas continuam preservadas.

O modo manual aceita somente `--allow-profile-changes --evidence-dir <nova folha absoluta no workspace compilado/out/product>`, sem nome/SID de perfil ou outros modos. Antes de qualquer preparação no host, exige processo primário Medium normal e ausência de token de thread. Deriva UUID internamente e só usa o locator do próprio backend S_OK. Não solicita elevação, instala, lança workers, acessa rede/WFP ou muda ACL/política do perfil. A pasta de evidência é criada NewOnly e permanece preservada; pins posteriores à criação não provam identidade atômica CreateDirectory→pin sob troca concorrente da folha.

Depois da autorização específica para o novo binário e a execução, a forma do comando é:

```powershell
$rcEvidence = Join-Path (Get-Location).Path ('out/product/test-runs/storage-inventory-' + [Guid]::NewGuid().ToString('D'))
& 'out/product/m1-desktop-release-on-20260906/rc_music_storage_inventory_probe.exe' --allow-profile-changes --evidence-dir $rcEvidence
```

O runtime Qt deve estar no PATH conforme o ambiente de build; o comando não baixa dependências. As folhas são fixas:

| Folha | Conteúdo |
|---|---|
| `00-before-profile.json` | UUID, imagem/hash diagnóstico, contexto do processo e intenção antes de criar perfil |
| `01-profile-result.json` | resultado do owner/criação e locator capturado da mesma operação/usuário |
| `02-inventory.json` | snapshot privado com owner/SD/flags e erros parciais, gravado com lease de leitura viva |
| `03-read-close.json` | pós-revalidação, fechamento e I/O do snapshot separados |
| `04-profile-cleanup.json` | pedido ao mesmo owner, Delete/ausência e Removed Saved, sem fallback |
| `05-result.json` | candidato `checks_passed_before_result_receipt`, antes de reconhecer a própria gravação |

O journal fica no novo filho privado `profile-journal`, com DACL protegida usuário/SYSTEM e duas ACEs de flags 0. Cada folha aceita no máximo uma aquisição, sem overwrite/delete/retry; Saved exige write/flush/close explícitos. Erro de close conserva o handle no processo até exit, sem ACK por destrutor. Apenas stdout posterior inclui `result_receipt_saved` e `passed`; exit 0 exige também fwrite/fflush completos. Readback exige job/UUID/SID/Removed exatos e nunca promove Save ou owner.close falho.

Run mantém observação, gravação e Closed/0 distintos; Recorder false/throw ainda fecha recursos conhecidos e cancelamento durante gravação resulta em Unknown/995 pela lease externa. Snapshot Incomplete não vira aprovação, mas pode permitir cleanup legítimo do perfil próprio após fechar as leituras. Storage/SID ausente continua Missing/Incomplete e não é criado para passar. A evidência/journal sempre é preservada, mesmo se o owner remover seu perfil com confirmação; incerteza preserva perfil/estado para revisão, sem limpeza recursiva de perfil/Registro. MIC, cotas/resíduos, política, rede e produção continuam pendentes; ADR-0011 Proposto e downloads OFF.

### Inventário autorizado com Storage ausente

Uma autorização específica permitiu uma execução dos bytes commitados de `c840fe1` e SHA-256 `5a1bc6d3985be5291e4da230eab3eb2e398116781cbe0f0d373dc36a00529d1e`, sem UAC, worker, rede/WFP ou alteração de política. Os recibos estão em `out/product/test-runs/media-storage-native-profile-20261104-01/`; o ledger mutável `authorized-native-run-plan.json`, `authorized-native-execution.json`, stdout e stderr ficam em `out/product/test-runs/media-storage-probe-20261104/`. O ledger atual registra `executed=true`, autorização `granted-one-shot-consumed`, fase `executed-native-inventory-incomplete` e exit terminal **1**, concordando com o recibo de execução. A imagem permaneceu inalterada, o driver foi PID **48884**, Medium **8192**, sessão **1**, não elevado/admin deny-only **16**, sem token de thread/erro **1008** e com creator SID correspondente. A autorização foi consumida, sem permissão para repetir o comando anterior automaticamente.

O perfil próprio `RCMusic.Media.edae67c8-7800-47e4-a251-9ff76ff44ee5` teve Create S_OK/SID liberado. O resultado foi **Incomplete/root, exit 1**: PackageRoot, `AC` e Mappings/SID presentes, com SDs **200/248/252 bytes** e owner **28 bytes** cada; o lookup da raiz completa Storage/SID retornou Missing/erro **2**. Não há identificação do componente que faltou nem prova de chegada isolada à folha SID. O Walker não enumerou descendentes nem correlacionou a sobreposição de `AC`; o snapshot permaneceu Unknown/not-called na validação e a pós-revalidação Unknown/2.

`02-inventory.json` foi salvo com **3494 bytes**, payload **784 bytes** e SHA-256 `34d2dcf292ef1b46a820af730fa3eee74c0a22279ac871bcd31ba1b9f2c4d385`. As leituras fecharam explicitamente com Closed/0; o mesmo owner registrou Delete 0/ausência Absent/Removed Saved. Os seis recibos tiveram I/O reconhecido, mas `05-result.json` conservou candidato falso e stdout `passed=false`, com `result_receipt_saved=true`. Encerramento/cleanup saudáveis não tornam o inventário Observed.

`07-independent-postflight.json` observou os quatro destinos exatos: PackageRoot erro **2**, `AC` erro **3**, opens OPEN_LINK dos paths completos Mappings/SID e Storage/SID erro **2**, sem HKEY/RegClose, e PID 48884 ausente. A primeira tentativa do script de leitura falhou por variável PowerShell reservada e só o postflight somente de leitura foi repetido; o probe não foi repetido. A conferência não prova universalmente não seguimento, resíduos ou atomicidade. Evidência/journal, perfil antigo e doze fixtures históricas continuam preservados; não houve cleanup alternativo. Storage ausente não foi criado ou tolerado para passar, e política/MIC, inventário completo, rede e produção continuam pendentes, com downloads OFF.

### Consulta contextual do Registro

`rc_music_registry_location_helper` é um helper separado, Win32 `/MT` sem Qt, preparado para consultar `GetAppContainerRegistryLocation` no próprio contexto AppContainer. Seu core `AppContainerRegistryLocationQuery` e o protocolo usam somente a biblioteca padrão. O binding Native não faz I/O ao ser construído. Esta preparação não executou `--query-context-registry`, worker ou perfil e não renovou as autorizações consumidas dos ensaios anteriores. A [descrição central](../../../docs/media-profile-lifecycle.md#consulta-contextual-do-registro) registra os contratos e limites.

Os comandos abaixo compilam a preparação e executam somente testes puros/falsos. No CTest, o helper recebe exclusivamente `--self-test`: parser, troca com portas falsas e três entradas Native que retornam antes de APIs. Os alvos Qt também usam token/Registro falsos, sem entrar no modo contextual:

```powershell
cmake --build out/product/m1-desktop-release-on-20260906 --target rc_music_registry_location_helper rc_music_AppContainerRegistryLocationProtocol_tests rc_music_AppContainerRegistryLocationQuery_tests --parallel 3
ctest --test-dir out/product/m1-desktop-release-on-20260906 -L registry-location --output-on-failure
```

O modo de consulta aceita somente pipes, request completo e EOF. O wire contém SID canônico de **40 bytes**, nonce de **32 bytes** e nome contado de até **32767 unidades UTF-16**, com tamanho/UTF-16 validados. Preserva HRESULT/NTSTATUS/Win32 brutos, inclusive `0xFFFFFFFF`, e distingue campo não observado de chamada que retornou esse valor. O core exige token primário AppContainer Low com SID correspondente, zero capabilities e ausência de token de thread; não faz impersonação. Solicita `KEY_READ`, sem `RegCreate*`, `RegSet*`, `RegDelete*`, SACL ou alteração de segurança. A documentação da API não garante ausência de efeitos de inicialização; não há qualificação de consulta somente de leitura.

O nome capturado permanece diagnóstico se o fechamento for incerto. Não há retry, ACK por destrutor, autoridade de cleanup por Reply ou inferência de negação a partir de timeout. O parent ainda precisa integrar `WindowsDownloadProcess`, runtime pinado/autenticado, capability/fence do mesmo owner, SID/nonce, timeout e ACK nativo de processo e pipes antes de um novo inventário por nova Source. Só depois dessa composição revisada/buildada/commitada poderá ocorrer um ensaio especificamente autorizado; não há comando automático de execução contextual nesta preparação.

Storage/SID Missing permanece sem solução, as quatro raízes continuam obrigatórias e nenhum canário foi implementado. DACL/MIC/política de storage, compatibilidade MF, rede e produção seguem sem aprovação; downloads OFF. `out/product/test-runs/media-registry-location-20261104/` conserva Reds e fechamento **3/3 CTest / 108 Qt + 25 casos puros**, sem falhas/skips. Formato **482 C++/62 QML**, build Lite e fronteira dos grafos passaram; o helper importa somente DLLs do Windows, sem Qt/CRT redistribuível. Esses checks não executam o modo contextual nem aprovam perfil/storage ou produção.

## Conversão com o mesmo owner

MP3, AAC/MP4 e WAV são convertidos e validados pelos workers reais com a mesma capability e fence do owner, sob sua lease. Isso avalia perfis específicos do Windows atual, não suporte universal, LPAC ou ausência universal de escrita. Recursos de sistema e armazenamento próprio do perfil continuam no modelo do AppContainer comum; escrita/cota/resíduos desse armazenamento mantêm gate antes da produção.

## Jornadas do owner e do supervisor

`WindowsNativeMediaProfile::system()` fornece as chamadas reais somente ao driver autorizado. `WindowsMediaProfileOwner` conserva a prova viva de sua própria criação; `WindowsDownloadMediaProfileStore` mantém o documento `profile-journal/download-media-profile.json`, limitado a 8 KiB, em diretório privado com locks de escrita e de lease distintos. Não há nova criação de perfil por fixture; os cenários compartilham o mesmo owner/capability e o fechamento é o último uso.

| Cenário | Critério implementado |
|---|---|
| NoChild | Imagem `.exe` própria existente, autenticada e deliberadamente inválida; start falha antes de admitir filho, fence retorna OpenIdle e journal permanece InUse |
| Backpressure de stdin | Helper `--hold-input <nonce>` aceita somente 32 hex minúsculos, após hardening do loader; emite `hold-ready:<PID>:<nonce>` e aguarda sem ler stdin ou abrir sockets/perfis; o driver envia payload limitado e verifica o processo vivo |
| Cancelamento e término nativo | Testemunha não herdável confere imagem/SID do processo e observa término após cancelamento sem fazer poll; a fence ainda OpenBusy não é promovida por esse término |
| Fechamento antes do ACK | Owner entra Draining, fence ClosingBusy, journal continua InUse e outra criação de processo é recusada; nenhuma remoção foi tentada |
| Drenagem e cleanup | Poll terminal Cancelled conclui os três pipes e fecha a fence em ClosedQuiescent; só depois de liberar os handles o owner pode gravar CleanupPending, remover e confirmar ausência antes de Removed Saved |

O modo hold usa apenas o fluxo de stdio necessário ao cenário e não executa as tentativas de isolamento do modo padrão. A [evidência física do novo fluxo](../../../docs/media-profile-lifecycle.md#ensaio-composto-autorizado) registra separadamente os critérios aprovados e o gate de rede inconclusivo. Destruir processo/ticket pendente deixa a fence Uncertain e bloqueia cleanup; esse cenário conserva cobertura determinística, sem execução física destrutiva adicional.

## Evidência e cleanup

Os receipts `00-before-profile.json`, `01-profile-result.json`, `02-helper-result.json`, `03-results.json` e `04-profile-cleanup.json` registram hashes, UUID, composição do owner, HRESULTs e resultados. `05-no-child.json` e `07-cancel-and-drain.json` registram as jornadas de ausência de filho e espera por ACK. O journal privado permanece junto da evidência. Um erro de criação não confirma cleanup; `ERROR_ALREADY_EXISTS` não autoriza derivar, reutilizar ou remover o perfil encontrado.

Somente o perfil criado com `S_OK` nesta execução pode ser removido, pela porta nativa do owner e depois dos ACKs nativos. Após Delete S_OK, a porta consulta o path de pacote capturado enquanto o perfil existia e as chaves SID exatas Mappings/Storage, sem seguir links/reparse. Somente ausência confirmada permite Removed Saved e liberação da lease; AccessDenied, resíduos ou erro de observação mantêm estado incerto. Essa observação tem escopo explícito, sem alegação universal de resíduos.

Deadline/destrutor não comprova parada. Se o ACK ficar incerto, novos casos e remoção do perfil são bloqueados, os proprietários incertos ficam retidos até a saída do driver e perfil/TEMP permanecem para revisão. Não há Delete alternativo no destrutor, limpeza de Registro por nome externo ou remoção recursiva como fallback de perfil. Loaded ativo nunca reconstrói propriedade viva; Failed AfterCommit exige nova escrita Saved, sem repetir chamada de sistema ou promover readback.

O TEMP só recebe cleanup automático no caminho integralmente bem-sucedido. O receipt precede o destrutor de `QTemporaryDir`; portanto, aguardar o processo terminar e verificar externamente o caminho TEMP registrado antes de afirmar ausência física. `FileNotFound`/`DirectoryNotFound` confirmam ausência; `AccessDenied` ou outro erro não confirmam. Preservar evidência e resíduos incertos.

## Histórico preservado

O [ensaio legado 03 de 2026-11-04](../../../docs/media-profile-lifecycle.md#ensaio-físico-autorizado-resultado-parcial) passou as três fixtures de mídia, mas conservou `helper_passed=false` e `passed=false`: erro bruto 1816 e timeout TCP 10060, sem a nova testemunha IOCP/owner. Seus recibos não são reinterpretados pelo protocolo v2 nem sobrescritos. Os ensaios compostos posteriores e o perfil preservado da primeira tentativa estão no registro central; as jornadas do owner aprovadas não promovem o gate de rede ou produção.
