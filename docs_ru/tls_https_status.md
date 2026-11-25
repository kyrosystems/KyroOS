# Поддержка TLS/HTTPS в KyroOS

В KyroOS реализована нативная клиентская поддержка **TLS 1.3** и протокола **HTTPS** через автономную библиотеку `userspace/lib/libtls` и системные утилиты `curl` и `wget`.

## Обзор

Автономная реализация TLS 1.3 обеспечивает:
- **Протокол TLS**: клиентское рукопожатие TLS 1.3 (RFC 8446).
- **Обмен ключами**: эфемерный протокол Диффи-Хеллмана на кривой Curve25519 (X25519).
- **Деривация ключей**: HKDF-Extract и HKDF-Expand на базе SHA-256 (RFC 5869).
- **Аутентифицированное шифрование (AEAD)**:
  - ChaCha20-Poly1305 (RFC 8439)
  - AES-128-GCM и AES-256-GCM
- **Уровень записей (Record Layer)**:
  - Формирование и парсинг TLS-записей Handshake и Application Data (тип содержимого 0x17 с внутренним типом).
  - Учёт порядковых номеров пакетов и построение nonce (`nonce ^ seq_num`).
- **Сетевые утилиты**:
  - `curl`: поддерживает протоколы `http://` и `https://`, флаги заголовков и подробный вывод (`-v`).
  - `wget`: загрузка файлов по протоколам `https://` и `http://` с сохранением на диск.

## Процесс рукопожатия (Handshake)

1. **ClientHello**:
   - Клиент отправляет запрос с шифронаборами TLS 1.3 (`TLS_AES_128_GCM_SHA256`, `TLS_CHACHA20_POLY1305_SHA256`).
   - Расширения: `supported_versions` (0x0304), `server_name` (SNI для виртуального хостинга), `supported_groups` (X25519), `key_share` с 32-байтным публичным ключом.
2. **ServerHello**:
   - Сервер отвечает согласованным набором шифров и собственным публичным ключом X25519.
3. **Генерация секретов**:
   - Вычисляется общий секрет через скалярное умножение точек X25519.
   - Через HKDF-Extract вычисляется Handshake Secret.
   - Формируются ключи шифрования трафика рукопожатия (`c_hs_traffic`, `s_hs_traffic`).
4. **Зашифрованные расширения и данные**:
   - Завершение рукопожатия (EncryptedExtensions, Finished) и переход на Application Traffic Keys для защищённой передачи HTTP-запросов и ответов.

## Использование в приложениях

Приложения линкуются с `userspace/lib/libtls` (`tls.h`):
```c
tls_session_t *tls = tls_create_session(sockfd, hostname);
if (tls_handshake(tls) == 0) {
    tls_write(tls, request_str, len);
    int n = tls_read(tls, response_buf, sizeof(response_buf));
}
tls_destroy_session(tls);
```
