# ADR-0020: Manifesto assinado do runtime YouTube

**Estado:** Aceito

**Data:** 2026-11-16

**Relaciona-se a:** [ADR-0017](0017-youtube-audio-and-lyrics-acquisition.md) (aquisição YouTube) e ao [perfil empacotado de downloads](../../packaged-download-profile.md).

## Contexto

O runtime do resolvedor YouTube tem um extractor `yt-dlp.exe` gerado pelo PyInstaller, o Node e os arquivos EJS. Hoje, o SHA-256 de cada arquivo desse runtime fica no header gerado `PlayerDownloadPackageProfile.hpp`, compilado no `RCMusic.exe`. Isso impede que alguém com escrita na pasta de instalação ou nos dados do usuário troque o extractor. Por outro lado, cada nova versão do yt-dlp exige recompilar o player, e o YouTube muda com frequência.

Em 2026-11-16 o mantenedor escolheu o manifesto assinado entre duas opções. A outra era um script único que reconstruísse o runtime, o header e o player.

## Decisão

1. O inventário do runtime YouTube (role, caminho relativo e SHA-256 de cada arquivo) passa a vir de `download-tools/youtube-runtime.json`. O arquivo fica fora da árvore `download-tools/youtube/`.
2. O manifesto só é aceito com assinatura válida em `download-tools/youtube-runtime.sig`:
   - algoritmo ECDSA P-256 sobre o SHA-256 dos bytes exatos do manifesto;
   - assinatura de 64 bytes no formato IEEE P1363 (`r||s`).
   A assinatura é conferida antes de interpretar o JSON.
3. A chave pública (coordenadas X||Y, 64 bytes) e a sequência mínima aceita ficam compiladas no player, pelo header gerado. A verificação usa o CNG do Windows (`bcrypt`), sem nova dependência.
4. A chave privada nunca entra no repositório nem no pacote. Ela fica com o mantenedor, protegida pela DPAPI da conta. A ferramenta de assinatura usa o .NET do PowerShell 7, já exigido pelos scripts do projeto.
5. O manifesto tem um número `sequence`. O player recusa sequência abaixo do mínimo compilado. O mínimo sobe quando um runtime antigo precisar ser bloqueado, e só então o player precisa de nova build.
6. O manifesto é lido uma vez, na inicialização, com tamanho limitado. A partir daí o inventário vira a mesma `WindowsYoutubeRuntimeConfig` de hoje, e cada job continua fazendo o preparo existente: hash de cada arquivo, cópia privada e verificação antes de cada lançamento. Manifesto ausente, inválido ou mal assinado desliga só o canal YouTube; o player local continua disponível.
7. Esta decisão substitui, apenas para o inventário do runtime YouTube, a regra do ADR-0017 e de `youtube-audio-and-lyrics.md` de que o inventário vem só de configuração C++ compilada. A autoridade continua vindo do código compilado, agora pela chave pública. Página, checkpoint, `PATH`, Registro ou descoberta em diretório continuam sem autoridade.

## Alternativas

- **Header compilado (estado anterior).** É o mais forte contra a troca de arquivos, mas cada versão do yt-dlp exige build do player.
- **Script único de atualização.** Mantém a segurança anterior e automatiza a rotina, mas não evita recompilar.
- **Hashes em arquivo sem assinatura.** Rejeitado: quem escreve na instalação trocaria juntos os arquivos e os hashes.
- **Authenticode do extractor.** Exigiria um certificado de assinatura de código, com custo e dependência externa, sem ganho para um projeto pessoal.

## Consequências

- Atualizar o yt-dlp passa a ser: reconstruir o runtime, assinar o novo manifesto e substituir `download-tools/youtube/`, `youtube-runtime.json` e `youtube-runtime.sig`. O player não muda.
- **Risco residual de retrocesso.** Quem tem escrita na instalação pode recolocar um runtime antigo, ainda assinado, com sequência maior ou igual ao mínimo. A mitigação é subir o mínimo compilado quando houver motivo de segurança.
- Perder a chave privada impede novas assinaturas. A recuperação é gerar outra chave e compilar o player com a nova chave pública. Vazar a chave dá a quem a obtiver o mesmo poder que trocar o binário do player, até a troca de chave.
- O próprio extractor continua fixando a versão do yt-dlp com que foi construído. Cada nova versão ainda exige reconstruir o runtime pelo builder existente.
- O modo assinado é opcional no gerador do perfil (`youtube.signed_runtime`). O empacotamento do instalador ainda não o aceita; essa integração acompanha a retomada do instalador.

## Critérios de revisão

Rever se o projeto passar a ter assinatura de código do publicador, distribuição para terceiros ou atualização automática. Esta decisão não autoriza atualização automática nem download do runtime pela rede.
