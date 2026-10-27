<?php
// Sign-in endpoints (used before you are signed in):
//   passkey.options / passkey.verify   sign in with a passkey
//   backup.start / backup.verify       password, then a code emailed to you
//   setup.start / setup.finish         first run: emailed code, then choose a password
declare(strict_types=1);
require __DIR__ . '/lib/app.php';
require __DIR__ . '/lib/mail.php';
require __DIR__ . '/lib/webauthn.php';

start_session();
header('Content-Type: application/json; charset=utf-8');
header('Cache-Control: no-store');
header('X-Content-Type-Options: nosniff');

function reply($data, int $status = 200): void
{
    http_response_code($status);
    echo json_encode($data, JSON_UNESCAPED_UNICODE);
    exit;
}

if ($_SERVER['REQUEST_METHOD'] !== 'POST') {
    reply(['error' => 'POST required'], 405);
}
if (!hash_equals($_SESSION['csrf'], (string) ($_SERVER['HTTP_X_CSRF_TOKEN'] ?? ''))) {
    reply(['error' => 'Your session expired — please reload the page.'], 403);
}
$in = json_decode((string) file_get_contents('php://input'), true);
$in = is_array($in) ? $in : [];

function sign_in(): array
{
    session_regenerate_id(true);
    unset($_SESSION['email_code'], $_SESSION['webauthn'], $_SESSION['password_ok_at']);
    $_SESSION['auth'] = true;
    return ['ok' => true];
}

function issue_code(string $purpose): void
{
    $wait = 30 - (time() - (int) setting('code_sent_at'));
    if ($wait > 0) {
        throw new UserError("Please wait $wait seconds before asking for another code.");
    }
    $code = str_pad((string) random_int(0, 999999), 6, '0', STR_PAD_LEFT);
    set_setting('code_sent_at', (string) time());
    $what = $purpose === 'setup' ? 'finish setting up' : 'sign in to';
    try {
        send_owner_mail("Your " . APP_NAME . " code: $code",
            "Your code to $what " . APP_NAME . " is:\n\n    $code\n\nIt expires in 10 minutes.\n\n"
            . "Requested from " . ($_SERVER['REMOTE_ADDR'] ?? 'an unknown address') . ' at ' . date('H:i, j M Y') . ".\n"
            . ($purpose === 'setup'
                ? "If you didn't ask for this, someone has found your Timesheets page before it was set up. Don't share the code.\n"
                : "If this wasn't you, someone knows your password — change it in Settings.\n"));
    } catch (UserError $e) {
        throw $e;
    } catch (Throwable $e) {
        error_log('[timetracker] sending code failed: ' . $e->getMessage());
        throw new UserError('The code email could not be sent. Check the mail settings in config.php (details are in the server error log).');
    }
    $_SESSION['email_code'] = ['purpose' => $purpose, 'hash' => password_hash($code, PASSWORD_DEFAULT), 'expires' => time() + 600, 'attempts' => 0];
}

function check_code(string $purpose, $code): void
{
    $p = $_SESSION['email_code'] ?? null;
    if (!$p || $p['purpose'] !== $purpose || $p['expires'] < time()) {
        unset($_SESSION['email_code']);
        throw new UserError('That code has expired. Please request a new one.');
    }
    if (!password_verify(preg_replace('/\D/', '', (string) $code), $p['hash'])) {
        if (++$_SESSION['email_code']['attempts'] >= 5) {
            unset($_SESSION['email_code']);
            throw new UserError('Too many wrong codes. Please start again.');
        }
        throw new UserError('That code is not right — check the latest email and try again.');
    }
    unset($_SESSION['email_code']);
}

try {
    $action = (string) ($_GET['action'] ?? '');
    if (in_array($action, ['setup.start', 'setup.finish'], true)) {
        if (password_is_set()) {
            throw new UserError('Setup is already complete.');
        }
    } elseif (!password_is_set()) {
        throw new UserError('Please finish setting up first.');
    }

    switch ($action) {
        case 'passkey.options':
            reply(passkey_login_options());

        case 'passkey.verify':
            passkey_verify_login($in);
            reply(sign_in());

        case 'backup.start':
            $lockedUntil = (int) setting('login_locked_until');
            if ($lockedUntil > time()) {
                throw new UserError('Too many failed attempts. Try again in ' . ceil(($lockedUntil - time()) / 60) . ' minute(s).');
            }
            if (!password_verify((string) ($in['password'] ?? ''), (string) setting('password_hash'))) {
                usleep(500000);
                $failures = (int) setting('login_failures') + 1;
                set_setting('login_failures', (string) ($failures >= 5 ? 0 : $failures));
                if ($failures >= 5) {
                    set_setting('login_locked_until', (string) (time() + 900));
                }
                throw new UserError('Incorrect password.');
            }
            set_setting('login_failures', '0');
            if (password_needs_rehash((string) setting('password_hash'), PASSWORD_DEFAULT)) {
                set_setting('password_hash', password_hash((string) $in['password'], PASSWORD_DEFAULT));
            }
            issue_code('login');
            $_SESSION['password_ok_at'] = time();
            reply(['sent' => true]);

        case 'backup.verify':
            if (time() - (int) ($_SESSION['password_ok_at'] ?? 0) > 600) {
                throw new UserError('Please enter your password again.');
            }
            check_code('login', $in['code'] ?? '');
            reply(sign_in());

        case 'setup.start':
            issue_code('setup');
            reply(['sent' => true]);

        case 'setup.finish':
            $password = (string) ($in['password'] ?? '');
            if (strlen($password) < 10) {
                throw new UserError('Choose a password of at least 10 characters.');
            }
            if ($password !== (string) ($in['confirm'] ?? '')) {
                throw new UserError('The two passwords do not match.');
            }
            check_code('setup', $in['code'] ?? '');
            set_setting('password_hash', password_hash($password, PASSWORD_DEFAULT));
            $tz = (string) ($in['tz'] ?? '');
            if ($tz !== '' && in_array($tz, timezone_identifiers_list(), true)) {
                set_setting('timezone', $tz);
            }
            reply(sign_in());
    }
    reply(['error' => 'Unknown action'], 404);
} catch (UserError $e) {
    reply(['error' => $e->getMessage()], 400);
} catch (Throwable $e) {
    error_log('[timetracker] ' . $e);
    reply(['error' => 'Server error — see the server error log.'], 500);
}
