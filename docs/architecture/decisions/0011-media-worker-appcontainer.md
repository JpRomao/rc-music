# ADR-0011: isolamento AppContainer do worker de mídia Windows

**Estado:** Proposto

**Data:** 2026-11-04

**Escopo:** caminho opcional de conversão/validação Media Foundation; não ativa downloads, instala componentes ou autoriza alterações no host.

## Contexto

O worker nativo recebe mídia por handle somente de leitura e devolve MP3 por stdout. O Job Object limita recursos e descendentes, mas não restringe acesso a arquivos ou rede. A [composição do player](../../download-jobs.md#composição-opcional-do-player-windows) já reúne conversão, publicação e importação durável; o isolamento efetivo continua sendo um gate do [ADR-0009](0009-download-execution-backends.md).

O fluxo documentado para aplicações sem pacote usa um perfil AppContainer. `CreateAppContainerProfile` cria armazenamento de arquivos e Registro por usuário/aplicação; o SID retornado deve ser liberado por `FreeSid`. Portanto, o ensaio físico exige autorização específica além de compilar o código. [Criação do perfil](https://learn.microsoft.com/en-us/windows/win32/api/userenv/nf-userenv-createappcontainerprofile).

## Proposta implementável

1. Usar AppContainer comum com lista vazia de capabilities. A composição confiável fornece o SID binário de um perfil previamente provisionado e conserva seu ciclo de vida até a drenagem de todos os processos. A capability valida e copia a identidade; não consulta, autentica, deriva ou cria perfis.
2. Preservar os três handles de stdio e a única entrada de mídia somente de leitura na lista explícita de herança. Nenhum handle de Job, token, diretório proprietário ou saída gravável é delegado ao codec.
3. Criar o processo suspenso com `SECURITY_CAPABILITIES`, conservando Job, cotas e deadlines. Conferir token AppContainer, SID esperado, integridade Low, capabilities vazias e pertencimento ao Job antes de retomar a thread. Falha de configuração, atributos, consulta ou retomada produz `Isolation` e drenagem, sem execução legada alternativa.
4. Criar somente a nova folha do runtime e seu EXE autenticado com DACL protegida: usuário/SYSTEM com controle total e SID específico com leitura/execução. Não alterar ACLs de pais, fontes, SDK, staging ou arquivos pessoais. Revalidar as ACLs por handle antes de cada subprocesso.
5. Manter produção remota OFF até a prova física e os demais gates de identidade, distribuição, resíduos e integração. Os caminhos legados permanecem restritos às suas composições explícitas de desenvolvimento.

O Windows documenta que a ausência da capability de rede restringe rede e que o acesso é a interseção dos direitos do usuário e do AppContainer. AppContainer comum ainda pode acessar recursos de sistema e seu armazenamento privado. A política não promete ausência universal de leitura/escrita no host. [Modelo e lançamento](https://learn.microsoft.com/en-us/windows/win32/secauthz/implementing-an-appcontainer).

## Alternativas

- Job Object ou integridade Low isoladamente: insuficientes para o gate de filesystem/rede.
- SID derivado de nome nunca provisionado: sem contrato suficiente para substituir o fluxo de perfil documentado.
- LPAC: superfície menor, mas COM/Registro usados pelo Media Foundation precisam de capabilities e validação próprias; não introduzir essas concessões como fallback.
- Converter no processo principal ou reduzir somente opções do codec: não preserva a fronteira de conteúdo não confiável.

## Validação e operação

Os testes automáticos comuns validam SID binário, recusa sem capability obrigatória, ACLs de folhas novas, alteração de ACL e compatibilidade legada. SIDs sintéticos usados nessas provas não podem ser lançados como perfis.

O probe manual deve exigir uma opção explícita e destino de evidência novo no workspace. Cria apenas perfil de teste com GUID interno, sem aceitar nome externo ou adotar perfil existente. Deve provar stdio/handle de leitura, negação de leitura/escrita em sentinela externo, escrita/alteração de ACL no runtime, rede com controle positivo local, Job e conversão/segunda validação de fixtures MP3/AAC/WAV. Timeout ou erro inespecífico não confirma negação de acesso.

Depois do ACK e fechamento dos handles, remover apenas o perfil criado nesta execução por `DeleteAppContainerProfile`, registrando HRESULT e resíduos. Falha deixa estado indeterminado; não substituir por remoção recursiva ou limpeza de Registro por nome. [Remoção oficial](https://learn.microsoft.com/en-us/windows/win32/api/userenv/nf-userenv-deleteappcontainerprofile).

## Consequências e critérios de revisão

O perfil adiciona ciclo de vida e armazenamento próprios que precisam ser geridos no produto/instalador. A criação transitória de teste não decide a política de perfis da distribuição. Compatibilidade com codecs Windows, negações reais, resíduos, execução interrompida e ausência de componentes continuam exigindo evidência específica. Adoção global, suporte universal e release não decorrem de testes de sintaxe/ACL ou de um único host.

As cotas atuais dos sinks não abrangem o armazenamento próprio do perfil. Sua política de escrita, limite de disco e resíduos precisa ser fechada antes da produção; este componente e o ensaio transitório não dispensam esses requisitos.

Revisar esta proposta se o Media Foundation não operar com capabilities vazias, se surgir necessidade de ampliar permissões, se o perfil não puder ser limpo por propriedade ou se o ensaio encontrar acesso fora do conjunto permitido. Qualquer concessão adicional exige justificativa e novos testes, sem downgrade silencioso.
