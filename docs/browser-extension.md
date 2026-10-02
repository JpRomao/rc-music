# Extensão principal Brave/Chromium

**Estado:** cliente e biblioteca de recepção C++ em desenvolvimento; sem executável de host nativo, download/conversão ou integração instalada. Não é uma entrega funcional ponta a ponta nem extensão publicada.

Contratos: [ADR-0006](architecture/decisions/0006-browser-editions-and-extensions.md), [ADR-0007](architecture/decisions/0007-main-extension-boundary.md), [ameaças e limites](security/browser-extension-threat-model.md); R1.7/FR-051–053. YouTube segue extensão independente, ainda não implementada.

## Implementação do cliente

O diretório [extensions/chromium/main](../extensions/chromium/main/manifest.json) contém o pacote-fonte MV3, sem build npm, bibliotecas externas, rede própria ou engine embarcada. Não foi carregado no Brave do usuário nem incluído nos instaladores. O playback e os dados `MusicPlayer` permanecem intactos; o cliente será compartilhado pelas edições Lite/Completa quando a ponte comum estiver integrada.

- Abrir o popup permite inspecionar temporariamente a página principal autorizada. Áudios, vídeos, elementos `source` e links MP3/MP4/M4A geram candidatos deduplicados, não downloads.
- Enquanto o popup está aberto, alterações DOM/eventos de mídia atualizam a lista de forma limitada. Ao fechar ou navegar, observadores/timers são encerrados. Reinjeção não duplica a infraestrutura. Um único listener inerte de conexão permanece no mundo isolado até o documento ser descartado, sem observação, coleta ou timer na ausência do popup.
- Lista com checkboxes, seleção individual/múltipla até 20, confirmação de uso autorizado, contador, truncamento e mensagens pt-BR. Títulos são texto literal; não renderizar HTML, previews remotos ou query strings da página.
- Somente “Baixar selecionados” produz pedido. O popup verifica aba, documento e URL atuais antes de enviar; novos itens não entram na seleção automaticamente e remoção/mudança relevante invalida consentimento.
- `policy.mjs` valida URLs/schema/limites; `native-client.mjs` abre o canal fixo somente no envio, valida resposta correlacionada e encerra sem retry automático. Nenhum comando arbitrário, path de saída, cookie ou header atravessa o contrato.
- Host ausente, resposta malformada, timeout e desconexão aparecem como falha/recebimento não confirmado. ACK significa apenas pedido aceito; conclusão/conversão/importação pertencem ao serviço futuro do app.

## Protocolo nativo de desenvolvimento

Host planejado `com.rcmusic.media`, não registrado. Pedido v1:

```json
{
  "schemaVersion": 1,
  "type": "download.request",
  "requestId": "01234567-89ab-4cde-8fab-0123456789ab",
  "items": [{ "url": "https://media.example.test/audio.mp3", "title": "Áudio", "kind": "audio" }]
}
```

`kind` é dica `audio`/`video`, não prova de codec ou segurança. O contrato pretende saída MP3; o serviço futuro precisa validar bytes e converter áudio de MP4, sem renomear extensão. O pedido não envia URL da página, histórico ou seleção não autorizada.

Resposta aceita tem exatamente `schemaVersion: 1`, `type: "download.response"`, mesmo `requestId`, `status: "accepted"` e `jobIds` distintos (um por item). IDs de job usam 1–80 caracteres ASCII alfanuméricos/`_`/`-`. Rejeição usa `status: "rejected"` e `code` de `unsupported_source`, `invalid_request`, `busy`, `not_available` ou `limit_exceeded`, sem `jobIds` ou texto livre. A decisão do lote é atômica; não existe aceitação parcial silenciosa.

Receber timeout ou desconexão não prova que o host não aceitou o pedido. Não reenviar automaticamente. A futura fila do app deverá permitir consulta e cancelamento e deduplicar IDs, sobrevivendo ao fechamento do popup. `OpenSources` continua estritamente local e não recebe este JSON.

## Checks locais

### Cliente

Usar Node.js já disponível (validado com v24.20.0); sem `npm install` ou downloads:

```powershell
node --test tests/extensions/*.test.mjs
Get-ChildItem extensions/chromium/main/*.mjs, extensions/chromium/main/*.js | ForEach-Object { node --check $_.FullName; if ($LASTEXITCODE -ne 0) { throw 'Falha de sintaxe da extensão' } }
pwsh -NoProfile -File scripts/validate-docs.ps1
pwsh -NoProfile -File scripts/validate-traceability.ps1
git diff --check
```

O runner de extensão é separado dos comandos C++: Node é ferramenta de desenvolvimento deste componente opcional, não dependência de compilação ou execução da Lite. Alterar a extensão exige seus checks acima. Nenhum QML mudou neste incremento; lint/testes de UI nativa e instalação continuam gates separados.

### Biblioteca nativa Windows

O alvo opcional `rc_music_browser_integration` compila [codec](../src/adapters/NativeDownloadCodec.cpp), [framing](../src/adapters/NativeMessageFrame.cpp), [sessão](../src/adapters/NativeDownloadSession.cpp) e o model de jobs com Qt Core 6.11.2 já provisionado. A [coordenação de downloads](download-jobs.md) implementa a porta pura `DownloadSubmission`: aceita ou recusa o lote inteiro e devolve IDs, sem baixar/converter sincronicamente. A integração de sessão usa esse serviço real nos testes, mas execução/importação ainda são falsas e não há conexão instalada.

`RC_MUSIC_BUILD_BROWSER_INTEGRATION` é OFF por padrão e não depende da opção desktop. O alvo não está ligado ao player, não distribui engine, não registra host e não instala arquivos. Com testes OFF não exige Qt Test; o core com ambas opções OFF continua sem Qt. Isso preserva a composição leve, mas ainda não prova os futuros pacotes Lite/Completa.

Na sessão MSVC descrita em [build do core](core-build.md#build-windows), com ferramentas locais:

```powershell
$rcSource = (Resolve-Path .).Path
$rcQt = Join-Path $rcSource 'q/f75eb9bea8280844c6d1a2b3f4/sdk'
$rcCatch2 = Join-Path $rcSource 'out/m0/toolchains/Catch2-3.15.3'
$rcNinja = 'C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe'
cmake -S . -B out/product/browser-integration-tests -G Ninja -DCMAKE_BUILD_TYPE=Release -DRC_MUSIC_BUILD_BROWSER_INTEGRATION=ON -DRC_MUSIC_BUILD_DESKTOP=OFF -DBUILD_TESTING=ON "-DCMAKE_PREFIX_PATH=$rcQt" "-DRC_MUSIC_CATCH2_SOURCE_DIR=$rcCatch2" "-DCMAKE_MAKE_PROGRAM=$rcNinja"
if ($LASTEXITCODE -ne 0) { throw 'Configure da ponte falhou' }
cmake --build out/product/browser-integration-tests --target rc_music_NativeDownloadCodec_tests rc_music_NativeMessageFrame_tests rc_music_NativeDownloadSession_tests rc_music_NativeDownloadService_tests rc_music_DownloadJobsModel_tests --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Build da ponte falhou' }
ctest --test-dir out/product/browser-integration-tests -R '^rc_music.browser.' --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Testes da ponte falharam' }
```

O CTest configura PATH e saída Qt dos cinco executáveis; não precisa de janela/áudio/rede. O filtro é obrigatório se só esses alvos foram compilados, pois o configure também registra os testes do core e do serviço puro. O build independente sem testes usa outro diretório, `-DBUILD_TESTING=OFF`, e alvo `rc_music_browser_integration`. Para incluir a suite Catch2 de coordenação, usar os [checks de jobs](download-jobs.md#checks-locais-windows).

### Contrato de recepção

- Uma mensagem por sessão, prefixo de 32 bits little-endian do Windows, corpo de 1 byte a 256 KiB validado antes da reserva. Recepção fragmentada tem deadline total de 5 s; EOF, excesso, segunda mensagem ou relógio regressivo fecham sem nova admissão.
- Origem deve coincidir exatamente com uma extensão configurada (`chrome-extension://` + 32 letras `a`–`p` + `/`). A configuração é entrada confiável do futuro host, não do JSON nem derivada do próprio chamador. Isso não autentica processos maliciosos do mesmo usuário.
- JSON UTF-8 estrito, profundidade limitada, chaves duplicadas inclusive escapadas recusadas antes do parser Qt, schema/campos/tipos exatos e lote atômico. URLs já normalizadas pelo cliente preservam escapes/query; não reserializar via QUrl e alterar assinaturas. Títulos usam até 200 unidades UTF-16, sem controles/bidi ou surrogates isolados; domínios têm rótulos até 63 e nome até 253 caracteres sem ponto final.
- A sessão fecha a admissão antes de chamar a porta, inclusive em reentrância. Exceção/receipt contraditório não geram ACK falso. O deadline de montagem não preempta uma implementação bloqueante de `submit`; a porta exige enfileiramento rápido e não faz downloads.
- O overload assíncrono observa `AsyncDownloadSubmission` por UUID, sem dirigir IO/tick/execução. Pending não tem ACK/IDs nem mensagem própria no wire; completion válida responde v1, com um ID distinto por item. Há prazo total próprio de 5 s inclusivos depois do frame completo, sem renovação por poll; clock regressivo falha fechado. EOF/entrada extra/timeout só fecham o observador, sem cancelar/repetir trabalho do dono.
- O serviço comum mantém idempotência entre conexões e coordena progresso/cancelamento via portas. A [admissão durável](download-jobs.md#admissão-durável-assíncrona) conecta controller/worker/store e só torna aceitação observável após Saved matching; novo startup restaura histórico inerte. O overload síncrono não oferece essa garantia de durabilidade e não deve servir ao host durável futuro. Transporte stdin/stdout binário, identidade instalada, composição do app/UI e duas edições continuam pendentes. Nenhuma URL é aberta por essa biblioteca.

## Evidência do incremento

TDD em 2026-09-08: policy começou com 9 falhas de normalização/seleção/recusa ausentes; cliente nativo com 7 falhas de envio/ACK/timeout ausentes; detector falhou por ausência do listener (`0 !== 1`); controller iniciou com 17 cenários sem comportamento. As implementações foram adicionadas depois dos Reds. A revisão acrescentou regressões para arrays esparsos, mutações contínuas e seleção durante verificação assíncrona. Resultado consolidado: **71/71 testes verdes** (26 detector, 25 popup/integração, 10 policy, 7 canal nativo, 3 manifesto), sem skips. Sintaxe dos scripts de produção/testes e `git diff --check` passaram. Documentação: 65 Markdown, 83 requisitos, 17 checks e 80 etapas; rastreabilidade: 15 planejados, 66 em andamento, 2 comprovados. Nenhum requisito foi promovido a comprovado por este incremento.

Os testes executam detector real em VM com DOM/Chrome/timers falsos, controller/DOM do popup, política real, canal nativo falso e manifesto/CSP estáticos. Não dependem de internet, contas, mídia pessoal, instalação ou Registry. Isso não comprova carregamento MV3/CSP no Brave real nem visualização nativa.

### Incremento nativo de 2026-09-08

Depois do baseline do cliente acima, os stubs C++ confirmaram Red comportamental em pedidos válidos/respostas ausentes, montagem de frames (3 passaram/32 falharam) e sessão (2 passaram/18 falharam). A saída do Qt Test precisou das mesmas flags de logging dos runners existentes; isso não exigiu instalar/copiar DLLs. Regressões adicionais expuseram tolerâncias do Qt para escapes, números JSON e BOM, além de divergências de normalização de URL. O cliente teve Red para percent inválido, surrogate isolado/corte de emoji e nomes DNS excessivos, seguido das correções.

- Build Release MSVC 19.44/toolset 14.44, Qt Core/Test 6.11.2, `/W4 /WX`: **3/3 alvos CTest**, **193/193 casos Qt** (137 codec, 35 framing, 21 sessão), sem skips. Diretório local `out/product/browser-integration-tests`; não foi alegada execução dos testes do core desse diretório.
- Extensão: **110/110 testes**, incluindo **36 vetores compartilhados** em [native-protocol-vectors.json](../tests/extensions/native-protocol-vectors.json). BOM inicial é escapado no arquivo da fixture para o loader de teste não o remover antes da validação; testes byte-level também cobrem o BOM UTF-8 literal.
- Regressão do núcleo sem Qt/ponte: rebuild de `out/product/m1-core-release-20260905`, **227/227 CTest**, opção da ponte OFF e ausência de Qt/ponte no grafo.
- Componente Release com `BUILD_TESTING=OFF`: biblioteca compilada em `out/product/browser-integration-release`, zero testes e nenhuma dependência Qt Test/Catch2/WebEngine no grafo. Não é runtime nem instalador novo.
- Formato dos dez C++ novos, sintaxe JS, validação documental (65 Markdown/83 requisitos/17 checks/80 etapas), rastreabilidade e `git diff --check` aprovados. Revisão independente sem achados materiais restantes no recorte.

Nenhum requisito foi promovido a comprovado: 15 planejados, 66 em andamento, 2 comprovados. Não houve benchmark, execução instalada/nativa, mídia pessoal, rede de download ou alteração do player. Executável de host, admissão idempotente real e workflow completo continuam próximos trabalhos, não resultados deste incremento.

## Pendências e próxima integração

1. Gate real isolado do Brave/Chromium, incluindo popup/teclado, permissão temporária, navegação, iframe e ativação/remoção; não executar em perfil pessoal sem autorização.
2. Integrar a biblioteca de recepção a um executável Native Messaging com stdin/stdout binário, origem/identidade estáveis e transporte para o serviço comum no app; acrescentar persistência/recuperação dos recibos. Registro/ownership/upgrade permanecem em R1.10.
3. Executor limitado/cancelável de download, validação DNS/redirects/TLS contra SSRF, ferramentas de conversão revisadas, importador e tela QML sobre a [coordenação já implementada](download-jobs.md). Sem essa revisão não habilitar downloads reais.
4. Evoluir descoberta para casos não cobertos: iframes, Shadow DOM aberto/fechado, mídia em tráfego anterior ao clique ou sem URL direta. Blob/MSE, manifestos HLS/DASH, live e DRM não são tratados como MP4 baixável nem contornados.
5. Navegação interna da Completa, opcionais no instalador e extensão YouTube têm implementação/gates separados. Este cliente não conclui R1.2/R1.7/R1.8, FR-051–053 ou release.
