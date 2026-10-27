<?php
// Sends the sign-in code emails. Configure in config.php:
//   'mail' => ['transport' => 'brevo', 'api_key' => …, 'from' => …]   Brevo's transactional email API
//   'mail' => ['transport' => 'smtp', 'host' => …, …]                 any SMTP server (Brevo relay, Gmail…)
//   'mail' => ['transport' => 'mail']                                 PHP's mail() (some web hosts)
//   'mail' => ['transport' => 'log']                                  write to data/mail.log (testing only)
declare(strict_types=1);

function owner_email(): string
{
    $email = trim((string) (config()['owner_email'] ?? ''));
    if (!filter_var($email, FILTER_VALIDATE_EMAIL)) {
        throw new UserError('Sign-in emails are not set up yet: add your address as owner_email in config.php (see README).');
    }
    return $email;
}

function send_owner_mail(string $subject, string $body): void
{
    $to = owner_email();
    $cfg = mail_config();
    $host = preg_replace('/:\d+$/', '', (string) ($_SERVER['HTTP_HOST'] ?? 'localhost'));
    $from = trim((string) ($cfg['from'] ?? ''));
    if ($from === '') {
        // Relay services (Brevo, Gmail) only accept a sender address you have verified with them.
        if (in_array($cfg['transport'], ['brevo', 'smtp'], true)) {
            throw new UserError("Add the sender address ('from') to the mail settings in config.php.");
        }
        $from = 'timesheets@' . $host;
    }
    $fromName = (string) ($cfg['from_name'] ?? (trim((string) setting('business_name')) ?: APP_NAME));
    $encodedSubject = '=?UTF-8?B?' . base64_encode($subject) . '?=';
    $headers = [
        'Date: ' . date(DATE_RFC2822),
        'From: =?UTF-8?B?' . base64_encode($fromName) . "?= <$from>",
        'Message-ID: <' . bin2hex(random_bytes(12)) . '@' . (explode('@', $from)[1] ?? 'localhost') . '>',
        'MIME-Version: 1.0',
        'Content-Type: text/plain; charset=UTF-8',
        'Content-Transfer-Encoding: 8bit',
        'X-Auto-Response-Suppress: All',
    ];
    $body = str_replace(["\r\n", "\r"], "\n", $body);

    switch ($cfg['transport']) {
        case 'log':
            file_put_contents(data_dir() . '/mail.log', "To: $to\nSubject: $subject\n\n$body\n-----\n", FILE_APPEND | LOCK_EX);
            return;
        case 'brevo':
            brevo_send($cfg, $from, $fromName, $to, $subject, $body);
            return;
        case 'smtp':
            $message = implode("\r\n", array_merge($headers, ["To: <$to>", "Subject: $encodedSubject"])) . "\r\n\r\n" . str_replace("\n", "\r\n", $body);
            smtp_send($cfg, $from, $to, $message);
            return;
        default:
            if (!mail($to, $encodedSubject, $body, implode("\r\n", $headers), '-f' . $from)) {
                throw new RuntimeException('mail() failed');
            }
    }
}

function mail_config(): array
{
    return (array) (config()['mail'] ?? []) + ['transport' => 'mail'];
}

/** What the Settings page may show about email delivery: no addresses, keys or passwords. */
function mail_status(): array
{
    $cfg = mail_config();
    $configured = filter_var(trim((string) (config()['owner_email'] ?? '')), FILTER_VALIDATE_EMAIL) !== false;
    switch ($cfg['transport']) {
        case 'brevo':
            $how = 'Brevo API';
            $configured = $configured && !empty($cfg['api_key']) && !empty($cfg['from']);
            break;
        case 'smtp':
            $how = 'SMTP via ' . ($cfg['host'] ?? '?') . ':' . ($cfg['port'] ?? 587);
            $configured = $configured && !empty($cfg['host']) && !empty($cfg['from']);
            break;
        case 'log':
            $how = 'Log file only (testing — emails are not sent)';
            break;
        default:
            $how = 'PHP mail() on this server';
    }
    return ['method' => $how, 'configured' => $configured];
}

/** Brevo transactional email API (https://developers.brevo.com/reference/sendtransacemail). */
function brevo_send(array $cfg, string $from, string $fromName, string $to, string $subject, string $body): void
{
    $key = trim((string) ($cfg['api_key'] ?? ''));
    if ($key === '') {
        throw new UserError("Add your Brevo API key ('api_key') to the mail settings in config.php.");
    }
    $url = (string) ($cfg['api_url'] ?? 'https://api.brevo.com/v3/smtp/email');
    $payload = json_encode([
        'sender' => ['name' => $fromName, 'email' => $from],
        'to' => [['email' => $to]],
        'subject' => $subject,
        'textContent' => $body,
        'tags' => ['timesheets'],
    ], JSON_UNESCAPED_UNICODE);
    $headers = ['api-key: ' . $key, 'Content-Type: application/json', 'Accept: application/json'];

    if (function_exists('curl_init')) {
        $ch = curl_init($url);
        curl_setopt_array($ch, [CURLOPT_POST => true, CURLOPT_POSTFIELDS => $payload, CURLOPT_HTTPHEADER => $headers,
            CURLOPT_RETURNTRANSFER => true, CURLOPT_TIMEOUT => 20, CURLOPT_CONNECTTIMEOUT => 10]);
        $response = curl_exec($ch);
        $status = (int) curl_getinfo($ch, CURLINFO_RESPONSE_CODE);
        $error = $response === false ? curl_error($ch) : '';
        curl_close($ch);
    } else {
        $ctx = stream_context_create(['http' => ['method' => 'POST', 'header' => implode("\r\n", $headers),
            'content' => $payload, 'timeout' => 20, 'ignore_errors' => true]]);
        $response = @file_get_contents($url, false, $ctx);
        $lines = function_exists('http_get_last_response_headers') ? (http_get_last_response_headers() ?? []) : ($http_response_header ?? []);
        $status = preg_match('#^HTTP/\S+\s+(\d{3})#', (string) ($lines[0] ?? ''), $m) ? (int) $m[1] : 0;
        $error = $response === false ? (string) (error_get_last()['message'] ?? 'no response') : '';
    }
    if ($response === false || $status < 200 || $status >= 300) {
        $detail = $error ?: (json_decode((string) $response, true)['message'] ?? trim((string) $response));
        throw new RuntimeException("Brevo API error (HTTP $status): $detail");
    }
}

/** Minimal SMTP client: SSL (port 465) or STARTTLS (port 587), AUTH LOGIN. */
function smtp_send(array $cfg, string $from, string $to, string $message): void
{
    $secure = $cfg['secure'] ?? 'tls';
    $port = (int) ($cfg['port'] ?? ($secure === 'ssl' ? 465 : 587));
    $remote = ($secure === 'ssl' ? 'ssl://' : 'tcp://') . $cfg['host'] . ':' . $port;
    $ctx = stream_context_create(['ssl' => ['verify_peer' => true, 'verify_peer_name' => true, 'peer_name' => $cfg['host']]]);
    $fp = @stream_socket_client($remote, $errno, $errstr, 20, STREAM_CLIENT_CONNECT, $ctx);
    if (!$fp) {
        throw new RuntimeException("SMTP connect to $remote failed: $errstr");
    }
    stream_set_timeout($fp, 20);
    $read = function () use ($fp): array {
        $text = '';
        while (($line = fgets($fp, 1024)) !== false) {
            $text .= $line;
            if (strlen($line) < 4 || $line[3] === ' ') {
                break;
            }
        }
        return [(int) substr($text, 0, 3), trim($text)];
    };
    $cmd = function (?string $line, array $ok) use ($fp, $read): void {
        if ($line !== null) {
            fwrite($fp, $line . "\r\n");
        }
        [$code, $text] = $read();
        if (!in_array($code, $ok, true)) {
            throw new RuntimeException('SMTP error after "' . (str_starts_with((string) $line, 'AUTH') ? 'AUTH' : strtok((string) $line, ' ')) . "\": $text");
        }
    };
    $ehlo = 'EHLO ' . (gethostname() ?: 'localhost');
    $cmd(null, [220]);
    $cmd($ehlo, [250]);
    if ($secure === 'tls') {
        $cmd('STARTTLS', [220]);
        if (!stream_socket_enable_crypto($fp, true, STREAM_CRYPTO_METHOD_TLSv1_2_CLIENT | STREAM_CRYPTO_METHOD_TLSv1_3_CLIENT)) {
            throw new RuntimeException('SMTP STARTTLS failed');
        }
        $cmd($ehlo, [250]);
    }
    if (!empty($cfg['username'])) {
        $cmd('AUTH LOGIN', [334]);
        $cmd(base64_encode((string) $cfg['username']), [334]);
        $cmd(base64_encode((string) ($cfg['password'] ?? '')), [235]);
    }
    $cmd("MAIL FROM:<$from>", [250]);
    $cmd("RCPT TO:<$to>", [250, 251]);
    $cmd('DATA', [354]);
    // Dot-stuffing: lines starting with "." get an extra one.
    $cmd(preg_replace('/^\./m', '..', $message) . "\r\n.", [250]);
    fwrite($fp, "QUIT\r\n");
    fclose($fp);
}
