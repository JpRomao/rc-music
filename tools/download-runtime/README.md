# Check estático do worker de rede

`Test-NetworkWorkerImports.ps1` é um check local somente leitura do perfil
Windows x64 atual. Não compila, copia, instala, executa os artefatos examinados,
altera o SDK ou faz requests de rede. Executa apenas o `dumpbin.exe` fornecido
por caminho absoluto e confiável; não procura ferramentas pelo PATH.

O perfil contém exatamente nove entradas com nomes fixos:

- `RCMusicDownloadWorker.exe`, bootstrap `/MT`;
- `rcmusic-download-network.dll`, payload Qt;
- `Qt6Core.dll` e `Qt6Network.dll`;
- `msvcp140.dll`, `msvcp140_1.dll`, `vcruntime140.dll`, `vcruntime140_1.dll`;
- `qrcmusicschanneloffline.dll`, backend TLS restrito.

Recusa entradas ausentes, relativas, com nome/tipo inesperado ou reparse point
no próprio arquivo/diretório informado; arquivo vazio ou maior que 128 MiB;
headers não reconhecidos, arquitetura diferente de x64/PE32+, tipo EXE/DLL
incorreto e qualquer delay-import não vazio. O bootstrap deve importar somente
`KERNEL32.dll`. Os demais imports devem pertencer à lista explícita de Qt/CRT,
DLLs do Windows e nomes de API sets observados neste perfil. Dependência nova
exige revisão e atualização deliberada da lista; não há wildcard `*.dll`.

Em sucesso, retorna JSON com nove paths, tamanhos, SHA-256, `SizeOfImage` e
imports. Em falha, lança erro e termina sem resultado `passed`; invocação por
`pwsh -File` retorna código diferente de zero. Mantém os arquivos abertos com
compartilhamento de leitura durante o snapshot, recusando writes/deletes/renames
usuais. Não salva relatório; redirecionamento de saída, se desejado, cabe ao
caller e não deve apontar para o SDK ou artefatos examinados.

## Uso local

PowerShell, na raiz do repositório, com caminhos locais já revisados:

```powershell
$inputs = @{
    DumpbinPath = 'C:\Program Files\Microsoft Visual Studio\18\BuildTools\VC\Tools\MSVC\14.44.35207\bin\HostX64\x64\dumpbin.exe'
    BootstrapPath = "$PWD\out\product\browser-integration-tests\RCMusicDownloadWorker.exe"
    PayloadPath = "$PWD\out\product\browser-integration-tests\rcmusic-download-network.dll"
    QtBinDirectory = "$PWD\out\product\qt-network-sdk\bin"
    CrtDirectory = 'C:\Program Files\Microsoft Visual Studio\18\BuildTools\VC\Redist\MSVC\14.44.35112\x64\Microsoft.VC143.CRT'
    TlsPluginPath = "$PWD\out\product\download-tls\rcmusic-schannel-offline-20261001-03\build\plugins\tls\qrcmusicschanneloffline.dll"
}
& .\tools\download-runtime\Test-NetworkWorkerImports.ps1 @inputs
```

Esse exemplo usa os quatro CRTs do redist MSVC local também usado no ensaio da
lease; não os substitui por DLLs de System32. As versões/caminhos mostrados foram
conferidos no host deste checkpoint, não são descobertos ou atualizados pela
ferramenta. A auditoria não autoriza cópia ou distribuição: runtime/CRT de entrega
precisam de proveniência, compatibilidade, licença e pins próprios. O plugin
restrito é opt-in, descrito em [download-tls](../download-tls/README.md).

## Regressões determinísticas

```powershell
& .\tools\download-runtime\Test-NetworkWorkerImports.ps1 -SelfTest
```

Esse modo não abre arquivos, executa ferramentas ou inicia processos: usa apenas
fixtures textuais em memória. Verifica headers válidos, zero/nonzero/ausência de
delay-import, tipo/arquitetura/magic/SizeOfImage inválidos, descriptors ausentes e
nomes desconhecidos (maiúsculas, `.exe`, espaços, Unicode, paths e whitespace
inicial). Reconhece todos os descriptors pelo bloco `Import Address Table`;
formato novo/incompleto é recusado, não ignorado. As fixtures não substituem o
check dos nove PEs reais nem a auditoria de manifests e carregamento dinâmico.

## Limites da evidência

Este check não é um parser seguro de PEs hostis nem uma aprovação de sandbox,
release, mínimo de Windows, TLS ou licença. `SizeOfImage` é registrado, não uma
garantia de limite de memória: file bytes, reservas, páginas compartilhadas e
mapeamentos são medidas distintas. A ferramenta e os arquivos examinados são
inputs locais confiáveis; o processo externo de inspeção não tem quota/deadline
próprios neste script.

Full path e flags de busca não autenticam universalmente todos os módulos:
redirection, API sets, SxS, módulos já carregados e KnownDLLs têm regras próprias.
Forwarders de exports e nomes usados por carregamentos dinâmicos não são
aprovados por esta inspeção. System32/OS/AppCompat e ausência de comprometimento
ativo do mesmo usuário/administrador continuam precondições da composição.
[Ordem de busca de DLLs](https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-search-order).

`icuuc.dll`, importado pelo Qt Core deste SDK, é parte da confiança no Windows,
não um décimo artefato privado: o Windows disponibiliza ICU desde 10 versão
1703, mas isso não comprova que o Qt/PE atual seja compatível com essa versão.
No host auditado, `icuuc.dll` encaminha exports para `icu.dll` do sistema.
[ICU no Windows](https://learn.microsoft.com/en-us/windows/win32/intl/international-components-for-unicode--icu-).

A auditoria somente leitura dos bootstrap/payload locais encontrou manifests
**embutidos** `asInvoker`, `uiAccess=false`, sem dependencies SxS. Este script não
extrai nem valida XML de todos os manifests. A lease deve recusar arquivos
externos extras (`qt.conf`, `qt6.conf`, `.local`, manifests e DLLs desconhecidas),
autenticar e ancorar os arquivos antes de `CreateProcess`, e reter handles até
saída nativa e ACK de todos os pipes. O recurso Qt `:/qt/etc/qt.conf` é parte do
payload pinado; não é validado por este checker. Handles gerenciados deste
snapshot não substituem esse protocolo nem protegem ancestrais/ACLs contra um
atacante ativo do mesmo usuário.
