# Diagnóstico temporário da recusa nativa

**Escopo autorizado:** o mantenedor escolheu “Autorizar o diagnóstico temporário e o reinício do RC Music”. Abrange candidato temporário, dados isolados, fechamento/reabertura somente do RC Music e ajuste/restauração apenas do host YouTube. Não abrange UAC, WFP, outras extensões, encerramento do Brave ou relaxamento de segurança. Complementa o [desenho Brave/Letras → MP3](2026-11-12-brave-letras-mp3-design.md), sem substituir seu aceite final.

## Problema e abordagens

O pedido real Asterisk chegou ao app e ao workspace, mas falhou sem bytes. O erro amplo `Restricted` não conserva qual ramo recusou a transferência. A inspeção estática e o manifesto local reduziram as possibilidades normais a DNS, redirect e HTTP 401/403; repetir sem observar esse ramo não resolve a incerteza.

A abordagem escolhida é o candidato diagnóstico temporário já autorizado. Um debugger no app pessoal exigiria símbolos/controle intrusivo de processo; uma CLI paralela não comprovaria a jornada do navegador. Não trocar headers nem atualizar ferramentas por hipótese.

## Decisões de implementação do agente

- Acrescentar observador opcional de enum fechado ao transporte: target, DNS, redirect, HTTP401, HTTP403 e restrição propagada de IO. Padrão vazio, sem logs/persistência na composição normal.
- A callback confiável não recebe URLs, IPs, headers, corpo, desafios, metadados ou texto externo. Exceção do observador não muda o erro original nem os ACKs. Manter os mesmos bloqueios e zero escrita de áudio quando recusado.
- Compor o observador somente no header C++ do candidato; gravar a primeira razão em arquivo novo no diretório isolado. Não usar Qt/stderr bruto: a política de privacidade existente continua ativa.
- Usar o padrão existente de shell diagnóstico com main/presenter/owner reais e `--m0-test-config-root`, sem cenários de fixture e sem promover essa variante a release. Ferramentas, pins, identidades Native e políticas de rede permanecem os da composição revisada.
- Antes de trocar o host, registrar seu destino exato e conferir o candidato. Fechar o app graciosamente; se houver decisão de salvamento, preservar dados e pedir a decisão necessária. Executar um novo envio explícito pelo navegador. Restaurar o host exato e reabrir o app original ao encerrar, mesmo se o diagnóstico falhar.

## Validação e aceite

Red/Green focado para enum de cada ramo, emissão única, callback ausente/exceção e conservação de `Restricted`, cancelamento e sink vazio. Ampliar somente para consumidores do limite/transporte e composição. Não repetir Node sem mudança de seus inputs. Build Release do candidato, conferência do pacote/host e leitura de volta após cada alteração externa. Resultado mínimo: motivo observado numa tentativa real e restauração confirmada; resultado MP3 continua condicionado ao aceite original, nunca a mocks ou ao diagnóstico isolado.
