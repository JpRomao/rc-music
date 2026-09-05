# Template de associações Windows — RC Music

Incremento funcional experimental de M4.6: módulo NSIS reutilizável de cadastro e remoção por usuário, com wrappers para as seções do instalador. Não é um instalador distribuível do player: deployment do runtime, matriz publicada de formatos e jornada Explorer/Aplicativos Padrão permanecem pendentes. O executável de teste chamado `RCMusic.exe` é somente um marcador de texto e nunca é iniciado.

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

## Toolchain e referências

NSIS 3.12 foi obtido da [publicação oficial](https://nsis.sourceforge.io/Download) em ZIP e extraído somente sob `out/m0/toolchains`, sem instalação global. A dependência já corresponde ao baseline experimental de distribuição Windows da arquitetura; acrescenta um compilador local, não uma dependência de runtime do C++. O [recibo](nsis-toolchain.json) registra origem, tamanho e SHA-256 observados do ZIP/compilador, sem alegar assinatura do publicador ou autenticidade independente desses hashes. Uma nova máquina precisa provisionar o ZIP oficial correspondente e verificar os valores antes da extração/execução.

O projeto NSIS publica sua [licença e exceções](https://nsis.sourceforge.io/License); o harness usa compressão zlib. Esse recibo não fecha obrigações de redistribuição do futuro player ou de suas bibliotecas. A [documentação NSIS](https://nsis.sourceforge.io/Docs/Chapter4.html) fundamenta as operações e a remoção somente de chaves vazias.

O schema segue o cadastro por usuário descrito em [Configurações de aplicativos padrão](https://learn.microsoft.com/en-us/windows/apps/develop/launch/launch-default-apps-settings), [ProgIDs](https://learn.microsoft.com/en-us/windows/win32/shell/fa-progids) e [Open With](https://learn.microsoft.com/en-us/windows/win32/shell/how-to-include-an-application-on-the-open-with-dialog-box). A leitura estrita usa [RegGetValueW](https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-reggetvaluew). Nenhuma API legada para forçar padrões é utilizada.
