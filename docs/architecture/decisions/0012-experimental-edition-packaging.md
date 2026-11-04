# ADR-0012: pacotes privados Lite e Completa experimental

**Estado:** Proposto

**Data:** 2026-11-04

**Escopo:** receitas de desenvolvimento locais; não aceita a engine final, distribuição pública, integração remota instalada ou execução do setup no host.

## Contexto

O [ADR-0006](0006-browser-editions-and-extensions.md) aceita duas edições da mesma aplicação. A [navegação experimental](0010-experimental-webview2-browser.md) já pode ser compilada, mas a receita Lite recusa esse build. Antes de gerar um pacote adicional é preciso definir identidade, pré-requisitos e substituição, preservando o player e seus dados.

## Contrato experimental implementado

1. `Lite` continua padrão. `CompleteExperimental` exige seleção explícita nas receitas de runtime e instalador; um manifesto não pode ser reinterpretado como outra edição. Manifestos antigos sem edição continuam Lite.
2. A Completa usa somente o entrypoint de produto, Release/x64, `BUILD_TESTING=OFF`, navegador ON e os cinco módulos de integração/downloads remotos OFF. SDK local explícito, headers, loader x64, licença e notices precisam existir sem reparse. A inspeção de build ON permanece separada da receita de empacotamento OFF.
3. As edições compartilham nome RC Music, identidade de instalação, root por usuário, mutex, atalho, ARP e dados `MusicPlayer/MusicPlayer`. Trocar a edição é substituir o único payload ativo pelo fluxo de verificação/handoff/cleanup já existente. Não introduzir coexistência independente, migração de dados ou um segundo dono do canal nativo. A identidade do payload inclui edição e pré-requisito, além dos arquivos e fontes da receita.
4. O pacote Completa inclui `WEBVIEW2-DEVELOPMENT.txt`, `LICENSE.txt` e `NOTICE.txt` do SDK com hashes no manifesto. O loader é estático. Não copiar o Evergreen instalado no host, incluir bootstrapper, baixar ou instalar o runtime. O setup informa que a navegação requer WebView2 Evergreen x64 já instalado; o player local conserva seu tratamento de engine ausente.
5. A factory compartilhada de opções pede somente o canal STABLE do WebView2, mantendo SSO e extensões desativados e perfil exclusivo. Essa máscara não autentica o runtime nem elimina overrides de Registro/políticas do host. Não afirmar descoberta/execução física validada sem smoke próprio.
6. Associações dependem da matriz explícita já existente. Integração externa, extensões e YouTube separado continuam nos seus gates; essa receita experimental não apresenta esses opcionais como instalados ou concluídos.

A Microsoft diferencia o WebView2 Runtime de canais do navegador e documenta a distribuição e os pré-requisitos. O pacote privado mantém a responsabilidade de provisionar o runtime fora deste setup. [Distribuição](https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/distribution), [seleção de canais](https://learn.microsoft.com/en-us/microsoft-edge/webview2/reference/win32/icorewebview2environmentoptions7?view=webview2-1.0.3179.45#put_releasechannels).

## Alternativas e consequências

- Incluir navegação em todos os pacotes rompe a Lite aceita no ADR-0006.
- Duas instalações independentes exigiriam um novo contrato de ownership/associações/IPC e dados; não adotar implicitamente no primeiro pacote.
- Distribuir ou instalar Evergreen acrescenta termos, manutenção e efeitos no host que não são necessários para construir este artefato privado.
- Identificar a edição apenas pelo nome da pasta ou do setup permite rotulagem incoerente; manifesto, receita e identidade precisam concordar.

## Validação e revisão

TDD cobre opt-in, SDK/flags, metadados, notices, identidade e encaminhamento dos parâmetros. O pré-processador NSIS real confere os textos e recusa combinações inconsistentes sem executar o setup. Builds Release ON/OFF, grafo Lite sem engine, inventários e hashes têm evidências separadas.

O primeiro configure Completa/OFF reproduziu a falta do target Qt Widgets no escopo raiz, ocultada pelo build de testes. Declarar a dependência no módulo opcional corrigiu o configure; isso não acrescenta Widgets à Lite, que já o usa no desktop.

Instalação, troca real Lite↔Completa, rollback/uninstall, processo em uso, privacidade/perfis, acessibilidade, identidade efetiva do runtime, licenças/SBOM completos e distribuição continuam pendentes. Revisar esta proposta antes de promover a edição final, adicionar instalação automática de dependências, permitir coexistência ou ligar serviços remotos.
