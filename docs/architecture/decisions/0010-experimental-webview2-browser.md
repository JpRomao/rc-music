# ADR-0010: navegação Windows experimental com WebView2

**Estado:** Proposto

**Data:** 2026-11-03

**Escopo:** implementação opt-in de navegação visível para avaliar R1.9; não aceita engine de distribuição, instalação ou integração de downloads de produção.

## Contexto

O [ADR-0006](0006-browser-editions-and-extensions.md) exige uma Completa com navegação interna isolada e uma Lite sem engine. O SDK Qt atual não contém WebEngine/WebView. Este host já possui o SDK WebView2 1.0.3179.45 e runtime 156.0.4314.58; usar esses inputs explícitos permite desenvolver a superfície Windows sem reconstruir Qt ou instalar componentes. Existência no disco não prova execução, compatibilidade geral ou licença de um futuro pacote.

## Proposta experimental

Adicionar `RC_MUSIC_BUILD_EMBEDDED_BROWSER=OFF` e um caminho explícito de SDK. Somente ON compila/linka a integração Win32 e mostra Navegar no player. Uma janela modeless pertencente ao RC Music contém endereço, voltar/avançar/recarregar/parar e viewport nativo; páginas não recebem objetos Qt, presenter, filesystem, comandos ou serviços de download. O núcleo/player local e sua composição padrão permanecem independentes da engine.

WebView2 é criado por ação explícita, no STA com message pump, usando ambiente/perfil próprios. Não importar cookies, histórico, credenciais ou perfil Brave. Recusar navegação não HTTPS e bloquear popups, downloads nativos, permissões de sites e autenticação integrada; desabilitar host objects, web messages, DevTools, extensões, autofill e password autosave. Página em branco interna não autoriza `about:` fornecido pelo usuário. Cancelar erro de certificado sem bypass. [Segurança WebView2](https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/security), [threading](https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/threading-model).

SDK e loader estático são inputs locais: nenhuma busca/download automático, instalação, alteração de Registry ou configuração de Brave. O `LICENSE.txt` do SDK provisionado permite redistribuição sob condições BSD de três cláusulas; futuras cópias do loader devem conservar copyright/licença e notices aplicáveis. O runtime instalado tem distribuição/termos próprios e não será copiado ao pacote neste incremento. Suporte a runtime ausente, atualização do runtime Evergreen, overrides de ambiente/política e conjunto exato distribuído exigem revisão antes da edição final. A API pode receber overrides de ambiente/Registry: não apresentar parâmetros explícitos como autenticação de todos os componentes. [Criação do ambiente](https://learn.microsoft.com/en-us/microsoft-edge/webview2/reference/win32/webview2-idl).

## Alternativas e consequências

- Qt WebEngine permanece alternativa de produto, com peso, dependências/licenças e manutenção Chromium próprios; não reconstruir o SDK para este experimento.
- Browser externo continua integração da Lite, mas não satisfaz a navegação interna da Completa.
- A janela nativa separa o HWND de conteúdo da cena Qt Quick e evita prometer composição de overlays QML sobre ele. A [detecção/seleção experimental](../../embedded-browser.md#detecção-e-seleção-de-mídia) reutiliza o coletor puro da extensão, com DTO validado e operações correlacionadas no host; envio e execução remota continuam pendentes. Tabs e cobertura interna de frames ficam para incrementos posteriores.

## Validação e critérios de revisão

TDD com policy de navegação, binding e janela falsa confirmou Red 0/3 CTest; Green dos contratos 3/3 com 70 casos Qt. Configure/build Release ON passou e a seleção de UI foi revalidada após regressão de retry (Quick Red 271 passagens/1 falha → Green 272/272, ShellUiContract 35/35, 2/2 CTest). O conjunto de evidências reúne 377 casos Qt sem falhas/skips nos cinco alvos; os quatro cenários novos de UI usam engine falsa, e contratos C++ inalterados não foram repetidos por essa correção QML. Formato e QML lint passaram separadamente. Lite Release OFF mantém grafo de 255 comandos sem engine e lista zero testes. A receita local/Lite recusa a opção ON (planner 78/78, receita 5/5), sem prometer pacote Completa.

Não iniciar runtime, browser, site público ou perfil neste host por esses checks. Smoke real WebView2, foco/DPI/acessibilidade nativos, isolamento efetivo, privacidade/retenção de perfil, política externa/identidade do runtime, instalação/upgrade/notices e distribuição permanecem gates separados. O adapter reutiliza um diretório próprio por instância e não tenta apagá-lo antes de comprovar término dos processos. Falha ou ausência da engine deve preservar reprodução local. Evidência em [embedded-browser](../../embedded-browser.md); esta proposta não aceita R1.9/R1.10 completos nem ativa produção remota.
