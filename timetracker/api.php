<?php
// JSON API used by the single-page app. Every call is a POST with a JSON body
// and the session's CSRF token in the X-CSRF-Token header.
declare(strict_types=1);
require __DIR__ . '/lib/app.php';
require __DIR__ . '/lib/mail.php';
require __DIR__ . '/lib/webauthn.php';

start_session();
header('Content-Type: application/json; charset=utf-8');
header('Cache-Control: no-store');
header('X-Content-Type-Options: nosniff');

function respond($data, int $status = 200): void
{
    http_response_code($status);
    echo json_encode($data, JSON_UNESCAPED_UNICODE | JSON_PRESERVE_ZERO_FRACTION);
    exit;
}

if (!is_logged_in()) {
    respond(['error' => 'Not signed in'], 401);
}
if ($_SERVER['REQUEST_METHOD'] !== 'POST') {
    respond(['error' => 'POST required'], 405);
}
if (!hash_equals($_SESSION['csrf'], (string) ($_SERVER['HTTP_X_CSRF_TOKEN'] ?? ''))) {
    respond(['error' => 'Security token mismatch — please reload the page.'], 403);
}
$in = json_decode((string) file_get_contents('php://input'), true);
if (!is_array($in)) {
    $in = [];
}

$actions = [
    'bootstrap' => 'api_bootstrap',
    'entries.list' => 'api_entries_list',
    'entries.save' => 'api_entries_save',
    'entries.delete' => 'api_entries_delete',
    'entries.bulk_add' => 'api_entries_bulk_add',
    'entries.bulk_update' => 'api_entries_bulk_update',
    'entries.duplicate' => 'api_entries_duplicate',
    'timesheet.set' => 'api_timesheet_set',
    'timer.start' => 'api_timer_start',
    'timer.update' => 'api_timer_update',
    'timer.stop' => 'api_timer_stop',
    'timer.discard' => 'api_timer_discard',
    'clients.save' => fn($in) => api_save_record('clients', $in),
    'projects.save' => fn($in) => api_save_record('projects', $in),
    'tasks.save' => fn($in) => api_save_record('tasks', $in),
    'categories.save' => fn($in) => api_save_record('categories', $in),
    'clients.delete' => fn($in) => api_delete_record('clients', $in),
    'projects.delete' => fn($in) => api_delete_record('projects', $in),
    'tasks.delete' => fn($in) => api_delete_record('tasks', $in),
    'categories.delete' => fn($in) => api_delete_record('categories', $in),
    'report.summary' => 'api_report_summary',
    'invoices.list' => 'api_invoices_list',
    'invoices.preview' => 'api_invoices_preview',
    'invoices.create' => 'api_invoices_create',
    'invoices.update' => 'api_invoices_update',
    'invoices.delete' => 'api_invoices_delete',
    'settings.save' => 'api_settings_save',
    'password.change' => 'api_password_change',
    'passkeys.list' => 'api_passkeys_list',
    'passkeys.options' => fn($in) => passkey_registration_options(),
    'passkeys.register' => 'api_passkeys_register',
    'passkeys.delete' => 'api_passkeys_delete',
];

$action = (string) ($_GET['action'] ?? '');
if (!isset($actions[$action])) {
    respond(['error' => 'Unknown action'], 404);
}
// Release the session lock so slow requests don't block each other
// (passkey registration keeps it, as it stores a challenge in the session).
if (!str_starts_with($action, 'passkeys.')) {
    session_write_close();
}
try {
    respond($actions[$action]($in));
} catch (UserError $e) {
    rollback_quietly();
    respond(['error' => $e->getMessage()], 400);
} catch (Throwable $e) {
    rollback_quietly();
    error_log('[timetracker] ' . $e);
    respond(['error' => 'Server error: ' . $e->getMessage()], 500);
}

function rollback_quietly(): void
{
    try {
        if (db()->inTransaction()) {
            db()->rollBack();
        }
    } catch (Throwable $e) {
    }
}

// ---------------------------------------------------------------------------
// Input helpers

function int_or_null($v): ?int
{
    return ($v === null || $v === '' || $v === false) ? null : (int) $v;
}

function float_or_null($v): ?float
{
    if ($v === null || $v === '') {
        return null;
    }
    if (!is_numeric($v)) {
        throw new UserError('Expected a number, got "' . $v . '".');
    }
    return (float) $v;
}

function str_or_null($v): ?string
{
    $v = trim((string) $v);
    return $v === '' ? null : $v;
}

function int_list($v): array
{
    return array_values(array_unique(array_filter(array_map('intval', (array) ($v ?? [])))));
}

function require_date($v, string $what = 'date'): string
{
    if (!valid_date($v)) {
        throw new UserError("Please give a valid $what.");
    }
    return $v;
}

function project_exists(int $id): bool
{
    $s = db()->prepare('SELECT 1 FROM projects WHERE id = ?');
    $s->execute([$id]);
    return (bool) $s->fetchColumn();
}

function check_task(?int $taskId, int $projectId): ?int
{
    if (!$taskId) {
        return null;
    }
    $s = db()->prepare('SELECT project_id FROM tasks WHERE id = ?');
    $s->execute([$taskId]);
    $row = $s->fetch();
    if (!$row) {
        throw new UserError('That task no longer exists.');
    }
    if ($row['project_id'] !== null && (int) $row['project_id'] !== $projectId) {
        throw new UserError('That task belongs to a different project.');
    }
    return $taskId;
}

/**
 * Works out start/end/minutes from whatever the user typed. Start + end wins
 * (an end before the start means the work ran past midnight); otherwise the
 * duration is used and, with a start, the end is calculated.
 */
function resolve_times(array $in): array
{
    $start = str_or_null($in['start_time'] ?? null);
    $end = str_or_null($in['end_time'] ?? null);
    if ($start !== null && !valid_time($start)) {
        throw new UserError('Start time must look like 09:30.');
    }
    if ($end !== null && !valid_time($end)) {
        throw new UserError('End time must look like 17:00.');
    }
    if ($start !== null && $end !== null) {
        $minutes = time_to_min($end) - time_to_min($start);
        if ($minutes <= 0) {
            $minutes += 1440;
        }
    } else {
        $minutes = (int) round((float) ($in['minutes'] ?? 0));
        if ($start !== null) {
            $end = min_to_time(time_to_min($start) + $minutes);
        } elseif ($end !== null) {
            $start = min_to_time(time_to_min($end) - $minutes);
        }
    }
    if ($minutes <= 0) {
        throw new UserError('Please enter how long you worked (e.g. 1:30, 1.5h or 90m).');
    }
    if ($minutes > 1440) {
        throw new UserError('An entry cannot be longer than 24 hours.');
    }
    return [$start, $end, $minutes];
}

function set_entry_categories(int $entryId, array $categoryIds): void
{
    db()->prepare('DELETE FROM entry_categories WHERE entry_id = ?')->execute([$entryId]);
    $ins = db()->prepare('INSERT OR IGNORE INTO entry_categories (entry_id, category_id)
        SELECT ?, id FROM categories WHERE id = ?');
    foreach ($categoryIds as $cid) {
        $ins->execute([$entryId, $cid]);
    }
}

function insert_entry(array $e): int
{
    $s = db()->prepare('INSERT INTO entries (date, start_time, end_time, minutes, description, project_id, task_id, billable, rate)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)');
    $s->execute([$e['date'], $e['start_time'], $e['end_time'], $e['minutes'], $e['description'],
        $e['project_id'], $e['task_id'], $e['billable'] ? 1 : 0, $e['rate']]);
    $id = (int) db()->lastInsertId();
    set_entry_categories($id, $e['category_ids'] ?? []);
    return $id;
}

function locked_entry_ids(array $ids): array
{
    if (!$ids) {
        return [];
    }
    $in = implode(',', array_map('intval', $ids));
    return array_map('intval', db()->query("SELECT id FROM entries WHERE id IN ($in) AND invoice_id IS NOT NULL")->fetchAll(PDO::FETCH_COLUMN));
}

// ---------------------------------------------------------------------------
// Bootstrap

function api_bootstrap(array $in): array
{
    $settings = array_diff_key(settings(), array_flip(PRIVATE_SETTINGS));
    return [
        'version' => APP_VERSION,
        'server_now' => (int) round(microtime(true) * 1000),
        'today' => date('Y-m-d'),
        'settings' => $settings,
        'clients' => db()->query('SELECT * FROM clients ORDER BY name COLLATE NOCASE')->fetchAll(),
        'projects' => db()->query('SELECT p.*, c.name AS client_name FROM projects p JOIN clients c ON c.id = p.client_id
            ORDER BY c.name COLLATE NOCASE, p.name COLLATE NOCASE')->fetchAll(),
        'tasks' => db()->query('SELECT * FROM tasks ORDER BY name COLLATE NOCASE')->fetchAll(),
        'categories' => db()->query('SELECT * FROM categories ORDER BY name COLLATE NOCASE')->fetchAll(),
        'timer' => timer_get(),
    ];
}

// ---------------------------------------------------------------------------
// Entries

function api_entries_list(array $in): array
{
    return ['entries' => fetch_entries($in, 'e.date DESC, e.start_time IS NULL, e.start_time DESC, e.id DESC')];
}

function api_entries_save(array $in): array
{
    $id = int_or_null($in['id'] ?? null);
    $date = require_date($in['date'] ?? null);
    [$start, $end, $minutes] = resolve_times($in);
    $projectIds = int_list($in['project_ids'] ?? [$in['project_id'] ?? null]);
    if (!$projectIds) {
        throw new UserError('Please choose a project.');
    }
    foreach ($projectIds as $pid) {
        if (!project_exists($pid)) {
            throw new UserError('That project no longer exists.');
        }
    }
    $base = [
        'date' => $date,
        'description' => str_or_null($in['description'] ?? null),
        'billable' => !empty($in['billable']),
        'rate' => float_or_null($in['rate'] ?? null),
        'category_ids' => int_list($in['category_ids'] ?? []),
    ];

    db()->beginTransaction();
    if ($id) {
        if (count($projectIds) !== 1) {
            throw new UserError('An existing entry can only belong to one project.');
        }
        if (locked_entry_ids([$id])) {
            throw new UserError('This entry is on an invoice, so it cannot be changed. Delete the invoice first if you need to edit it.');
        }
        $pid = $projectIds[0];
        $s = db()->prepare("UPDATE entries SET date = ?, start_time = ?, end_time = ?, minutes = ?, description = ?, project_id = ?,
            task_id = ?, billable = ?, rate = ?, updated_at = CURRENT_TIMESTAMP WHERE id = ?");
        $s->execute([$date, $start, $end, $minutes, $base['description'], $pid, check_task(int_or_null($in['task_id'] ?? null), $pid),
            $base['billable'] ? 1 : 0, $base['rate'], $id]);
        set_entry_categories($id, $base['category_ids']);
        $ids = [$id];
    } else {
        // Several projects: either split the time between them or log it against each.
        $n = count($projectIds);
        $split = ($in['split_mode'] ?? 'split') === 'split' && $n > 1;
        $ids = [];
        $cursor = $start !== null ? time_to_min($start) : null;
        foreach ($projectIds as $i => $pid) {
            $m = $split ? intdiv($minutes, $n) + ($i < $minutes % $n ? 1 : 0) : $minutes;
            if ($m <= 0) {
                continue;
            }
            $e = $base + ['project_id' => $pid, 'minutes' => $m, 'start_time' => $start, 'end_time' => $end,
                'task_id' => $n === 1 ? check_task(int_or_null($in['task_id'] ?? null), $pid) : task_if_allowed(int_or_null($in['task_id'] ?? null), $pid)];
            if ($split && $cursor !== null) {
                $e['start_time'] = min_to_time($cursor);
                $e['end_time'] = min_to_time($cursor + $m);
                $cursor += $m;
            }
            $ids[] = insert_entry($e);
        }
    }
    db()->commit();
    return ['ids' => $ids];
}

/** For multi-project entries, keep the task only on projects it applies to. */
function task_if_allowed(?int $taskId, int $projectId): ?int
{
    try {
        return check_task($taskId, $projectId);
    } catch (UserError $e) {
        return null;
    }
}

function api_entries_delete(array $in): array
{
    $ids = int_list($in['ids'] ?? []);
    $locked = locked_entry_ids($ids);
    $ids = array_diff($ids, $locked);
    if ($ids) {
        db()->exec('DELETE FROM entries WHERE id IN (' . implode(',', $ids) . ')');
    }
    return ['deleted' => count($ids), 'skipped' => count($locked)];
}

/** Same entry on every chosen weekday in a date range — e.g. "7.5h a day on Project X all of last week". */
function api_entries_bulk_add(array $in): array
{
    $from = require_date($in['from'] ?? null, 'start date');
    $to = require_date($in['to'] ?? null, 'end date');
    if ($to < $from) {
        throw new UserError('The end date is before the start date.');
    }
    $weekdays = int_list($in['weekdays'] ?? [1, 2, 3, 4, 5]);
    if (!$weekdays) {
        throw new UserError('Choose at least one day of the week.');
    }
    $pid = (int) ($in['project_id'] ?? 0);
    if (!project_exists($pid)) {
        throw new UserError('Please choose a project.');
    }
    [$start, $end, $minutes] = resolve_times($in);
    $skip = !empty($in['skip_existing']);
    $e = [
        'start_time' => $start, 'end_time' => $end, 'minutes' => $minutes,
        'description' => str_or_null($in['description'] ?? null),
        'project_id' => $pid,
        'task_id' => check_task(int_or_null($in['task_id'] ?? null), $pid),
        'billable' => !empty($in['billable']),
        'rate' => float_or_null($in['rate'] ?? null),
        'category_ids' => int_list($in['category_ids'] ?? []),
    ];
    $has = db()->prepare('SELECT 1 FROM entries WHERE date = ? LIMIT 1');
    $d = new DateTime($from);
    $last = new DateTime($to);
    $count = 0;
    $skipped = 0;
    db()->beginTransaction();
    for (; $d <= $last; $d->modify('+1 day')) {
        if (!in_array((int) $d->format('N'), $weekdays, true)) {
            continue;
        }
        $date = $d->format('Y-m-d');
        if ($skip) {
            $has->execute([$date]);
            if ($has->fetchColumn()) {
                $skipped++;
                continue;
            }
        }
        insert_entry($e + ['date' => $date]);
        if (++$count > 1000) {
            throw new UserError('That would create more than 1000 entries — please choose a shorter range.');
        }
    }
    db()->commit();
    return ['created' => $count, 'skipped' => $skipped];
}

function api_entries_bulk_update(array $in): array
{
    $ids = int_list($in['ids'] ?? []);
    $locked = locked_entry_ids($ids);
    $ids = array_values(array_diff($ids, $locked));
    $set = (array) ($in['set'] ?? []);
    if (!$ids) {
        return ['updated' => 0, 'skipped' => count($locked)];
    }
    db()->beginTransaction();
    $list = implode(',', $ids);
    if (!empty($set['project_id'])) {
        $pid = (int) $set['project_id'];
        if (!project_exists($pid)) {
            throw new UserError('That project no longer exists.');
        }
        // Tasks that belong to the old project no longer apply.
        db()->prepare("UPDATE entries SET project_id = ?, task_id = CASE WHEN task_id IN (SELECT id FROM tasks WHERE project_id IS NULL OR project_id = ?) THEN task_id ELSE NULL END WHERE id IN ($list)")
            ->execute([$pid, $pid]);
    }
    if (array_key_exists('task_id', $set) && $set['task_id'] !== 'keep') {
        $tid = int_or_null($set['task_id']);
        foreach (db()->query("SELECT id, project_id FROM entries WHERE id IN ($list)") as $row) {
            db()->prepare('UPDATE entries SET task_id = ? WHERE id = ?')->execute([$tid ? task_if_allowed($tid, (int) $row['project_id']) : null, $row['id']]);
        }
    }
    if (isset($set['billable']) && $set['billable'] !== '' && $set['billable'] !== 'keep') {
        db()->prepare("UPDATE entries SET billable = ? WHERE id IN ($list)")->execute([(int) (bool) $set['billable']]);
    }
    if (!empty($set['date'])) {
        db()->prepare("UPDATE entries SET date = ? WHERE id IN ($list)")->execute([require_date($set['date'])]);
    }
    if (array_key_exists('description', $set) && $set['description'] !== null) {
        db()->prepare("UPDATE entries SET description = ? WHERE id IN ($list)")->execute([str_or_null($set['description'])]);
    }
    foreach (int_list($set['add_category_ids'] ?? []) as $cid) {
        db()->prepare("INSERT OR IGNORE INTO entry_categories (entry_id, category_id) SELECT id, ? FROM entries WHERE id IN ($list)")->execute([$cid]);
    }
    foreach (int_list($set['remove_category_ids'] ?? []) as $cid) {
        db()->prepare("DELETE FROM entry_categories WHERE category_id = ? AND entry_id IN ($list)")->execute([$cid]);
    }
    db()->exec("UPDATE entries SET updated_at = CURRENT_TIMESTAMP WHERE id IN ($list)");
    db()->commit();
    return ['updated' => count($ids), 'skipped' => count($locked)];
}

function api_entries_duplicate(array $in): array
{
    $ids = int_list($in['ids'] ?? []);
    $date = isset($in['date']) && $in['date'] !== '' ? require_date($in['date']) : null;
    $new = [];
    db()->beginTransaction();
    foreach ($ids as $id) {
        $s = db()->prepare('SELECT * FROM entries WHERE id = ?');
        $s->execute([$id]);
        $e = $s->fetch();
        if (!$e) {
            continue;
        }
        $c = db()->prepare('SELECT category_id FROM entry_categories WHERE entry_id = ?');
        $c->execute([$id]);
        $e['category_ids'] = $c->fetchAll(PDO::FETCH_COLUMN);
        $e['date'] = $date ?? $e['date'];
        $new[] = insert_entry($e);
    }
    db()->commit();
    return ['ids' => $new];
}

/**
 * Weekly timesheet grid: set the total time for one project/task on one day.
 * More time adds to (or creates) an entry; less time trims un-invoiced
 * entries, newest first. Invoiced time can't be reduced.
 */
function api_timesheet_set(array $in): array
{
    $date = require_date($in['date'] ?? null);
    $pid = (int) ($in['project_id'] ?? 0);
    if (!project_exists($pid)) {
        throw new UserError('That project no longer exists.');
    }
    $tid = check_task(int_or_null($in['task_id'] ?? null), $pid);
    $target = (int) round((float) ($in['minutes'] ?? 0));
    if ($target < 0 || $target > 1440) {
        throw new UserError('A day has at most 24 hours.');
    }

    $s = db()->prepare('SELECT id, minutes, start_time, invoice_id FROM entries WHERE date = ? AND project_id = ? AND task_id IS ? ORDER BY id DESC');
    $s->execute([$date, $pid, $tid]);
    $rows = $s->fetchAll();
    $current = array_sum(array_column($rows, 'minutes'));
    $locked = array_sum(array_map(fn($r) => $r['invoice_id'] !== null ? (int) $r['minutes'] : 0, $rows));
    $open = array_values(array_filter($rows, fn($r) => $r['invoice_id'] === null));
    $delta = $target - $current;

    db()->beginTransaction();
    if ($delta > 0) {
        if (count($open) === 1) {
            $r = $open[0];
            $m = (int) $r['minutes'] + $delta;
            $end = $r['start_time'] !== null ? min_to_time(time_to_min($r['start_time']) + $m) : null;
            db()->prepare('UPDATE entries SET minutes = ?, end_time = ?, updated_at = CURRENT_TIMESTAMP WHERE id = ?')->execute([$m, $end, $r['id']]);
        } else {
            $billable = (int) db()->query('SELECT billable FROM projects WHERE id = ' . $pid)->fetchColumn();
            insert_entry(['date' => $date, 'start_time' => null, 'end_time' => null, 'minutes' => $delta,
                'description' => str_or_null($in['description'] ?? null), 'project_id' => $pid, 'task_id' => $tid,
                'billable' => (bool) $billable, 'rate' => null, 'category_ids' => int_list($in['category_ids'] ?? [])]);
        }
    } elseif ($delta < 0) {
        if ($target < $locked) {
            throw new UserError(fmt_hm($locked) . ' of this time is already invoiced and cannot be reduced.');
        }
        $remove = -$delta;
        foreach ($open as $r) {
            if ($remove <= 0) {
                break;
            }
            $m = (int) $r['minutes'];
            if ($m <= $remove) {
                db()->prepare('DELETE FROM entries WHERE id = ?')->execute([$r['id']]);
                $remove -= $m;
            } else {
                $m -= $remove;
                $end = $r['start_time'] !== null ? min_to_time(time_to_min($r['start_time']) + $m) : null;
                db()->prepare('UPDATE entries SET minutes = ?, end_time = ?, updated_at = CURRENT_TIMESTAMP WHERE id = ?')->execute([$m, $end, $r['id']]);
                $remove = 0;
            }
        }
    }
    db()->commit();
    return ['minutes' => $target];
}

// ---------------------------------------------------------------------------
// Timer (kept on the server so it follows you between machines)

function timer_get(): ?array
{
    $t = setting('timer');
    return $t ? json_decode($t, true) : null;
}

function timer_fields(array $in): array
{
    return [
        'description' => (string) ($in['description'] ?? ''),
        'project_id' => int_or_null($in['project_id'] ?? null),
        'task_id' => int_or_null($in['task_id'] ?? null),
        'category_ids' => int_list($in['category_ids'] ?? []),
        'billable' => !empty($in['billable']),
    ];
}

function api_timer_start(array $in): array
{
    if (timer_get()) {
        throw new UserError('A timer is already running.');
    }
    $timer = timer_fields($in) + ['started_at' => (int) round(microtime(true) * 1000)];
    set_setting('timer', json_encode($timer));
    return ['timer' => $timer];
}

function api_timer_update(array $in): array
{
    $timer = timer_get();
    if (!$timer) {
        return ['timer' => null];
    }
    $timer = timer_fields($in) + ['started_at' => $timer['started_at']];
    set_setting('timer', json_encode($timer));
    return ['timer' => $timer];
}

function api_timer_stop(array $in): array
{
    $timer = timer_get();
    if (!$timer) {
        throw new UserError('No timer is running.');
    }
    $fields = timer_fields($in + $timer);
    if (!$fields['project_id'] || !project_exists($fields['project_id'])) {
        throw new UserError('Choose a project before stopping the timer.');
    }
    $startTs = intdiv((int) $timer['started_at'], 1000);
    $startedAt = (new DateTime())->setTimestamp($startTs);
    $seconds = time() - $startTs;
    $minutes = max(1, (int) round($seconds / 60));
    if ($minutes > 1440) {
        throw new UserError('The timer has been running for more than 24 hours. Discard it and add the time manually.');
    }
    $start = $startedAt->format('H:i');
    db()->beginTransaction();
    $id = insert_entry([
        'date' => $startedAt->format('Y-m-d'),
        'start_time' => $start,
        'end_time' => min_to_time(time_to_min($start) + $minutes),
        'minutes' => $minutes,
        'description' => str_or_null($fields['description']),
        'project_id' => $fields['project_id'],
        'task_id' => task_if_allowed($fields['task_id'], $fields['project_id']),
        'billable' => $fields['billable'],
        'rate' => null,
        'category_ids' => $fields['category_ids'],
    ]);
    set_setting('timer', null);
    db()->commit();
    return ['id' => $id];
}

function api_timer_discard(array $in): array
{
    set_setting('timer', null);
    return ['timer' => null];
}

// ---------------------------------------------------------------------------
// Clients, projects, tasks and categories

function record_fields(string $table): array
{
    // field => type (s = text, f = number, i = integer, b = yes/no, r = required text)
    return [
        'clients' => ['name' => 'r', 'contact' => 's', 'email' => 's', 'address' => 's', 'billing_unit' => 's', 'rate' => 'f',
            'day_hours' => 'f', 'tax_rate' => 'f', 'payment_terms' => 'i', 'invoice_notes' => 's', 'archived' => 'b'],
        'projects' => ['client_id' => 'i', 'name' => 'r', 'code' => 's', 'rate' => 'f', 'color' => 's', 'billable' => 'b', 'archived' => 'b'],
        'tasks' => ['project_id' => 'i', 'name' => 'r', 'rate' => 'f', 'archived' => 'b'],
        'categories' => ['name' => 'r', 'color' => 's', 'archived' => 'b'],
    ][$table];
}

function api_save_record(string $table, array $in): array
{
    $fields = record_fields($table);
    $values = [];
    foreach ($fields as $f => $type) {
        $v = $in[$f] ?? null;
        switch ($type) {
            case 'r':
                $v = str_or_null($v);
                if ($v === null) {
                    throw new UserError('Please enter a ' . str_replace('_', ' ', $f) . '.');
                }
                break;
            case 's': $v = str_or_null($v); break;
            case 'f': $v = float_or_null($v); break;
            case 'i': $v = int_or_null($v); break;
            case 'b': $v = !empty($v) ? 1 : 0; break;
        }
        $values[$f] = $v;
    }
    if ($table === 'clients') {
        if (!in_array($values['billing_unit'], ['hour', 'half_day', 'day'], true)) {
            $values['billing_unit'] = 'hour';
        }
        if ($values['day_hours'] !== null && ($values['day_hours'] <= 0 || $values['day_hours'] > 24)) {
            throw new UserError('A working day must be between 0 and 24 hours.');
        }
    }
    if ($table === 'projects' && (!$values['client_id'] || !db()->query('SELECT 1 FROM clients WHERE id = ' . (int) $values['client_id'])->fetchColumn())) {
        throw new UserError('Please choose a client for the project.');
    }
    if ($table === 'tasks' && $values['project_id'] && !project_exists($values['project_id'])) {
        throw new UserError('That project no longer exists.');
    }
    if ($table === 'categories') {
        $s = db()->prepare('SELECT id FROM categories WHERE name = ? COLLATE NOCASE AND id IS NOT ?');
        $s->execute([$values['name'], int_or_null($in['id'] ?? null)]);
        if ($s->fetchColumn()) {
            throw new UserError('There is already a category with that name.');
        }
    }
    $id = int_or_null($in['id'] ?? null);
    if ($id) {
        $set = implode(', ', array_map(fn($f) => "$f = :$f", array_keys($values)));
        $s = db()->prepare("UPDATE $table SET $set WHERE id = :id");
        $s->execute(array_combine(array_map(fn($f) => ":$f", array_keys($values)), array_values($values)) + [':id' => $id]);
    } else {
        $cols = implode(', ', array_keys($values));
        $ph = implode(', ', array_map(fn($f) => ":$f", array_keys($values)));
        $s = db()->prepare("INSERT INTO $table ($cols) VALUES ($ph)");
        $s->execute(array_combine(array_map(fn($f) => ":$f", array_keys($values)), array_values($values)));
        $id = (int) db()->lastInsertId();
    }
    return ['id' => $id];
}

function api_delete_record(string $table, array $in): array
{
    $id = (int) ($in['id'] ?? 0);
    $usage = [
        'clients' => ['SELECT COUNT(*) FROM projects WHERE client_id = ?', 'This client has projects. Archive it instead, or delete its projects first.'],
        'projects' => ['SELECT COUNT(*) FROM entries WHERE project_id = ?', 'This project has time entries. Archive it instead so your history is kept.'],
        'tasks' => ['SELECT COUNT(*) FROM entries WHERE task_id = ?', 'This task has time entries. Archive it instead so your history is kept.'],
        'categories' => ['SELECT COUNT(*) FROM entry_categories WHERE category_id = ?', 'This category is used by time entries. Archive it instead so your history is kept.'],
    ][$table];
    $s = db()->prepare($usage[0]);
    $s->execute([$id]);
    if ($s->fetchColumn() > 0) {
        throw new UserError($usage[1]);
    }
    if ($table === 'clients') {
        $s = db()->prepare('SELECT COUNT(*) FROM invoices WHERE client_id = ?');
        $s->execute([$id]);
        if ($s->fetchColumn() > 0) {
            throw new UserError('This client has invoices. Archive it instead.');
        }
    }
    db()->prepare("DELETE FROM $table WHERE id = ?")->execute([$id]);
    return ['deleted' => 1];
}

// ---------------------------------------------------------------------------
// Reports

function api_report_summary(array $in): array
{
    $entries = fetch_entries($in);
    return ['summary' => summarise($entries)];
}

// ---------------------------------------------------------------------------
// Invoices

function api_invoices_list(array $in): array
{
    $rows = db()->query("SELECT i.*, c.name AS client_name,
            (SELECT COUNT(*) FROM entries e WHERE e.invoice_id = i.id) AS entry_count
        FROM invoices i LEFT JOIN clients c ON c.id = i.client_id
        ORDER BY i.issue_date DESC, i.id DESC")->fetchAll();
    return ['invoices' => $rows];
}

function invoice_request(array $in): array
{
    $clientId = int_or_null($in['client_id'] ?? null);
    $from = str_or_null($in['from'] ?? null);
    $to = str_or_null($in['to'] ?? null);
    if ($from !== null) {
        require_date($from, 'start date');
    }
    if ($to !== null) {
        require_date($to, 'end date');
    }
    $group = in_array($in['group'] ?? '', ['project', 'task', 'day', 'entry'], true) ? $in['group'] : 'project';
    return [$clientId, $from, $to, $group];
}

function default_tax_rate(?int $clientId): float
{
    if ($clientId) {
        $s = db()->prepare('SELECT tax_rate FROM clients WHERE id = ?');
        $s->execute([$clientId]);
        $r = $s->fetchColumn();
        if ($r !== null && $r !== false) {
            return (float) $r;
        }
    }
    return (float) setting('tax_rate');
}

function totals(array $lines, float $taxRate): array
{
    $subtotal = round(array_sum(array_column($lines, 'amount')), 2);
    $tax = round($subtotal * $taxRate / 100, 2);
    return ['subtotal' => $subtotal, 'tax' => $tax, 'total' => round($subtotal + $tax, 2)];
}

function extra_lines(array $in): array
{
    $out = [];
    foreach ((array) ($in['extra_lines'] ?? []) as $l) {
        $desc = str_or_null($l['description'] ?? null);
        if ($desc === null) {
            continue;
        }
        $qty = float_or_null($l['quantity'] ?? null) ?? 1.0;
        $price = float_or_null($l['unit_price'] ?? null) ?? 0.0;
        $out[] = ['description' => $desc, 'quantity' => $qty, 'unit' => str_or_null($l['unit'] ?? null), 'unit_price' => $price,
            'amount' => round($qty * $price, 2), 'client_id' => null, 'project_id' => null, 'entry_ids' => []];
    }
    return $out;
}

function api_invoices_preview(array $in): array
{
    [$clientId, $from, $to, $group] = invoice_request($in);
    $lines = build_invoice_lines($clientId, $from, $to, $group);
    $taxRate = isset($in['tax_rate']) && $in['tax_rate'] !== '' ? (float) $in['tax_rate'] : default_tax_rate($clientId);
    $all = array_merge($lines, extra_lines($in));
    $entryCount = array_sum(array_map(fn($l) => count($l['entry_ids']), $lines));
    foreach ($lines as &$l) {
        unset($l['entry_ids']);
    }
    return ['lines' => $lines, 'entry_count' => $entryCount, 'tax_rate' => $taxRate,
        'next_number' => format_invoice_number((int) setting('invoice_next'))] + totals($all, $taxRate);
}

function api_invoices_create(array $in): array
{
    [$clientId, $from, $to, $group] = invoice_request($in);
    $issue = require_date($in['issue_date'] ?? date('Y-m-d'), 'invoice date');
    $due = str_or_null($in['due_date'] ?? null);
    if ($due !== null) {
        require_date($due, 'due date');
    }
    $taxRate = isset($in['tax_rate']) && $in['tax_rate'] !== '' ? (float) $in['tax_rate'] : default_tax_rate($clientId);

    db()->beginTransaction();
    $lines = array_merge(build_invoice_lines($clientId, $from, $to, $group), extra_lines($in));
    if (!$lines) {
        throw new UserError('There is no un-invoiced billable time for that client and period.');
    }
    $next = (int) setting('invoice_next');
    $number = str_or_null($in['number'] ?? null) ?? format_invoice_number($next);
    $s = db()->prepare('SELECT 1 FROM invoices WHERE number = ?');
    $s->execute([$number]);
    if ($s->fetchColumn()) {
        throw new UserError("Invoice number $number is already used.");
    }
    $t = totals($lines, $taxRate);
    $s = db()->prepare('INSERT INTO invoices (number, client_id, bill_to, issue_date, due_date, period_from, period_to, grouping, status, tax_rate, notes, subtotal, tax, total)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)');
    $s->execute([$number, $clientId, str_or_null($in['bill_to'] ?? null), $issue, $due, $from, $to, $group, 'draft', $taxRate,
        str_or_null($in['notes'] ?? null), $t['subtotal'], $t['tax'], $t['total']]);
    $invoiceId = (int) db()->lastInsertId();
    $li = db()->prepare('INSERT INTO invoice_lines (invoice_id, sort, description, quantity, unit, unit_price, amount, client_id, project_id)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)');
    $mark = db()->prepare('UPDATE entries SET invoice_id = ? WHERE id = ?');
    foreach ($lines as $i => $l) {
        $li->execute([$invoiceId, $i, $l['description'], $l['quantity'], $l['unit'], $l['unit_price'], $l['amount'], $l['client_id'], $l['project_id']]);
        foreach ($l['entry_ids'] as $eid) {
            $mark->execute([$invoiceId, $eid]);
        }
    }
    if (str_or_null($in['number'] ?? null) === null) {
        set_setting('invoice_next', (string) ($next + 1));
    }
    db()->commit();
    return ['id' => $invoiceId, 'number' => $number];
}

function api_invoices_update(array $in): array
{
    $id = (int) ($in['id'] ?? 0);
    $sets = [];
    $params = [];
    if (isset($in['status'])) {
        if (!in_array($in['status'], ['draft', 'sent', 'paid'], true)) {
            throw new UserError('Unknown status.');
        }
        $sets[] = 'status = ?';
        $params[] = $in['status'];
        $sets[] = 'paid_date = ?';
        $params[] = $in['status'] === 'paid' ? (valid_date($in['paid_date'] ?? null) ? $in['paid_date'] : date('Y-m-d')) : null;
    }
    foreach (['bill_to', 'notes'] as $f) {
        if (array_key_exists($f, $in)) {
            $sets[] = "$f = ?";
            $params[] = str_or_null($in[$f]);
        }
    }
    foreach (['issue_date', 'due_date'] as $f) {
        if (array_key_exists($f, $in)) {
            $v = str_or_null($in[$f]);
            if ($v !== null) {
                require_date($v);
            }
            $sets[] = "$f = ?";
            $params[] = $v;
        }
    }
    if (!empty($in['number'])) {
        $s = db()->prepare('SELECT 1 FROM invoices WHERE number = ? AND id != ?');
        $s->execute([$in['number'], $id]);
        if ($s->fetchColumn()) {
            throw new UserError('That invoice number is already used.');
        }
        $sets[] = 'number = ?';
        $params[] = trim((string) $in['number']);
    }
    if ($sets) {
        $params[] = $id;
        db()->prepare('UPDATE invoices SET ' . implode(', ', $sets) . ' WHERE id = ?')->execute($params);
    }
    return ['id' => $id];
}

function api_invoices_delete(array $in): array
{
    $id = (int) ($in['id'] ?? 0);
    db()->beginTransaction();
    // Releases the time entries so they can be invoiced again.
    db()->prepare('UPDATE entries SET invoice_id = NULL WHERE invoice_id = ?')->execute([$id]);
    db()->prepare('DELETE FROM invoices WHERE id = ?')->execute([$id]);
    db()->commit();
    return ['deleted' => 1];
}

// ---------------------------------------------------------------------------
// Settings

function api_settings_save(array $in): array
{
    $defaults = setting_defaults();
    foreach ($in as $k => $v) {
        if (!array_key_exists($k, $defaults)) {
            continue;
        }
        $v = trim((string) $v);
        if ($k === 'timezone' && !in_array($v, timezone_identifiers_list(), true)) {
            throw new UserError('Unknown time zone: ' . $v);
        }
        if (in_array($k, ['default_rate', 'default_day_rate', 'day_hours', 'tax_rate', 'invoice_next', 'invoice_digits', 'payment_terms', 'week_start'], true) && $v !== '' && !is_numeric($v)) {
            throw new UserError(str_replace('_', ' ', ucfirst($k)) . ' must be a number.');
        }
        set_setting($k, $v);
    }
    return ['settings' => array_diff_key(settings(), array_flip(PRIVATE_SETTINGS))];
}

function api_password_change(array $in): array
{
    if (!password_verify((string) ($in['current'] ?? ''), (string) setting('password_hash'))) {
        throw new UserError('Your current password is not correct.');
    }
    $new = (string) ($in['new'] ?? '');
    if (strlen($new) < 10) {
        throw new UserError('Choose a new password of at least 10 characters.');
    }
    set_setting('password_hash', password_hash($new, PASSWORD_DEFAULT));
    return ['ok' => true];
}

// ---------------------------------------------------------------------------
// Passkeys

function api_passkeys_list(array $in): array
{
    return ['passkeys' => db()->query('SELECT id, name, created_at, last_used_at FROM passkeys ORDER BY id')->fetchAll(),
        'supported_here' => rp_id() !== '' && !filter_var(rp_id(), FILTER_VALIDATE_IP)];
}

function api_passkeys_register(array $in): array
{
    $r = passkey_register($in);
    try {
        send_owner_mail('A passkey was added to ' . APP_NAME,
            "A new passkey (\"{$r['name']}\") was added to your " . APP_NAME . ' account at ' . date('H:i, j M Y')
            . ' from ' . ($_SERVER['REMOTE_ADDR'] ?? 'an unknown address') . ".\n\nIf this wasn't you, sign in and remove it under Settings → Sign-in & security, then change your password.\n");
    } catch (Throwable $e) {
        error_log('[timetracker] passkey notification failed: ' . $e->getMessage());
    }
    return $r;
}

function api_passkeys_delete(array $in): array
{
    db()->prepare('DELETE FROM passkeys WHERE id = ?')->execute([(int) ($in['id'] ?? 0)]);
    return ['deleted' => 1];
}
