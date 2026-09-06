# Instalador Windows experimental — RC Music

**Atualização de 2026-09-06:** a [receita de runtime e seus helpers](../../../../../docs/windows-runtime.md) foram promovidos para `packaging/windows`. Os caminhos de runtime abaixo são encaminhadores compatíveis, não outra implementação. O instalador passa a registrar o hash real do planejador promovido; sua matriz e os artefatos históricos permanecem intactos. Este incremento não montou/instalou outro pacote nem revalidou formatos.

O candidato possui módulo NSIS de associações, receita do runtime real e integração local de instalação/atualização/remoção. A [matriz local explícita](#matriz-local-e-evolucao-dos-formatos) habilita somente os formatos básicos caracterizados com o runtime indicado; sem esse argumento, associações permanecem OFF. As seções abaixo preservam a evidência de cada incremento. Não é uma release distribuível: suporte público e jornada Explorer/Aplicativos Padrão permanecem pendentes. Os testes de associação/lifecycle usam `RCMusic.exe` sintético e inerte; o teste opcional de instalação real usa o runtime indicado, sem iniciar o player.

<a id="local-delivery-refresh"></a>

## Atualização da entrega local — a43faa1

O runtime `out/m0/rc-music-local/rc-music-windows-dev-a43faa1/app` reúne menu da fila vinculado à ocorrência, aviso de caminhos inválidos e ícone/propriedades do executável. Foi montado do commit `a43faa173fe65e5c9e826ef66d48b7de1415f775`, com receita limpa, **554 arquivos/96.282.231 bytes** e manifesto SHA-256 `2dd54f18aa9c0f24ff2e737c57d66efea1f4361fd231ae7636397013d481bc55`. A montagem incremental mantém SDK/receita anteriores, sem incluir o gerador do ícone ou executáveis de teste no payload.

O smoke `out/m0/test-runs/rc-music-runtime-smoke/e0e62723d611466b84df51744afedeaa/smoke-result.json` passou OFF/ON com streams vazios e áudio **18/18**, sem skips, usando somente o runtime declarado. Seu SHA-256 é `f644c45847d7bb0b38c27f146c9801f301a9c497f10722730e2501be8c5c9682`. A matriz foi vinculada a esses novos hashes; corpus, fonte do teste, sete extensões e limites dos perfis permanecem iguais. A atualização não é benchmark ou ampliação de formatos. Os arquivos históricos e o instalador `d793563` foram preservados.

O runtime mantém o commit de produção acima mesmo após o commit da matriz: gerar novamente o manifesto apenas para mudar HEAD invalidaria esse vínculo. A matriz foi commitada em `28c1ff2c9d88d2e3ed7bbeb02e21a212bf3d4720` antes da compilação do instalador; tanto o manifesto do runtime quanto o receipt do instalador registram `recipe_dirty=false`. A auditoria independente conferiu os 554 arquivos sem extras/reparse, exclusão de ferramentas/testes/mídia e propriedades do `RCMusic.exe` montado; seu oracle PE passou **5/5**, sem executar o player.

Artefato: `out/m0/rc-music-installer/rc-music-windows-dev-a43faa1/RCMusic-Setup.exe`, **38.076.668 bytes**, SHA-256 `a450d259f98babe55937b3fc99d0ed474888b3aed856ef3b8c011f9808e3e104`. O `installer-receipt.json` adjacente tem SHA-256 `2b83312fb712ea81e3cc15079ccfb721bd8032211a1fc16290d7816271bdae56`; vincula payload, matriz, configuração e receita limpa. A compilação `/WX` e seu wrapper confirmaram os 554 arquivos e sete extensões, habilitadas somente por `-FormatMatrix` explícito.

O caso `Installer.Tests.ps1 -RuntimeDirectory <novo>/app -CasePattern 'real runtime*'` passou **1/1**: instalação e remoção dos bytes reais sob `out/m0/it/e02ec6bd30034d06a68d03043fa95ab5`, origem imutável e sentinelas preservadas. Compila um harness isolado do mesmo template, com Registry/menu GUID e lista sintética MP3/WAV; não executa o setup de entrega ou o player no host. A lista real de sete extensões é conferida separadamente na compilação acima. Não extrapolar esse caso para upgrade real ou ativação via Explorer. Receita/C++/QML não foram alterados neste refresh; os 32/14 testes de lifecycle/associações e benchmarks não foram repetidos.

A revisão independente recalculou hashes, assinatura dos formatos e ID do payload sem divergências. Depois da remoção, o menu GUID continha somente a sentinela externa intacta, sem atalho próprio ou temporários; a subárvore de Registry estava ausente e os 554 arquivos de origem continuavam íntegros. Validação documental e `git diff --check` passaram.

Inspeção visual, associação real, máquina limpa/CRT, segurança, assinatura, licenças/notices/SBOM e aprovação de distribuição continuam pendentes. Esta entrega é privada e experimental, não uma release; nenhum aplicativo padrão, instalação pessoal ou menu Iniciar real foi modificado.

## Integração

O instalador futuro deve usar NSIS Unicode, `RequestExecutionLevel user`, `SetShellVarContext current` e uma única execução por vez. Incluir `RcMusicAssociationSections.nsh` depois de definir:

```nsis
!define RCMUSIC_REGISTRY_BASE "Software"
!macro RCMUSIC_FOR_EACH_FORMAT CALLBACK
    ; Inserir aqui somente extensões e descrições da matriz validada do pacote.
    ; Sem ponto inicial, sem duplicatas; nenhuma extensão é habilitada implicitamente.
!macroend
```

Inserir `RCMUSIC_INSTALL_ASSOCIATIONS` depois de colocar o executável real em `$INSTDIR\RCMusic.exe`; inserir `RCMUSIC_UNINSTALL_ASSOCIATIONS` antes de removê-lo. O caminho deve ser absoluto. Os wrappers abortam a seção com exit 1 se a operação falhar; não ignorar esse resultado nem apagar o payload depois de falha de remoção. As funções subjacentes retornam 0/1 na pilha e preservam seus registradores de trabalho, mas selecionam a vista 64 do Registry. Os wrappers usam `$0` para o resultado.

Na base de produção `Software`, os wrappers também chamam `SHChangeNotify(SHCNE_ASSOCCHANGED)` após sucesso. Essa chamada fica excluída da compilação isolada de teste; não foi validada no Explorer. Cada desinstalador conserva sua lista compilada. O instalador completo coordena a execução por mutex e aplica a política de evolução descrita na seção de matriz; usar o módulo isolado não fornece essa coordenação.

## Contrato do Registry

Todas as chaves ficam em HKCU, vista 64. Nos testes, `BASE` é `Software\RCMusic.AssociationTests\<GUID>\Software`; na integração futura, é `Software`. Todos os valores escritos são `REG_SZ`, incluindo o valor vazio de `OpenWithProgids`.

| Chave relativa a BASE | Valores próprios |
|---|---|
| `RC Music` | `AssociationProduct=RCMusic-v1`, `AssociationOwner=<exe>` |
| `RC Music\Capabilities` | Nome, descrição não vazia, ícone do executável e `FileAssociations` explícitas |
| `RegisteredApplications` | `RC Music=BASE\RC Music\Capabilities` |
| `Classes\RCMusic.Audio.<ext>` | Descrição, marcas de propriedade, `DefaultIcon` e `shell\open\command` |
| `Classes\.<ext>\OpenWithProgids` | `RCMusic.Audio.<ext>` com string vazia |

O comando é `"<caminho absoluto>\RCMusic.exe" -- "%1"`. Caminhos com espaços, Unicode e metacaracteres são exercitados sem shell intermediário. O template não altera `UserChoice`, o padrão de `.ext`, `OpenWithList` ou dados de outros aplicativos. Não grava `FriendlyTypeName` com um literal nem publica MIME/tipos não comprovados. O usuário continua escolhendo o padrão pelo Windows.

As colisões são verificadas antes dos writes. Leituras exigem tipo e conteúdo exatos, sem expansão de variáveis, truncamento aceito ou dados extras. A remoção exige marcas correspondentes e remove somente valores ainda iguais aos produzidos pelo instalador; entradas modificadas/desconhecidas são preservadas. Um desinstalador antigo não remove o cadastro de uma atualização concluída. A poda é somente de chaves vazias, nunca recursiva.

Não há transação de Registry, rollback geral ou coordenação entre instaladores neste módulo. Falhas intermediárias retornam erro; o contrato de retry após negação de permissão é coberto pelos testes abaixo. Isso não prova recuperação de interrupção do processo entre quaisquer duas instruções, nem substitui a política de recuperação do instalador completo.

## Check executável

Na raiz do repositório, com PowerShell 7 e Windows x64:

```powershell
./experiments/m0/candidates/qt/windows-installer/tests/Associations.Tests.ps1
```

O runner aceita `-MakeNsis <caminho>` e verifica o hash do compilador contra `nsis-toolchain.json`. O caminho local padrão é `out/m0/toolchains/nsis-3.12/makensis.exe`. Compila o harness real com `/WX`, executa instaladores/desinstaladores silenciosos com timeout e valida as operações com leitura independente via .NET Registry64. Não depende de Qt, rede, Pester, músicas pessoais ou áudio físico.

Cada execução cria uma chave GUID nova, usa exclusivamente essa subárvore e a remove no `finally`. As negações de permissão são temporárias, restritas a chaves dessa execução e restauradas com o handle retido antes da limpeza. O teste reaplica explicitamente a seção Access do descritor salvo e compara as regras/proteção efetivas após restaurar: um `RegistrySecurity` apenas lido não possui seções modificadas a persistir, conforme a [implementação .NET](https://raw.githubusercontent.com/dotnet/runtime/main/src/libraries/Microsoft.Win32.Registry/src/System/Security/AccessControl/RegistrySecurity.cs). Binários de teste ficam em `out/m0/test-runs/rc-music-associations/<GUID>`, ignorado pelo Git. O runner nunca instala associações reais nem notifica o shell.

Cobertura: cadastro explícito de MP3/WAV sintéticos, remoção de cadastro previamente semeado, reinstalação, atualização de diretório, desinstalador obsoleto, desinstalação repetida, conflitos sem mutação, preservação de valores externos/default/UserChoice, preservação de tipo modificado e falhas/retry de escrita e remoção. MP3/WAV aqui exercitam somente o schema; não são publicação de formatos suportados pelo player.

Red inicial: 4 passagens/7 falhas com funções inertes. Primeiro Green: 11/11. A revisão adicionou três regressões de retry; sua primeira execução tinha uma restauração de ACL ineficaz e não é usada como prova causal. Depois de corrigir a fixture e verificar suas permissões efetivas, a versão anterior do módulo confirmou novamente o Red de 11 passagens/3 falhas por instalação inicial/upgrade bloqueados e remoção órfã. A implementação corrigida passou **14/14**, incluindo a restauração verificada das ACLs. O fechamento documental fica no checkpoint do candidato Qt.

## Runtime real para a integração do instalador

`New-RcMusicLocalRuntime.ps1` monta o player Release real com `BUILD_TESTING=OFF`, renomeando somente o arquivo de saída para `RCMusic.exe`. Não usa o antigo pacote de fixture, não executa `package-v2` e não reconstrói o SDK. Faz um build incremental do aplicativo, descobre os imports QML com `qmlimportscanner` e resolve a árvore de dependências PE x64 a partir do SDK Qt composto, do overlay Multimedia/FFmpeg e do TagLib registrados no cache CMake. O compilador de shaders D3D e os plugins necessários também são incluídos explicitamente, pois nem toda carga dinâmica aparece nos imports PE.

Na raiz, no Developer PowerShell MSVC 14.44 x64 usado pelo build de produção:

```powershell
./experiments/m0/candidates/qt/windows-installer/tests/WindowsRuntimePlan.Tests.ps1
./experiments/m0/candidates/qt/windows-installer/tests/LocalRuntime.Tests.ps1 -OutputDirectory ./out/m0/rc-music-local/meu-runtime-01
./experiments/m0/candidates/qt/windows-installer/tests/LocalRuntime.Smoke.ps1 -RuntimeDirectory ./out/m0/rc-music-local/meu-runtime-01/app
```

O destino deve ser novo. O comando de montagem também aceita `-BuildDirectory`, que precisa apontar para a composição atual Release/Qt/áudio sem hooks. O helper valida fontes permitidas, paths, aliases Windows, duplicatas, reparse points e colisões antes de copiar; usa criação exclusiva e verifica hashes. Uma falha intermediária pode deixar um diretório parcial, não um runtime aprovado: não reutilizar esse destino. Não há rollback destrutivo nem proteção alegada contra um escritor hostil concorrente.

O resultado contém `app/`, `runtime-manifest.json` e `qml-imports.json`. O manifesto identifica HEAD, exige limpos os inputs compilados de shell/protocolo/fixture e os inputs promovidos da raiz (`CMakeLists.txt`, `cmake`, `include`, `src`, `tests`), registra receita suja ou limpa, hashes de receita/helper, arquivos e dependências externas. O módulo QML próprio, tradução pt-BR e assets já são estáticos no executável; não se copia a fixture nem um SDK para o payload. Qt/FFmpeg/TagLib vêm das suas origens explícitas; dependências de Windows e Microsoft Visual C++ Redistributable continuam pré-requisitos, não são copiadas de System32 ou do compilador do host.

A [promoção do core](../../../../../docs/core-build.md) ampliou a proteção de origem com Red/Green **38/38**, incluindo staged, unstaged e untracked em cada nova raiz. Não gerou outro runtime ou instalador; os manifests históricos conservam seus inputs e hashes originais.

O smoke usa ambiente reconstruído, sem paths Qt do SDK. Primeiro verifica que o executável OFF recusa os hooks com exit 1, streams vazios e configuração ausente. Depois executa uma cópia ON separada do payload, com configuração temporária, para observar estado vazio real e saída pelo lifecycle. Por fim, copia o teste de integração e QtTest somente para o probe, resolve QtTest pela origem de QtCore registrada no manifesto e exercita 16 linhas de load/play mutado/seek/pause/metadados/EOF com o runtime montado. Exige 18/18 incluindo setup/cleanup, sem skips; diagnósticos de corpus/FFmpeg são preservados e não equivalem a streams vazios. Os limites e a declaração local estão na seção de matriz abaixo. Isso não transforma os executáveis instrumentados em parte do aplicativo entregue nem comprova desktop nativo, áudio audível ou máquina limpa. Nenhum cadastro real de associações ocorre nesses comandos.

O `windeployqt` do SDK composto falhou no dry-run por procurar Multimedia dentro do SDK base em vez do overlay; não se ignorou essa falha nem se modificou o SDK. A receita explícita usa o scanner e a resolução PE, coerente com a [orientação Qt sobre bibliotecas adicionais e teste sem ambiente de desenvolvimento](https://doc.qt.io/qt-6/windows-deployment.html). Traduções genéricas Qt, licenças/notices/SBOM, segurança, assinatura, pré-requisitos em máquina limpa, instalador/upgrade/uninstall e associação pelo Explorer continuam gates distintos. `LOCAL-DEVELOPMENT.txt` acompanha o runtime para impedir confundi-lo com uma release distribuível.

## Instalador local por usuário

`New-RcMusicInstaller.ps1` transforma um runtime local íntegro e de receita limpa em `RCMusic-Setup.exe`. PowerShell 7 e NSIS são ferramentas de build, não dependências do computador de destino. A montagem não reinstala ferramentas, não reconstrói Qt e não executa benchmarks. O artefato permanece **privado e experimental**, sem assinatura, bootstrapper do CRT ou aprovação de distribuição.

```powershell
./experiments/m0/candidates/qt/windows-installer/tests/WindowsInstallerPlan.Tests.ps1
./experiments/m0/candidates/qt/windows-installer/tests/InstallerFiles.Tests.ps1
./experiments/m0/candidates/qt/windows-installer/tests/Installer.Tests.ps1
./experiments/m0/candidates/qt/windows-installer/tests/Installer.Tests.ps1 -RuntimeDirectory ./out/m0/rc-music-local/rc-music-windows-dev-717c88a/app -CasePattern 'real runtime*'
./experiments/m0/candidates/qt/windows-installer/tests/InstallerBuild.Tests.ps1 -RuntimeDirectory ./out/m0/rc-music-local/rc-music-windows-dev-717c88a/app
```

Para escolher um destino novo do artefato, o builder aceita `-RuntimeDirectory`, `-OutputDirectory` e `-MakeNsis` opcional. O receipt registra os hashes do instalador, compilador, configuração gerada, scripts/templates/helpers e inventário do runtime; não equivale a assinatura. O ID de cada versão inclui os bytes do payload e a receita do instalador, permitindo atualizar também uma correção exclusiva do desinstalador. Entradas inválidas, divergência de hash, build de teste e receita de runtime suja são recusadas antes de criar a saída. Falha de compilação pode deixar uma pasta parcial sem receipt aprovado; não reutilizar nem publicar essa pasta.

O template fixa uma raiz própria sob `LocalAppData/Programs/RC Music`, com diretório separado por ID. Não oferece redirecionamento para uma pasta arbitrária ou remoção de dados pessoais. O executável instalado continua usando os identificadores Qt `MusicPlayer`, fora da árvore de binários. Instalação e desinstalação são por usuário, sem elevação, serviço, tarefa agendada ou lançamento automático do player. O contrato do atalho do menu Iniciar está descrito abaixo.

Instalar prepara e verifica a nova versão antes de atualizar o cadastro ativo. Reinstalar a mesma versão exige marker e bytes íntegros. Atualizar verifica a versão anterior, publica a nova e aposenta a anterior por seu desinstalador com hash conferido contra o marker; mudanças no conjunto de arquivos são permitidas. Falha no cadastro conserva os payloads e estado pendente para retry, sem sucesso silencioso. Uma extração incompleta, sem marker íntegro, não é adotada automaticamente. Não há alegação de rollback atômico, recuperação em qualquer instrução ou proteção contra escritor hostil concorrente. Os hashes/markers são consistência local, não autenticidade contra outro processo do mesmo usuário. A evolução da lista segue o protocolo v2 abaixo; o mutex coordena a sessão Windows atual.

A desinstalação usa inventário compilado, hash, validação de caminhos/reparse e remoção não recursiva. Arquivos desconhecidos são preservados; arquivos próprios modificados ou em uso bloqueiam a operação antes da remoção. Não apaga preferências, sessão, logs ou mídia do usuário. O desinstalador antigo não retira o registro ativo de uma versão nova. As operações de arquivos seguem as [instruções NSIS](https://nsis.sourceforge.io/Docs/Chapter4.html), com `Delete` explícito e `RMDir` somente para diretórios vazios.

O builder local deixa associações **desabilitadas por padrão**; `-FormatMatrix` é a única entrada explícita para habilitá-las, conforme a seção abaixo. Os testes do mesmo template habilitam listas sintéticas sob `HKCU/Software/RCMusic.InstallerTests/<GUID>/Software`, com diretórios GUID ignorados. Não registram o player real no host, não escrevem `UserChoice` real e não notificam Explorer. O teste opcional com `-RuntimeDirectory` instala/remove bytes do runtime real dentro desse isolamento, sem iniciar o player. Registro real, inspeção da UI nativa, máquina limpa, segurança/licenças/assinatura e os demais gates M4.6/M4.8 continuam pendentes.

No fechamento inicial (`6f29af0`), o planner passou **12/12**, o helper nativo **25/25** e o lifecycle **15/15**. O caso real passou **1/1**, verificando os **554 arquivos** instalados e removidos, origem intacta e sentinelas externas preservadas. A compilação real com associações OFF passou `/INPUTCHARSET UTF8 /WX`, inclusive num destino com `$R0`, `${RCMUSIC_PAYLOAD_ID}`, `${U+24}`, `$%TEMP%` e Unicode literais. O compilador tem deadline de 60 s; processos de instalação/remoção do harness mantêm 20 s, sem relaxar suas asserções.

O Red adicional de escape mostrou que `$$` não protege dados em todos os contextos NSIS. A receita distingue strings de compilação/runtime e transmite tokens ASCII pelos callbacks; apenas as instruções finais expandem os paths. As passagens de `${U+24}` seguem o [pré-processador NSIS 3.12](https://github.com/NSIS-Dev/nsis/blob/v312/Source/script.cpp). Outros Reds reproduziram remoção indevida com `PendingCleanup` e extração parcial não recuperável. O template corrigido bloqueia a primeira antes de mutações e limpa somente o inventário da tentativa fresca na segunda, antes de publicar cadastro; falha após cadastro continua conservando as versões para retry. Crash no meio dessas operações e falha durante a própria limpeza permanecem limites explícitos.

## Contrato do atalho Windows

O instalador oferece uma entrada estável **RC Music** no menu Iniciar do usuário atual. Ela abre diretamente o executável da versão ativa, sem argumentos, com diretório de trabalho nessa versão, conforme o [contrato de `CreateShortCut`](https://nsis.sourceforge.io/Reference/CreateShortCut). Não cria atalho no desktop, entrada de inicialização automática ou lançamento ao terminar a instalação. Atualização deve redirecionar a entrada para a nova versão antes de remover a anterior; um desinstalador obsoleto não pode remover a entrada atual.

Os checks desse contrato substituem a raiz do menu por uma pasta GUID em `out/m0`, inclusive nos casos que não verificam atalhos diretamente. A leitura independente do `.lnk` confere destino, argumentos e diretório de trabalho. Colisões, links modificados, arquivos em uso e reparse points não autorizam sobrescrever ou apagar conteúdo externo. O menu Iniciar real não é alterado pelos testes, e a ativação pela interface nativa continua sendo um gate separado.

A propriedade exige hash registrado, não apenas nome ou destino coincidente. Isso preserva também personalizações de comentário/ícone: um atalho alterado bloqueia a operação com mensagem acionável; o usuário pode preservá-lo em outro nome antes de repetir a instalação. Um atalho ausente pode ser recriado numa reinstalação explícita. A publicação usa temporário único adjacente e rename, mantendo hashes atual/pendente para reconhecer uma publicação interrompida. Isso não promete transação geral, recuperação em qualquer instrução ou proteção contra escritor hostil concorrente. O desinstalador conserva a pasta compartilhada do menu e todas as entradas externas.

O cenário de migração recompila o template sem atalhos da revisão Git `6f29af02bbe124d0ba1bf34417ce56c82766c9ce` em uma pasta isolada. Esse objeto precisa existir no clone; o runner não usa rede para obtê-lo. O runtime permanece idêntico entre as versões desse cenário, comprovando a atualização de uma correção exclusiva do instalador, cujo ID inclui a receita.

O Red inicial confirmou a ausência do `.lnk` após instalação bem-sucedida. O Red adicional com a raiz do menu ocupada por arquivo mostrou falha tardia, depois da cópia/cadastro; a validação antecipada corrigiu esse caso sem mutar o snapshot. O planner passou **13/13** (Red **12/1**) e o lifecycle sintético **25/25**, sem skips: inclui os 15 cenários anteriores, os dez novos e leitura independente do atalho. Associações ON sintéticas e OFF são exercitadas; o retry do hash pendente termina sem temporário residual. Nenhum C++/QML ou helper de hash foi alterado; não repetir seus gates ou benchmarks neste incremento.

O caso real passou **1/1** com a receita de atalhos: **554 arquivos** íntegros, `.lnk` apontando para o executável instalado e origem preservada, sem iniciar o player. A inspeção adicional do menu isolado após a remoção encontrou somente a sentinela externa original, sem atalho próprio ou temporário. Sintaxe PowerShell, links/IDs documentais e diff também passaram; isso não substitui ativação pelo menu real, assinatura ou máquina limpa.

<a id="matriz-local-e-evolucao-dos-formatos"></a>

## Matriz local e evolução dos formatos

`formats.windows.local.json` é uma declaração revisada para o runtime Windows x64 privado, não uma aprovação de release. O planner confere seu vínculo SHA-256 com o manifesto do runtime, o corpus e a fonte do teste atual; exige escopo/status/backend esperados, limite de 64 KiB, extensões/descrições seguras e fixtures básicas válidas. Rejeita candidatos, duplicatas e combinações incompatíveis de extensão/contêiner antes de criar saída. O campo `result_sha256` referencia o recibo observado pelo mantenedor/agente: não executa testes, não autentica uma aprovação e não substitui revisão da evidência ao atualizar a declaração.

| Extensões declaradas | Contêiner e perfis caracterizados |
|---|---|
| `.mp3` | MPEG Layer III CBR 128/96 kb/s e VBR quality 4, inclusive tags ID3v2.4 Unicode |
| `.m4a`, `.mp4` | ISO MP4/M4A com AAC-LC 128 kb/s; sem promessa de vídeo ou outros codecs |
| `.flac` | FLAC 16-bit, compression level 5 |
| `.wav` | PCM signed 16-bit little-endian, mono |
| `.ogg`, `.oga` | Ogg/Vorbis quality 4 |

As sete fixtures duram 1–2 s e usam 48 kHz; não representam todos os perfis desses contêineres. As 16 linhas abrangem as sete originais, dois aliases e sete cópias temporárias com extensão neutra `.audio`. Cada jornada pausa, busca 50% com erro máximo de 500 ms observado no presenter e backend, volta a zero e reproduz até um único `Ended`, sem nova carga, mudança de geração/ocorrência ou erro. Duração e metadata continuam verificadas. Extensão neutra não vira associação: demonstra que os bytes, e não apenas o sufixo, foram decodificados. Não classifica Opus/ALAC, capa canônica, arquivos longos/hostis ou audibilidade física.

Na execução histórica `out/m0/test-runs/rc-music-runtime-smoke/34cf17dc7c64497f9ad3ac04804199c7/smoke-result.json`, Qt 6.11.2/FFmpeg 7.1.5/Windows 11 x64 passaram **18/18**, zero skips, em 31.723 ms. Esses hashes estavam na primeira declaração; a [atualização da entrega local](#local-delivery-refresh) registra o vínculo atual e sua nova execução. No incremento histórico, somente o alvo C++ de integração foi recompilado; não houve mudança no player entregue, no corpus congelado ou na aceitação da stack. OFF/ON do smoke conservaram streams vazios; os diagnósticos do probe de áudio foram preservados.

Para compilar o instalador com essa lista, execute na raiz:

```powershell
./experiments/m0/candidates/qt/windows-installer/tests/InstallerBuild.Tests.ps1 -RuntimeDirectory ./out/m0/rc-music-local/rc-music-windows-dev-a43faa1/app -FormatMatrix ./experiments/m0/candidates/qt/windows-installer/formats.windows.local.json
```

O builder também aceita `-FormatMatrix` diretamente e revalida seus bytes após compilar. Sem o argumento não infere lista nem abre a declaração padrão. O receipt inclui flag, assinatura da lista ordenada, extensões, contêineres/perfis/fixtures e declaração/hash/proveniência; matriz e assinatura participam do ID do pacote. A saída continua privada; compilar não executa o instalador nem altera associações reais.

Atualizações com `AssociationCleanupProtocol=2` permitem adicionar/retirar extensões e ON→OFF, conservando descrições das extensões retidas. Cada ProgID só é removido por seu proprietário; a limpeza de A retira entradas exclusivas de A e preserva as já publicadas por B. O cadastro global só é removido pelo proprietário ativo. A assinatura global representa a última publicação, não necessariamente o payload ainda ativo. Falha ao limpar A conserva ambos os payloads e `PendingCleanup`; retry de B conclui a aposentadoria. Não há enumeração irrestrita do Registry ou parser de matriz no computador de destino.

O desinstalador anterior é validado por hash e `/VERIFY` com sua própria lista. Legado sem protocolo v2 só migra com lista idêntica ou lista OFF canônica (`SHA256("[]")`); legado ON com lista diferente é recusado antes de mutações. Os testes recompilam também a revisão histórica `80286b712313e46af1fa35356f9e5e26bb66471c`, além da revisão sem atalho já registrada. Esses objetos Git devem existir localmente.

Evidência: planner Red inicial **15/2** → Green **17/17**; revisão de LF final malformado Red **17/1** → Green **18/18**. Evolução do instalador Red **1/7** → Green **7/7**; regressões **32/32** no lifecycle e **14/14** nas associações, sem skips. Cobrem legado OFF→ON, retirada/adição/retenção, ON→OFF, conflito estrangeiro, legacy ON incompatível e falha/retry com ACL efetivamente negada/restaurada. Menu/Registry permanecem isolados, preservando defaults, `UserChoice` e dados externos. Não há benchmark, registro real, inspeção de UI nativa ou fechamento de M4.6/M4.8.

O smoke de instalação real também passou **1/1** com esta receita, verificando os **554 arquivos**, origem intacta e remoção sob raízes GUID (`out/m0/it/3d5eb93f6b2d404c914ff7fe67db6e68`), sem iniciar `RCMusic.exe`. Esse caso usa a lista sintética do harness; a declaração real de sete extensões é conferida separadamente pelo build do instalador. Formato do único C++ alterado, sintaxe dos seis PowerShell alterados, validação documental (46 Markdown/83 requisitos/17 checks/76 etapas) e diff passaram. QML/produção não mudaram e seus gates amplos não foram repetidos.

## Toolchain e referências

NSIS 3.12 foi obtido da [publicação oficial](https://nsis.sourceforge.io/Download) em ZIP e extraído somente sob `out/m0/toolchains`, sem instalação global. A dependência já corresponde ao baseline experimental de distribuição Windows da arquitetura; acrescenta um compilador local, não uma dependência de runtime do C++. O [recibo](nsis-toolchain.json) registra origem, tamanho e SHA-256 observados do ZIP/compilador, sem alegar assinatura do publicador ou autenticidade independente desses hashes. Uma nova máquina precisa provisionar o ZIP oficial correspondente e verificar os valores antes da extração/execução.

O projeto NSIS publica sua [licença e exceções](https://nsis.sourceforge.io/License); o harness usa compressão zlib. Esse recibo não fecha obrigações de redistribuição do futuro player ou de suas bibliotecas. A [documentação NSIS](https://nsis.sourceforge.io/Docs/Chapter4.html) fundamenta as operações e a remoção somente de chaves vazias.

O schema segue o cadastro por usuário descrito em [Configurações de aplicativos padrão](https://learn.microsoft.com/en-us/windows/apps/develop/launch/launch-default-apps-settings), [ProgIDs](https://learn.microsoft.com/en-us/windows/win32/shell/fa-progids) e [Open With](https://learn.microsoft.com/en-us/windows/win32/shell/how-to-include-an-application-on-the-open-with-dialog-box). A leitura estrita usa [RegGetValueW](https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-reggetvaluew). Nenhuma API legada para forçar padrões é utilizada.
