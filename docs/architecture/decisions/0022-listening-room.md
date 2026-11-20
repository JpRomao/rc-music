# ADR-0022: sala de escuta para ouvir junto

**Estado:** Aceito

**Data:** 2026-11-17

**Decisão de escopo:** o mantenedor aprovou o [desenho da sala de escuta](../../superpowers/specs/2026-11-17-listening-room-design.md) em 2026-11-17 ("Aprovar"). O [plano de execução](../../superpowers/plans/2026-11-17-listening-room.md) organiza a entrega em fatias. Esta decisão abre uma exceção limitada ao local-first. A implementação das fatias S1 a S12 existe no código; o teste entre redes diferentes (S13.2) ainda não foi feito.

**Relaciona-se a:** [ADR-0003](0003-local-first-and-remote-sources.md) (local-first e fontes remotas), [ADR-0005](0005-windows-product-stack.md) (stack do produto) e [ADR-0015](0015-named-playlists.md) (playlists, que ganham visibilidade para a sala).

## Contexto

O mantenedor quer ouvir música com amigos que moram em outras cidades, cada um no próprio computador. Pedido de 2026-11-17:

> Teremos 1 host que envia um link e com esse link as pessoas se conectam e escutam juntos (cada um no seu volume, porém as mesmas músicas).

Uma pessoa é o host, e a sala funciona como um "streaming" pessoal da biblioteca dela. Os convidados também podem trazer músicas próprias, se o host der a permissão. Todos têm o RC Music instalado. A meta de sincronia é abaixo de 100 ms entre participantes, e 150 ms ainda é aceitável.

Isso conflita com duas regras vigentes: `agents.md` §10 proíbe integrar outros serviços remotos, e NFR-PRIV-004 limita a rede à ação solicitada. O mantenedor aceitou um servidor de apresentação "se não for custar nada". Anti-objetivos: a música passar por servidor de terceiros ou ficar guardada nele, o app depender de rede para tocar música local fora da sala, caminhos, biblioteca completa ou playlists privadas chegarem aos convidados, e qualquer custo financeiro.

## Decisão

1. **Caminho A: o arquivo chega antes e cada um toca no horário combinado.** A música inteira vai para um cache temporário do convidado, e a próxima é enviada durante a atual. Os apps acertam o relógio e tocam o arquivo local no instante marcado pelo host. Nada depende da rede na hora de tocar.
2. **Estrela com o host no centro.** Cada convidado se conecta só ao host por WebRTC (data channels `control` e `media`, confiáveis e ordenados). Não há conexão entre convidados. O estado da sala é autoritativo no host; convidados enviam intenções, e o host confere a permissão antes de aplicar.
3. **libdatachannel** para a conexão P2P, apenas com data channels, sem trilhas de mídia. STUN gratuito da Cloudflare (`stun.cloudflare.com:3478`) e nenhum TURN nesta frente.
4. **Servidor de apresentação no plano gratuito do Cloudflare Workers**, em `services/room-signaling/`: um Worker e um Durable Object por sala. Ele só repassa mensagens de apresentação entre o host e cada convidado, nunca a música, e não guarda nada. Limites: mensagens de 16 KiB, 20 por segundo por socket, 11 sockets por sala; a sala fecha após 10 minutos sem o host. A conta e o `wrangler deploy` são passos do mantenedor.
5. **Cifragem ponta a ponta das mensagens de apresentação**, com chave derivada do segredo do convite (HKDF e AES-GCM). O servidor vê blocos opacos e não troca as impressões digitais dos certificados. O segredo fica depois do `#` do link e nunca chega ao servidor.
   O token que identifica o host no servidor (`X-Room-Host-Token`) não sai do segredo do convite, porque todo convidado conhece esse segredo e poderia usá-lo para tomar o lugar do host no servidor e receber as mensagens dos outros convidados. O token é HKDF-SHA256 de um segredo que só o computador do host tem (32 bytes aleatórios protegidos pela DPAPI, os mesmos que protegem a chave do certificado), com o ID da sala como sal e o rótulo "rc-music room host v1". Ele vai em base64url sem preenchimento (43 caracteres).
6. **Identidade sem conta.** O ID da pessoa é a impressão digital do certificado autoassinado gerado no primeiro uso (chave privada protegida pela DPAPI). O link carrega o ID da sala, o segredo do convite e a impressão digital do host.
7. **Entrada com aprovação dos novos.** Quem o host já aceitou antes entra direto; pessoas novas ficam em "Aguardando o host" até ele aceitar ou recusar; bloqueados são recusados sem pedido. Assim, um link vazado não basta para ouvir a biblioteca do host.
8. **"Abrir sala" é explícito.** A sala só existe entre "Abrir sala" e "Encerrar sala" (ou o fechamento do app). O convite "até revogar" fica salvo no perfil, mas a sala nunca abre sozinha. Revogar encerra a sala, desconecta todos e invalida o segredo.
9. **Lotação de 1 a 10 pessoas** (padrão 5). Versões de protocolo diferentes são recusadas com "Atualize o RC Music para entrar nesta sala".
10. **O que viaja:** metadados limitados, capa reduzida, a letra escolhida pelo host (até 64 KiB) e o arquivo de áudio por conteúdo (SHA-256). Nunca caminhos ou nomes de arquivo locais. Só as playlists do host marcadas como visíveis para a sala aparecem aos convidados (revisão no ADR-0015).
11. **Limites fixos** nos valores de NFR-LIMIT-005: mensagem de controle de 256 KiB, bloco de arquivo de 64 KiB, envio de convidado de 50 MiB, cache do convidado de 1 GiB apagado ao sair, antecipação de 3 itens.
12. **Abrir o app não conecta a nada.** A rede existe só entre "Abrir sala" e "Encerrar sala", ou entre "Entrar com link" e "Sair da sala". O app continua inteiro sem rede, e nenhum teste depende de internet.

## Alternativas

- **B, transmissão ao vivo como rádio:** o host decodificaria e codificaria em Opus pela trilha de áudio do WebRTC. Cada convidado ouviria com 150 a 300 ms de atraso diferente, o que estoura a meta, e exigiria um codificador novo. Descartada.
- **C, sincronizar só a posição com cada um tocando a própria cópia:** só funciona se todos tiverem o mesmo arquivo, e os amigos não compartilham bibliotecas. Fica como caso particular futuro para itens do YouTube. Descartada.
- **ntfy.sh com mensagens cifradas:** depende de um terceiro e dos limites de uso dele. Descartada.
- **Troca manual de código:** cada amigo teria de devolver um código ao host. Descartada.
- **TURN:** relé para quando a conexão direta falhar. Adiado; o TURN da Cloudflare (1.000 GB gratuitos por mês) pode entrar depois, com credenciais temporárias emitidas pelo Worker.

## Consequências

- **O endereço IP fica visível entre os pares**, como em chamadas de voz. O app avisa uma vez, ao criar ou entrar na primeira sala.
- **O host envia os arquivos.** Com 10 pessoas e músicas grandes, a internet de envio do host pode atrasar a primeira música; a antecipação reduz, mas não elimina.
- **Cópias temporárias:** cada convidado guarda um cache temporário das músicas do host, apagado ao sair e na abertura seguinte do app se sobrar de uma queda. Os arquivos não entram na biblioteca nem na fila local. A responsabilidade pelo conteúdo compartilhado é de quem hospeda, e o guia do usuário vai dizer isso.
- Sem TURN, a conexão direta pode falhar (CGNAT simétrico, redes móveis ou corporativas); a sala mostra "Não foi possível conectar diretamente ao host".
- O plano gratuito do Workers tem limites fixos: ao passar deles, as salas novas falham até o dia seguinte (UTC), sem cobrança.
- Exceção explícita ao local-first: `agents.md` §10 e NFR-PRIV-004 passam a admitir o servidor de apresentação, só enquanto uma sala está aberta e só com mensagens cifradas. Nenhuma outra rede remota é autorizada.
- A precisão do `QMediaPlayer` pode impedir a meta de 100 ms. A fatia S1 (protótipo descartável) mede isso antes do resto, e a decisão volta ao mantenedor se 150 ms não for alcançável.

## Licenças e dependências

Dependências novas exigem autorização explícita do mantenedor no momento da tarefa, com nome, origem e tamanho.

| Componente | Licença | Observação |
| --- | --- | --- |
| libdatachannel | MPL-2.0 | conexão P2P por data channels; copyleft por arquivo, sem contaminar o código do app |
| libjuice | MPL-2.0 | ICE usado pela libdatachannel |
| usrsctp | BSD-3-Clause | SCTP dos data channels |
| plog | MIT | log interno da libdatachannel |
| Mbed TLS | Apache-2.0 (usada sob Apache-2.0 da licença dupla Apache-2.0 OR GPL-2.0-or-later) | pilha TLS/DTLS da libdatachannel, estática dentro de `datachannel.dll` |

### Pilha TLS e versões fixadas (S6.1)

Em 2026-11-18, o mantenedor escolheu **Mbed TLS 3.6.8** em vez de OpenSSL 3.5 LTS. O build do Mbed TLS usa só CMake, sem Perl nem NASM. A libdatachannel é compilada com `USE_MBEDTLS=ON`, `NO_MEDIA=ON`, `NO_EXAMPLES=ON` e `NO_TESTS=ON`, em Release, com MSVC 14.44. O resultado é uma `datachannel.dll` compartilhada. Mbed TLS, libjuice, usrsctp e plog ficam estáticos dentro dela. No CMake do produto, só `rc_music_listening_room` linka `LibDataChannel::LibDataChannel`.

| Componente | Versão | Origem fixada |
| --- | --- | --- |
| libdatachannel | 0.24.6 | tag `v0.24.6` (objeto `d9617a53201615ef729e99dc39ad6736982d0346`), commit `6b1e2e620f1e37f0eafeee702eaea0043cb305fd` |
| libjuice | submódulo `deps/libjuice` | commit `b89c792e3612faf2f12cf35bcc56857313a06be3` |
| usrsctp | submódulo `deps/usrsctp` | commit `fec583d54493f879d2ae44a743423bf8a04371ab` |
| plog | submódulo `deps/plog` | commit `94899e0b926ac1b0f4750bfbd495167b4a6ae9ef` |
| Mbed TLS | 3.6.8 | `mbedtls-3.6.8.tar.bz2` da release oficial, SHA-256 `bc54b818e87f49c0b3281dd85d24adaede0e63b4e822e3fbd49c005a38b9595b`, conferido contra `mbedtls-3.6.8-sha256sum.txt` |

Os submódulos `deps/json` e `deps/libsrtp` não foram baixados: sem mídia e sem exemplos, o build não os usa.

O Mbed TLS usa o mecanismo documentado `MBEDTLS_USER_CONFIG_FILE` com duas mudanças em relação ao padrão. O Mbed TLS e a `datachannel.dll` são compilados com o mesmo arquivo. A receita está em [Comandos de desenvolvimento](../../development-commands.md#libdatachannel-da-sala-de-escuta).

- **`MBEDTLS_SSL_DTLS_SRTP` ligado:** a libdatachannel 0.24.6 chama `mbedtls_ssl_conf_dtls_srtp_protection_profiles()` no transporte DTLS mesmo com `NO_MEDIA=ON`.
- **`MBEDTLS_SSL_PROTO_TLS1_3` desligado:** o Mbed TLS fica sem `MBEDTLS_THREADING_C`. Não há backend de threads no Windows, e o Mbed TLS está estático dentro do `datachannel.dll`, sem como chamar `mbedtls_threading_set_alt`. Por isso, o TLS 1.3 fica desligado, assim como `MBEDTLS_USE_PSA_CRYPTO`, que já vem desligado por padrão. O wss usa TLS 1.2, e o DTLS usa a versão 1.2, ambos com contextos próprios.

A conferência foi feita nas fontes da v0.24.6. Fora dos testes, a única chamada PSA da libdatachannel é `psa_crypto_init()`, em `src/impl/tlstransport.cpp`. No build com TLS 1.3 desligado, `mbedtls.lib` e `mbedx509.lib` não têm referências não resolvidas a `psa_*`. O estado global do PSA só é tocado por esse `psa_crypto_init()`, que é idempotente.

- **Risco residual:** corrida no primeiro `psa_crypto_init()` se duas conexões TLS abrirem ao mesmo tempo.
- **Requisito para a S6.3:** no máximo um WebSocket seguro de sinalização por processo, aberto a partir de uma única thread.
- **Como a S6.3 cumpre o requisito:** o app não abre nenhum WebSocket da libdatachannel. O `rtc::WebSocket` 0.24.6 não envia headers próprios, e o host precisa do `X-Room-Host-Token`. Ele também não informa o status HTTP de um upgrade recusado, e o 409 indica que a sala é de outro host. Por isso, o WebSocket do servidor de apresentação usa o WinHTTP do Windows (`WindowsRoomSignalingSocket`), com TLS do Schannel conferido contra as raízes do sistema. O número de WebSockets com Mbed TLS é zero por construção, e o `psa_crypto_init()` do `TlsTransport` nunca roda. O Mbed TLS só faz DTLS, com um contexto por `PeerConnection`.

Se o projeto voltar a precisar de TLS 1.3 ou de PSA, este ponto deve ser revisto antes.

As licenças e os avisos de libdatachannel e libjuice (MPL-2.0), usrsctp (BSD-3-Clause), plog (MIT) e Mbed TLS (Apache-2.0) precisam acompanhar o instalador. Essa entrega fica para a fatia S13.

O Worker usa pacotes npm próprios, também sujeitos à mesma autorização e ao registro da licença. Os termos do plano gratuito do Cloudflare Workers são condição de operação, não dependência do app.

## Consequências observadas

Evidência da S13.1 (2026-11-19), com dois apps isolados no mesmo computador e um servidor de apresentação local (`wrangler dev --local`). O relato completo está em [Teste real](../../listening-room-sync-spike.md#teste-real).

- **Sincronia:** a diferença entre os dois apps ficou em 9,3 ms de forma estável, com máximo de 9,8 ms, e se manteve após um salto. Isso vale só para o mesmo computador: a medição entre computadores depende da S13.2. O caminho com `QMediaPlayer` bastou nesse cenário, e o critério de revisão sobre o caminho próprio de saída não foi acionado.
- **Falhas achadas e corrigidas:** a barra do convidado ficava vazia (`a680257`); pedaços que chegavam antes da oferta passavam de 2 MiB e eram descartados, e o pedido de reenvio ganhou limite de tentativas (`0039112`, `1401c37`, `75b0f81`); o cache do convidado não era apagado ao fim da sala com a própria música tocando (`231f372`).
- **Cache temporário:** confirmado que o `room-cache/` do convidado e o `room-uploads/` do host somem ao fechar a sala, e que sobras de uma queda são apagadas na abertura seguinte.
- **Ainda sem evidência:** conexão direta entre redes diferentes (sem TURN), tempo para entrar, servidor publicado no Cloudflare e teste com amigos. O instalador segue bloqueado: faltam os avisos de licença de libdatachannel e Mbed TLS e o redesenho não terminou.

## Critérios de revisão

Revisar este ADR se:

- a fatia S1 reprovar a meta de sincronia com o `QMediaPlayer` e o caminho próprio de saída (FFmpeg com `QAudioSink`) for necessário;
- o plano gratuito do Cloudflare Workers se mostrar insuficiente ou mudar de condições;
- TURN se tornar necessário por falhas de conexão frequentes;
- o mantenedor pedir itens do YouTube na sala, chat, voz, Linux ou outro limite de pessoas.

Fonte, testes e a aceitação com amigos (fatia S13, dependente do instalador) têm evidência própria. Aceitar este ADR não afirma desempenho, sincronia, segurança verificada ou release.
