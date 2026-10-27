<?php
declare(strict_types=1);
require __DIR__ . '/lib/app.php';

start_session();
send_security_headers();

if (isset($_GET['logout'])) {
    if (hash_equals($_SESSION['csrf'], (string) ($_GET['t'] ?? ''))) {
        $_SESSION = [];
        session_regenerate_id(true);
    }
    header('Location: login.php');
    exit;
}

$setup = !password_is_set();
$error = '';

if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    $password = (string) ($_POST['password'] ?? '');
    if (!hash_equals($_SESSION['csrf'], (string) ($_POST['csrf'] ?? ''))) {
        $error = 'Your session expired. Please try again.';
    } elseif ($setup) {
        if (strlen($password) < 8) {
            $error = 'Choose a password of at least 8 characters.';
        } elseif ($password !== (string) ($_POST['confirm'] ?? '')) {
            $error = 'The two passwords do not match.';
        } else {
            set_setting('password_hash', password_hash($password, PASSWORD_DEFAULT));
            $tz = (string) ($_POST['tz'] ?? '');
            if ($tz !== '' && in_array($tz, timezone_identifiers_list(), true)) {
                set_setting('timezone', $tz);
            }
            session_regenerate_id(true);
            $_SESSION['auth'] = true;
            header('Location: ./#settings');
            exit;
        }
    } else {
        $lockedUntil = (int) setting('login_locked_until');
        if ($lockedUntil > time()) {
            $error = 'Too many failed attempts. Try again in ' . ceil(($lockedUntil - time()) / 60) . ' minute(s).';
        } elseif (password_verify($password, (string) setting('password_hash'))) {
            set_setting('login_failures', '0');
            if (password_needs_rehash((string) setting('password_hash'), PASSWORD_DEFAULT)) {
                set_setting('password_hash', password_hash($password, PASSWORD_DEFAULT));
            }
            session_regenerate_id(true);
            $_SESSION['auth'] = true;
            header('Location: ./');
            exit;
        } else {
            usleep(500000);
            $failures = (int) setting('login_failures') + 1;
            set_setting('login_failures', (string) $failures);
            if ($failures >= 5) {
                set_setting('login_locked_until', (string) (time() + 900));
                set_setting('login_failures', '0');
            }
            $error = 'Incorrect password.';
        }
    }
}
?><!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="robots" content="noindex, nofollow">
<title><?= h(APP_NAME) ?> — <?= $setup ? 'Set up' : 'Sign in' ?></title>
<link rel="stylesheet" href="assets/app.css?v=<?= APP_VERSION ?>">
</head>
<body class="login-page">
<form method="post" class="card login-card">
    <h1><?= h(APP_NAME) ?></h1>
    <?php if ($setup): ?>
        <p class="muted">Welcome! Choose the password you will use to sign in. You are the only user.</p>
    <?php endif ?>
    <?php if ($error): ?><p class="error"><?= h($error) ?></p><?php endif ?>
    <input type="hidden" name="csrf" value="<?= h($_SESSION['csrf']) ?>">
    <label>Password
        <input type="password" name="password" required autofocus autocomplete="<?= $setup ? 'new-password' : 'current-password' ?>">
    </label>
    <?php if ($setup): ?>
        <label>Confirm password
            <input type="password" name="confirm" required autocomplete="new-password">
        </label>
        <input type="hidden" name="tz" id="tz">
        <script src="assets/login.js"></script>
    <?php endif ?>
    <button class="btn primary" type="submit"><?= $setup ? 'Create password' : 'Sign in' ?></button>
</form>
</body>
</html>
