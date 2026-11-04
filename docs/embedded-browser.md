# Navegação interna Windows opcional

**Estado:** navegação e detecção/seleção de mídia experimentais opt-in; compilação e testes de contrato são separados do smoke com engine real. Downloads de produção permanecem desligados.

O [ADR-0010](architecture/decisions/0010-experimental-webview2-browser.md) propõe WebView2 para a primeira vertical visível da Completa, preservando o [ADR-0006](architecture/decisions/0006-browser-editions-and-extensions.md). O build padrão/Lite não compila nem linka a engine. Navegação não ativa download, importação ou Native Messaging.

## Inputs e composição

`RC_MUSIC_BUILD_EMBEDDED_BROWSER=ON` exige Windows e `RC_MUSIC_WEBVIEW2_SDK_ROOT` absoluto, contendo headers e loader estático x64. O SDK local revisado é `C:/Users/Joao/.nuget/packages/microsoft.web.webview2/1.0.3179.45`; o runtime já existente é 156.0.4314.58. Não há fetch, NuGet, instalação ou alteração de Registry. O runtime é descoberto pelas APIs oficiais; não se copia o runtime Evergreen do host nem se presume identidade fixa por existir um diretório versionado.

O SDK possui `LICENSE.txt` BSD de três cláusulas e `NOTICE.txt`, que precisam acompanhar uma futura distribuição aplicável do loader. O runtime possui termos próprios. Este experimento usa inputs já disponíveis e não aprova licenças/notices/instaladores da edição final.

Na sessão MSVC e com `@rcConfigure` do [build desktop](desktop-build.md#configure-build-e-testes), a composição pode ser compilada sem executar o aplicativo:

```powershell
cmake -S . -B out/product/complete-experimental @rcConfigure -DBUILD_TESTING=OFF -DRC_MUSIC_BUILD_EMBEDDED_BROWSER=ON "-DRC_MUSIC_WEBVIEW2_SDK_ROOT=C:/Users/Joao/.nuget/packages/microsoft.web.webview2/1.0.3179.45"
if ($LASTEXITCODE -ne 0) { throw 'Configure da navegação opcional falhou' }
cmake --build out/product/complete-experimental --target m0_qt_shell --parallel 2
if ($LASTEXITCODE -ne 0) { throw 'Build da navegação opcional falhou' }
```

Essa receita gera um executável de desenvolvimento dependente do SDK/runtime já provisionado. O planner de runtime local/Lite **recusa `RC_MUSIC_BUILD_EMBEDDED_BROWSER=ON`**; ausência da chave e OFF permanecem compatíveis com as receitas anteriores. Não usar o builder do instalador Lite para publicar essa composição como Completa. Runtime/notices/smoke/edição do pacote precisam de implementação e aprovação próprias.

## Fronteira e validação

O botão Navegar abre uma janela modeless do RC Music, com endereço e controles de navegação. Não há engine durante construção do binding ou startup do player; somente a ação explícita pode criá-la. Conteúdo remoto não recebe host objects, web messages, presenter ou acesso aos serviços de download. Perfil/cache são próprios, sem leitura ou importação do perfil Brave. Navegação explícita aceita HTTPS e recusa schemes locais/executáveis; permissões, popups e downloads nativos são bloqueados.

A janela usa Qt Widgets já presente no player e um HWND de viewport para o controller WebView2. Abertura, COM e callbacks ficam no STA da UI com message pump, sem espera síncrona pela inicialização. Callbacks usam referência fraca e geração da sessão; fechar/shutdown invalida callbacks antigos antes de remover handlers e fechar o controller. Endereço e mensagens de erro/progresso não expõem objetos da aplicação; falha preserva reprodução local e permite tentar novamente. SSO de conta do Windows, Basic Authentication, seleção de certificado cliente, bypass de certificado servidor, extensões, DevTools, autofill e password autosave ficam desabilitados/recusados. Accelerators nativos do browser também são desabilitados para bloquear caminhos como Ctrl+S/Ctrl+P, preservando os controles da toolbar e a edição normal do endereço. Isso não comprova todos os comportamentos/diálogos do runtime sem um smoke real.

O adapter escolhe **um diretório próprio por instância**, sob CacheLocation/EmbeddedBrowser, e conserva o mesmo valor ao fechar/reabrir. Clique repetido com engine ativo não cria outra árvore de perfil. Usa perfil InPrivate e acesso exclusivo, recusa overrides WEBVIEW2 explícitos no ambiente e confere o user-data folder efetivo antes de criar o controller. Políticas/Registry e carregamentos do runtime continuam confiança/gate do host: essa conferência não prova que a criação inicial não realizou acessos antes do callback. InPrivate não comprova ausência de arquivos residuais; não há deleção recursiva ou limpeza de diretório enquanto processos do runtime puderem utilizá-lo. Ownership e limpeza após término real, privacidade e políticas externas precisam de evidência própria antes do produto final.

Os testes de contrato usam janela falsa e não iniciam WebView2. Build ON comprova compilação/link da composição; grafo OFF comprova ausência do módulo. Smoke visual com runtime real, sites, foco/DPI/acessibilidade, privacidade, permissões, autenticação do runtime e distribuição permanecem evidências separadas. Nenhum teste falso comprova essas propriedades do engine real.

## Detecção e seleção de mídia

Na janela experimental, **Mídia → Detectar mídia** coleta candidatos da página principal e de Shadow DOM aberto. A lista permite escolher até 20 itens; **Conferir seleção** faz outra coleta e exige URL, título e tipo iguais para cada item escolhido. Mudanças em itens não selecionados ou na ordem não ampliam nem revogam a seleção por si só. Fonte selecionada alterada/removida exige nova detecção e escolha. **Baixar selecionados** permanece desabilitado, com a razão visível: a composição protegida de downloads ainda não está habilitada. A conferência não importa, baixa ou grava mídia e não vale como autorização permanente de envio futuro.

O mesmo `extensions/chromium/main/media-collector.js` atende a extensão e o recurso embutido no C++. A função é pura, sem chrome, timers, listeners ou estado persistente. A extensão conserva sua sessão V2 no mundo isolado e habilita frames acessíveis; o wrapper nativo cria um escopo local e usa `includeFrames:false`, sem sentinel global. Frames internos, roots fechados, blob/MSE, HLS/DASH, live e DRM continuam fora desta cobertura. YouTube não se torna elegível na principal por aparecer numa página ou nesta lista.

O host usa `ExecuteScript` apenas por ação explícita. O resultado é JSON não confiável, com schema fechado, UTF-8/strings/keys estritos, até 1 MiB, 100 candidatos, URL de 8.192 unidades e título de 200. Policy de URL e preflight JSON são compartilhados com o codec Native Messaging, sem incorporar serviço, rede ou host à navegação. URLs assinadas são conservadas na memória; a lista mostra título, tipo e host, sem query. A página não recebe objetos Qt, comandos ou serviços.

Tokens de operação e lifecycle pertencem ao host. Navegação principal, mudança de fonte, fechamento e falha invalidam dados e seleção; completions antigas são inertes. O adapter correlaciona `NavigationId` e também observa `SourceChanged`, pois mudança de fragmento pode ocorrer sem novo ID. Há uma única chamada de script pendente por engine; timeout de cinco segundos revoga o resultado, mas conserva o slot físico até callback ou fechamento do engine. Não há retry automático. A Microsoft documenta [ExecuteScript](https://learn.microsoft.com/en-us/microsoft-edge/webview2/reference/win32/icorewebview2#executescript) e a [ordem de eventos de navegação](https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/navigation-events).

Esse script roda no contexto da página, não no mundo isolado da extensão. DOM e APIs JavaScript podem ser controlados pela página; limites e contexto no host não autenticam o conteúdo ou provam isolamento do runtime. Nenhum WebView2 real, perfil ou site foi iniciado neste incremento. A futura ação Baixar deve realizar conferência fresca e handoff autorizado no mesmo fluxo, com os gates próprios de execução remota.

Evidências em `out/product/test-runs/browser-media-discovery-20261104/`: extensão baseline 150/150; coletor novo Red 1 passagem/10 falhas, Green 162/162 incluindo caracterização posterior do wrapper IIFE. Codec Native Messaging passou 188/188 antes/depois da extração. Novos codec/seleção tiveram Red 44/23 e 8/17, seguido de Green 68/68 e 25/25. Painel teve Red 2/15 e a regressão de aviso após timeout/navegação 17/1 → 18/18; tracker cancelado 6/1 → 7/7. Fechamento 7/7 CTest, 188 casos Qt, com painel reexecutado em 340 × 360/fontes Windows/falha em warnings inesperados. Builds/lint ON, Lite OFF/grafo 255/zero testes, biblioteca opcional Release OFF/zero testes e formato 383 C++/62 QML passaram. A única mensagem esperada do painel offscreen é a limitação exata de size hints; warnings de fonte ou de aplicação não são ignorados.

`verification.json` registra o grafo ON de 287 comandos com coletor/codec/painel e sem ingresso/execução de downloads. O executável experimental mantém x64 GUI (subsistema 2), com 2.330.112 bytes e SHA-256 `0e8b65ecdd24f23029ed41f1c890dbd448d1b08514ff906c6ed08af6f1cd8669`. O receipt final vincula as fontes commitadas; nenhum instalador foi atualizado neste incremento.

## Evidência anterior da navegação

Registros em `out/product/test-runs/embedded-browser-20261103`: Red comportamental **0/3 CTest**, com policy **39 passagens/14 falhas**, binding **2/10** e contrato nativo inerte **3/2**. Green inicial **3/3 CTest, 70 casos Qt** (policy 53, binding 12, contrato nativo 5), sem engine ou janela nativa criada. O contrato nativo usa QCoreApplication e owners inválidos, sem tentar inicializar WebView2; o binding usa uma porta falsa. Essas provas não simulam os efeitos reais de permissões/COM/network do runtime.

A guarda de packaging teve Red **66 passagens/12 falhas**, Green **78/78** no planner e **5/5** na receita. Ausência/OFF e recusa ON/valor inválido são testados nos entrypoints de produto e legado, sem mudar cache ou instalar componente.

Build Release ON com MSVC 19.44/Qt 6.11.2 e SDK WebView2 explícito passou, incluindo o executável real, sem executá-lo. A validação final reúne **377 casos Qt, zero falhas/skips**, em cinco alvos: os três contratos inalterados (**3/3 CTest**, policy 53, binding 12, contrato nativo inerte 5) e o fechamento atual de UI (**2/2 CTest em 39,14 s**, ShellUiContract 35 e Quick 272). Os quatro cenários novos QML exercitam Lite sem ação visível, abertura somente por clique, falha modal sem alterar playback e reabertura do aviso na repetição da mesma falha, sempre com engine falsa. `all_qmllint` passou para UI de produção e teste; formato de 11 C++/headers e três QML passou separadamente. O grafo ON inclui adapter/binding/loader estático e exclui a composição de download; o PE mantém subsistema Windows GUI, sem console. Executável de desenvolvimento: 2.250.240 bytes, SHA-256 `44b1f951cdd47caaf362a696ae2e14083785cb3f7a94fc21e7593c0123a66fc3` (`on-verification.json`).

A regressão de retry primeiro reproduziu **271 passagens/1 falha** em Quick: após dispensar o diálogo, a segunda tentativa com a mesma mensagem não o reabria. O fake passou a emitir `errorChanged` somente quando `errorText` muda, reproduzindo o contrato real; o click handler agora verifica também o erro retornado pela tentativa explícita, preservando a semântica do Q_PROPERTY. Green **272/272 Quick** e **35/35 ShellUiContract**, com rebuild do app e lint (`retry-dialog-*.log`); os três contratos C++ inalterados não foram reexecutados por essa mudança de UI.

O build Lite Release OFF passou separadamente, com `all_qmllint`, grafo de **255 comandos** e **zero testes** (`lite-off-*.log`), sem módulo/loader WebView2. Isso é compilação e inspeção do grafo; não um smoke do aplicativo instalado. Revisões de lifetime e das recusas de identidade/accelerators não substituem execução nativa. Nenhum WebView2/player compilado, site público ou browser Brave foi aberto, e não houve instalação, alteração de Registry ou aprovação de release.
