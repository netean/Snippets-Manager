<?php
// Printable timesheets and reports, plus CSV export.
//   type=timesheet  weekly/monthly timesheet to send to a client (optional sign-off box)
//   type=summary    detailed report with totals by client, project, task, category and day
//   format=csv      every matching entry as a spreadsheet
declare(strict_types=1);
require __DIR__ . '/lib/app.php';

start_session();
send_security_headers();
require_login();

$f = array_intersect_key($_GET, array_flip(['from', 'to', 'client_id', 'project_id', 'task_id', 'category_id', 'billable', 'invoiced', 'q']));
$entries = fetch_entries($f);
$set = settings();

if (($_GET['format'] ?? '') === 'csv') {
    $catNames = category_names();
    $name = 'time-' . ($f['from'] ?? 'all') . '-to-' . ($f['to'] ?? 'now') . '.csv';
    header('Content-Type: text/csv; charset=utf-8');
    header('Content-Disposition: attachment; filename="' . preg_replace('/[^\w.-]/', '', $name) . '"');
    $out = fopen('php://output', 'w');
    fwrite($out, "\xEF\xBB\xBF"); // so Excel reads UTF-8 correctly
    fputcsv($out, ['Date', 'Start', 'End', 'Hours', 'Duration', 'Client', 'Project', 'Task', 'Categories', 'Description', 'Billable', 'Rate', 'Amount', 'Invoice']);
    foreach ($entries as $e) {
        fputcsv($out, [$e['date'], $e['start_time'], $e['end_time'], fmt_hours($e['minutes']), fmt_hm($e['minutes']), $e['client_name'],
            $e['project_name'], $e['task_name'], implode('; ', array_map(fn($c) => $catNames[$c] ?? '', $e['category_ids'])),
            $e['description'], $e['billable'] ? 'Yes' : 'No', $e['eff_rate'], $e['billable'] ? number_format($e['amount'], 2, '.', '') : '',
            $e['invoice_number']]);
    }
    exit;
}

$type = ($_GET['type'] ?? 'timesheet') === 'summary' ? 'summary' : 'timesheet';
$showAmounts = !empty($_GET['amounts']);
$signoff = !empty($_GET['signoff']);
$sum = summarise($entries);

$clientName = '';
$clientAddress = '';
if (!empty($f['client_id'])) {
    $s = db()->prepare('SELECT name, contact, address FROM clients WHERE id = ?');
    $s->execute([(int) $f['client_id']]);
    if ($c = $s->fetch()) {
        $clientName = $c['name'];
        $clientAddress = trim(implode("\n", array_filter([$c['contact'], $c['address']])));
    }
} elseif (count($sum['clients']) === 1) {
    $clientName = $sum['clients'][0]['label'];
}
$filterNotes = [];
if (!empty($f['project_id'])) {
    $filterNotes[] = 'Project: ' . ($sum['projects'][0]['label'] ?? '');
}
if (!empty($f['category_id'])) {
    $filterNotes[] = 'Category: ' . (category_names()[(int) $f['category_id']] ?? '');
}
if (isset($f['billable']) && $f['billable'] !== '') {
    $filterNotes[] = $f['billable'] ? 'Billable time only' : 'Non-billable time only';
}
$period = describe_period($f['from'] ?? null, $f['to'] ?? null);
$title = ($type === 'timesheet' ? 'Timesheet' : 'Time report') . ($clientName ? ' — ' . $clientName : '') . ' — ' . $period;
$toggle = function (string $key) {
    $q = $_GET;
    if (empty($q[$key])) {
        $q[$key] = 1;
    } else {
        unset($q[$key]);
    }
    return '?' . h(http_build_query($q));
};
$pct = fn($m) => $sum['minutes'] ? round($m / $sum['minutes'] * 100) : 0;

function breakdown(string $heading, array $rows, bool $showAmounts, ?string $extraKey = null, string $extraLabel = ''): void
{
    if (!$rows) {
        return;
    }
    $total = array_sum(array_column($rows, 'minutes'));
    ?>
    <h3><?= h($heading) ?></h3>
    <table class="lines">
        <thead><tr>
            <th><?= h(ucfirst($heading === 'By task' ? 'task' : rtrim(substr($heading, 3), 's'))) ?></th>
            <?php if ($extraKey): ?><th><?= h($extraLabel) ?></th><?php endif ?>
            <th class="num">Hours</th><th class="num">Billable</th><th class="num">%</th>
            <?php if ($showAmounts): ?><th class="num">Amount</th><?php endif ?>
        </tr></thead>
        <tbody>
        <?php foreach ($rows as $r): ?>
            <tr>
                <td><?= h($heading === 'By day' ? date('D', strtotime($r['label'])) . ' ' . fmt_date($r['label']) : $r['label']) ?></td>
                <?php if ($extraKey): ?><td><?= h($r[$extraKey] ?? '') ?></td><?php endif ?>
                <td class="num"><?= fmt_hours($r['minutes']) ?></td>
                <td class="num"><?= fmt_hours($r['billable_minutes']) ?></td>
                <td class="num"><?= $total ? round($r['minutes'] / $total * 100) : 0 ?>%</td>
                <?php if ($showAmounts): ?><td class="num"><?= h(fmt_money($r['amount'])) ?></td><?php endif ?>
            </tr>
        <?php endforeach ?>
        </tbody>
    </table>
    <?php
}
?><!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title><?= h($title) ?></title>
<link rel="stylesheet" href="assets/print.css?v=<?= APP_VERSION ?>">
</head>
<body>
<div class="toolbar no-print">
    <button type="button" data-print>Print / Save as PDF</button>
    <a href="<?= $toggle('amounts') ?>"><?= $showAmounts ? 'Hide' : 'Show' ?> amounts</a>
    <?php if ($type === 'timesheet'): ?><a href="<?= $toggle('signoff') ?>"><?= $signoff ? 'Remove' : 'Add' ?> sign-off box</a><?php endif ?>
    <a href="?<?= h(http_build_query(['format' => 'csv'] + $f)) ?>">Download CSV</a>
    <a href="./#reports">Back</a>
</div>

<article class="page">
    <header class="doc-head">
        <div>
            <h1 class="biz"><?= h($set['business_name'] ?: $set['your_name']) ?></h1>
            <div><?= h(implode(' · ', array_filter([$set['your_name'] !== $set['business_name'] ? $set['your_name'] : null, $set['email'], $set['phone']]))) ?></div>
        </div>
        <div class="doc-title">
            <h2><?= $type === 'timesheet' ? 'Timesheet' : 'Time report' ?></h2>
            <div><?= h($period) ?></div>
        </div>
    </header>

    <?php if ($clientName): ?>
        <section class="bill-to">
            <h3>Client</h3>
            <div class="pre"><?= h($clientName . ($clientAddress ? "\n" . $clientAddress : '')) ?></div>
        </section>
    <?php endif ?>
    <?php if ($filterNotes): ?><p class="muted"><?= h(implode(' · ', $filterNotes)) ?></p><?php endif ?>

    <div class="stats">
        <div><span>Total hours</span><strong><?= fmt_hours($sum['minutes']) ?></strong></div>
        <div><span>Billable hours</span><strong><?= fmt_hours($sum['billable_minutes']) ?></strong></div>
        <?php if ($showAmounts): ?><div><span>Billable amount</span><strong><?= h(fmt_money($sum['amount'])) ?></strong></div><?php endif ?>
        <div><span>Entries</span><strong><?= $sum['count'] ?></strong></div>
    </div>

    <?php if ($type === 'summary'): ?>
        <?php
        if (count($sum['clients']) > 1) {
            breakdown('By client', $sum['clients'], $showAmounts);
        }
        breakdown('By project', $sum['projects'], $showAmounts, count($sum['clients']) > 1 ? 'client' : null, 'Client');
        breakdown('By task', $sum['tasks'], $showAmounts, 'project', 'Project');
        breakdown('By category', $sum['categories'], $showAmounts);
        if (count($sum['categories']) > 1) {
            echo '<p class="sub">An entry in several categories counts towards each of them.</p>';
        }
        breakdown('By day', $sum['days'], $showAmounts);
        ?>
        <h3 class="break-before">All entries</h3>
        <?php require __DIR__ . '/lib/timesheet_table.php'; ?>
    <?php else: ?>
        <?php breakdown('By project', $sum['projects'], $showAmounts, count($sum['clients']) > 1 ? 'client' : null, 'Client'); ?>
        <h3>Daily detail</h3>
        <?php require __DIR__ . '/lib/timesheet_table.php'; ?>
        <?php if ($signoff): ?>
            <section class="signoff">
                <div><span>Submitted by</span><strong><?= h($set['your_name']) ?></strong><div class="line">Signature</div><div class="line">Date</div></div>
                <div><span>Approved by (client)</span><strong>&nbsp;</strong><div class="line">Name &amp; signature</div><div class="line">Date</div></div>
            </section>
        <?php endif ?>
    <?php endif ?>
    <footer class="doc-foot">Generated <?= h(fmt_date(date('Y-m-d'))) ?></footer>
</article>
<script src="assets/print.js"></script>
</body>
</html>
