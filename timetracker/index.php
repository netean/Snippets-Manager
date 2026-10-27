<?php
declare(strict_types=1);
require __DIR__ . '/lib/app.php';

start_session();
send_security_headers();
require_login();
?><!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="robots" content="noindex, nofollow">
<meta name="csrf-token" content="<?= h($_SESSION['csrf']) ?>">
<title><?= h(APP_NAME) ?></title>
<link rel="icon" href="data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'><circle cx='12' cy='12' r='10' fill='%232563eb'/><path d='M12 6v6l4 2' stroke='white' stroke-width='2' fill='none' stroke-linecap='round'/></svg>">
<link rel="stylesheet" href="assets/app.css?v=<?= APP_VERSION ?>">
</head>
<body>
<header class="topbar">
    <a class="brand" href="#track"><?= h(APP_NAME) ?></a>
    <nav id="nav">
        <a href="#track">Track</a>
        <a href="#timesheet">Timesheet</a>
        <a href="#reports">Reports</a>
        <a href="#invoices">Invoices</a>
        <a href="#manage">Clients &amp; projects</a>
        <a href="#settings">Settings</a>
    </nav>
    <span id="running-indicator" class="running-indicator" hidden></span>
    <span id="who" class="who"></span>
    <a class="logout" href="login.php?logout=1&amp;t=<?= h($_SESSION['csrf']) ?>">Sign out</a>
</header>
<main id="view"><p class="muted">Loading…</p></main>
<div id="toast" class="toast" hidden></div>
<script src="assets/passkey.js?v=<?= APP_VERSION ?>"></script>
<script src="assets/app.js?v=<?= APP_VERSION ?>"></script>
</body>
</html>
