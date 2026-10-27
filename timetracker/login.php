<?php
declare(strict_types=1);
require __DIR__ . '/lib/app.php';

start_session();
send_security_headers();
header('X-Robots-Tag: noindex, nofollow');

if (isset($_GET['logout'])) {
    if (hash_equals($_SESSION['csrf'], (string) ($_GET['t'] ?? ''))) {
        $_SESSION = [];
        session_regenerate_id(true);
    }
    header('Location: login.php');
    exit;
}
if (is_logged_in()) {
    header('Location: ./');
    exit;
}

$setup = !password_is_set();
$hasPasskeys = !$setup && (int) db()->query('SELECT COUNT(*) FROM passkeys')->fetchColumn() > 0;
?><!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="robots" content="noindex, nofollow">
<meta name="csrf-token" content="<?= h($_SESSION['csrf']) ?>">
<title><?= h(APP_NAME) ?> — <?= $setup ? 'Set up' : 'Sign in' ?></title>
<link rel="stylesheet" href="assets/app.css?v=<?= APP_VERSION ?>">
</head>
<body class="login-page" data-mode="<?= $setup ? 'setup' : 'login' ?>" data-passkeys="<?= $hasPasskeys ? '1' : '0' ?>">
<main class="card login-card">
    <h1><?= h(APP_NAME) ?></h1>
    <p class="error" id="error" hidden></p>
    <p class="notice" id="notice" hidden></p>

    <?php if ($setup): ?>
        <section data-step="setup-start">
            <p class="muted">Welcome! To prove this is you, we’ll email a 6-digit code to the address set up on the server.</p>
            <button class="btn primary" type="button" id="setup-send">Email me a setup code</button>
        </section>
        <form data-step="setup-finish" hidden>
            <label>Code from the email
                <input name="code" inputmode="numeric" autocomplete="one-time-code" maxlength="7" required>
            </label>
            <label>Choose a password (10+ characters)
                <input type="password" name="password" autocomplete="new-password" minlength="10" required>
            </label>
            <label>Confirm password
                <input type="password" name="confirm" autocomplete="new-password" required>
            </label>
            <button class="btn primary" type="submit">Finish setting up</button>
            <p class="muted small">Next you’ll be able to add a passkey so you can sign in with your fingerprint, face or device PIN.</p>
        </form>
    <?php else: ?>
        <section data-step="passkey">
            <button class="btn primary big" type="button" id="passkey-login">🔑 Sign in with a passkey</button>
            <button class="btn ghost small" type="button" data-goto="backup-password">Can’t use your passkey? Use your password and an email code</button>
        </section>
        <form data-step="backup-password" hidden>
            <label>Password
                <input type="password" name="password" autocomplete="current-password" required>
            </label>
            <button class="btn primary" type="submit">Continue</button>
            <button class="btn ghost small" type="button" data-goto="passkey" data-needs-passkey>Back to passkey sign-in</button>
        </form>
        <form data-step="backup-code" hidden>
            <p class="muted">We’ve emailed you a 6-digit code. It expires in 10 minutes.</p>
            <label>Code
                <input name="code" inputmode="numeric" autocomplete="one-time-code" maxlength="7" required>
            </label>
            <button class="btn primary" type="submit">Sign in</button>
            <button class="btn ghost small" type="button" data-goto="backup-password">Send a new code</button>
        </form>
    <?php endif ?>
</main>
<script src="assets/passkey.js?v=<?= APP_VERSION ?>"></script>
<script src="assets/login.js?v=<?= APP_VERSION ?>"></script>
</body>
</html>
