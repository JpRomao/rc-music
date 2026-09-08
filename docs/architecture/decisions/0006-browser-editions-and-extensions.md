# ADR-0006: edições Lite/Completa e extensões separadas

**Estado:** Aceito

**Data:** 2026-09-06

**Decisor:** mantenedor do projeto

**Escopo da aceitação:** organização do produto solicitada pelo mantenedor. Não representa funcionalidades implementadas, escolha definitiva da engine de navegação, aprovação de ferramentas externas ou autorização para instalar/publicar.

## Contexto

Atualização de implementação em 2026-09-08: o [ADR-0007](0007-main-extension-boundary.md) especifica o primeiro cliente da extensão principal. O estado abaixo descreve o momento desta decisão; host/download, edições completas e instalação continuam pendentes.

O mantenedor pediu uma extensão que detecte mídia nas páginas, apresente uma lista com seleção individual/múltipla e envie downloads MP4/MP3 ao RC Music, convertendo o áudio de MP4 em MP3. Confirmou Brave como alvo primário, com compatibilidade Chromium em geral; retirou YouTube da lista obrigatória da principal, destinando-o a outra extensão; e pediu uma edição Lite como o player atual e outra com navegação interna, ambas integradas à extensão.

Hoje existe somente o player local, sem extensão, download remoto ou navegador embarcado. O [ADR-0005](0005-windows-product-stack.md) mantém a stack C++/Qt aceita. O [ADR-0003](0003-local-first-and-remote-sources.md) permanece proposto para resolução/reprodução remota: baixar um arquivo e depois importá-lo não exige aprovar streaming remoto no backend local.

## Decisão de produto

| Composição planejada | Player local | Extensão externa Brave/Chromium | Navegação interna |
|---|---|---|---|
| Lite | Núcleo e funcionalidades atuais preservados | Integração opcional disponível | Ausente, inclusive engine e processos no pacote |
| Completa | Mesmo núcleo e funcionalidades da Lite | Mesma integração opcional | Módulo isolado adicional |

- Manter uma única base de código e contratos compartilhados; não criar dois players independentes. A integração opcional não pode tornar a engine embarcada uma dependência da Lite.
- A extensão principal detecta candidatos de áudio/vídeo em páginas autorizadas e mostra uma lista ao clicar no ícone, com seleção de um ou vários itens. Detectar não inicia download nem grava áudio de microfone/sistema. Só a ação explícita de baixar envia os selecionados ao app.
- Downloads têm fila separada da reprodução, progresso, cancelamento e erros acionáveis. MP3 concluído pode ser importado no fluxo local; MP4 precisa de extração/conversão real de sua faixa de áudio para MP3 antes de entrar na fila do player. Vídeo sem áudio, proteção ou codec indisponível produz erro, não um MP3 fictício ou mera troca de extensão. A retenção opcional do vídeo original será definida na UX; arquivos pessoais não serão sobrescritos nem apagados implicitamente.
- YouTube pertence a uma segunda extensão opcional, com pacote, identidade, permissões, instalação e validação próprios. Ausência ou falha dela não bloqueia a principal, a Lite ou a Completa. A principal não oferece YouTube direta ou indiretamente por encaminhamento para a outra extensão/backend. O backend deve distinguir as capacidades de cada cliente autorizado, sem confiar em uma flag enviada pela página.
- Em ambas as edições, a integração externa é opcional durante a instalação e inicialmente desmarcada. A opção deve explicar o componente local e o passo de ativação exigido pelo navegador; cancelar/desmarcar preserva o player funcional. A extensão YouTube não é instalada como dependência oculta. Upgrade/uninstall retiram somente os componentes/registros pertencentes ao RC Music e preservam perfis do navegador.
- A navegação interna da Completa é uma superfície separada da UI privilegiada. Deve oferecer o mesmo fluxo de seleção/envio de mídia, usando os serviços comuns; isso não promete que a engine interna execute literalmente a extensão do Brave. Executar pacotes de extensões dentro do navegador embarcado depende de capacidade comprovada e decisão técnica posterior.
- Conteúdo remoto não recebe acesso direto ao presenter, filesystem, comandos nativos ou execução arbitrária. Não importar cookies, credenciais ou histórico do Brave; o perfil interno é separado. Permissões de sites devem ser mínimas e explícitas. Sem telemetria, DRM bypass ou promessa de suporte universal a sites.

## Desenho técnico a validar antes da implementação

O fluxo pretendido é seleção autorizada → pedido tipado → download/conversão isolados → arquivo local validado e finalizado → importação pelo caminho existente. O protocolo local `OpenSources` conserva paths locais; não vira um executor genérico de URLs ou comandos. Rede, redirects, limites de tamanho/tempo/disco, cancelamento, nomes seguros, arquivos parciais e logs redigidos precisam de threat model e testes próprios.

Manifest V3 e Native Messaging são candidatos para Brave/Chromium. O mecanismo oficial do Chromium permite uma ponte nativa com origens de extensão explícitas; ainda é preciso validar a compatibilidade e o registro específicos do Brave. Não será aberto um servidor HTTP local genérico como atalho para a integração. [Native Messaging](https://developer.chrome.com/docs/extensions/develop/concepts/native-messaging).

Qt WebEngine é um candidato para a Completa: oferece integração C++/QML e processos separados de renderização, mas acrescenta bibliotecas/processos ao pacote e ciclo de correções Chromium. A escolha, compatibilidade com a toolchain existente, distribuição/licenças e política de atualizações devem ser decididas antes de adicioná-lo; não será incluído na Lite. [Visão oficial do Qt WebEngine](https://doc.qt.io/qt-6/qtwebengine-overview.html).

Downloader e conversor ainda não foram escolhidos como dependências do produto. Executáveis instalados no computador de desenvolvimento não são automaticamente componentes aprovados do instalador. Versões, hashes, licenças e atualização explícita precisam de registro; argumentos serão tipados, sem shell concatenado, auto-update ou código remoto oculto.

## Políticas e distribuição

A separação é de produto, não uma forma de eliminar restrições do provedor/loja. A documentação da Chrome Web Store lista facilitar downloads do YouTube como motivo de rejeição. A principal deve ser revisada pelo comportamento efetivo, não apenas pelo nome/pacote; não há garantia de aprovação de nenhuma extensão. [Troubleshooting oficial](https://developer.chrome.com/docs/webstore/troubleshooting/).

Os termos do YouTube restringem downloads fora das autorizações previstas pelo serviço ou de permissão escrita aplicável. A extensão independente continua sujeita à revisão técnica/de termos de FR-050/FR-052, para usos autorizados; sua publicação ou uso não é aprovado por este ADR. [Termos do YouTube](https://www.youtube.com/static?template=terms).

No Chrome/Windows comum, o fluxo externo documentado passa pela loja e confirmação do usuário. Não presumir instalação silenciosa de CRX local nem aplicar políticas empresariais para contornar consentimento. Brave e demais Chromium terão instalação/remoção verificadas separadamente; o formato de entrega das edições permanece a definir. [Instalação externa no Chrome](https://developer.chrome.com/docs/extensions/how-to/distribute/install-extensions).

Antes de empacotar, definir upgrade Lite↔Completa, eventual coexistência e qual instalação é dona do canal nativo/associações. Não presumir duas instalações simultâneas, sobrescrever o registro de outra edição ou migrar os dados `MusicPlayer` para resolver essa escolha.

## Alternativas e consequências

- Um player único sempre com navegador: rejeitado pelo peso/dependência imposto a quem quer somente a Lite.
- Dois forks de player: rejeitados por duplicação, divergência de comportamento e manutenção.
- YouTube embutido na principal ou exigido por ela: rejeitado pela separação explícita e por acoplar os riscos de suporte/distribuição.
- Apenas navegador externo: insuficiente para a edição Completa pedida; continua sendo o caminho da Lite.

O escopo aumenta: haverá matriz de duas edições, cliente externo e interno, opcionais, instalação/upgrade e isolamento. A existência de uma edição não comprova os gates da outra. Os budgets continuam metas; nenhum benchmark é retomado por este documento.

## Critérios de aceitação e revisão

1. Provar com testes que Lite não carrega/distribui engine de navegação e funciona offline sem opcionais.
2. Provar a mesma integração externa em Lite e Completa, preservando instância única, dados e comandos do player.
3. Testar detecção incremental, deduplicação, seleção múltipla, envio explícito, desconexão, rejeição e limites com páginas/áudios sintéticos; testes padrão não usam sites ou contas pessoais.
4. Testar download/conversão/cancelamento/falha com portas falsas e servidor local controlado, depois com pequenas fixtures próprias; só importar arquivo finalizado e validado.
5. Provar isolamento entre principal e YouTube, inclusive pedidos indiretos identificados como YouTube, sem expor comandos genéricos a páginas.
6. Validar a seleção opcional e remoção por proprietário sem instalar no host durante gates sintéticos; execução real exige autorização própria.
7. Revisar esta decisão se isolamento, requisitos de loja, licenças ou manutenção impedirem a composição. Decisões sobre engine, ferramentas e publicação permanecem em seus próprios gates; não substituir os requisitos por um protótipo incompleto.
