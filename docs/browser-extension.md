# Extensão principal Brave/Chromium

**Estado:** cliente em desenvolvimento, sem host nativo, download/conversão ou integração instalada. Não é uma entrega funcional ponta a ponta nem extensão publicada.

Contratos: [ADR-0006](architecture/decisions/0006-browser-editions-and-extensions.md), [ADR-0007](architecture/decisions/0007-main-extension-boundary.md), [ameaças e limites](security/browser-extension-threat-model.md); R1.7/FR-051–053. YouTube segue extensão independente, ainda não implementada.

## Implementação do cliente

O diretório [extensions/chromium/main](../extensions/chromium/main/manifest.json) contém o pacote-fonte MV3, sem build npm, bibliotecas externas, rede própria ou engine embarcada. Não foi carregado no Brave do usuário nem incluído nos instaladores. O código C++ e os dados `MusicPlayer` permanecem intactos; o cliente será compartilhado pelas edições Lite/Completa quando a ponte comum estiver implementada.

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

Usar Node.js já disponível (validado com v24.20.0); sem `npm install` ou downloads:

```powershell
node --test tests/extensions/*.test.mjs
Get-ChildItem extensions/chromium/main/*.mjs, extensions/chromium/main/*.js | ForEach-Object { node --check $_.FullName; if ($LASTEXITCODE -ne 0) { throw 'Falha de sintaxe da extensão' } }
pwsh -NoProfile -File scripts/validate-docs.ps1
pwsh -NoProfile -File scripts/validate-traceability.ps1
git diff --check
```

O runner de extensão é separado dos comandos C++: Node é ferramenta de desenvolvimento deste componente opcional, não dependência de compilação ou execução da Lite. Alterar a extensão exige seus checks acima; builds Qt/CTest/QML não foram repetidos por este incremento sem mudança C++/QML/CMake.

## Evidência do incremento

TDD em 2026-09-08: policy começou com 9 falhas de normalização/seleção/recusa ausentes; cliente nativo com 7 falhas de envio/ACK/timeout ausentes; detector falhou por ausência do listener (`0 !== 1`); controller iniciou com 17 cenários sem comportamento. As implementações foram adicionadas depois dos Reds. A revisão acrescentou regressões para arrays esparsos, mutações contínuas e seleção durante verificação assíncrona. Resultado consolidado: **71/71 testes verdes** (26 detector, 25 popup/integração, 10 policy, 7 canal nativo, 3 manifesto), sem skips. Sintaxe dos scripts de produção/testes e `git diff --check` passaram. Documentação: 65 Markdown, 83 requisitos, 17 checks e 80 etapas; rastreabilidade: 15 planejados, 66 em andamento, 2 comprovados. Nenhum requisito foi promovido a comprovado por este incremento.

Os testes executam detector real em VM com DOM/Chrome/timers falsos, controller/DOM do popup, política real, canal nativo falso e manifesto/CSP estáticos. Não dependem de internet, contas, mídia pessoal, instalação ou Registry. Isso não comprova carregamento MV3/CSP no Brave real nem visualização nativa.

## Pendências e próxima integração

1. Gate real isolado do Brave/Chromium, incluindo popup/teclado, permissão temporária, navegação, iframe e ativação/remoção; não executar em perfil pessoal sem autorização.
2. Receptor nativo C++ com origem/identidade estáveis, framing Windows, idempotência e ponte para as duas edições; registro/ownership/upgrade permanecem em R1.10.
3. Serviço limitado/cancelável de download, validação DNS/redirects/TLS contra SSRF, fila/progresso, ferramentas de conversão revisadas e importação de MP3 finalizado. Sem essa revisão não habilitar downloads reais.
4. Evoluir descoberta para casos não cobertos: iframes, Shadow DOM aberto/fechado, mídia em tráfego anterior ao clique ou sem URL direta. Blob/MSE, manifestos HLS/DASH, live e DRM não são tratados como MP4 baixável nem contornados.
5. Navegação interna da Completa, opcionais no instalador e extensão YouTube têm implementação/gates separados. Este cliente não conclui R1.2/R1.7/R1.8, FR-051–053 ou release.
