<?php
// Sends the sign-in code emails. Configure in config.php:
//   'mail' => ['transport' => 'mail']                      PHP's mail() (most web hosts)
//   'mail' => ['transport' => 'smtp', 'host' => …, …]      an SMTP server, e.g. Gmail with an app password
//   'mail' => ['transport' => 'log']                       write to data/mail.log (testing only)
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
    $cfg = (array) (config()['mail'] ?? []) + ['transport' => 'mail'];
    $host = preg_replace('/:\d+$/', '', (string) ($_SERVER['HTTP_HOST'] ?? 'localhost'));
    $from = (string) ($cfg['from'] ?? ($cfg['username'] ?? 'timesheets@' . $host));
    $fromName = (string) ($cfg['from_name'] ?? APP_NAME);
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
