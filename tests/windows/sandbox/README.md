# Probe manual do worker em AppContainer

Este programa prepara o ensaio físico do [ADR-0011](../../../docs/architecture/decisions/0011-media-worker-appcontainer.md). Ele não integra o player ou o instalador. Somente o autoteste puro do parser/protocolo/política de cleanup é registrado no CTest; esse modo não cria perfil, diretório, token ou entrada de Registro.

## Compilação e autoteste

No [ambiente MSVC/Qt existente](../../../docs/desktop-build.md), com os opcionais de rede e testes configurados:

```powershell
cmake --build out/product/m1-desktop-release-on-20260906 --target rc_music_media_sandbox_probe --parallel 3
ctest --test-dir out/product/m1-desktop-release-on-20260906 -R '^rc_music\.download-network\.MediaSandboxProbeParser$' -V
```

O helper é Win32 `/MT`, sem Qt; o driver usa os adapters do produto. `--help`, argumentos ausentes/inválidos e `--self-test` não alcançam o modo que altera o host. O autoteste trata nomes DOS reservados, formato e limites do protocolo e confirmação de limpeza somente após criação bem-sucedida e ACKs.

## Execução física — autorização específica necessária

`CreateAppContainerProfile` cria pastas e armazenamento no Registro do usuário. O contrato do projeto e a autorização anterior para gerar sem instalar não concedem essa operação. Executar o comando abaixo somente após aprovação específica do mantenedor para esses testes temporários.

O nome do perfil é um GUID gerado internamente; não há opção para fornecer nome, SID, perfil existente ou reutilizar uma criação anterior. O destino de evidência precisa ser novo, local, sem reparse e dentro de `out/product` do workspace compilado. O programa usa fixtures próprias e um listener efêmero de loopback, com controle positivo no processo principal; não consulta sites ou mídia pessoal.

```powershell
$rcQtBin = (Resolve-Path 'q/f75eb9bea8280844c6d1a2b3f4/sdk/bin').Path
$env:PATH = "$rcQtBin;$env:PATH"
$rcEvidence = Join-Path (Get-Location).Path 'out/product/test-runs/media-appcontainer-native-20261104-01'
& 'out/product/m1-desktop-release-on-20260906/rc_music_media_sandbox_probe.exe' --allow-profile-changes --evidence-dir $rcEvidence
```

O teste confere token/SID/Low/capabilities vazias, stdio e entrada de leitura; tenta acesso a um sentinela externo, escrita/alteração de ACL/owner/delete no runtime, criação de descendente e loopback. Só negações específicas passam; timeout ou sharing violation são inconclusivos. A restrição de descendentes pertence ao Job Object. O helper confirma os quatro handles autorizados; a ausência de herança adicional tem cobertura separada no supervisor.

MP3, AAC/MP4 e WAV são convertidos e validados pelos workers reais com a mesma lease. Isso avalia perfis específicos do Windows atual, não suporte universal, LPAC ou ausência universal de escrita. Recursos de sistema e armazenamento próprio do perfil continuam no modelo do AppContainer comum; escrita/cota/resíduos desse armazenamento mantêm gate antes da produção.

## Evidência e cleanup

Os receipts `00-before-profile.json`, `01-profile-result.json`, `02-helper-result.json`, `03-results.json` e `04-profile-cleanup.json` registram hashes, GUID, HRESULTs e resultados. Um erro de criação não confirma cleanup; `ERROR_ALREADY_EXISTS` não autoriza derivar, reutilizar ou remover o perfil encontrado.

Somente o perfil criado com `S_OK` nesta execução pode ser removido, pela API oficial e depois dos ACKs nativos. Deadline/destrutor não comprova parada. Se o ACK ficar incerto, novos casos e remoção do perfil são bloqueados, os proprietários incertos ficam retidos até a saída do driver e perfil/TEMP permanecem para revisão. Não há limpeza de Registro por nome externo ou remoção recursiva como fallback de perfil.

O TEMP só recebe cleanup automático no caminho integralmente bem-sucedido. O receipt precede o destrutor de `QTemporaryDir`; portanto, aguardar o processo terminar e verificar externamente o caminho TEMP registrado antes de afirmar ausência física. `FileNotFound`/`DirectoryNotFound` confirmam ausência; `AccessDenied` ou outro erro não confirmam. Preservar evidência e resíduos incertos.
