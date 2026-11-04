# Probe manual do worker em AppContainer

Este programa prepara o ensaio físico do [ADR-0011](../../../docs/architecture/decisions/0011-media-worker-appcontainer.md). O driver compõe owner, store, fence e porta nativa de perfil; ele não integra o player ou o instalador. Somente os autotestes puros de parser/protocolo/política de cleanup são registrados como modos automáticos do probe; não criam perfil, diretório, token ou entrada de Registro. O modo físico exige autorização específica.

## Compilação e autoteste

No [ambiente MSVC/Qt existente](../../../docs/desktop-build.md), com os opcionais de rede e testes configurados:

```powershell
cmake --build out/product/m1-desktop-release-on-20260906 --target rc_music_media_sandbox_probe rc_music_AppContainerProbeProtocol_tests --parallel 3
ctest --test-dir out/product/m1-desktop-release-on-20260906 -R '^rc_music\.download-network\.(MediaSandboxProbeParser|AppContainerProbeProtocol)$' -V
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
