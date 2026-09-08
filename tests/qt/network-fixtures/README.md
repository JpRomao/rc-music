# Intentionally non-secret synthetic TLS fixture

`server-cert.pem` and `server-key.pem` are an intentionally non-secret, newly generated
test certificate/key pair for `media.rc-music.invalid`. They are project-owned
synthetic test data under the private fixture terms in
[LICENSE.assets.txt](../../ui/fixtures/LICENSE.assets.txt), not credentials for any
service. Being non-secret does not authorize public redistribution. Never install
them in an OS/browser trust store or use them outside
isolated loopback tests. Tests add the certificate only to an individual client's
`QSslConfiguration` and never ignore verification errors.

Generated on 2026-09-08 with the already available OpenSSL CLI (no dependency was
installed), RSA 2048/SHA-256, self-signed, SAN `DNS:media.rc-music.invalid`, CA and
server-auth usage. Validity ends on 2126-08-15. The test server accepts no client
certificate and listens only on loopback. The unusually
long validity avoids test churn and is not a production TLS recommendation.

SHA-256 of the repository bytes (PEM fixtures keep LF via `.gitattributes`):

- `server-cert.pem`: `a9e6d27d408f7c606c42bc17a44f25acb10352adca7368c34c8cf18610546807`
- `server-key.pem`: `a1f4be252feaabf80e8bf5e36fa34784c6ab90d89ae3369dcc60340a06743233`
