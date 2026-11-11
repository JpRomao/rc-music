# Threat model

**Estado:** baseline de ameaças do MVP local; controles específicos condicionados ao candidato do ADR-0001
**Última revisão:** 2026-08-23  
**Revisar quando:** mudar a stack, ou entrar rede, sidecar, auto-update, plugin, biblioteca indexada ou credencial

## 1. Escopo e objetivo

O player processa arquivos e metadados potencialmente não confiáveis e integra-se ao shell. No desafiante Tauri, uma UI WebView cruza uma fronteira IPC privilegiada; no candidato Qt, QML cruza um boundary de objetos e operações C++ deliberadamente expostos. O objetivo é reduzir e testar o risco de que arquivo, path, conteúdo remoto futuro ou segunda instância execute código, leia dados fora do necessário, cause indisponibilidade reproduzível ou exponha a biblioteca do usuário. O MVP não alega isolamento absoluto contra OOM/falha nativa de decoder em processo.

Este modelo cobre:

- shell/UI desktop em Qt/QML ou Tauri/WebView durante o M0;
- núcleo C++ ou Rust, áudio, metadata e persistência;
- arquivos/paths recebidos pelo usuário ou shell;
- IPC da UI e da instância secundária;
- tray, SMTC/MPRIS, associações e instalador;
- desenho da porta remota futura.

Não cobre ainda servidor, conta, nuvem ou auto-update, pois não fazem parte do MVP.

## 2. Ativos

- integridade e disponibilidade do processo/player;
- arquivos de áudio do usuário, que nunca devem ser alterados no MVP;
- privacidade de paths, nomes, tags, fila e histórico de sessão;
- preferências e estado recuperável;
- dispositivo de áudio e controles do sistema;
- operações concedidas à UI/boundary; capabilities no Tauri e objetos/tipos expostos ao QML no Qt;
- integridade do executável, instalador e dependências;
- credenciais/cookies futuros, caso um requisito separado os introduza.

## 3. Fronteiras de confiança

```text
Não confiável                     Fronteira                     Privilegiado

arquivo/tag/capa ───────────────► parser/limites ─────────────► metadata DTO
path do shell ──────────────────► validação/canonicalização ──► MediaSource
UI (QML ou WebView) ────────────► boundary + commands ────────► application
segunda instância ──────────────► IPC local + schema/limites ─► controller
SO/tray/media keys ─────────────► adapter tipado ──────────────► commands
URL futura ─────────────────────► resolver/policy/timeout ─────► descriptor
```

A UI é menos confiável que o núcleo. Um bug de frontend/XSS no Tauri ou uma API QML exposta em excesso no Qt não pode oferecer operação genérica de filesystem, shell, processo ou rede.

## 4. Atores e falhas consideradas

- arquivo baixado, malformado ou deliberadamente hostil;
- tags/capas com tamanho, encoding ou estrutura maliciosos;
- path elaborado para explorar quoting, Unicode, symlink ou caminho longo;
- apresentação comprometida por dependência, injeção ou objeto exposto em excesso;
- processo local enviando mensagem à instância única;
- pacote/dependência comprometida;
- falha de energia/crash durante persistência;
- provedor remoto ou URL maliciosa em fase futura;
- erro acidental do próprio app com permissões excessivas.

Um adversário com controle administrativo total da máquina está fora do escopo. Mesmo assim, o app não deve elevar privilégio nem armazenar segredo desnecessário.

## 5. Ameaças e controles

| ID | Ameaça | Controles de desenho | Evidência esperada |
|---|---|---|---|
| SEC-001 | Parser encerra o processo com mídia corrompida | APIs de erro, worker fora da UI, limites e corpus hostil; crash vira bloqueador. Isolamento de processo não é alegado no MVP | testes por contêiner e regressão de cada crash reproduzível |
| SEC-002 | Capa causa consumo excessivo/decompression bomb | NFR-LIMIT-001: bytes, dimensões, pixels e thumbnail limitados antes/ao decodificar quando a biblioteca permitir | fixtures exatamente no/fora de cada limite e medição de memória |
| SEC-003 | Tags enormes/inválidas travam UI ou injetam markup | NFR-LIMIT-002/003, sanitização, texto como texto, truncamento visual, sem HTML arbitrário | testes Unicode, markup, payload grande e timeout/cancelamento |
| SEC-004 | Path vira comando de shell | APIs tipadas de path/processo, nenhum `shell -c`, argumentos separados | testes com espaços, aspas, metacaracteres e Unicode |
| SEC-005 | Symlink/TOCTOU troca arquivo durante probe/play | abrir handle quando aplicável, tolerar mudança, não escrever fonte, verificar erros | teste controlado de remoção/troca |
| SEC-006 | UI invoca filesystem/shell arbitrário | allowlist de commands/objetos, DTOs específicos, capabilities Tauri mínimas e superfície QML estreita | revisão da superfície exposta e testes de operação rejeitada |
| SEC-007 | WebView navega/carrega conteúdo remoto e ganha bridge | CSP restrita, assets locais, navigation handler, sem iframe/script remoto | teste de navegação bloqueada e revisão CSP |
| SEC-008 | Custom protocol expõe path arbitrário | IDs opacos, cache dedicado, path resolvido no backend, MIME fixo, limites | testes de traversal, ID inválido e arquivo ausente |
| SEC-009 | IPC de segunda instância aceita payload hostil | canal local restrito, schema/tamanho, somente comando OpenSources, ativação sem privilégio | fuzz/schema tests e payload excessivo |
| SEC-010 | Corrupção/perda de preferências/sessão | temp no mesmo volume, flush/replace atômico, schema, backup limitado | fault-injection antes/depois do replace |
| SEC-011 | Log expõe biblioteca ou URL | redação de paths/query, nível e retenção limitados, exportação explícita | testes de formatter e inspeção manual |
| SEC-012 | DLL/plugin hijacking no pacote Windows | paths de busca seguros, dependências no diretório esperado, assinatura pública | smoke em instalação limpa e análise do bundle |
| SEC-013 | Associação toma controle indevido ou deixa registro | ProgID próprio, por usuário, escolha final do Windows, uninstall escopado | install/upgrade/uninstall em VM |
| SEC-014 | Dependência vulnerável/licença incompatível | lockfiles, advisories, revisão de features/licença, SBOM/notices | gate security-and-licenses |
| SEC-015 | FFI, `unsafe` ou código nativo viola memória | módulo estreito, ownership explícito, invariantes, wrappers seguros e testes de plataforma | revisão explícita de cada bloco `unsafe`, FFI e boundary nativo crítico |
| SEC-016 | Shutdown/suspend deixa lock, processo zumbi ou arquivo parcial | lifecycle idempotente, timeout, ordem de shutdown, persistência atômica | testes suspend/resume/exit/kill |
| SEC-017 | URL futura acessa localhost/arquivos ou rede interna | allowlist de esquemas, política SSRF, DNS/IP checks quando aplicável, sem `file:` | suite de resolver antes de habilitar rede |
| SEC-018 | Sidecar futuro permite command injection/update comprometido | argumentos tipados, hash/assinatura, versão pinada, diretório controlado | ADR, teste e verificação de artefato |

## 6. Políticas condicionais por candidato

O ramo escolhido transforma somente sua subseção em contrato de implementação e remove o harness descartado. A aceitação do ADR atualiza este threat model com o pacote e as dependências reais.

### C++/Qt/QML

- domínio, application e ports não importam Qt nem expõem `QObject`;
- registrar e revisar cada tipo, propriedade, signal, slot e invokable exposto ao QML;
- não expor filesystem, processo, shell, rede ou ponteiros/ownership arbitrários à camada visual;
- validar lifetime, thread affinity e conexão assíncrona de objetos Qt nos testes de presenter/adapters;
- configurar busca de DLLs/plugins de forma escopada e inspecionar o pacote contra hijacking;
- tratar TagLib, Qt Multimedia e FFmpeg como parsers nativos não confiáveis, com corpus hostil, limites e advisories;
- registrar link dinâmico/obrigações LGPL do Qt e configuração/licenças do FFmpeg antes da distribuição.

### Rust/Tauri/WebView

- somente janelas e commands necessários;
- nenhuma capability global curinga;
- desabilitar/evitar shell, process spawn, filesystem amplo e HTTP na UI;
- diálogos retornam paths ao backend, que valida a intenção;
- CSP começa em `default-src 'self'` e abre apenas o indispensável;
- evitar `unsafe-inline`; se tooling exigir durante desenvolvimento, release recebe política própria e justificativa;
- capas usam protocolo/cache controlado ou blob criado a partir de bytes limitados;
- dev server só existe em modo desenvolvimento e não é exposto à rede;
- DevTools desativado no bundle público, sem ser tratado como controle de segurança primário;
- mensagens IPC possuem enum/DTO fechado, limites e erros neutros.

## 7. Arquivos e parsers

- abrir read-only;
- não seguir input para escrita/edição;
- limitar metadata, arte, profundidade e tempo de trabalho;
- cancellation token ao trocar faixa;
- detectar por conteúdo quando a biblioteca permitir;
- separar “não suportado” de “corrompido” sem revelar memória interna;
- evitar hash integral na abertura normal;
- atualizar decoder/tag reader com gates de corpus e advisories;
- manter casos mínimos de regressão legalmente redistribuíveis.

### Limites iniciais

Os valores normativos estão nos [requisitos](../product/requirements.md) (NFR-LIMIT-001–004): arte até 10 MiB codificados/4.096 px por eixo/16 MP, thumbnail até 1.024 px, metadata retida até 256 KiB e 16 KiB por campo, budget interativo de probe de 2 s e IPC de até 1.000 paths/1 MiB. O M0 deve confirmar que as bibliotecas permitem aplicar os limites antes de alocações perigosas; quando não permitirem, registrar o risco ou trocar/isolar o parser.

Fuzzing é desejável nos wrappers e parsers próprios. Não prometer que fuzzing de uma biblioteca externa elimina o risco dela.

O adaptador TagLib limita cada buffer de entrada a 32 MiB antes de ler. A mesma abertura somente leitura serve à detecção e à extração, com orçamento de 64 MiB de entrada e 65.536 chamadas. Para Ogg/Vorbis, as tabelas de segmentos limitam cada pacote inicial a 30 MiB antes da montagem. Pico de memória, alocações internas e isolamento do decoder continuam em avaliação. [Testes dos limites](../audio-tests.md#limite-por-leitura-de-metadados)

## 8. Persistência e privacidade

Dados persistidos são mínimos:

- preferências;
- fila/paths, item e posição;
- cache de metadata/thumbnail limitado;
- logs locais limitados quando habilitados.

Não persistir conteúdo de áudio, credencial, cookie, identificador publicitário ou telemetria. O usuário pode limpar sessão/cache sem apagar arquivos de mídia. Uninstall preserva dados por padrão ou oferece escolha explícita; nunca apaga diretório amplo calculado sem validação.

## 9. Instalação e supply chain

- builds partem de lockfiles e CI identificável;
- release registra commit, toolchains e hashes;
- dependências diretas/transitivas entram em SBOM/notices;
- assinatura de código antes de distribuição pública Windows;
- artefatos são verificados depois de empacotar;
- secrets de assinatura ficam fora do repo/log;
- update automático fica fora do MVP; inclusão futura exige assinatura, rollback e threat model específico;
- nenhuma publicação externa ocorre sem autorização explícita do mantenedor.

## 10. Fontes remotas futuras

Antes de habilitar qualquer rede:

- atualizar diagrama de fluxo de dados;
- definir hosts/esquemas, redirects, proxy, DNS e limite de bytes;
- definir cache, expiração, cookies e política de logs;
- impedir acesso a `file:`, custom protocol e endpoints locais/metadata de cloud;
- isolar resolver/sidecar com timeout e cancelamento;
- validar licença/termos e atualização;
- garantir que UI privilegiada não renderize página remota;
- provar que desligar/remover o adapter não afeta arquivos locais.

## 11. Gates de segurança do M0/MVP

### M0

- capabilities/CSP revisadas;
- payload IPC inválido rejeitado;
- URL/navegação remota bloqueada;
- tags/capa hostis tratadas;
- paths com metacaracteres não executados;
- dependências/licenças inventariadas.

### MVP/release

- corpus e regressões verdes;
- fault-injection de persistência;
- install/upgrade/uninstall escopados;
- SBOM/notices;
- advisories triados;
- `unsafe`/FFI revisados;
- artefatos assinados se públicos;
- logs inspecionados para dados pessoais;
- ameaça remota continua ausente/desabilitada.

## 12. Resposta a vulnerabilidade

1. registrar versão/ambiente e reproduzir com segurança;
2. não publicar fixture maliciosa sensível antes de correção;
3. adicionar teste de regressão mínimo;
4. corrigir no boundary mais estreito;
5. revisar versões/formatos afetados;
6. gerar release corretiva e hashes;
7. documentar impacto e mitigação sem expor dados do usuário;
8. atualizar este modelo se a classe de ameaça não estava coberta.
