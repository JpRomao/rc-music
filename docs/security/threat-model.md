# Threat model

**Estado:** baseline do MVP local  
**Última revisão:** 2026-08-23  
**Revisar quando:** entrar rede, sidecar, auto-update, plugin, biblioteca indexada ou credencial

## 1. Escopo e objetivo

O player processa arquivos e metadados potencialmente não confiáveis, possui uma UI WebView privilegiada por IPC e integra-se ao shell. O objetivo é impedir que um arquivo, path, página remota ou segunda instância consiga executar código, ler dados fora do necessário, travar o processo de forma persistente ou expor a biblioteca do usuário.

Este modelo cobre:

- app Tauri local e seu WebView;
- backend Rust, áudio, metadata e persistência;
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
- permissões concedidas ao WebView;
- integridade do executável, instalador e dependências;
- credenciais/cookies futuros, caso um requisito separado os introduza.

## 3. Fronteiras de confiança

```text
Não confiável                     Fronteira                     Privilegiado

arquivo/tag/capa ───────────────► parser/limites ─────────────► metadata DTO
path do shell ──────────────────► validação/canonicalização ──► MediaSource
UI WebView ─────────────────────► commands + capabilities ────► application
segunda instância ──────────────► IPC local + schema/limites ─► controller
SO/tray/media keys ─────────────► adapter tipado ──────────────► commands
URL futura ─────────────────────► resolver/policy/timeout ─────► descriptor
```

O WebView é menos confiável que o backend, mesmo carregando assets locais. Um bug de frontend/XSS não deve oferecer comando genérico de filesystem, shell ou rede.

## 4. Atores e falhas consideradas

- arquivo baixado, malformado ou deliberadamente hostil;
- tags/capas com tamanho, encoding ou estrutura maliciosos;
- path elaborado para explorar quoting, Unicode, symlink ou caminho longo;
- conteúdo frontend comprometido por dependência ou injeção;
- processo local enviando mensagem à instância única;
- pacote/dependência comprometida;
- falha de energia/crash durante persistência;
- provedor remoto ou URL maliciosa em fase futura;
- erro acidental do próprio app com permissões excessivas.

Um adversário com controle administrativo total da máquina está fora do escopo. Mesmo assim, o app não deve elevar privilégio nem armazenar segredo desnecessário.

## 5. Ameaças e controles

| ID | Ameaça | Controles de desenho | Evidência esperada |
|---|---|---|---|
| SEC-001 | Parser entra em panic/crash com mídia corrompida | APIs fallible, worker isolado, limites, corpus hostil, panic tratado como bug | testes por contêiner e regressão de cada crash |
| SEC-002 | Capa causa consumo excessivo/decompression bomb | limite de bytes antes de decode, dimensões/pixels máximos, thumbnail e cancelamento | fixtures no/fora do limite e medição de memória |
| SEC-003 | Tags enormes/inválidas travam UI ou injetam markup | limite/sanitização, texto como texto, truncamento visual, sem HTML arbitrário | testes Unicode, markup e payload grande |
| SEC-004 | Path vira comando de shell | APIs tipadas de path/processo, nenhum `shell -c`, argumentos separados | testes com espaços, aspas, metacaracteres e Unicode |
| SEC-005 | Symlink/TOCTOU troca arquivo durante probe/play | abrir handle quando aplicável, tolerar mudança, não escrever fonte, verificar erros | teste controlado de remoção/troca |
| SEC-006 | UI invoca filesystem/shell arbitrário | allowlist de commands, DTOs específicos, Tauri capabilities mínimas | revisão de manifest e testes de command rejeitado |
| SEC-007 | WebView navega/carrega conteúdo remoto e ganha bridge | CSP restrita, assets locais, navigation handler, sem iframe/script remoto | teste de navegação bloqueada e revisão CSP |
| SEC-008 | Custom protocol expõe path arbitrário | IDs opacos, cache dedicado, path resolvido no backend, MIME fixo, limites | testes de traversal, ID inválido e arquivo ausente |
| SEC-009 | IPC de segunda instância aceita payload hostil | canal local restrito, schema/tamanho, somente comando OpenSources, ativação sem privilégio | fuzz/schema tests e payload excessivo |
| SEC-010 | Corrupção/perda de preferências/sessão | temp no mesmo volume, flush/replace atômico, schema, backup limitado | fault-injection antes/depois do replace |
| SEC-011 | Log expõe biblioteca ou URL | redação de paths/query, nível e retenção limitados, exportação explícita | testes de formatter e inspeção manual |
| SEC-012 | DLL/plugin hijacking no pacote Windows | paths de busca seguros, dependências no diretório esperado, assinatura pública | smoke em instalação limpa e análise do bundle |
| SEC-013 | Associação toma controle indevido ou deixa registro | ProgID próprio, por usuário, escolha final do Windows, uninstall escopado | install/upgrade/uninstall em VM |
| SEC-014 | Dependência vulnerável/licença incompatível | lockfiles, advisories, revisão de features/licença, SBOM/notices | gate security-and-licenses |
| SEC-015 | FFI/`unsafe` viola memória | módulo estreito, invariantes, wrappers seguros, testes plataforma | revisão explícita de cada bloco `unsafe` |
| SEC-016 | Shutdown/suspend deixa lock, processo zumbi ou arquivo parcial | lifecycle idempotente, timeout, ordem de shutdown, persistência atômica | testes suspend/resume/exit/kill |
| SEC-017 | URL futura acessa localhost/arquivos ou rede interna | allowlist de esquemas, política SSRF, DNS/IP checks quando aplicável, sem `file:` | suite de resolver antes de habilitar rede |
| SEC-018 | Sidecar futuro permite command injection/update comprometido | argumentos tipados, hash/assinatura, versão pinada, diretório controlado | ADR, teste e verificação de artefato |

## 6. Política de Tauri/WebView

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

Fuzzing é desejável nos wrappers e parsers próprios. Não prometer que fuzzing de uma biblioteca externa elimina o risco dela.

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

