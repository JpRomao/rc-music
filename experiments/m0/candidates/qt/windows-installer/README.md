# Instalador Windows experimental — RC Music

O candidato possui módulo NSIS de associações, receita do runtime real e integração local de instalação/atualização/remoção. As seções abaixo preservam a evidência de cada incremento. Não é uma release distribuível: matriz publicada de formatos e jornada Explorer/Aplicativos Padrão permanecem pendentes. Os testes de associação/lifecycle usam `RCMusic.exe` sintético e inerte; o teste opcional de instalação real usa o runtime indicado, sem iniciar o player.

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

Na base de produção `Software`, os wrappers também chamam `SHChangeNotify(SHCNE_ASSOCCHANGED)` após sucesso. Essa chamada fica excluída da compilação isolada de teste; não foi validada no Explorer. O instalador deverá garantir payload íntegro, mesma lista de formatos na remoção e política explícita para mudanças dessa lista entre versões. Esta fatia testa reinstalação e troca de diretório com a mesma lista, não evolução da matriz ou instaladores simultâneos.

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

O resultado contém `app/`, `runtime-manifest.json` e `qml-imports.json`. O manifesto identifica HEAD, exige limpos os inputs compilados de shell/protocolo/fixture, registra receita suja ou limpa, hashes de receita/helper, arquivos e dependências externas. O módulo QML próprio, tradução pt-BR e assets já são estáticos no executável; não se copia a fixture nem um SDK para o payload. Qt/FFmpeg/TagLib vêm das suas origens explícitas; dependências de Windows e Microsoft Visual C++ Redistributable continuam pré-requisitos, não são copiadas de System32 ou do compilador do host.

O smoke usa ambiente reconstruído, sem paths Qt do SDK. Primeiro verifica que o executável OFF recusa os hooks com exit 1, streams vazios e configuração ausente. Depois executa uma cópia ON separada do payload, com configuração temporária, para observar estado vazio real e saída pelo lifecycle. Por fim, copia o teste de integração e QtTest somente para o probe, resolve QtTest pela origem de QtCore registrada no manifesto e exercita as nove linhas de load/play mutado/seek/pause/metadados com o runtime montado. Exige 11/11 incluindo setup/cleanup, sem skips; diagnósticos de corpus/FFmpeg são preservados e não equivalem a streams vazios. Isso não transforma os executáveis instrumentados em parte do aplicativo entregue nem comprova EOF, desktop nativo, áudio audível ou máquina limpa. A matriz experimental de sete extensões ainda não é uma publicação de suporte, e nenhum cadastro real de associações ocorre nesses comandos.

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

O template fixa uma raiz própria sob `LocalAppData/Programs/RC Music`, com diretório separado por ID. Não oferece redirecionamento para uma pasta arbitrária ou remoção de dados pessoais. O executável instalado continua usando os identificadores Qt `MusicPlayer`, fora da árvore de binários. Instalação e desinstalação são por usuário, sem elevação, serviço, tarefa agendada ou lançamento automático do player. Atalhos do menu Iniciar ainda são uma etapa separada.

Instalar prepara e verifica a nova versão antes de atualizar o cadastro ativo. Reinstalar a mesma versão exige marker e bytes íntegros. Atualizar verifica a versão anterior, publica a nova e aposenta a anterior por seu desinstalador com hash conferido contra o marker; mudanças no conjunto de arquivos são permitidas. Falha no cadastro conserva os payloads e estado pendente para retry, sem sucesso silencioso. Uma extração incompleta, sem marker íntegro, não é adotada automaticamente. Não há alegação de rollback atômico, recuperação em qualquer instrução ou proteção contra escritor hostil concorrente. Os hashes/markers são consistência local, não autenticidade contra outro processo do mesmo usuário. A assinatura da lista de formatos deve permanecer igual entre versões neste recorte; o mutex coordena a sessão Windows atual.

A desinstalação usa inventário compilado, hash, validação de caminhos/reparse e remoção não recursiva. Arquivos desconhecidos são preservados; arquivos próprios modificados ou em uso bloqueiam a operação antes da remoção. Não apaga preferências, sessão, logs ou mídia do usuário. O desinstalador antigo não retira o registro ativo de uma versão nova. As operações de arquivos seguem as [instruções NSIS](https://nsis.sourceforge.io/Docs/Chapter4.html), com `Delete` explícito e `RMDir` somente para diretórios vazios.

O builder local deixa associações **desabilitadas**: o manifesto experimental anterior não é uma matriz aprovada. Os testes do mesmo template habilitam MP3/WAV somente como schema sintético, sob `HKCU/Software/RCMusic.InstallerTests/<GUID>/Software`, com diretórios GUID ignorados. Não registram o player real no host, não escrevem `UserChoice` real e não notificam Explorer. O teste opcional com `-RuntimeDirectory` instala/remove bytes do runtime real dentro desse isolamento, sem iniciar o player. Registro real, alteração da lista de formatos, atalhos, inspeção da UI nativa, máquina limpa, segurança/licenças/assinatura e os demais gates M4.6/M4.8 continuam pendentes.

O check do planner passou **12/12**, o helper nativo **25/25** e o lifecycle **15/15**. O caso real passou **1/1**, verificando os **554 arquivos** instalados e removidos, origem intacta e sentinelas externas preservadas. A compilação real com associações OFF passou `/INPUTCHARSET UTF8 /WX`, inclusive num destino com `$R0`, `${RCMUSIC_PAYLOAD_ID}`, `${U+24}`, `$%TEMP%` e Unicode literais. O compilador tem deadline de 60 s; processos de instalação/remoção do harness mantêm 20 s, sem relaxar suas asserções.

O Red adicional de escape mostrou que `$$` não protege dados em todos os contextos NSIS. A receita distingue strings de compilação/runtime e transmite tokens ASCII pelos callbacks; apenas as instruções finais expandem os paths. As passagens de `${U+24}` seguem o [pré-processador NSIS 3.12](https://github.com/NSIS-Dev/nsis/blob/v312/Source/script.cpp). Outros Reds reproduziram remoção indevida com `PendingCleanup` e extração parcial não recuperável. O template corrigido bloqueia a primeira antes de mutações e limpa somente o inventário da tentativa fresca na segunda, antes de publicar cadastro; falha após cadastro continua conservando as versões para retry. Crash no meio dessas operações e falha durante a própria limpeza permanecem limites explícitos.

## Toolchain e referências

NSIS 3.12 foi obtido da [publicação oficial](https://nsis.sourceforge.io/Download) em ZIP e extraído somente sob `out/m0/toolchains`, sem instalação global. A dependência já corresponde ao baseline experimental de distribuição Windows da arquitetura; acrescenta um compilador local, não uma dependência de runtime do C++. O [recibo](nsis-toolchain.json) registra origem, tamanho e SHA-256 observados do ZIP/compilador, sem alegar assinatura do publicador ou autenticidade independente desses hashes. Uma nova máquina precisa provisionar o ZIP oficial correspondente e verificar os valores antes da extração/execução.

O projeto NSIS publica sua [licença e exceções](https://nsis.sourceforge.io/License); o harness usa compressão zlib. Esse recibo não fecha obrigações de redistribuição do futuro player ou de suas bibliotecas. A [documentação NSIS](https://nsis.sourceforge.io/Docs/Chapter4.html) fundamenta as operações e a remoção somente de chaves vazias.

O schema segue o cadastro por usuário descrito em [Configurações de aplicativos padrão](https://learn.microsoft.com/en-us/windows/apps/develop/launch/launch-default-apps-settings), [ProgIDs](https://learn.microsoft.com/en-us/windows/win32/shell/fa-progids) e [Open With](https://learn.microsoft.com/en-us/windows/win32/shell/how-to-include-an-application-on-the-open-with-dialog-box). A leitura estrita usa [RegGetValueW](https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-reggetvaluew). Nenhuma API legada para forçar padrões é utilizada.
