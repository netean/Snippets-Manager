<?php
// Core: configuration, database, settings, sessions and shared helpers.
declare(strict_types=1);

const APP_NAME = 'Timesheets';
const APP_VERSION = '1.0';

class UserError extends RuntimeException {}

function config(): array
{
    static $config;
    if ($config === null) {
        $config = [
            // Where the SQLite database and session files live. Keep it outside the
            // web root if your host allows it; data/.htaccess blocks web access otherwise.
            'data_dir' => dirname(__DIR__) . '/data',
            'session_days' => 30,
        ];
        $file = dirname(__DIR__) . '/config.php';
        if (is_file($file)) {
            $config = array_merge($config, (array) require $file);
        }
    }
    return $config;
}

function data_dir(): string
{
    $dir = rtrim(config()['data_dir'], '/\\');
    if (!is_dir($dir) && !mkdir($dir, 0700, true) && !is_dir($dir)) {
        throw new RuntimeException("Cannot create data directory: $dir");
    }
    return $dir;
}

function db_path(): string
{
    return data_dir() . '/timetracker.sqlite';
}

function db(): PDO
{
    static $pdo;
    if ($pdo === null) {
        $pdo = new PDO('sqlite:' . db_path());
        $pdo->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
        $pdo->setAttribute(PDO::ATTR_DEFAULT_FETCH_MODE, PDO::FETCH_ASSOC);
        $pdo->setAttribute(PDO::ATTR_EMULATE_PREPARES, false);
        // The default rollback journal (not WAL) is used on purpose: WAL needs shared
        // memory, which does not work when the database sits on a network drive.
        $pdo->exec('PRAGMA foreign_keys = ON');
        $pdo->exec('PRAGMA busy_timeout = 5000');
        migrate($pdo);
        date_default_timezone_set(setting('timezone') ?: 'UTC');
    }
    return $pdo;
}

function migrate(PDO $pdo): void
{
    $version = (int) $pdo->query('PRAGMA user_version')->fetchColumn();
    $migrations = [
        1 => <<<SQL
            CREATE TABLE settings (
                key   TEXT PRIMARY KEY,
                value TEXT
            );
            CREATE TABLE clients (
                id            INTEGER PRIMARY KEY,
                name          TEXT NOT NULL,
                contact       TEXT,
                email         TEXT,
                address       TEXT,
                rate          REAL,
                tax_rate      REAL,
                payment_terms INTEGER,
                invoice_notes TEXT,
                archived      INTEGER NOT NULL DEFAULT 0,
                created_at    TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
            );
            CREATE TABLE projects (
                id         INTEGER PRIMARY KEY,
                client_id  INTEGER NOT NULL REFERENCES clients(id) ON DELETE RESTRICT,
                name       TEXT NOT NULL,
                code       TEXT,
                rate       REAL,
                color      TEXT,
                billable   INTEGER NOT NULL DEFAULT 1,
                archived   INTEGER NOT NULL DEFAULT 0,
                created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
            );
            -- A task with no project is a general work item usable on every project.
            CREATE TABLE tasks (
                id         INTEGER PRIMARY KEY,
                project_id INTEGER REFERENCES projects(id) ON DELETE CASCADE,
                name       TEXT NOT NULL,
                rate       REAL,
                archived   INTEGER NOT NULL DEFAULT 0
            );
            CREATE TABLE categories (
                id       INTEGER PRIMARY KEY,
                name     TEXT NOT NULL UNIQUE COLLATE NOCASE,
                color    TEXT,
                archived INTEGER NOT NULL DEFAULT 0
            );
            CREATE TABLE invoices (
                id          INTEGER PRIMARY KEY,
                number      TEXT NOT NULL UNIQUE,
                client_id   INTEGER REFERENCES clients(id) ON DELETE RESTRICT,
                bill_to     TEXT,
                issue_date  TEXT NOT NULL,
                due_date    TEXT,
                period_from TEXT,
                period_to   TEXT,
                grouping    TEXT,
                status      TEXT NOT NULL DEFAULT 'draft',
                paid_date   TEXT,
                tax_rate    REAL NOT NULL DEFAULT 0,
                notes       TEXT,
                subtotal    REAL NOT NULL DEFAULT 0,
                tax         REAL NOT NULL DEFAULT 0,
                total       REAL NOT NULL DEFAULT 0,
                created_at  TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
            );
            CREATE TABLE invoice_lines (
                id          INTEGER PRIMARY KEY,
                invoice_id  INTEGER NOT NULL REFERENCES invoices(id) ON DELETE CASCADE,
                sort        INTEGER NOT NULL DEFAULT 0,
                description TEXT NOT NULL,
                quantity    REAL NOT NULL,
                unit        TEXT,
                unit_price  REAL NOT NULL,
                amount      REAL NOT NULL,
                client_id   INTEGER,
                project_id  INTEGER
            );
            CREATE TABLE entries (
                id          INTEGER PRIMARY KEY,
                date        TEXT NOT NULL,
                start_time  TEXT,
                end_time    TEXT,
                minutes     INTEGER NOT NULL,
                description TEXT,
                project_id  INTEGER NOT NULL REFERENCES projects(id) ON DELETE RESTRICT,
                task_id     INTEGER REFERENCES tasks(id) ON DELETE SET NULL,
                billable    INTEGER NOT NULL DEFAULT 1,
                rate        REAL,
                invoice_id  INTEGER REFERENCES invoices(id) ON DELETE SET NULL,
                created_at  TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
                updated_at  TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
            );
            CREATE TABLE entry_categories (
                entry_id    INTEGER NOT NULL REFERENCES entries(id) ON DELETE CASCADE,
                category_id INTEGER NOT NULL REFERENCES categories(id) ON DELETE CASCADE,
                PRIMARY KEY (entry_id, category_id)
            );
            CREATE INDEX entries_date ON entries(date);
            CREATE INDEX entries_project ON entries(project_id);
            CREATE INDEX entries_invoice ON entries(invoice_id);
            CREATE INDEX projects_client ON projects(client_id);
            SQL,
    ];
    foreach ($migrations as $v => $sql) {
        if ($version < $v) {
            $pdo->beginTransaction();
            $pdo->exec($sql);
            $pdo->exec("PRAGMA user_version = $v");
            $pdo->commit();
        }
    }
}

function setting_defaults(): array
{
    return [
        'business_name' => '',
        'your_name' => '',
        'address' => '',
        'email' => '',
        'phone' => '',
        'website' => '',
        'tax_number' => '',
        'company_number' => '',
        'bank_details' => '',
        'currency_code' => 'GBP',
        'currency_symbol' => '£',
        'default_rate' => '0',
        'tax_label' => 'VAT',
        'tax_rate' => '0',
        'invoice_prefix' => 'INV-',
        'invoice_next' => '1',
        'invoice_digits' => '4',
        'payment_terms' => '30',
        'invoice_footer' => 'Thank you for your business.',
        'umbrella_bill_to' => '',
        'date_format' => 'd/m/Y',
        'week_start' => '1',
        'timezone' => 'Europe/London',
        'show_rates_on_timesheet' => '0',
    ];
}

// Settings that must never be sent to the browser.
const PRIVATE_SETTINGS = ['password_hash', 'login_failures', 'login_locked_until', 'timer'];

function setting(string $key): ?string
{
    $all = settings();
    return $all[$key] ?? null;
}

function settings(bool $reload = false): array
{
    static $cache;
    if ($cache === null || $reload) {
        $rows = db()->query('SELECT key, value FROM settings')->fetchAll(PDO::FETCH_KEY_PAIR);
        $cache = array_merge(setting_defaults(), $rows);
    }
    return $cache;
}

function set_setting(string $key, ?string $value): void
{
    $stmt = db()->prepare('INSERT INTO settings (key, value) VALUES (?, ?)
        ON CONFLICT(key) DO UPDATE SET value = excluded.value');
    $stmt->execute([$key, $value]);
    settings(true);
}

// ---------------------------------------------------------------------------
// Sessions and authentication (single user, password only)

function is_https(): bool
{
    return (!empty($_SERVER['HTTPS']) && $_SERVER['HTTPS'] !== 'off')
        || ($_SERVER['HTTP_X_FORWARDED_PROTO'] ?? '') === 'https';
}

function start_session(): void
{
    $lifetime = 86400 * (int) config()['session_days'];
    $dir = data_dir() . '/sessions';
    if (!is_dir($dir)) {
        mkdir($dir, 0700, true);
    }
    // Own session directory so shared hosts' session clean-up doesn't log us out early.
    session_save_path($dir);
    ini_set('session.gc_maxlifetime', (string) $lifetime);
    ini_set('session.use_strict_mode', '1');
    session_name('tt_session');
    session_set_cookie_params([
        'lifetime' => $lifetime,
        'path' => '/',
        'secure' => is_https(),
        'httponly' => true,
        'samesite' => 'Strict',
    ]);
    session_start();
    if (empty($_SESSION['csrf'])) {
        $_SESSION['csrf'] = bin2hex(random_bytes(16));
    }
}

function is_logged_in(): bool
{
    return !empty($_SESSION['auth']) && password_is_set();
}

function password_is_set(): bool
{
    return (string) setting('password_hash') !== '';
}

function require_login(): void
{
    if (!is_logged_in()) {
        header('Location: login.php');
        exit;
    }
}

function send_security_headers(): void
{
    header('X-Frame-Options: DENY');
    header('X-Content-Type-Options: nosniff');
    header('Referrer-Policy: same-origin');
    header("Content-Security-Policy: default-src 'self'; style-src 'self' 'unsafe-inline'; img-src 'self' data:; frame-ancestors 'none'");
}

// ---------------------------------------------------------------------------
// Formatting

function h(?string $s): string
{
    return htmlspecialchars((string) $s, ENT_QUOTES, 'UTF-8');
}

function fmt_date(?string $ymd): string
{
    if (!$ymd) {
        return '';
    }
    $d = DateTime::createFromFormat('!Y-m-d', $ymd);
    return $d ? $d->format(setting('date_format') ?: 'd/m/Y') : $ymd;
}

function fmt_money(float $amount): string
{
    $sym = (string) setting('currency_symbol');
    $s = number_format(abs($amount), 2);
    return ($amount < 0 ? '-' : '') . $sym . $s;
}

function fmt_hours(int $minutes): string
{
    return number_format($minutes / 60, 2);
}

function fmt_hm(int $minutes): string
{
    return intdiv($minutes, 60) . ':' . str_pad((string) ($minutes % 60), 2, '0', STR_PAD_LEFT);
}

// "Week commencing …", "October 2026" or "01/10/2026 – 15/10/2026"
function describe_period(?string $from, ?string $to): string
{
    if (!$from && !$to) {
        return 'All time';
    }
    if ($from && $to) {
        $f = new DateTime($from);
        $t = new DateTime($to);
        if ($f->format('d') === '01' && $t->format('Y-m-d') === $f->format('Y-m-t')) {
            return $f->format('F Y');
        }
        if ((int) $f->diff($t)->days === 6 && (int) $f->format('N') === (int) (setting('week_start') ?: 1)) {
            return 'Week commencing ' . fmt_date($from);
        }
        if ($from === $to) {
            return fmt_date($from);
        }
        return fmt_date($from) . ' – ' . fmt_date($to);
    }
    return $from ? 'From ' . fmt_date($from) : 'Up to ' . fmt_date($to);
}

function format_invoice_number(int $n): string
{
    $prefix = str_replace(['{YYYY}', '{YY}', '{MM}'], [date('Y'), date('y'), date('m')], (string) setting('invoice_prefix'));
    return $prefix . str_pad((string) $n, max(1, (int) setting('invoice_digits')), '0', STR_PAD_LEFT);
}

// ---------------------------------------------------------------------------
// Validation

function valid_date($s): bool
{
    if (!is_string($s) || !preg_match('/^\d{4}-\d{2}-\d{2}$/', $s)) {
        return false;
    }
    [$y, $m, $d] = array_map('intval', explode('-', $s));
    return checkdate($m, $d, $y);
}

function valid_time($s): bool
{
    return is_string($s) && preg_match('/^([01]\d|2[0-3]):[0-5]\d$/', $s) === 1;
}

function time_to_min(string $t): int
{
    [$h, $m] = explode(':', $t);
    return (int) $h * 60 + (int) $m;
}

function min_to_time(int $m): string
{
    $m = (($m % 1440) + 1440) % 1440;
    return sprintf('%02d:%02d', intdiv($m, 60), $m % 60);
}

// ---------------------------------------------------------------------------
// Time entry queries shared by the API, reports and invoices

/**
 * Builds a WHERE clause from report-style filters.
 * Recognised keys: from, to, client_id, project_id, task_id, category_id,
 * billable (0/1), invoiced ('yes'/'no'), invoice_id, q.
 */
function entry_filter(array $f): array
{
    $where = ['1 = 1'];
    $params = [];
    if (!empty($f['from']) && valid_date($f['from'])) {
        $where[] = 'e.date >= :from';
        $params[':from'] = $f['from'];
    }
    if (!empty($f['to']) && valid_date($f['to'])) {
        $where[] = 'e.date <= :to';
        $params[':to'] = $f['to'];
    }
    foreach (['client_id' => 'p.client_id', 'project_id' => 'e.project_id', 'task_id' => 'e.task_id', 'invoice_id' => 'e.invoice_id'] as $key => $col) {
        if (isset($f[$key]) && $f[$key] !== '' && $f[$key] !== null) {
            $where[] = "$col = :$key";
            $params[":$key"] = (int) $f[$key];
        }
    }
    if (isset($f['category_id']) && $f['category_id'] !== '' && $f['category_id'] !== null) {
        $where[] = 'EXISTS (SELECT 1 FROM entry_categories ec WHERE ec.entry_id = e.id AND ec.category_id = :category_id)';
        $params[':category_id'] = (int) $f['category_id'];
    }
    if (isset($f['billable']) && $f['billable'] !== '' && $f['billable'] !== null) {
        $where[] = 'e.billable = :billable';
        $params[':billable'] = (int) (bool) $f['billable'];
    }
    if (($f['invoiced'] ?? '') === 'yes') {
        $where[] = 'e.invoice_id IS NOT NULL';
    } elseif (($f['invoiced'] ?? '') === 'no') {
        $where[] = 'e.invoice_id IS NULL';
    }
    if (!empty($f['q'])) {
        $where[] = "(e.description LIKE :q ESCAPE '\\' OR t.name LIKE :q ESCAPE '\\')";
        $params[':q'] = '%' . addcslashes((string) $f['q'], '%_\\') . '%';
    }
    return [implode(' AND ', $where), $params];
}

/** Entries matching the filters, with names, effective rate, amount and category ids. */
function fetch_entries(array $filters, string $order = 'e.date, e.start_time IS NULL, e.start_time, e.id'): array
{
    [$where, $params] = entry_filter($filters);
    $params[':defrate'] = (float) setting('default_rate');
    $sql = "SELECT e.*, p.name AS project_name, p.code AS project_code, p.color AS project_color,
                   p.client_id, c.name AS client_name, t.name AS task_name, i.number AS invoice_number,
                   COALESCE(e.rate, t.rate, p.rate, c.rate, :defrate) AS eff_rate
            FROM entries e
            JOIN projects p ON p.id = e.project_id
            JOIN clients c ON c.id = p.client_id
            LEFT JOIN tasks t ON t.id = e.task_id
            LEFT JOIN invoices i ON i.id = e.invoice_id
            WHERE $where
            ORDER BY $order";
    $stmt = db()->prepare($sql);
    $stmt->execute($params);
    $rows = $stmt->fetchAll();
    if (!$rows) {
        return [];
    }

    $cats = [];
    $ids = array_column($rows, 'id');
    foreach (array_chunk($ids, 500) as $chunk) {
        $in = implode(',', array_map('intval', $chunk));
        foreach (db()->query("SELECT entry_id, category_id FROM entry_categories WHERE entry_id IN ($in)") as $r) {
            $cats[$r['entry_id']][] = (int) $r['category_id'];
        }
    }
    foreach ($rows as &$r) {
        foreach (['id', 'minutes', 'project_id', 'client_id', 'billable'] as $k) {
            $r[$k] = (int) $r[$k];
        }
        $r['task_id'] = $r['task_id'] === null ? null : (int) $r['task_id'];
        $r['invoice_id'] = $r['invoice_id'] === null ? null : (int) $r['invoice_id'];
        $r['rate'] = $r['rate'] === null ? null : (float) $r['rate'];
        $r['eff_rate'] = (float) $r['eff_rate'];
        $r['amount'] = $r['billable'] ? round($r['minutes'] / 60 * $r['eff_rate'], 2) : 0.0;
        $r['category_ids'] = $cats[$r['id']] ?? [];
    }
    return $rows;
}

function category_names(): array
{
    return db()->query('SELECT id, name FROM categories')->fetchAll(PDO::FETCH_KEY_PAIR);
}

/** Totals grouped several ways, used by the reports screen and printable reports. */
function summarise(array $entries): array
{
    $catNames = category_names();
    $out = ['minutes' => 0, 'billable_minutes' => 0, 'amount' => 0.0, 'uninvoiced_amount' => 0.0, 'count' => count($entries),
        'clients' => [], 'projects' => [], 'tasks' => [], 'categories' => [], 'days' => []];
    $add = function (array &$bucket, $key, string $label, array $e, array $extra = []) {
        if (!isset($bucket[$key])) {
            $bucket[$key] = ['key' => $key, 'label' => $label, 'minutes' => 0, 'billable_minutes' => 0, 'amount' => 0.0] + $extra;
        }
        $bucket[$key]['minutes'] += $e['minutes'];
        if ($e['billable']) {
            $bucket[$key]['billable_minutes'] += $e['minutes'];
        }
        $bucket[$key]['amount'] += $e['amount'];
    };
    foreach ($entries as $e) {
        $out['minutes'] += $e['minutes'];
        if ($e['billable']) {
            $out['billable_minutes'] += $e['minutes'];
            if ($e['invoice_id'] === null) {
                $out['uninvoiced_amount'] += $e['amount'];
            }
        }
        $out['amount'] += $e['amount'];
        $add($out['clients'], $e['client_id'], $e['client_name'], $e);
        $add($out['projects'], $e['project_id'], $e['project_name'], $e, ['client' => $e['client_name'], 'color' => $e['project_color']]);
        $add($out['tasks'], $e['project_id'] . ':' . ($e['task_id'] ?? 0), $e['task_name'] ?? '(no task)', $e, ['project' => $e['project_name']]);
        $add($out['days'], $e['date'], $e['date'], $e);
        if (!$e['category_ids']) {
            $add($out['categories'], 0, '(uncategorised)', $e);
        }
        foreach ($e['category_ids'] as $cid) {
            $add($out['categories'], $cid, $catNames[$cid] ?? '?', $e);
        }
    }
    $byMinutes = fn($a, $b) => $b['minutes'] <=> $a['minutes'];
    foreach (['clients', 'projects', 'tasks', 'categories'] as $k) {
        usort($out[$k], $byMinutes);
    }
    ksort($out['days']);
    $out['days'] = array_values($out['days']);
    $out['amount'] = round($out['amount'], 2);
    $out['uninvoiced_amount'] = round($out['uninvoiced_amount'], 2);
    return $out;
}

/**
 * Turns uninvoiced billable entries into invoice lines.
 * $clientId null means every client (for an umbrella company).
 * $group: project | task | day | entry
 */
function build_invoice_lines(?int $clientId, ?string $from, ?string $to, string $group): array
{
    $filters = ['from' => $from, 'to' => $to, 'billable' => 1, 'invoiced' => 'no'];
    if ($clientId) {
        $filters['client_id'] = $clientId;
    }
    $entries = fetch_entries($filters, 'c.name COLLATE NOCASE, p.name COLLATE NOCASE, e.date, e.start_time, e.id');
    $multiClient = $clientId === null && count(array_unique(array_column($entries, 'client_id'))) > 1;
    $lines = [];
    foreach ($entries as $e) {
        $proj = ($multiClient ? $e['client_name'] . ' — ' : '') . $e['project_name'];
        switch ($group) {
            case 'task':
                $key = "{$e['project_id']}|{$e['task_id']}|{$e['eff_rate']}";
                $desc = $proj . ($e['task_name'] ? ': ' . $e['task_name'] : '');
                break;
            case 'day':
                $key = "{$e['date']}|{$e['project_id']}|{$e['eff_rate']}";
                $desc = fmt_date($e['date']) . ' — ' . $proj;
                break;
            case 'entry':
                $key = 'e' . $e['id'];
                $what = $e['description'] ?: ($e['task_name'] ?: '');
                $desc = fmt_date($e['date']) . ' — ' . $proj . ($what !== '' ? ': ' . $what : '');
                break;
            default:
                $key = "{$e['project_id']}|{$e['eff_rate']}";
                $desc = $proj;
        }
        if (!isset($lines[$key])) {
            $lines[$key] = ['description' => $desc, 'minutes' => 0, 'unit' => 'hours', 'unit_price' => $e['eff_rate'],
                'client_id' => $e['client_id'], 'project_id' => $e['project_id'], 'entry_ids' => []];
        }
        $lines[$key]['minutes'] += $e['minutes'];
        $lines[$key]['entry_ids'][] = $e['id'];
    }
    if ($group === 'day') {
        uasort($lines, fn($a, $b) => strcmp($a['description'], $b['description']));
    }
    foreach ($lines as &$l) {
        // The amount is the quantity shown times the rate, so the invoice always adds up.
        $l['quantity'] = round($l['minutes'] / 60, 2);
        $l['amount'] = round($l['quantity'] * $l['unit_price'], 2);
    }
    return array_values($lines);
}
