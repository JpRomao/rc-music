# Player sintético para o caminho EJS `type=player`

`synthetic-player.js` é uma fixture original do RC Music, sem código baixado de player, mídia ou página pública. Sua transformação é deliberadamente simples e não representa algoritmos usados por um serviço real. A compatibilidade foi inicialmente deduzida das fontes fixadas e depois confirmada no [ensaio com EJS/Node reais](../../../docs/youtube-audio-and-lyrics.md#runtime-real-no-ensaio-offline).

O [entrypoint EJS 0.8.0](https://raw.githubusercontent.com/yt-dlp/ejs/0.8.0/src/yt/solver/main.ts) chama `preprocessPlayer` quando `type` é `player`. O [preprocessador](https://raw.githubusercontent.com/yt-dlp/ejs/0.8.0/src/yt/solver/solvers.ts) reconhece um IIFE único com chamada de método e conserva declarações/atribuições no corpo. A [extração de solvers](https://raw.githubusercontent.com/yt-dlp/ejs/0.8.0/src/yt/solver/nsig.ts) procura uma função contendo a chamada com os marcadores literais `alr` e `yes`; executa essa factory com uma assinatura URI-encoded, usa `set`/`get` e chama outro método próprio do protótipo para obter as transformações. Essa estrutura mínima foi implementada aqui, sem copiar a implementação de qualquer player.

O objeto de teste não abre URL ou arquivo. `source` é ignorado. `set`/`get` usam um mapa privado, e `applyTransforms` opera por codepoints: `n` é revertido, enquanto `sig` é rotacionado um codepoint à esquerda. A assinatura é decodificada/codificada em torno da transformação para respeitar o contrato da factory; percentuais e espaços permanecem dados.

| Tipo | Input | Output esperado |
|---|---|---|
| `n` | `abcdef` | `fedcba` |
| `n` | `曲A😀` | `😀A曲` |
| `sig` | `abcd` | `bcda` |
| `sig` | `a b%?` | ` b%?a` (um espaço inicial) |
| `sig` | `曲A😀` | `A😀曲` |

Não usar input palíndromo ou unitário para `n` na jornada com o diretor real: o [diretor do yt-dlp fixado](https://raw.githubusercontent.com/yt-dlp/yt-dlp/2026.03.17/yt_dlp/extractor/youtube/jsc/_director.py) rejeita um resultado `n` que termine com o input original. Não repetir inputs dentro de um request nessa prova; a validação upstream também exige contagem da lista, além do conjunto de chaves aceito pelo nosso wire.

O teste real proposto deve usar o mesmo `youtubeBrokerNodeInput` do produto: EJS core/lib `0.8.0` reais, esta fixture como `player`, requests públicos `n`/`sig`, `type=player` e `output_preprocessed=false`. Não substituir por `type=preprocessed`, funções prontas ou código gerado manualmente. Comparar todos os pares acima na resposta produzida pelo Node real, além de exit/EOF reconhecidos e ausência de diagnósticos livres.

`tests/youtube/offline_entry.py` é o entrypoint **de teste separado** que chama `rcmusic_youtube.run` sem alterar o módulo de produção. Usa as classes reais do yt-dlp; substitui temporariamente `YoutubeDL.extract_info`, `YoutubeDL.urlopen`, `YoutubeIE._load_player` e `YoutubeIE._real_initialize`. A extração de teste chama o diretor JSC real com os tipos públicos e o registro já configurado pelo provider. Os métodos remotos são recusados e contados, assim como eventos de socket pelo audit hook Python.

Somente após conferir todas as respostas acima são retornados metadata e um formato M4A/AAC/HTTPS inteiramente sintéticos. O URL é inerte, com `download=False` e `check_formats=false`. O broker C++ real executa extractor/Node irmãos com NoChild e exige os ACKs antes de Ready. O recibo `runtime-smoke-01.json` comprova os cinco outputs, `http=socket=init=0`, um player/provider e a liberação do runtime; esses contadores instrumentados não são uma captura externa de tráfego.

O [corpus upstream](https://raw.githubusercontent.com/yt-dlp/ejs/0.8.0/src/yt/solver/test/tests.ts) e seu [loader](https://raw.githubusercontent.com/yt-dlp/ejs/0.8.0/src/yt/solver/test/utils.ts) foram consultados apenas como contrato de teste. Seus players de sites e valores não foram copiados ou baixados. A preparação inicial destes arquivos não executou runtimes; os checks e a execução real posteriores estão registrados separadamente no guia.
