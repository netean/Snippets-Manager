<?php
// Downloads a consistent copy of the whole database.
declare(strict_types=1);
require __DIR__ . '/lib/app.php';

start_session();
require_login();
if (!hash_equals($_SESSION['csrf'], (string) ($_GET['t'] ?? ''))) {
    http_response_code(403);
    exit('Reload the app and try again.');
}

$tmp = tempnam(sys_get_temp_dir(), 'tt');
unlink($tmp);
try {
    db()->exec('VACUUM INTO ' . db()->quote($tmp));
} catch (Throwable $e) {
    copy(db_path(), $tmp); // very old SQLite without VACUUM INTO
}
header('Content-Type: application/vnd.sqlite3');
header('Content-Disposition: attachment; filename="timetracker-backup-' . date('Y-m-d') . '.sqlite"');
header('Content-Length: ' . filesize($tmp));
readfile($tmp);
unlink($tmp);
