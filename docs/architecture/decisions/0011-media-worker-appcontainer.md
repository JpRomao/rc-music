# ADR-0011: isolamento AppContainer do worker de mídia Windows

**Estado:** Proposto

**Data:** 2026-11-04

**Escopo:** caminho opcional de conversão/validação Media Foundation; não ativa downloads, instala componentes ou autoriza alterações no host.

## Contexto

O worker nativo recebe mídia por handle somente de leitura e devolve MP3 por stdout. O Job Object limita recursos e descendentes, mas não restringe acesso a arquivos ou rede. A [composição do player](../../download-jobs.md#composição-opcional-do-player-windows) já reúne conversão, publicação e importação durável; o isolamento efetivo continua sendo um gate do [ADR-0009](0009-download-execution-backends.md).

O fluxo documentado para aplicações sem pacote usa um perfil AppContainer. `CreateAppContainerProfile` cria armazenamento de arquivos e Registro por usuário/aplicação; o SID retornado deve ser liberado por `FreeSid`. Portanto, o ensaio físico exige autorização específica além de compilar o código. [Criação do perfil](https://learn.microsoft.com/en-us/windows/win32/api/userenv/nf-userenv-createappcontainerprofile).

## Proposta implementável

1. Usar AppContainer comum com lista vazia de capabilities. A composição confiável fornece o SID binário de um perfil previamente provisionado e conserva seu ciclo de vida até a drenagem de todos os processos. A capability valida e copia a identidade; não consulta, autentica, deriva ou cria perfis.
2. Preservar os três handles de stdio e a única entrada de mídia somente de leitura na lista explícita de herança. Nenhum handle de Job, token, diretório proprietário ou saída gravável é delegado ao codec.
3. Criar o processo suspenso com `SECURITY_CAPABILITIES`, conservando Job, cotas e deadlines. Conferir token AppContainer, SID esperado, integridade Low, capabilities vazias e pertencimento ao Job antes de retomar a thread. Falha de configuração, atributos, consulta ou retomada produz `Isolation` e drenagem, sem execução legada alternativa.
4. Criar somente a nova folha do runtime e seu EXE autenticado com DACL protegida: usuário/SYSTEM com controle total e SID específico com leitura/execução. Não alterar ACLs de pais, fontes, SDK, staging ou arquivos pessoais. Revalidar as ACLs por handle antes de cada subprocesso.
5. Manter produção remota OFF até a prova física e os demais gates de identidade, distribuição, resíduos e integração. Os caminhos legados permanecem restritos às suas composições explícitas de desenvolvimento.

O Windows documenta que a ausência da capability de rede restringe rede e que o acesso é a interseção dos direitos do usuário e do AppContainer. AppContainer comum ainda pode acessar recursos de sistema e seu armazenamento privado. A política não promete ausência universal de leitura/escrita no host. [Modelo e lançamento](https://learn.microsoft.com/en-us/windows/win32/secauthz/implementing-an-appcontainer).

## Alternativas

- Job Object ou integridade Low isoladamente: insuficientes para o gate de filesystem/rede.
- SID derivado de nome nunca provisionado: sem contrato suficiente para substituir o fluxo de perfil documentado.
- LPAC: superfície menor, mas COM/Registro usados pelo Media Foundation precisam de capabilities e validação próprias; não introduzir essas concessões como fallback.
- Converter no processo principal ou reduzir somente opções do codec: não preserva a fronteira de conteúdo não confiável.

## Validação e operação

### Dono durável e confirmação de uso — componentes implementados

O [contrato e a evidência do ciclo de vida](../../media-profile-lifecycle.md) detalham o modelo/JSON, store, lease, owner, fence e backend nativo implementados. Os testes unitários usam chamadas de perfil falsas; o probe manual autorizado compõe `WindowsNativeMediaProfile`, owner, store e fence reais. A composição no player permanece pendente. Isso não promove o estado Proposto deste ADR nem ativa produção remota.

Usar um terceiro documento privado fixo, `download-media-profile.json`, limitado a 8 KiB e a uma operação. Checkpoint e journal de arquivos mantêm seus schemas, nomes e limites. O registro contém somente owner/schema, job ID, UUID v4 de operação, estágio e SID canônico quando criado; nome é derivado internamente como `RCMusic.Media.<UUID>`, sem caminho ou nome livre vindo da UI.

O fluxo é `Planned → Created → InUse → CleanupPending → Removed`. `Planned → Abandoned` é permitido exclusivamente quando o dono vivo sabe que nunca chamou a API de criação; esse fato é registrado antes de liberar o slot. Não usar Abandoned para uma chamada falha/incerta ou colisão. `Created → CleanupPending` permite abortar antes do uso. Um registro terminal Removed/Abandoned permite outro Planned com UUID diferente; nenhuma operação pendente é expulsa. Replay idêntico exige nova gravação Saved.

Além do lock curto de gravação, uma lease exclusiva mantém `download-media-profile-owner.lock` aberto durante a operação viva. Isso impede dois donos, inclusive com a mesma identidade de operação, de atravessarem a janela entre ler o journal e criar o perfil. Liberar o handle após estado terminal Saved permite outra operação; destruição sem confirmação apenas libera a lease, conservando o registro ativo que bloqueará o próximo startup. O arquivo de lock vazio e privado não é removido por PID nem interpretado como perfil. Dados desconhecidos são inspecionados antes de tentar criar essa lease.

Criar somente após Planned Saved; emitir a capability somente após Created e InUse Saved. O dono conserva em memória a prova da própria criação bem-sucedida. Loaded não terminal, SID inválido, criação incerta, colisão ou escrita ambígua bloqueiam novos perfis e nunca autorizam adotar/remover o perfil encontrado. Crash entre criação e Created Saved deixa Planned incerto; readback não confirma um Failed do dono vivo. Retry de persistência deve executar outra gravação, sem repetir chamadas incertas de sistema.

A capability emitida pelo dono compartilha uma fence de uso nativo. O supervisor adquire um ticket antes da possibilidade de criar filho e confirma explicitamente ausência de filho ou parada nativa com os três pipes drenados. Destruir ticket pendente marca incerteza permanente; cancelamento, timeout, contagem de referências e destrutores não são ACK. O fechamento da admissão é irreversível e sincronizado com aquisição; somente a fence fechada e quiescente permite prosseguir.

Cleanup exige a prova viva de criação S_OK, CleanupPending Saved, remoção reconhecida e ausência confirmada por uma porta confiável, seguida de Removed Saved. A fence sozinha não autoriza remover perfil e protege a instância emitida pelo dono, não todo uso possível do mesmo SID no host. O owner foi exercitado com porta de perfil falsa e store real em árvores de teste próprias: 49/49 casos Qt do owner e 31/31 do store. Loaded não reconstrói autoridade de cleanup.

`WindowsNativeMediaProfile::system()` vincula as APIs reais sem chamá-las na construção. Seu uso é explícito no probe autorizado, para o usuário desktop atual e sem impersonation; não é um backend de serviço ou uma factory implícita do player. Uma instância admite uma tentativa de criação e conserva somente o par nome/SID criado com S_OK nesta chamada. Colisão, SID inválido, falha ao liberar SID, localização incerta ou exceção não autorizam adotar/remover perfil. Delete não é repetido após falha ou observação incerta, e o destrutor não chama APIs de perfil.

A observação de ausência usa o path de pacote capturado enquanto esse perfil existia e suas chaves SID exatas Mappings/Storage. A entrada de Registro usa o anchor predefinido `HKEY_CURRENT_USER_LOCAL_SETTINGS`, sem atravessar o alias REG_LINK de Classes; componentes abaixo dele são abertos sem seguir links. Reparse, links, AccessDenied ou observação desconhecida não confirmam ausência. Nenhuma chave de perfil ou ACL de ancestrais é reparada para tornar o ensaio aprovável.

A capability do owner compartilha a fence com o supervisor. Construtor/destrutor não chamam portas de perfil ou persistência; destruição fecha admissão e libera a lease sem inventar ACK, Removed ou Abandoned. Reentrada é limitada: close durante callback é retido e fecha admissão antes da continuação. Retry explícito de Failed regrava o mesmo registro pendente, sem repetir chamadas Create/Delete já executadas. BlockedByExistingData preserva o conflito.

Os testes automáticos comuns validam SID binário, recusa sem capability obrigatória, ACLs de folhas novas, alteração de ACL e compatibilidade legada. SIDs sintéticos usados nessas provas não podem ser lançados como perfis.

O probe manual deve exigir uma opção explícita e destino de evidência novo no workspace. Cria apenas perfil de teste com GUID interno, sem aceitar nome externo ou adotar perfil existente. Deve provar stdio/handle de leitura, negação de leitura/escrita em sentinela externo, escrita/alteração de ACL no runtime, rede com controle positivo local, Job e conversão/segunda validação de fixtures MP3/AAC/WAV. Timeout ou erro inespecífico não confirma negação de acesso.

Depois do ACK e fechamento dos handles, remover apenas o perfil criado nesta execução por `DeleteAppContainerProfile`, registrando HRESULT e resíduos. Falha deixa estado indeterminado; não substituir por remoção recursiva ou limpeza de Registro por nome. [Remoção oficial](https://learn.microsoft.com/en-us/windows/win32/api/userenv/nf-userenv-deleteappcontainerprofile).

### Evidência de 2026-11-04 e gate físico parcial

O fechamento anterior passou 18/18 CTest, com 707 passagens Qt e dois skips históricos de symlink do checkpoint; o parser puro do probe passou 1/1. O TDD preserva Reds de modelo/JSON, fence, store, lease, owner e supervisor. A regressão do ambiente MF passou de 32 passagens/1 falha para 33/33: no caminho AppContainer, LOCALAPPDATA vem de KnownFolder, sem criar pasta ou herdar valores do pai. A seleção ampliada do backend nativo e do probe passou 20/20 CTest, com 779 passagens nos 19 alvos Qt, nenhuma falha e dois skips históricos de symlink. Inclui 39/39 casos do backend nativo, 32/32 do protocolo e 59/59 do supervisor. Release dos documentos e do executável opcional passou, com zero testes registrados nos caches de produção; formato de 414 C++/62 QML e fronteira Lite de 255 comandos passaram. Os logs e limites estão no documento de ciclo de vida. Esses resultados não equivalem à suíte inteira ou à integração instalada.

Após autorização específica, os três probes físicos legados criaram perfis de teste novos e fizeram Delete S_OK depois dos ACKs. Conferências externas posteriores registraram ausência dos três paths de pacote exatos e das chaves SID exatas Mappings/Storage; os três diretórios TEMP de fixture continuavam presentes e foram preservados. A prova é limitada a esses paths/chaves, sem alegação universal de resíduos.

Os ensaios legados 01/02 falharam antes da execução. No legado 03, após corrigir o ambiente LOCALAPPDATA, as três fixtures MP3, AAC/M4A e WAV passaram por conversão e segunda validação independente. O helper permaneceu reprovado: máscara 29695/32767, faltando JobChildDenied (erro 1816 de quota) e LoopbackDenied (erro 10060 de timeout). Nenhum dos dois foi reclassificado como negação estrita; `media_passed=true` conservou `helper_passed=false` e `passed=false`.

Esses probes legados usaram capability sem a fence do owner e não atravessaram seu journal/lease ou sua porta abstrata. Seus resultados não validaram os hooks de propriedade/uso do owner. Os recibos de `media-appcontainer-native-authorized-20261104-{01,02,03}` e a [descrição histórica do resultado parcial](../../media-profile-lifecycle.md#ensaio-físico-autorizado-resultado-parcial) permanecem preservados e não são reinterpretados pelo protocolo novo.

### Probe com owner, fence e backend nativo

No ensaio `media-owner-native-authorized-20261104-01`, a API criou o perfil com S_OK, mas a captura do locator recusou o alias REG_LINK de Classes antes de liberar a capability ou iniciar workers. O journal permaneceu Planned e o perfil/TEMP foram preservados sem Delete. A correção usa o anchor predefinido de Local Settings, preservando a recusa de links abaixo dele e o guard de usuário desktop sem impersonation. Essa correção não reconcilia nem autoriza cleanup automático da operação 01.

Os ensaios `media-owner-native-authorized-20261104-02` e `media-owner-native-authorized-20261104-03` atravessaram owner, journal/lease, fence, supervisor e backend nativo reais. O 03 também exercitou a captura do path com a grafia exata retornada pelo Windows. As três fixtures passaram por conversão e segunda validação em ambos. O caso NoChild retornou a fence a OpenIdle; o cancelamento testemunhou término nativo antes do ACK do supervisor, manteve owner Draining/journal InUse e recusou novo lançamento. Somente a drenagem dos três pipes levou a ClosedQuiescent. Depois, Delete S_OK, ausência Absent e Removed Saved encerraram cada operação; as conferências externas registraram ausência dos pacotes e das chaves SID exatas.

Nesses ensaios, o helper conservou máscara bruta 29695 e máscara efetiva 30719, de 32767. O erro bruto 1816 foi preservado; a notificação `JOB_OBJECT_MSG_ACTIVE_PROCESS_LIMIT` do Job privado, sem notificações de memória observadas, comprovou a restrição específica de descendentes. A completion port não é herdada, coleta até 32 mensagens por poll, inclusive após estado terminal, e não altera ACK ou autoridade de cleanup. Ausência de notificação significa não observada, não prova de ausência de violação.

A rede continuou inconclusiva: TCP retornou 10060 e UDP retornou sucesso de envio, sem negação explícita. Nenhum desses resultados preenche `LoopbackDenied`; `helper_passed=false` e `passed=false` foram mantidos. Os TEMP dos dois ensaios permaneceram presentes e foram preservados pelo resultado global falso. O [registro central](../../media-profile-lifecycle.md) conserva os recibos, as conferências externas e os limites. LPAC, negação de rede concluída, cotas do armazenamento do perfil, composição no player e produção continuam pendentes.

### Observador separado executado com autorização específica

O [ensaio separado de 2026-11-04](../../media-profile-lifecycle.md#ensaio-separado-autorizado-sem-evento-correlacionado) executou uma vez os artefatos congelados de `48d8542`, com driver Medium/não elevado e ator High via UAC. Request/Ready/Result, imagem/identidade do peer e ACK nativo foram reconhecidos; janela de 15 s e Subscribe/Unsubscribe/EngineClose retornaram 0. Ator e launcher encerraram com exit 0; driver com exit 1. Os hashes de probe/helper/launcher permaneceram iguais e não houve alteração de firewall, coleta ou auditoria. A autorização foi consumida nessa execução e não autoriza outro ensaio elevado.

`proof_eligible=true` confirma transporte e lifecycle elegíveis, mas `first_candidate`/`first_match` nulos, `matched=false` e `rule_verified=false` impedem prova de bloqueio. TCP 10060, UDP 0 e máscaras do helper 29695/30719 foram preservados; a rede permanece inconclusiva, com `helper_passed=false` e `passed=false`. A ausência de evento na janela não comprova bloqueio ou ausência de bloqueio.

As três fixtures MP3/AAC/WAV, NoChild e cancelamento/drenagem passaram, com todos os ACKs nativos. O perfil próprio `RCMusic.Media.2f038dbf-921a-4bee-a105-23048f26a23e` teve Create S_OK, Delete 0 e Removed Saved. O postflight independente observou ausência das folhas exatas de pacote/Mappings/Storage e todos os RegClose 0, sem processos remanescentes. O TEMP `RCMusic-AppContainer-Probe-SisyTE` foi preservado pelo resultado global falso; o perfil antigo preservado não foi tocado. Isso não qualifica inventário nativo das quatro raízes, política de storage/MIC, integração instalada ou produção. O ADR permanece Proposto.

## Consequências e critérios de revisão

O [observador WFP do ensaio](../../media-profile-lifecycle.md#observador-de-eventos-de-rede-do-ensaio) acrescenta evidência correlacionada de CAPABILITY_DROP sem mudar firewall/coleta/ACL. A implementação e o protocolo de retenção foram validados por chamadas/eventos injetados. O preflight sem elevação recebeu AccessDenied 5; uma única leitura elevada autorizada confirmou coleta 1 e máscara 0, com todas as chamadas/fechamento reconhecidos. Não houve assinatura, perfil novo, alteração de opções globais ou evento real aceito. Timeout isolado continua inconclusivo; somente testemunha estrita com start/stop reconhecidos pode complementar 10060. Isso não promove este ADR nem ativa o owner no player.

O [oráculo separado ClassifyIsolation](../../media-profile-lifecycle.md#bloqueio-por-regra-de-isolamento) permite investigar os eventos já coletados sem exigir keyword4. Exige CLASSIFY_DROP correlacionado, direção 0x3901, filtro habilitado BLOCK, provider/sublayer App Isolation e camadas resolvidas ALE_AUTH_CONNECT_V4, com queries após Unsubscribe e antes de Close na mesma engine. Os 148 casos do observador passaram, mas nenhum evento real foi aprovado, inclusive no ensaio separado autorizado acima. As consultas descrevem objetos atuais, sem garantia de snapshot retroativo ou política imutável. Executar todo o driver elevado não comprova equivalência com o lançamento normal; o ensaio preservou driver/worker normais e observador privilegiado separado. A autorização anterior do preflight somente de leitura e a autorização posterior da execução separada foram consumidas em seus respectivos escopos.

O perfil adiciona ciclo de vida e armazenamento próprios que precisam ser geridos no produto/instalador. A criação transitória de teste não decide a política de perfis da distribuição. Compatibilidade com codecs Windows, negações reais, resíduos, execução interrompida e ausência de componentes continuam exigindo evidência específica. Adoção global, suporte universal e release não decorrem de testes de sintaxe/ACL ou de um único host.

As cotas atuais dos sinks não abrangem o armazenamento próprio do perfil. Sua política de escrita, limite de disco e resíduos precisa ser fechada antes da produção; este componente e o ensaio transitório não dispensam esses requisitos.

Revisar esta proposta se o Media Foundation não operar com capabilities vazias, se surgir necessidade de ampliar permissões, se o perfil não puder ser limpo por propriedade ou se o ensaio encontrar acesso fora do conjunto permitido. Qualquer concessão adicional exige justificativa e novos testes, sem downgrade silencioso.
